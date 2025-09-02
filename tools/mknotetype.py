#!/usr/bin/env python3
############################################################################
# tools/mknotetype.py
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
import subprocess
import tempfile
from enum import Enum

try:
    from elftools.elf.elffile import ELFFile
except ModuleNotFoundError as e:
    print(f"Error:{e}.\nPlease execute the following command to install dependencies:")
    print("pip install construct pyelftools")
    exit(1)

TAG_SECTION = "note_type"
FORMAT_SECTION = "note_format"
NOTE_TYPE_SIZE = 8


class NoteTag(Enum):
    UINT32 = 0
    UINT64 = 1
    DOUBLE = 2
    STRING = 3


class NoteFormatType:
    def __init__(self, bitwidth: int):
        self.bitwidth = bitwidth
        # Regular expression to match note format specifiers, capturing each part
        self.pattern = re.compile(
            r"%(?P<flags>[-+ #0]*)?(?P<width>\d+|\*)?(?:\.(?P<precision>\d+|\*))?"
            r"(?P<length>[hljztL]|ll|hh)?(?P<specifier>[diuxXopfFeEgGaAcspn%])"
        )

    def get_integer_type(self, length_modifier: str) -> int:
        """Determine the integer type based on the length modifier"""
        if length_modifier in ["ll", "j"]:
            # long long, intmax_t -> 64-bit
            return NoteTag.UINT64
        elif length_modifier in ["l", "z", "t"]:
            # long, size_t, ptrdiff_t -> based on architecture
            return NoteTag.UINT64 if self.bitwidth == 64 else NoteTag.UINT32
        else:
            # hh, h, or default -> 32-bit
            return NoteTag.UINT32

    def get_pointer_type(self) -> int:
        """Determine the pointer type based on the architecture"""
        return NoteTag.UINT64 if self.bitwidth == 64 else NoteTag.UINT32

    def note_format_type(self, fmt: str) -> int:
        """
        Parse the format string and return the type code
        Each parameter occupies 2 bits: u32=0, u64=1, double=2, string=3
        The highest 4 bits represent the number of parameters
        """

        matches = self.pattern.finditer(fmt)
        typelist = []

        for match in matches:
            specifier = match.group("specifier")

            if specifier == "%":
                continue

            # Check if the width field is *
            if match.group("width") == "*":
                typelist.append(NoteTag.UINT32)

            # Check if the precision field is *
            if match.group("precision") == "*":
                typelist.append(NoteTag.UINT32)

            length_modifier = match.group("length") or ""

            # Determine the main parameter type based on the conversion specifier
            if specifier in "cdiuxXon":  # All integer types (including pointers)
                typelist.append(self.get_integer_type(length_modifier))

            elif specifier == "p":
                typelist.append(self.get_pointer_type())
            elif specifier in "fFeEgGaA":  # Floating point numbers
                typelist.append(NoteTag.DOUBLE)  # L modifier is also treated as double

            elif specifier in "s":  # String
                typelist.append(NoteTag.STRING)

        # Construct the final type type
        if len(typelist) > 30:
            raise ValueError(f"format string {fmt} has too many arguments")

        # The number of parameters is placed in the highest 4 bits
        notetypes = len(typelist) << 60

        for i, notetype in enumerate(typelist):
            # Each parameter occupies 2 bits
            notetypes |= notetype.value << (i * 2)

        return notetypes, typelist


class NoteFormat:
    def __init__(self, elf):
        self.elf = ELFFile(open(elf, "rb"))
        self.note_format_section = None
        self.note_type_section = None

        try:
            self.note_format_section = self.get_section_by_name(FORMAT_SECTION)
            self.note_type_section = self.get_section_by_name(TAG_SECTION)
        except ValueError as e:
            raise ValueError(f"Failed to get note section: {e}")

        self.pointer_size = (
            4 if self.elf.header["e_ident"]["EI_CLASS"] == "ELFCLASS32" else 8
        )
        self.byteorder = (
            "little"
            if self.elf.header["e_ident"]["EI_DATA"] == "ELFDATA2LSB"
            else "big"
        )
        self.note_type_list = []

    def get_section_by_name(self, name):
        for section in self.elf.iter_sections():
            if section.name == name:
                return section
        raise ValueError(f"Section {name} not found")

    def escape_non_printable(self, s: str) -> str:
        escaped_string = ""
        for char in s:
            if 32 <= ord(char) <= 126:
                escaped_string += char
            else:
                escaped_string += repr(char)[1:-1]
        return escaped_string

    def get_note_format_by_addr(self, addr: int) -> str:
        if self.note_format_section is None:
            raise ValueError("note_format section is not found")

        # Calculate the offset in the note_format section
        section_start_addr = self.note_format_section.header["sh_addr"]
        if addr < section_start_addr:
            raise ValueError(f"Address {addr} is out of bounds")

        offset = addr - section_start_addr
        section_data = self.note_format_section.data()

        # Check if the offset is out of bounds
        if offset >= len(section_data):
            raise ValueError(f"Offset {offset} is out of bounds")

        # Read from the offset until the null terminator is encountered
        end_offset = offset
        while end_offset < len(section_data) and section_data[end_offset] != 0:
            end_offset += 1

        # Extract the string
        if end_offset > offset:
            string_bytes = section_data[offset:end_offset]
            string = string_bytes.decode("utf-8", errors="replace")
        else:
            string = ""

        return self.escape_non_printable(string)

    def get_note_type(self):
        if self.note_type_section is None:
            return

        # Get the note type section data directly
        section_data = self.note_type_section.data()
        section_start_addr = self.note_type_section.header["sh_addr"]
        typelist = list()

        # Read data in blocks of pointer_size
        for i in range(0, len(section_data), NOTE_TYPE_SIZE):
            # Extract the pointer value from the data block
            format_addr_bytes = section_data[i : i + NOTE_TYPE_SIZE]
            format_addr = int.from_bytes(
                format_addr_bytes, byteorder=self.byteorder, signed=False
            )

            # Calculate the current index in the section
            type_addr = section_start_addr + i

            # Get the format string
            format_string = self.get_note_format_by_addr(format_addr)
            typelist.append((type_addr, format_addr, format_string))

        return typelist

    def generate_note_type_section(self, output, debug):
        typelist = self.get_note_type()

        note_format_type = NoteFormatType(self.pointer_size)
        for i, (index_addr, format_addr, format_string) in enumerate(typelist):
            typeflags, types = note_format_type.note_format_type(format_string)
            type_bytes = typeflags.to_bytes(NOTE_TYPE_SIZE, byteorder=self.byteorder)
            output.write(type_bytes)
            if debug:
                print(
                    f'index: {i}, format: "{format_string}", type: 0x{typeflags:08x}, types: {[item.name for item in types]}'
                )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("-e", "--elf", help="ELF file", required=True)
    parser.add_argument("-o", "--output", type=str, help="Output file")
    parser.add_argument("-c", "--objcopy", type=str, help="Objcopy path", required=True)
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    args = parser.parse_args()

    output = args.output

    with tempfile.NamedTemporaryFile(delete=False) as typefile:
        note = NoteFormat(args.elf)
        print(args.verbose)
        note.generate_note_type_section(typefile, args.verbose)

        typefile_name = typefile.name
        output = args.output if args.output else ""
        objcopy = args.objcopy
        cmd = f"{objcopy} --update-section {TAG_SECTION}={typefile_name} {args.elf} {output}"
        subprocess.run(cmd, shell=True)
