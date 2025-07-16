############################################################################
# tools/pynuttx/rpmsgtrace2pcap.py
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

import argparse
import re
import struct
import sys
from dataclasses import dataclass
from typing import List, Optional

from nxelf.elf import ELFParser
from nxtrace.trace import NoteFactory, NoteParser, NotePrintf

RPMSG_NAME_SIZE = 32
RPMSG_TRANSPORT_VIRTIO = 1
RPMSG_TRANSPORT_PORT = 2


class tracePcap:
    def __init__(self):
        """Initialize the tracePcap with an empty packet list and current base time."""
        self.packets = []

    def process_input_trace(self, ts_sec, ts_usec, payload) -> None:
        self.packets.append((ts_sec, ts_usec, payload))

    def write_pcap(self, filename: str) -> None:
        """Write captured packets to a PCAP file in standard format."""
        with open(filename, "wb") as f:
            # PCAP Global Header
            f.write(
                struct.pack(
                    "<IHHiIII",
                    0xA1B2C3D4,  # Magic number (little-endian)
                    2,  # Version major
                    4,  # Version minor
                    0,  # Timezone (GMT/UTC)
                    0,  # Sigfigs
                    65535,  # Max packet length (snaplen)
                    147,  # Link-layer header type (DLT_USER_0, custom for RPMSG)
                )
            )

            # Write each packet with its header and data
            for ts_sec, ts_usec, packet_data in self.packets:
                # PCAP Packet Header
                f.write(
                    struct.pack(
                        "<IIII",
                        ts_sec,  # Timestamp seconds
                        ts_usec,  # Timestamp microseconds
                        len(packet_data),  # Length of packet on wire
                        len(packet_data),  # Length of packet stored in file
                    )
                )
                # Packet payload
                f.write(packet_data)


@dataclass
class Result:
    """Result structure containing parsed message information with optional header fields."""

    ts_sec: int
    ts_usec: int
    type: str
    name: str
    src: Optional[int] = None
    dst: Optional[int] = None
    service_type: Optional[int] = None
    length: Optional[int] = None
    flag: Optional[int] = None
    rdev: Optional[int] = None


def parse_printf(log_line: str, result: Result) -> Optional[Result]:
    """Parse RPMSG log line and extract relevant information."""

    # Determine message type
    if "get tx buffer" in log_line:
        result.type = "get_tx"
    elif "send ept" in log_line:
        result.type = "send_tx"
    elif "rx ept->cb start" in log_line:
        result.type = "rx_start"
    elif "rx ept->cb end" in log_line:
        result.type = "rx_end"
    elif "release tx buffer" in log_line:
        result.type = "release_tx"
    else:
        return None  # Unknown type

    # Extract name
    match = re.search(r"name:(\S+)", log_line)
    result.name = match.group(1) if match else "unknown"

    # Extract rdev (void* pointer) from all message types
    match = re.search(r"rdev:(\S+)", log_line)
    if match:
        rdev_str = match.group(1)
        result.rdev = int(rdev_str, 16)
    else:
        result.rdev = 0
        print(f"Warning: rdev not found in line: {log_line}")

    # Extract header information for relevant message types
    if result.type in ["send_tx", "rx_start"]:
        # Extract src and dst
        match = re.search(r"src:(\d+)", log_line)
        result.src = int(match.group(1)) if match else 0
        match = re.search(r"dst:(\d+)", log_line)
        result.dst = int(match.group(1)) if match else 0
        match = re.search(r"len:(\d+)", log_line)
        result.length = int(match.group(1)) if match else 0

        # Special handling for rpmsg-ns
        if result.src == 53 or result.dst == 53:
            result.name = "rpmsg-ns"

    return result


def parse_note_content(elf_parser, note) -> Optional[Result]:
    """
    Parse note content into a formatted string and timestamp in seconds.
    """
    result = Result(
        ts_sec=0,
        ts_usec=0,
        type="",
        name="",
        src=None,
        dst=None,
        length=None,
        flag=0,
        rdev=None,
    )

    time_ns = NoteFactory.cpu_cycles_to_ns(note.nc_systime)
    timestamp_s = time_ns / 1_000_000_000.0
    result.ts_sec = int(timestamp_s)
    result.ts_usec = int((timestamp_s - result.ts_sec) * 1000000)

    line = elf_parser.readstring(note.npt_fmt)
    if len(note.npt_data) > 0:
        line = NotePrintf().printf(elf_parser, line, note.npt_data)

    return parse_printf(line, result)


def build_message(result: Result, data: bytes) -> Optional[bytes]:
    """Build complete message based on result type.

    For 'send_tx' type: Merge header and data
    For 'rx_start' type: Return data only
    For other types: Return None

    Args:
        result: Result object containing header information
        data: Raw data bytes

    Returns:
        bytes: Complete message or None if type not supported
    """
    if result.type == "send_tx":
        # Convert Hdr structure to binary data
        # Format: src(4 bytes), dst(4 bytes), service_type(4 bytes), length(2 bytes), flag(2 bytes)
        src = result.src if result.src is not None else 0
        dst = result.dst if result.dst is not None else 0
        service_type = result.service_type if result.service_type is not None else 0
        length = result.length if result.length is not None else 0
        flag = result.flag if result.flag is not None else 0

        # Use little-endian byte order, format: I(4 bytes) I(4 bytes) I(4 bytes) H(2 bytes) H(2 bytes)
        hdr_binary = struct.pack("<3I2H", src, dst, service_type, length, flag)
        return hdr_binary + data
    elif result.type == "rx_start":
        return data
    else:
        return None


