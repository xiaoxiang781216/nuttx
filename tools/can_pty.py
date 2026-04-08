#!/usr/bin/env python3

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

"""CAN raw payload -> PTY bridge.

Creates a pseudo-terminal (PTY) and forwards payload bytes from SocketCAN
frames with the selected CAN ID into that PTY.

Examples:
  python3 tools/can_pty.py can0 0x3e8
  minicom -D /tmp/can-pty

This bridge forwards raw bytes exactly as received in CAN/CAN-FD payload.
No decoding, filtering, or NUL stripping is performed.
"""

import argparse
import os
import pty
import select
import socket
import struct
import sys
import threading
import tty

CAN_FRAME_FMT = "=IB3x8s"
CAN_FRAME_SIZE = struct.calcsize(CAN_FRAME_FMT)
CANFD_FRAME_FMT = "=IB3x64s"
CANFD_FRAME_SIZE = struct.calcsize(CANFD_FRAME_FMT)
CAN_EFF_FLAG = 0x80000000
CAN_RAW_FD_FRAMES = 5
DEFAULT_SYMLINK = "/tmp/can-pty"


def parse_args():
    parser = argparse.ArgumentParser(
        description="CAN payload -> PTY bridge (supports classic CAN and CAN FD)",
    )
    parser.add_argument("interface", help="CAN interface name, e.g. can0 or vcan0")
    parser.add_argument(
        "can_id",
        type=lambda x: int(x, 0),
        help="CAN ID filter (decimal or 0x hex)",
    )
    parser.add_argument(
        "--symlink",
        default=DEFAULT_SYMLINK,
        help=f"symlink path for slave PTY (default: {DEFAULT_SYMLINK})",
    )
    parser.add_argument(
        "-d",
        "--debug",
        action="store_true",
        help="print received frame metadata and payload hex",
    )
    return parser.parse_args()


def open_can_socket(interface, can_id):
    sock = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)

    # Accept CAN FD frames in addition to classic CAN.
    sock.setsockopt(socket.SOL_CAN_RAW, CAN_RAW_FD_FRAMES, 1)

    # Filter only the selected CAN ID.
    mask = 0x1FFFFFFF if can_id & CAN_EFF_FLAG else 0x7FF
    can_filter = struct.pack("=II", can_id, mask)
    sock.setsockopt(socket.SOL_CAN_RAW, socket.CAN_RAW_FILTER, can_filter)

    sock.bind((interface,))
    return sock


def can_to_pty(sock, master_fd, stop, debug):
    """Forward selected CAN frame payload bytes to PTY."""

    while not stop.is_set():
        try:
            ready, _, _ = select.select([sock], [], [], 0.2)
        except Exception:
            break

        if not ready:
            continue

        try:
            data = sock.recv(CANFD_FRAME_SIZE)
        except OSError as e:
            print(f"[can->pty] recv error ({e})", file=sys.stderr, flush=True)
            continue

        if len(data) == CANFD_FRAME_SIZE:
            can_id, dlc, payload = struct.unpack(CANFD_FRAME_FMT, data)
            can_id &= 0x1FFFFFFF
            dlc = min(dlc, 64)
        elif len(data) == CAN_FRAME_SIZE:
            can_id, dlc, payload = struct.unpack(CAN_FRAME_FMT, data)
            can_id &= 0x1FFFFFFF
            dlc = min(dlc, 8)
        else:
            continue

        chunk = payload[:dlc]

        if debug:
            raw = " ".join(f"{b:02X}" for b in chunk)
            print(f"[can->pty] ID=0x{can_id:X} DLC={dlc} DATA={raw}",
                  file=sys.stderr, flush=True)

        try:
            os.write(master_fd, chunk)
        except OSError as e:
            print(f"[can->pty] write error ({e})", file=sys.stderr, flush=True)
            break

    stop.set()


def main():
    args = parse_args()

    try:
        sock = open_can_socket(args.interface, args.can_id)
    except OSError as e:
        print(f"Cannot open CAN socket on {args.interface}: {e}", file=sys.stderr)
        sys.exit(1)

    master_fd, slave_fd = pty.openpty()
    slave_name = os.ttyname(slave_fd)
    tty.setraw(slave_fd)

    try:
        os.unlink(args.symlink)
    except FileNotFoundError:
        pass
    os.symlink(slave_name, args.symlink)

    print("CAN PTY bridge ready")
    print(f"  interface : {args.interface}")
    print(f"  CAN ID    : 0x{args.can_id:X}")
    print(f"  PTY slave : {slave_name}")
    print(f"  symlink   : {args.symlink}")
    print(f"  debug     : {'on' if args.debug else 'off'}")
    print("")
    print("Connect with:")
    print(f"  minicom -D {args.symlink}")
    print("")
    print("Press Ctrl+C to stop.")
    sys.stdout.flush()

    stop = threading.Event()
    worker = threading.Thread(
        target=can_to_pty,
        args=(sock, master_fd, stop, args.debug),
        daemon=True,
    )
    worker.start()

    try:
        stop.wait()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        sock.close()
        try:
            os.close(master_fd)
        except OSError:
            pass
        try:
            os.close(slave_fd)
        except OSError:
            pass
        try:
            os.unlink(args.symlink)
        except FileNotFoundError:
            pass
        print("\nBridge stopped.")


if __name__ == "__main__":
    main()
