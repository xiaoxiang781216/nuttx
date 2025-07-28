#!/usr/bin/env python3
# tools/checkelf.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import os
import struct
import sys
from typing import List

from construct import (
    Bytes,
    Computed,
    IfThenElse,
    Int8ul,
    Int16ul,
    Int32ul,
    Int64ul,
    Struct,
    this,
)

# ELF identification constants
ELF_MAGIC = b"\x7fELF"  # Magic number identifying ELF files
MIN_FILE_SIZE = 64  # Minimum valid ELF file size (ELF header size)

# ELF header sizes
ELF_HEADER_SIZE_32 = 52  # Expected size of 32-bit ELF header
ELF_HEADER_SIZE_64 = 64  # Expected size of 64-bit ELF header
PROG_HEADER_SIZE_32 = 32  # Expected size of 32-bit program header entry
PROG_HEADER_SIZE_64 = 56  # Expected size of 64-bit program header entry
SECTION_HEADER_SIZE_32 = 40  # Expected size of 32-bit section header entry
SECTION_HEADER_SIZE_64 = 64  # Expected size of 64-bit section header entry

# ELF identification values
ELFCLASS32 = 1  # Identifies 32-bit architecture
ELFCLASS64 = 2  # Identifies 64-bit architecture
ELFDATA2LSB = 1  # Little-endian data encoding
ELFDATA2MSB = 2  # Big-endian data encoding

# ELF type constants
ET_CORE = 4  # Identifies core dump files

# Program header type constants
PT_NOTE = 4  # Identifies note type program segment

# NuttX core dump constants
COREDUMP_MAGIC = 0x434F5245  # Magic number for NuttX core dump files
NUTTX_NOTE_NAME = b"NuttX\x00"  # Expected name prefix for NuttX core dump files
SHN_UNDEF = 0  # Special section index meaning "undefined"
SHN_LORESERVE = 0xFF00  # Start of reserved indices