@dataclass
class ProtocolTable:
    """Protocol table structure for different RPMSG services."""

    name_pattern: str  # Name pattern supporting regular expressions
    level: int
    cmd_offset: int  # Command field offset in data
    cmd_len: int  # Command field length
    service_type: int  # Service type


# Create protocol table array
protocol_table = [
    ProtocolTable("rpmsg-ping", 1, 0, 0, 1),
    ProtocolTable("rpmsg-ns", 1, 0, 0, 2),
    ProtocolTable("s:.*", 2, 0, 4, 2),
    ProtocolTable("rpmsg-test", 1, 0, 0, 5),
    ProtocolTable("rpmsgfs-.*", 2, 0, 4, 5),
]


def update_message(rdev: int, name: str, message: bytes) -> bytes:
    """Update service type in message."""
    # Assume rpmsg_hdr length is 16 bytes
    rpmsg_hdr_length = 16
    SERVICE_TYPE_OFFSET = 8
    SERVICE_TYPE_LENGTH = 4

    # Check if message is valid
    if len(message) < 16:
        raise ValueError("Invalid message")

    """Merge transport type with service message."""
    transport = struct.pack("<I", rdev)

    for protocol in protocol_table:
        # Use regular expression to match name
        if re.match(protocol.name_pattern, name):
            if protocol.level == 1:
                # level=1, Update service type
                message = modify_binary_data_bytes(
                    message,
                    SERVICE_TYPE_OFFSET,
                    SERVICE_TYPE_LENGTH,
                    protocol.service_type,
                )
            elif protocol.level == 2:
                # level=2, extract command value from binary data
                # Calculate command data start position
                cmd_start = rpmsg_hdr_length + protocol.cmd_offset
                cmd_end = cmd_start + protocol.cmd_len

                if len(message) < cmd_end:
                    print(
                        f"Error: Binary data length insufficient, need at least \
                        {cmd_end} bytes"
                    )
                    return transport + message

                # Extract command data
                cmd_value = int.from_bytes(
                    message[cmd_start:cmd_end], byteorder="little"
                )

                # Return message
                message = modify_binary_data_bytes(
                    message,
                    SERVICE_TYPE_OFFSET,
                    SERVICE_TYPE_LENGTH,
                    protocol.service_type + cmd_value,
                )

            return transport + message

    return transport + message


def modify_binary_data_bytes(
    original_data: bytes, offset: int, length: int, new_value: int
) -> bytes:
    """Modify binary data at specified offset and length with new value."""
    # Check if offset and length are valid
    if offset < 0 or offset + length > len(original_data):
        raise ValueError(
            f"Invalid offset or length: offset={offset}, length={length}, data length={len(original_data)}"
        )

    # Convert new value to byte array of specified length (little-endian)
    new_bytes = new_value.to_bytes(length, byteorder="little")

    # Create new byte array and replace data
    modified_data = bytearray(original_data)
    modified_data[offset : offset + length] = new_bytes

    return bytes(modified_data)


def note_binary_parser(elf_parser, note_parser, freq) -> List[str]:
    """Parse binary notes and process RPMSG messages."""
    # Initialize NoteFactory
    NoteFactory.init_instance(
        elf_parser=elf_parser, output="output_trace.perfetto", frequency_hz=freq
    )

    result = None
    message = None
    pcap_processor = tracePcap()
    note_type = elf_parser.get_type("note_type_e")

    for note in note_parser.notes:
        # Step 1: Parse text content
        if note.nc_type == note_type.NOTE_DUMP_PRINTF:
            if message is not None:
                message = update_message(result.rdev, result.name, message)
                pcap_processor.process_input_trace(
                    result.ts_sec, result.ts_usec, message
                )
            message = None
            result = parse_note_content(elf_parser, note)
        elif note.nc_type == note_type.NOTE_DUMP_BINARY and result is not None:
            if message is None:
                message = build_message(result, note.nev_data)
            else:
                message = message + note.nev_data

    pcap_processor.write_pcap("rpmsg_trace.pcap")


def arg_parse():
    """Parse command line arguments."""
    parser = argparse.ArgumentParser()
    parser.add_argument("-e", "--elf", help="ELF file", required=True)
    parser.add_argument("-o", "--output", help="Output file", default="trace.perfetto")
    parser.add_argument("-b", "--binary", help="Binary note file")
    parser.add_argument(
        "-f", "--freq", type=int, default=1000000000, help="Frequency of the trace"
    )
    args = parser.parse_args()

    # Validate the arguments
    if args.binary:
        return vars(args)
    elif args.device and args.interface:
        return vars(args)
    elif args.device or args.interface:
        print("Error: Both --device and --interface must be specified together.")
        sys.exit(1)
    else:
        print(
            "Error: You must specify either a binary file or both RTT parameters (--device and --interface)."
        )
        sys.exit(1)


if __name__ == "__main__":
    args = arg_parse()
    elf_parser = ELFParser(args["elf"])
    note_parser = NoteParser(elf_parser, output=args["output"])
    note_parser.parse_file(args["binary"])
    note_parser.dump()
    note_binary_parser(elf_parser, note_parser, args["freq"])
