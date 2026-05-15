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

Requires the python-can package:
  pip install python-can
"""

import argparse
import os
import pty
import sys
import threading
import tty

try:
    import can
except ImportError:
    print(
        "error: python-can is required; install with `pip install python-can`",
        file=sys.stderr,
    )
    sys.exit(1)

CAN_EFF_FLAG = 0x80000000
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


def open_can_bus(interface, can_id):
    """Open a python-can SocketCAN bus filtered to the selected CAN ID."""

    extended = bool(can_id & CAN_EFF_FLAG)
    if extended:
        filter_id = can_id & 0x1FFFFFFF
        mask = 0x1FFFFFFF
    else:
        filter_id = can_id & 0x7FF
        mask = 0x7FF

    filters = [{"can_id": filter_id, "can_mask": mask, "extended": extended}]

    return can.Bus(
        channel=interface,
        interface="socketcan",
        fd=True,
        can_filters=filters,
    )


def can_to_pty(bus, master_fd, stop, debug):
    """Forward selected CAN frame payload bytes to PTY."""

    while not stop.is_set():
        try:
            msg = bus.recv(timeout=0.2)
        except can.CanError as e:
            print(f"[can->pty] recv error ({e})", file=sys.stderr, flush=True)
            continue
        except Exception:
            break

        if msg is None:
            continue

        if msg.is_error_frame or msg.is_remote_frame:
            continue

        can_id = msg.arbitration_id & 0x1FFFFFFF
        dlc = min(msg.dlc, len(msg.data))
        chunk = bytes(msg.data[:dlc])

        if debug:
            raw = " ".join(f"{b:02X}" for b in chunk)
            print(
                f"[can->pty] ID=0x{can_id:X} DLC={dlc} DATA={raw}",
                file=sys.stderr,
                flush=True,
            )

        try:
            os.write(master_fd, chunk)
        except OSError as e:
            print(f"[can->pty] write error ({e})", file=sys.stderr, flush=True)
            break

    stop.set()


def main():
    args = parse_args()

    try:
        bus = open_can_bus(args.interface, args.can_id)
    except (OSError, can.CanError) as e:
        print(f"Cannot open CAN bus on {args.interface}: {e}", file=sys.stderr)
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
        args=(bus, master_fd, stop, args.debug),
        daemon=True,
    )
    worker.start()

    try:
        stop.wait()
    except KeyboardInterrupt:
        pass
    finally:
        stop.set()
        try:
            bus.shutdown()
        except Exception:
            pass
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