class ElfHeaderParser:
    """Parses ELF file headers and provides validation capabilities"""

    ELF_HEADER_STRUCT = Struct(
        "ei_magic" / Bytes(4),  # ELF identification magic number (0x7f, 'E', 'L', 'F')
        "ei_class" / Int8ul,  # File class (32-bit=1, 64-bit=2)
        "ei_data" / Int8ul,  # Data encoding (little-endian=1, big-endian=2)
        "ei_version" / Int8ul,  # ELF version (must be 1 for current version)
        "ei_osabi" / Int8ul,  # OS/ABI identification
        "ei_abiversion" / Int8ul,  # ABI version
        "ei_pad" / Bytes(7),  # Padding bytes (unused/reserved)
        "e_type" / Int16ul,  # Object file type (executable=2, core=4, etc.)
        "e_machine" / Int16ul,  # Target architecture (x86=3, ARM=40, etc.)
        "e_version" / Int32ul,  # ELF version (same as ei_version)
        "e_entry"
        / IfThenElse(
            this.ei_class == ELFCLASS32, Int32ul, Int64ul
        ),  # Entry point address
        "e_phoff"
        / IfThenElse(
            this.ei_class == ELFCLASS32, Int32ul, Int64ul
        ),  # Program header table offset
        "e_shoff"
        / IfThenElse(
            this.ei_class == ELFCLASS32, Int32ul, Int64ul
        ),  # Section header table offset
        "e_flags" / Int32ul,  # Processor-specific flags
        "e_ehsize" / Int16ul,  # ELF header size in bytes
        "e_phentsize" / Int16ul,  # Size of program header entry
        "e_phnum" / Int16ul,  # Number of program header entries
        "e_shentsize" / Int16ul,  # Size of section header entry
        "e_shnum" / Int16ul,  # Number of section header entries
        "e_shstrndx" / Int16ul,  # Section header string table index
        "endian"
        / Computed(
            lambda ctx: "<" if ctx.ei_data == ELFDATA2LSB else ">"
        ),  # Endianness for struct
        "is_core" / Computed(lambda ctx: ctx.e_type == ET_CORE),  # Is core dump file
    )

    def __init__(self, data):
        self.data = data
        self.header = None

    def parse_header(self) -> List[str]:
        """Parse ELF header and return list of issues"""
        issues = []

        # Validate magic number
        if self.data[0:4] != ELF_MAGIC:
            return [
                f"Invalid ELF magic number (expected 0x7f454c46, got 0x{self.data[0:4].hex()})"
            ]

        try:
            self.header = self.ELF_HEADER_STRUCT.parse(self.data)
        except Exception as e:
            issues.append(f"ELF header parsing failed: {str(e)}")

        return issues

    def validate_header_sizes(self) -> List[str]:
        """Validate critical size fields in ELF header"""
        issues = []
        if not self.header:
            return ["Cannot validate sizes: header parsing failed"]

        # Expected sizes for different architectures
        expected_sizes = {
            ELFCLASS32: {
                "ehsize": ELF_HEADER_SIZE_32,
                "phentsize": PROG_HEADER_SIZE_32,
                "shentsize": SECTION_HEADER_SIZE_32,
            },
            ELFCLASS64: {
                "ehsize": ELF_HEADER_SIZE_64,
                "phentsize": PROG_HEADER_SIZE_64,
                "shentsize": SECTION_HEADER_SIZE_64,
            },
        }

        if self.header.ei_class not in expected_sizes:
            return [f"Unknown ELF class: {self.header.ei_class}"]

        exp_sizes = expected_sizes.get(self.header.ei_class)

        # Validate ELF header size
        if self.header.e_ehsize != exp_sizes["ehsize"]:
            issues.append(
                f"Invalid ELF header size: expected {exp_sizes['ehsize']} bytes, "
                f"actual {self.header.e_ehsize} bytes"
            )

        # Validate program header entry size
        if self.header.e_phentsize != exp_sizes["phentsize"]:
            issues.append(
                f"Invalid program header entry size: expected {exp_sizes['phentsize']} bytes, "
                f"actual {self.header.e_phentsize} bytes"
            )

        # Validate section header entry size (only if sections exist or not core dump)
        if (
            self.header.e_shnum > 0 or not self.header.is_core
        ) and self.header.e_shentsize != exp_sizes["shentsize"]:
            issues.append(
                f"Invalid section header entry size: expected {exp_sizes['shentsize']} bytes, "
                f"actual {self.header.e_shentsize} bytes"
            )

        return issues

    def validate_header_tables(self, file_size) -> List[str]:
        """Validate program and section header tables are within file bounds"""
        issues = []
        if not self.header:
            return ["Cannot validate tables: header parsing failed"]

        # Validate program header table
        if self.header.e_phnum > 0:
            prog_table_size = self.header.e_phnum * self.header.e_phentsize
            prog_table_end = self.header.e_phoff + prog_table_size
            if prog_table_end > file_size:
                issues.append(
                    f"Program header table exceeds file: "
                    f"offset={self.header.e_phoff}, size={prog_table_size}, "
                    f"file_size={file_size}"
                )

        # Validate section header table
        if self.header.e_shnum > 0:
            sect_table_size = self.header.e_shnum * self.header.e_shentsize
            sect_table_end = self.header.e_shoff + sect_table_size
            if sect_table_end > file_size:
                issues.append(
                    f"Section header table exceeds file: "
                    f"offset={self.header.e_shoff}, size={sect_table_size}, "
                    f"file_size={file_size}"
                )

            # Validate section header string table index
            if (
                self.header.e_shstrndx != SHN_UNDEF
                and self.header.e_shstrndx < SHN_LORESERVE
            ):
                if self.header.e_shstrndx >= self.header.e_shnum:
                    issues.append(
                        f"Invalid section string table index: {self.header.e_shstrndx} >= "
                        f"{self.header.e_shnum} (number of sections)"
                    )

        return issues

    def validate(self, file_size) -> List[str]:
        """Unified ELF header parsing and validation"""
        issues = []

        # Parse header
        issues.extend(self.parse_header())

        if not self.header:
            return issues

        # Validate header sizes
        issues.extend(self.validate_header_sizes())

        # Validate table information
        issues.extend(self.validate_header_tables(file_size))

        return issues


