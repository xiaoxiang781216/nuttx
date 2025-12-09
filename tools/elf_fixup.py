#!/usr/bin/env python3
# tools/elf_fixup.py
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

import argparse
import logging
import os
import subprocess
from abc import ABC, abstractmethod
from pathlib import Path

import lief
from elftools.elf.elffile import ELFFile

nuttxroot = Path(__file__).parent.parent


def run_command(cmd: list[str], stdout=None):
    try:
        cmd_str = [str(c) for c in cmd]
        logging.debug(f"Running command: {' '.join(cmd_str)}")
        if stdout:
            subprocess.run(
                cmd, stdout=stdout, stderr=subprocess.PIPE, text=True, check=True
            )
        else:
            subprocess.run(cmd, text=True, check=True)

    except subprocess.CalledProcessError as e:
        logging.error(f"Command failed with error: {e.stderr}")
        raise RuntimeError(f"Command failed: {' '.join(cmd)}") from e


def elf_parse(elf):
    config = lief.ELF.ParserConfig()
    config.parse_notes = False
    config.parse_relocations = False
    config.parse_symtab_symbols = True

    return lief.ELF.parse(str(elf), config)


class Toolchain(ABC):

    @abstractmethod
    def run_cpp(
        self,
        args,
        flash_start: int,
        ram_start: int,
        heap_size: int,
        extern_symbols,
        out_ld: str,
        flags: list[str] = [],
    ):
        pass

    @abstractmethod
    def run_cc(self, args, in_src: str, out_obj: str):
        pass

    @abstractmethod
    def run_ld(self, args, in_elf, in_ld, out_elf, gc_sections=True):
        pass

    @abstractmethod
    def run_hex(self, args, in_elf, out_hex, extern):
        pass


class GnuToolchain(Toolchain):

    def run_cpp(
        self,
        args,
        flash_start: int,
        ram_start: int,
        heap_size: int,
        extern_symbols,
        out_ld: str,
        flags: list[str] = [],
    ):

        extern_symbols_str = ""
        for name, addr in extern_symbols.items():
            extern_symbols_str += f"{name} = 0x{addr:x};"
        cmd = (
            [
                args.cc,
                "-E",
                "-P",
                "-x",
                "c",
                str(nuttxroot / "libs" / "libc" / "elf" / "gnu-elf.ld.in"),
                f"-DTEXT={hex(flash_start)}",
                f"-DDATA={hex(ram_start)}",
                f"-DEXTERN_SYMBOLS={extern_symbols_str}",
            ]
            + args.cflags
            + flags
        )
        if heap_size > 0:
            cmd.append(f"-DHEAPSIZE={hex(heap_size)}")

        with open(out_ld, "w") as f:
            run_command(cmd, stdout=f)

    def run_cc(self, args, in_src: str, out_obj: str):
        cmd = [
            args.cc,
            "-c",
            in_src,
            "-o",
            out_obj,
        ] + args.cflags
        run_command(cmd)

    def run_ld(self, args, in_elf, in_ld, out_elf, gc_sections=True):
        nostart = []
        if args.ld.endswith("cc"):
            nostart = ["-nostartfiles", "-nostdlib"]
            if gc_sections:
                nostart.extend(["-Wl,--gc-sections"])
            nostart.extend(args.cflags)
        else:
            nostart = ["--nostdlib"]

        cmd = [
            args.ld,
            "-e",
            "__start",
            "-T",
            str(in_ld),
            str(in_elf),
            "-o",
            out_elf,
        ]
        cmd.extend(nostart)
        run_command(cmd)

    def run_hex(self, args, in_elf, out_hex, extern=[]):
        cmd = [args.objcopy, "-O", "ihex", in_elf, out_hex] + extern
        run_command(cmd)


def get_elf_fixup_s_size(elf_path: Path) -> int:
    elf = ELFFile(open(elf_path, "rb"))
    debug_info = elf.get_dwarf_info()
    for cu in debug_info.iter_CUs():
        for die in cu.iter_DIEs():
            if die.tag == "DW_TAG_structure_type":
                name = die.attributes.get("DW_AT_name")
                if name and name.value.decode("utf-8") == "elf_fixup_s":
                    return die.attributes["DW_AT_byte_size"].value
    return 0


def setup_logging(verbose: bool):
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.ERROR,
        format="[%(relativeCreated)6dms] - %(levelname)s - %(lineno)d %(message)s",
    )


def align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) // alignment * alignment


def align_down(value: int, alignment: int) -> int:
    return value // alignment * alignment


