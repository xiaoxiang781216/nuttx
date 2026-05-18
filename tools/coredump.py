#!/usr/bin/env python3
############################################################################
# tools/coredump.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

"""Convert or capture NuttX coredumps."""

import argparse
import base64
import binascii
import io
import mmap
import os
import select
import socket
import struct
import subprocess
import sys

try:
    import isotp
except ImportError:
    print("isotp module not found, execute `pip install python-isotp`")
    isotp = None

try:
    import lzf
except ImportError:
    print("lzf module not found, execute `pip install python-lzf`")
    lzf = None


CAN_SFF_MASK = 0x000007FF
CAN_EFF_FLAG = 0x80000000
ISOTP_MAX_MSG_LEN = 4095


class CaptureError(Exception):
    pass


class CaptureTimeout(CaptureError):
    pass


def detect_encoding(filepath):
    with open(filepath, "rb") as f:
        bom = f.read(4)
    if bom[:2] == b"\xff\xfe":
        return "utf-16-le"
    if bom[:2] == b"\xfe\xff":
        return "utf-16-be"
    return "utf-8"


def try_extract(filepath) -> io.BytesIO:
    """
    Parse log file lines, and try to extract coredump data text
    Return extracted coredump binary data or None if not found

    Note: if multiple core dump found in file, only the first one is extracted
    """
    in_coredump = False
    bas64 = False
    coredump = []
    encoding = detect_encoding(filepath)
    with open(filepath, "r", encoding=encoding, errors="ignore") as f:
        for line in f.readlines():
            line = line.rstrip("\n\r")
            if "Start coredump" in line:
                in_coredump = True
                print(f"Found coredump start marker at '{line}'")
                if coredump:
                    print("Warning: multiple core dump found.")
                    break
            elif "Coredump finish" in line:
                bas64 = "base64 formatted" in line
                line = line.strip("\n\r")
                print(f"Found Coredump finish marker at '{line}'")
                in_coredump = False
            elif in_coredump:
                index = line.rfind(" ")
                if index > 0:
                    line = line[index + 1 :]
                if line:
                    coredump += line

    print(f"Found {len(coredump)} lines of coredump data")

    try:
        return coredump and io.BytesIO(
            base64.b64decode("".join(coredump))
            if bas64
            else binascii.unhexlify("".join(coredump))
        )
    except Exception:
        return None


def try_decompress(input: io.BytesIO) -> bytearray:
    output = bytearray()
    while True:
        prefix = input.read(2)  # 0, 1
        typ = input.read(1)  # 2
        clen = input.read(2)  # 3, 4
        if prefix != b"ZV":
            break

        clen = struct.unpack(">H", clen)[0]
        if typ == b"\x00":
            chunk = input.read(clen)
            output.extend(chunk)
        elif typ == b"\x01":
            if lzf is None:
                raise RuntimeError(
                    "lzf module not found, execute `pip install python-lzf`"
                )

            uncompressed_len = struct.unpack(">H", input.read(2))[0]
            cdata = input.read(clen)
            chunk = lzf.decompress(cdata, uncompressed_len)
            output.extend(chunk)
        else:
            break

    input.seek(0)
    return output or input.read()


def mmap_file(file_path, size=None):
    with open(file_path, "rb") as f:
        fd = f.fileno()
        if size is None:
            size = os.fstat(fd).st_size
        return mmap.mmap(fd, size, access=mmap.ACCESS_READ)


def parse_can_id(value):
    lower = value.lower()
    base = 16 if lower.startswith("0x") or any(c in lower for c in "abcdef") else 10
    can_id = int(value, base)
    extended = can_id > CAN_SFF_MASK or bool(can_id & CAN_EFF_FLAG)

    return can_id & ~CAN_EFF_FLAG, extended


def make_isotp_address(rx_id, extended):
    if extended:
        mode = isotp.AddressingMode.Normal_29bits
    else:
        mode = isotp.AddressingMode.Normal_11bits

    return isotp.Address(
        addressing_mode=mode,
        rxid=rx_id,
        txid=rx_id + 1,
    )


def open_isotp_socket(interface, rx_id, extended, canfd):
    if isotp is None:
        raise CaptureError(
            "can-isotp module not found, execute `pip install can-isotp`"
        )

    sock = isotp.socket()

    try:
        if canfd:
            sock.set_ll_opts(
                mtu=isotp.socket.LinkLayerProtocol.CAN_FD,
                tx_dl=64,
            )

        sock.bind(interface, make_isotp_address(rx_id, extended))
        return sock
    except OSError:
        sock.close()
        raise