def validate_core_file(core_file, header, file_size) -> str:
    """Validate nuttx core dump file, return error string or empty string"""
    if header.e_phnum <= 0:
        return "No program headers found in core file"

    # Check for NuttX-specific core dump (last program header must be PT_NOTE)
    try:
        # Calculate and seek to last program header position
        last_ph_position = header.e_phoff + (header.e_phnum - 1) * header.e_phentsize
        core_file.seek(last_ph_position)
        phdr_data = core_file.read(header.e_phentsize)

        if len(phdr_data) != header.e_phentsize:
            return "Failed to read last program header"

        seg_type = struct.unpack(header.endian + "I", phdr_data[0:4])[0]
        if seg_type != PT_NOTE:
            return ""  # Not a NuttX core dump, no further validation needed

        # Parse segment offset and size
        if header.ei_class == ELFCLASS32:
            seg_offset = struct.unpack(header.endian + "I", phdr_data[4:8])[0]
            seg_filesz = struct.unpack(header.endian + "I", phdr_data[16:20])[0]
        else:
            seg_offset = struct.unpack(header.endian + "Q", phdr_data[8:16])[0]
            seg_filesz = struct.unpack(header.endian + "Q", phdr_data[32:40])[0]

        # Validate note segment boundaries
        if seg_offset + seg_filesz > file_size:
            return f"Note segment exceeds file: offset={seg_offset}, size={seg_filesz}, file_size={file_size}"

        # Seek to note segment start
        core_file.seek(seg_offset)
        # Read note header (12 bytes)
        note_header = core_file.read(12)
        if len(note_header) != 12:
            return "Note segment header incomplete (expected 12 bytes)"

        name_size, desc_size, note_type = struct.unpack(
            f"{header.endian}III", note_header
        )

        # Validate name size cannot be zero
        if not name_size:
            return "Note name size cannot be zero"

        # Validate description size cannot be zero
        if not desc_size:
            return "Note description size cannot be zero"

        # Calculate total size of the note entry:
        #   - 12 bytes for the note header (namesz + descsz + type)
        #   - name_size bytes for the note name
        #   - desc_size bytes for the note description
        total_size = 12 + name_size + desc_size

        # Validate note segment has enough space for the entire entry
        if total_size > seg_filesz:
            return f"Note segment too small: expected {total_size} bytes, actual {seg_filesz} bytes"

        # Read note name
        core_file.seek(seg_offset + 12)  # Skip 12-byte header
        name_data = core_file.read(name_size)
        if len(name_data) != name_size:
            return f"Failed to read note name (expected {name_size} bytes)"

        # Check if it's a NuttX note
        if not name_data.startswith(NUTTX_NOTE_NAME):
            return ""  # Not a NuttX core dump, no further validation needed

        # Validate NuttX-specific magic number in note type
        if note_type != COREDUMP_MAGIC:
            return f"Invalid NuttX note type: expected 0x{COREDUMP_MAGIC:08X}, actual 0x{note_type:08X}"

    except (OSError, struct.error) as e:
        return f"Error validating core dump: {str(e)}"

    return ""  # No issues found, valid NuttX core dump


def check_elf_integrity(filename) -> List[str]:
    """Perform ELF file integrity check"""
    issues = []

    try:
        file_size = os.path.getsize(filename)
        if file_size < MIN_FILE_SIZE:
            return [f"File too small (min {MIN_FILE_SIZE} bytes required)"]

        with open(filename, "rb") as f:
            data = f.read(MIN_FILE_SIZE)
            if len(data) < MIN_FILE_SIZE:
                return [
                    f"Failed to read ELF header: read {len(data)} bytes (expected {MIN_FILE_SIZE} bytes)"
                ]

            parser = ElfHeaderParser(data)
            issues.extend(parser.validate(file_size))

            # Special validation for core dump files
            if parser.header and parser.header.is_core:
                if error := validate_core_file(f, parser.header, file_size):
                    issues.append(error)

            return issues

    except OSError as e:
        return [f"File operation error: {str(e)}"]
    except Exception as e:
        return [f"Unexpected error: {str(e)}"]


def main():
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <file>")
        print(f"Example: {sys.argv[0]} example.elf")
        sys.exit(1)

    file_path = sys.argv[1]

    if not os.path.isfile(file_path):
        print(f"Error: File '{file_path}' does not exist")
        sys.exit(1)

    issues = check_elf_integrity(file_path)
    passed = len(issues) == 0

    print(f"Check Integrity: {'PASSED' if passed else 'FAILED'}")

    if not passed:
        print(f"Found {len(issues)} issues:")
        for i, issue in enumerate(issues, 1):
            print(f"  [{i}] {issue}")
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