def generate_link_script(
    args,
    in_elf: Path,
    nuttx_symbols,
    flash_start: int,
    flash_remaining: int,
    ram_start: int,
    ram_remaining: int,
    out_ld: str,
):
    elf = elf_parse(str(in_elf))
    flash_used = 0
    ram_used = 0
    heap_size = 0
    flash_start_aligned = 0
    ram_start_aligned = 0
    extern_symbols = {}

    if nuttx_symbols:
        for symbol in elf.symbols:
            if symbol.name == "nx_heapsize":
                heap_size = symbol.value
                ram_used += heap_size

            if symbol.shndx == 0 and symbol.name != "":
                nuttx_symbol = next(
                    (s for s in nuttx_symbols if s.name == symbol.name), None
                )
                if not nuttx_symbol:
                    continue
                extern_symbols[nuttx_symbol.name] = nuttx_symbol.value

    for section in elf.sections:
        logging.debug(
            f"Section: {section.name}, Size: {section.size}, Type: {section.type}, Flags: {section.flags}"
        )
        if section.flags & int(lief.ELF.Section.FLAGS.ALLOC):
            if section.flags & int(lief.ELF.Section.FLAGS.WRITE):
                if section.type != lief.ELF.Section.TYPE.NOBITS:
                    flash_used += align_up(section.size, section.alignment)
                ram_used += align_up(section.size, section.alignment)
                if ram_start_aligned < section.alignment:
                    ram_start_aligned = section.alignment
            else:
                flash_used += align_up(section.size, section.alignment)
                if flash_start_aligned < section.alignment:
                    flash_start_aligned = section.alignment

    elf_flash_start = flash_start + flash_remaining - flash_used
    elf_flash_start = align_down(elf_flash_start, flash_start_aligned)
    flash_used = flash_start + flash_remaining - elf_flash_start

    elf_ram_start = ram_start + ram_remaining - ram_used
    elf_ram_start = align_down(elf_ram_start, ram_start_aligned)
    ram_used = ram_start + ram_remaining - elf_ram_start

    args.toolchain.run_cpp(
        args,
        elf_flash_start,
        elf_ram_start,
        heap_size,
        extern_symbols,
        out_ld,
    )

    return flash_used, ram_used


def generate_elf_hex(args):
    nuttx_symbols = elf_parse(args.elf).symbols
    flash_base, flash_remaining = args.flash
    ram_base, ram_remaining = args.ram

    for elf_file in sorted(args.indir.rglob("*")):
        if not elf_file.is_file():
            continue

        logging.debug(f"Processing ELF file: {elf_file}")

        ld_file = args.outdir / "ld" / f"{elf_file.stem}.ld"
        flash_used, ram_used = generate_link_script(
            args,
            elf_file,
            nuttx_symbols,
            flash_base,
            flash_remaining,
            ram_base,
            ram_remaining,
            ld_file,
        )

        # Update the remaining memory
        flash_remaining -= flash_used
        ram_remaining -= ram_used

        logging.info(f"{elf_file.name}: Linker script written to {ld_file}")
        logging.debug(
            f"Remaining: FLASH=0x{flash_remaining:x}, RAM=0x{ram_remaining:x}"
        )
        print(
            f"{elf_file.name} Current Flash used: 0x{flash_used:x}, RAM used: 0x{ram_used:x}"
        )

        elf_out = str(args.outdir / "elf" / os.path.basename(elf_file))
        args.toolchain.run_ld(args, elf_file, ld_file, elf_out)
        hex_out = str(args.outdir / "hex" / f"{elf_file.stem}.hex")
        args.toolchain.run_hex(args, elf_out, hex_out)

    args.flash = (flash_base, flash_remaining)
    args.ram = (ram_base, ram_remaining)


def generate_fixup_src(args, in_dir, out_src: str):
    elf_fixup = "#include <nuttx/binfmt/elf_fixup.h>\n"
    elf_fixup += 'const struct elf_fixup_s g_elf_fixup[] locate_data(".rodata") = \n{\n'
    elf_fixup += "  {{0}}" + ",\n"
    for elf_file in sorted(in_dir.rglob("*")):
        elf = elf_parse(str(elf_file))
        if elf is None:
            logging.error(f"Failed to parse ELF file: {elf_file}")
            continue
        stacksize = ""
        priority = ""
        uid = ""
        gid = ""
        mode = ""
        heap_size = ""
        heap_start = ""
        for symbol in elf.symbols:
            if symbol.name == "nx_stacksize":
                stacksize = f"    .stacksize=0x{symbol.value:x},\n"
            elif symbol.name == "nx_priority":
                priority = f"    .priority=0x{symbol.value:x},\n"
            elif symbol.name == "nx_uid":
                uid = f"    .uid=0x{symbol.value:x},\n"
            elif symbol.name == "nx_gid":
                gid = f"    .gid=0x{symbol.value:x},\n"
            elif symbol.name == "nx_mode":
                mode = f"    .mode=0x{symbol.value:x},\n"
            elif symbol.name == "nx_heapsize":
                heap_size = f"    .heapsize=0x{symbol.value:x},\n"
            elif symbol.name == "_sheap":
                heap_start = f"    .heapstart=0x{symbol.value:x},\n"

        if heap_size == "":
            heap_start = ""

        phdr = "    .phdr =\n"
        phdr += "    {\n"
        for ph in elf.segments:
            phdr += (
                "      {\n"
                f"        .p_type=0x{ph.type.value:x},\n"
                f"        .p_offset=0x{ph.file_offset:x},\n"
                f"        .p_vaddr=0x{ph.virtual_address:x},\n"
                f"        .p_paddr=0x{ph.physical_address:x},\n"
                f"        .p_filesz=0x{ph.physical_size:x},\n"
                f"        .p_memsz=0x{ph.virtual_size:x},\n"
                f"        .p_flags={ph.flags.value},\n"
                f"        .p_align=0x{ph.alignment:x},\n"
                "      },\n"
            )
        phdr += "    },\n"

        entry = elf.entrypoint if elf.entrypoint else 0
        elf_fixup += (
            "\n  {\n"
            f'    .name="{elf_file.stem}",\n'
            f"    .entry={entry},\n"
            f"{stacksize}"
            f"{priority}"
            f"{uid}"
            f"{gid}"
            f"{mode}"
            f"{heap_start}"
            f"{heap_size}"
            f"{phdr}"
            "  },\n"
        )

    elf_fixup += "};\n"
    with open(out_src, "w") as f:
        f.write(elf_fixup)