def recv_isotp_message(sock, timeout):
    ready, _, _ = select.select([sock], [], [], timeout)
    if not ready:
        raise CaptureTimeout("timeout waiting for ISO-TP message")

    return sock.recv(ISOTP_MAX_MSG_LEN)


def capture_isotp(args):
    rx_id, extended = parse_can_id(args.can_id)

    try:
        sock = open_isotp_socket(args.interface, rx_id, extended, True)
    except OSError:
        sock = open_isotp_socket(args.interface, rx_id, extended, False)

    try:
        print(f"waiting for CAN coredump on 0x{rx_id:X}", flush=True)

        return capture_messages(
            lambda timeout: recv_isotp_message(sock, timeout),
            args,
        )
    finally:
        sock.close()


def capture_udp(args):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.bind, args.port))

    with sock:
        print(f"waiting for UDP coredump on {args.port}", flush=True)

        def recv_message(timeout):
            ready, _, _ = select.select([sock], [], [], timeout)
            if not ready:
                raise CaptureTimeout("timeout waiting for UDP datagram")

            return sock.recv(65535)

        return capture_messages(recv_message, args)


def capture_messages(recv_message, args):
    output = bytearray()

    while True:
        if not output:
            print("coredump capture started", flush=True)

        try:
            msg = recv_message(args.timeout)
        except CaptureTimeout as exc:
            if output:
                print(f"coredump capture finished: {len(output)} bytes", flush=True)
                return bytes(output)

            raise CaptureError("timeout waiting for coredump data") from exc

        output.extend(msg)


def convert_coredump(args):
    if not os.path.exists(args.input):
        sys.exit(f"Error: Input file {args.input} does not exist.")

    output = args.output or f"{args.input}.core"

    extracted = try_extract(args.input)
    if extracted is None:
        sys.exit(f"Error: none hex or base64 content in {args.input}.")

    input = mmap_file(args.input) if args.binary or not extracted else extracted
    coredump = try_decompress(input)
    with open(output, "wb") as outfile:
        outfile.write(coredump)

    print("Core file conversion completed: " + output)


def capture_coredump(args):
    data = args.capture(args)
    if args.decompress:
        data = bytes(try_decompress(io.BytesIO(data)))

    output = args.output or "coredump-netpoll.core"

    with open(output, "wb") as stream:
        stream.write(data)

    if args.check:
        tool = os.path.join(os.path.dirname(__file__), "checkelf.py")
        subprocess.run([sys.executable, tool, output], check=True)

    print(f"wrote {output}")


def parse_args():
    parser = argparse.ArgumentParser(
        description=(
            "Convert log coredumps, or capture netpoll coredumps over "
            "UDP or CAN ISO-TP."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
        epilog="""\
examples:
  coredump.py elf.dump
  coredump.py --decompress -o elf.core udp 6677
  coredump.py --decompress -o elf.core isotp can0 1800
""",
    )
    parser.add_argument("command", nargs="?", metavar="input|transport")
    parser.add_argument("arguments", nargs=argparse.REMAINDER, metavar="args")
    parser.add_argument("-o", "--output", help="Output core file.")
    parser.add_argument(
        "-b",
        "--binary",
        action="store_true",
        default=False,
        help="Treat input file as binary data.",
    )
    parser.add_argument(
        "--timeout",
        type=float,
        default=10.0,
        help="Netpoll idle timeout in seconds.",
    )
    parser.add_argument(
        "--decompress",
        action="store_true",
        help="Decompress captured netpoll data.",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="Validate captured netpoll output with checkelf.py.",
    )

    args = parser.parse_args()
    if args.command in ("udp", "isotp"):
        return parse_capture_args(args)

    if args.arguments:
        parser.error("unexpected arguments: " + " ".join(args.arguments))

    if args.command is None:
        parser.error("the following arguments are required: input")

    args.input = args.command
    args.func = convert_coredump
    return args


def parse_capture_args(args):
    parser = argparse.ArgumentParser(
        prog=f"{os.path.basename(sys.argv[0])} {args.command}",
        allow_abbrev=False,
    )

    if args.command == "udp":
        parser.add_argument("--bind", default="0.0.0.0")
        parser.add_argument("port", type=int)
        parser.set_defaults(capture=capture_udp)
    else:
        parser.add_argument("interface")
        parser.add_argument("can_id")
        parser.set_defaults(capture=capture_isotp)

    capture_args = parser.parse_args(args.arguments)
    capture_args.output = args.output
    capture_args.timeout = args.timeout
    capture_args.decompress = args.decompress
    capture_args.check = args.check
    capture_args.func = capture_coredump
    return capture_args


def main():
    args = parse_args()

    try:
        args.func(args)
    except (CaptureError, OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        sys.exit(f"error: {exc}")


if __name__ == "__main__":
    main()