def generate_fixup_hex(args):
    generate_fixup_src(args, args.outdir / "elf", args.outdir / "fixup" / "elf_fixup.c")

    args.toolchain.run_cc(
        args,
        str(args.outdir / "fixup" / "elf_fixup.c"),
        str(args.outdir / "fixup" / "elf_fixup.o"),
    )

    rodata_cmd = ["--change-section-address", f".rodata={args.fixup_addr}"]

    args.toolchain.run_hex(
        args,
        str(args.outdir / "fixup" / "elf_fixup.o"),
        str(args.outdir / "hex" / "elf_fixup.hex"),
        extern=rodata_cmd,
    )


def generate_merge_hex(args):
    hex_cmd = ["srec_cat", "-o", args.output, "-Intel"]

    args.toolchain.run_hex(args, args.elf, str(args.outdir / "hex" / f"{args.elf}.hex"))
    for hex_file in sorted((args.outdir / "hex").rglob("*")):
        hex_cmd.extend([str(hex_file), "-Intel"])

    run_command(hex_cmd)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Link the relocated ELF file into an executable file using the given memory region."
    )
    parser.add_argument(
        "--flash_start",
        type=str,
        required=True,
        help="Origin address of flash memory",
    )
    parser.add_argument(
        "--flash_size",
        type=str,
        required=True,
        help="Length of flash memory",
    )
    parser.add_argument(
        "--ram_start", type=str, required=True, help="Origin address of RAM"
    )
    parser.add_argument("--ram_size", type=str, required=True, help="Length of RAM")
    parser.add_argument(
        "--elf", type=str, required=True, help="Export symbol ELF (nuttx ELF)"
    )
    parser.add_argument("--indir", type=str, required=True, help="ELF file directory")
    parser.add_argument(
        "--outdir", type=str, required=True, help="Output directory for generated files"
    )
    parser.add_argument(
        "--output",
        type=str,
        required=True,
        help="Final output hex file path, will merge all hex files",
    )
    parser.add_argument("--verbose", action="store_true", help="Enable verbose logging")
    args = parser.parse_args()

    setup_logging(args.verbose)
    logging.debug(f"Arguments: {args}")

    if not Path(args.indir).exists():
        raise FileNotFoundError(f"Path does not exist: {args.indir}")

    flash_end = int(args.flash_start, 0) + int(args.flash_size, 0)
    count = 1
    for _ in Path(args.indir).rglob("*"):
        count = count + 1
    args.fixup_size = count * get_elf_fixup_s_size(args.elf)
    args.fixup_addr = flash_end - args.fixup_size

    args.flash = int(args.flash_start, 0), int(args.flash_size, 0) - args.fixup_size
    args.ram = int(args.ram_start, 0), int(args.ram_size, 0)

    args.outdir = Path(args.outdir)
    if not args.outdir.exists():
        args.outdir.mkdir(parents=True, exist_ok=True)
    (args.outdir / "fixup").mkdir(parents=True, exist_ok=True)
    (args.outdir / "elf").mkdir(parents=True, exist_ok=True)
    (args.outdir / "ld").mkdir(parents=True, exist_ok=True)
    (args.outdir / "hex").mkdir(parents=True, exist_ok=True)

    args.indir = Path(args.indir)
    args.cc = os.environ.get("CC", "gcc")
    args.objcopy = os.environ.get("OBJCOPY", "objcopy")
    args.cflags = list(dict.fromkeys(os.environ.get("CFLAGS", "").split()))
    args.ld = os.environ.get("LD", "ld")

    elf = elf_parse(str(args.elf))
    args.is_64bit = elf.header.identity_class == lief.ELF.Header.CLASS.ELF64
    args.toolchain = GnuToolchain()
    return args


def run():
    args = parse_args()
    generate_elf_hex(args)
    generate_fixup_hex(args)
    generate_merge_hex(args)
    print("Fixed ELF generation completed successfully.")
    for elf_file in sorted((args.outdir / "elf").rglob("*")):
        print(f"Generated ELF file: {elf_file.name}")
    for elf_file in sorted((args.outdir / "hex").rglob("*")):
        print(f"Generated Hex file: {elf_file.name}")
    for elf_file in sorted((args.outdir / "ld").rglob("*")):
        print(f"Generated ld script: {elf_file.name}")


if __name__ == "__main__":
    run()
