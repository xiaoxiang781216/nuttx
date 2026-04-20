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
import re
import subprocess
import tempfile
from abc import ABC, abstractmethod
from pathlib import Path

import lief

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


def load_link_command(cmd_file: Path) -> tuple[str, str, str]:
    """Load saved link command from .cmd file.

    Args:
        cmd_file: Path to the .cmd file

    Returns:
        tuple: (app_name, cwd, link_command)

    Raises:
        FileNotFoundError: If .cmd file doesn't exist
        ValueError: If .cmd file format is invalid
    """
    if not cmd_file.exists():
        raise FileNotFoundError(f"Link command file not found: {cmd_file}")

    content = cmd_file.read_text().strip()
    parts = content.split("|", 2)  # Split into max 3 parts
    if len(parts) != 3:
        raise ValueError(f"Invalid .cmd file format: {cmd_file}")

    return parts[0], parts[1], parts[2]


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

    def run_ld(self, args, cmd_file: Path, new_ld: str, out_elf: str):
        """Execute saved link command with new linker script and output path.

        Args:
            args: Command line arguments
            cmd_file: Path to the .cmd file containing original link command
            new_ld: Path to the new linker script
            out_elf: Path to the output ELF file
        """
        import re
        import shlex

        app_name, cwd, link_cmd = load_link_command(cmd_file)

        # Convert to absolute paths before changing directory
        new_ld = os.path.abspath(new_ld)
        out_elf = os.path.abspath(out_elf)

        # CMake uses semicolons as list separators, convert to spaces
        link_cmd = link_cmd.replace(";", " ")

        link_cmd = re.sub(r"\s+-r\s+", " ", link_cmd)
        link_cmd = re.sub(
            self.ld_script_pattern, self.ld_script_format.format(new_ld), link_cmd
        )
        link_cmd = re.sub(r"-o\s+\S+", f"-o {out_elf}", link_cmd)
        cmd = shlex.split(link_cmd)
        original_cwd = os.getcwd()
        try:
            os.chdir(cwd)
            run_command(cmd)
        finally:
            os.chdir(original_cwd)

    @abstractmethod
    def run_hex(self, args, in_elf, out_hex, extern):
        pass

    def get_phdr(self, args, elf):
        phdr = "    .phdr =\n"
        phdr += "    {\n"
        for ph in elf.segments:
            phdr += (
                "      {\n"
                f"        .p_type=0x{0xffffffff & ph.type.value:x},\n"
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
        return phdr


class GnuToolchain(Toolchain):
    ld_script_pattern = r"-T\s+\S+"
    ld_script_format = "-T {}"

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

    def run_hex(self, args, in_elf, out_hex, extern=[]):
        cmd = [args.objcopy, "-O", "ihex", in_elf, out_hex] + extern
        run_command(cmd)


class TaskingToolchain(Toolchain):
    ld_script_pattern = r"--lsl-file=\S+"
    ld_script_format = "--lsl-file={}"

    def run_cpp(
        self,
        args,
        flash_start: int,
        ram_start: int,
        heap_size: int,
        extern_symbols: str,
        out_ld: str,
        flags: list[str] = [],
    ):
        tmp_lsl = tempfile.NamedTemporaryFile(suffix=".cpp", mode="w")
        tmp_out = tempfile.NamedTemporaryFile(suffix=".lsl", mode="w+")
        fd = open(str(nuttxroot / "libs" / "libc" / "elf" / "tasking-elf.lsl"), "r")
        tmp_lsl.write(fd.read())
        tmp_lsl.flush()
        fd.close()

        extern_symbols_str = ""
        for name, addr in extern_symbols.items():
            extern_symbols_str += f'"{name}"=0x{addr:x};'

        toolchain_lsl = args.cc.replace("bin/cctc", "include.lsl")
        cmd = (
            [
                args.cc,
                "-E",
                "--preprocess=+noline",
                tmp_lsl.name,
                f"-DTEXT={hex(flash_start)}",
                f"-DDATA={hex(ram_start)}",
                f"-DEXTERN_SYMBOLS={extern_symbols_str}",
                f"-I{toolchain_lsl}",
                f"-o{tmp_out.name}",
            ]
            + args.cflags
            + flags
        )

        if heap_size > 0:
            cmd.append(f"-DHEAPSIZE={heap_size}")

        run_command(cmd)
        tmp_lsl.close()
        tmp_out.flush()
        tmp_out.seek(0)
        with open(out_ld, "w") as f:
            for line in tmp_out:
                if line.strip() == "" or "__builtin" in line:
                    continue
                f.write(line)

    def run_cc(self, args, in_src: str, out_obj: str):
        cmd = [
            args.cc,
            "--create",
            in_src,
            "-o",
            out_obj,
        ] + args.cflags
        run_command(cmd)

    def run_ld(self, args, cmd_file: Path, new_ld: str, out_elf: str):
        """Execute saved link command with new linker script and output path."""
        import re
        import shlex

        app_name, cwd, link_cmd = load_link_command(cmd_file)

        # Convert to absolute paths before changing directory
        new_ld = os.path.abspath(new_ld)
        out_elf = os.path.abspath(out_elf)

        # CMake uses semicolons as list separators, convert to spaces
        link_cmd = link_cmd.replace(";", " ")

        link_cmd = re.sub(r"\s+-r\s+", " ", link_cmd)
        link_cmd = re.sub(
            self.ld_script_pattern, self.ld_script_format.format(new_ld), link_cmd
        )
        link_cmd = re.sub(r"-o\s+\S+", f"-o {out_elf}", link_cmd)
        cmd = shlex.split(link_cmd)
        original_cwd = os.getcwd()
        try:
            os.chdir(cwd)
            run_command(cmd)
        finally:
            os.chdir(original_cwd)

    def run_hex(self, args, in_elf, out_hex, extern=[]):
        cmd = [args.objcopy, "-O", "ihex", in_elf, out_hex] + extern
        run_command(cmd)

    def get_phdr(self, args, elf):
        # Because the data copy generated by tasking does not conform to the ELF standard,
        # we will not use the original ELF phdr. Instead, we will generate the corresponding phdr based on the section.
        # [.section.symbol] is used to record the PhysAddr of the section. It needs to be copied to the
        # address of the .section; this address will be used as the VirtAddr.
        # If there are sections that can be merged, we will merge them.

        # Collect sections by name for lookups
        sections = {}
        entries = []

        # Pre-scan bracketed sections to know bases that will be handled as copy groups
        bracket_groups = {}
        for section in elf.sections:
            name = section.name
            sections[name] = section
            if not name or not name.startswith("["):
                continue
            inner = name.strip("[]")
            if inner.startswith("."):
                parts = inner.split(".")
                base = "." + parts[1] if len(parts) > 1 else inner
            else:
                parts = inner.split(".")
                base = parts[0]
            bracket_groups.setdefault(base, []).append(section)

        # First handle normal ALLOC sections (not the special bracketed copy sections)
        for section in elf.sections:
            name = section.name
            if not name:
                continue
            if not (section.flags & int(lief.ELF.Section.FLAGS.ALLOC)):
                continue
            # if this base has bracketed copy sections, skip emitting a separate phdr for it
            if name in bracket_groups:
                continue
            if name.startswith("["):
                # bracketed ones handled later
                continue

            p_vaddr = int(section.virtual_address)
            p_paddr = int(section.virtual_address)
            p_offset = (
                int(section.file_offset)
                if section.type != lief.ELF.Section.TYPE.NOBITS
                else 0
            )
            p_filesz = (
                int(section.size) if section.type != lief.ELF.Section.TYPE.NOBITS else 0
            )
            p_memsz = int(section.size)

            # compute PF_* flags: PF_X=1, PF_W=2, PF_R=4
            p_flags = 0
            p_flags |= 4
            if section.flags & int(lief.ELF.Section.FLAGS.WRITE):
                p_flags |= 2
            if section.flags & int(lief.ELF.Section.FLAGS.EXECINSTR):
                p_flags |= 1

            p_alignment = int(section.alignment) or 1

            entries.append(
                {
                    "p_vaddr": p_vaddr,
                    "p_paddr": p_paddr,
                    "p_offset": p_offset,
                    "p_filesz": p_filesz,
                    "p_memsz": p_memsz,
                    "p_flags": p_flags,
                    "p_alignment": p_alignment,
                }
            )

        # bracket_groups already computed above; reuse it for creating PHDRs for bracketed sources
        # Create entries for each bracketed source section mapping into the base target region.
        # Each bracketed section becomes a separate phdr entry (so they can be merged later if contiguous).
        for base, src_sections in bracket_groups.items():
            target = sections.get(base)
            min_src_vaddr = min(int(s.virtual_address) for s in src_sections)
            base_vaddr = (
                int(target.virtual_address) if target is not None else min_src_vaddr
            )

            # sort by source virtual address to preserve order
            for s in sorted(src_sections, key=lambda x: int(x.virtual_address)):
                # compute target virtual address offset from base
                offset_within_src = int(s.virtual_address) - min_src_vaddr
                p_vaddr = base_vaddr + offset_within_src
                p_paddr = int(s.virtual_address)
                p_offset = (
                    int(s.file_offset)
                    if s.file_offset is not None
                    and s.type != lief.ELF.Section.TYPE.NOBITS
                    else 0
                )
                p_filesz = int(s.size) if s.type != lief.ELF.Section.TYPE.NOBITS else 0
                p_memsz = int(s.size)

                # Copy entries should be read+write (PF_R|PF_W)
                p_flags = 4 | 2

                p_alignment = int(s.alignment) or 1

                entries.append(
                    {
                        "p_vaddr": p_vaddr,
                        "p_paddr": p_paddr,
                        "p_offset": p_offset,
                        "p_filesz": p_filesz,
                        "p_memsz": p_memsz,
                        "p_flags": p_flags,
                        "p_alignment": p_alignment,
                    }
                )

        # Sort entries by virtual address then physical address
        entries.sort(key=lambda e: (e["p_vaddr"], e["p_paddr"]))

        # Merge contiguous entries: contiguous in virtual and physical addresses and same flags
        merged = []
        for e in entries:
            if not merged:
                merged.append(e.copy())
                continue
            last = merged[-1]
            last_v_end = last["p_vaddr"] + last["p_memsz"]
            last_p_end = last["p_paddr"] + last["p_filesz"]
            # contiguous if virtual addresses and physical addresses are contiguous and flags equal
            if (
                e["p_flags"] == last["p_flags"]
                and e["p_vaddr"] == last_v_end
                and e["p_paddr"] == last_p_end
            ):
                # merge sizes and adjust alignment
                last["p_filesz"] += e["p_filesz"]
                last["p_memsz"] += e["p_memsz"]
                last["p_alignment"] = max(last["p_alignment"], e["p_alignment"])
            else:
                merged.append(e.copy())

        # Build phdr C initializer string for LOAD segments only
        phdr = "    .phdr =\n"
        phdr += "    {\n"
        for m in merged:
            phdr += (
                "      {\n"
                f"        .p_type=0x1,\n"
                f"        .p_offset=0x{m['p_offset']:x},\n"
                f"        .p_vaddr=0x{m['p_vaddr']:x},\n"
                f"        .p_paddr=0x{m['p_paddr']:x},\n"
                f"        .p_filesz=0x{m['p_filesz']:x},\n"
                f"        .p_memsz=0x{m['p_memsz']:x},\n"
                f"        .p_flags={m['p_flags']},\n"
                f"        .p_align=0x{m['p_alignment']:x},\n"
                "      },\n"
            )
        phdr += "    },\n"
        return phdr


def get_elf_fixup_s_size(elf_path: Path) -> int:
    result = subprocess.run(
        ["llvm-dwarfdump", "--name=elf_fixup_s", str(elf_path)],
        capture_output=True,
        text=True,
    )
    for line in result.stdout.splitlines():
        if "DW_AT_byte_size" in line:
            m = re.search(r"\((\d+)\)", line)
            if m:
                return int(m.group(1))
    return 0


def setup_logging(verbose: bool):
    logging.basicConfig(
        level=logging.DEBUG if verbose else logging.ERROR,
        format="[%(relativeCreated)6dms] - %(levelname)s - %(lineno)d %(message)s",
    )


def align_up(value: int, alignment: int) -> int:
    if alignment == 0:
        return value
    return (value + alignment - 1) // alignment * alignment


def align_down(value: int, alignment: int) -> int:
    if alignment == 0:
        return value
    return value // alignment * alignment


def calculate_elf_size(
    args,
    in_elf: Path,
):
    flash_used = 0
    flash_aligned = 0
    ram_used = 0
    ram_aligned = 0

    elf = elf_parse(str(in_elf))
    for section in elf.sections:
        logging.debug(
            f"Section: {section.name}, Size: {section.size}, Type: {section.type}, Flags: {section.flags}"
        )
        if section.flags & int(lief.ELF.Section.FLAGS.ALLOC):
            if section.flags & int(lief.ELF.Section.FLAGS.WRITE):
                if section.type != lief.ELF.Section.TYPE.NOBITS:
                    flash_used += align_up(section.size, section.alignment)
                ram_used += align_up(section.size, section.alignment)
                if ram_aligned < section.alignment:
                    ram_aligned = section.alignment
            else:
                flash_used += align_up(section.size, section.alignment)
                if flash_aligned < section.alignment:
                    flash_aligned = section.alignment

    return flash_used, flash_aligned, ram_used, ram_aligned


def generate_link_script(
    args,
    in_elf: Path,
    nuttx_symbols,
    flash_start: int,
    ram_start: int,
    out_ld: str,
    extern_symbols=None,
    heap_size=0,
):
    elf = elf_parse(str(in_elf))

    if not extern_symbols and nuttx_symbols:
        extern_symbols = {}
        for symbol in elf.symbols:
            if symbol.name == "nx_heapsize":
                heap_size = symbol.value

            if symbol.shndx == 0 and symbol.name != "":
                nuttx_symbol = next(
                    (s for s in nuttx_symbols if s.name == symbol.name), None
                )
                if not nuttx_symbol:
                    continue
                extern_symbols[nuttx_symbol.name] = nuttx_symbol.value

    args.toolchain.run_cpp(
        args,
        flash_start,
        ram_start,
        heap_size,
        extern_symbols,
        out_ld,
    )

    return extern_symbols, heap_size


def generate_elf_hex(args, apps, flash_start, flash_size, ram_start, ram_size):
    nuttx_symbols = elf_parse(args.elf).symbols
    flash_base, flash_remaining = flash_start, flash_size
    ram_base, ram_remaining = ram_start, ram_size

    for elf_file in apps:
        if not elf_file.is_file():
            continue

        logging.debug(f"Processing ELF file: {elf_file}")

        ld_file = args.outdir / "ld" / f"{elf_file.stem}.ld"

        # Frist preliminary link to determine sizes
        extern_symbols, heap_size = generate_link_script(
            args,
            elf_file,
            nuttx_symbols,
            0,
            0x80000000,
            ld_file,
        )

        elf_out = str(args.outdir / "elf" / os.path.basename(elf_file))
        cmd_file = args.outdir.parent / "link_cmd" / f"{elf_file.stem}.cmd"
        args.toolchain.run_ld(args, cmd_file, ld_file, elf_out)
        flash_used, flash_aligned, ram_used, ram_aligned = calculate_elf_size(
            args, elf_out
        )
        flash_remaining -= flash_used
        ram_remaining -= ram_used

        real_flash_start = align_down(flash_base + flash_remaining, flash_aligned)
        real_ram_start = align_down(ram_base + ram_remaining, ram_aligned)

        # Second link with actual used sizes
        generate_link_script(
            args,
            elf_file,
            nuttx_symbols,
            real_flash_start,
            real_ram_start,
            ld_file,
            extern_symbols,
            heap_size,
        )

        logging.info(f"{elf_file.name}: Linker script written to {ld_file}")
        cmd_file = args.outdir.parent / "link_cmd" / f"{elf_file.stem}.cmd"
        args.toolchain.run_ld(args, cmd_file, ld_file, elf_out)
        hex_out = str(args.outdir / "hex" / f"{elf_file.stem}.hex")
        args.toolchain.run_hex(args, elf_out, hex_out)

        logging.debug(
            f"Remaining: FLASH=0x{flash_remaining:x}, RAM=0x{ram_remaining:x}"
        )
        print(
            f"{elf_file.name} Current Flash used: 0x{flash_used:x}, RAM used: 0x{ram_used:x}"
        )


def generate_elf_out(args):
    all_elf_files = {f.stem: f for f in sorted(args.indir.rglob("*")) if f.is_file()}

    # Parse custom_layout: app1,app2,flash_start,flash_size,ram_start,ram_size,...
    tokens = [t.strip() for t in args.custom_layout.split(",")]

    i = 0
    while i < len(tokens):
        # Collect app names
        app_names = []
        while i < len(tokens):
            token = tokens[i]
            # Check if token is a hex number or decimal number (memory address/size)
            try:
                int(token, 0)
                break
            except ValueError:
                app_names.append(token)
                i += 1

        # Now expect 4 numeric values: flash_start, flash_size, ram_start, ram_size
        if i + 4 > len(tokens):
            logging.error(
                f"Invalid custom_layout format: missing memory parameters after apps {app_names}"
            )
            break

        flash_start = int(tokens[i], 0)
        flash_size = int(tokens[i + 1], 0)
        ram_start = int(tokens[i + 2], 0)
        ram_size = int(tokens[i + 3], 0)
        i += 4

        # Handle wildcard: replace "*" with all remaining apps
        if "*" in app_names:
            app_names = [name for name in sorted(all_elf_files.keys())]
            all_elf_files.clear()
        else:
            # Find matching ELF files for this region
            for app_name in app_names:
                if app_name not in all_elf_files:
                    logging.error(f"App '{app_name}' not found in {args.indir}")
                    os._exit(1)
                del all_elf_files[app_name]

        # Convert app names to file paths
        app_files = [all_elf_files.get(name) or args.indir / name for name in app_names]

        if app_files:
            logging.info(
                f"Processing region with apps {app_names}: "
                f"FLASH=0x{flash_start:x}/0x{flash_size:x}, RAM=0x{ram_start:x}/0x{ram_size:x}"
            )
            generate_elf_hex(
                args, app_files, flash_start, flash_size, ram_start, ram_size
            )


def generate_fixup_src(args, in_dir, out_src: str):
    elf_fixup = "#include <nuttx/binfmt/elf_fixup.h>\n"
    elf_fixup += 'const struct elf_fixup_s g_elf_fixup[] locate_data(".rodata") = \n{\n'
    elf_fixup += "  {{0}}" + ",\n"
    for elf_file in sorted(in_dir.rglob("*")):
        elf = elf_parse(str(elf_file))
        if elf is None:
            logging.error(f"Failed to parse ELF file: {elf_file}")
            continue

        rel_elf_path = args.indir / elf_file.name
        rel_elf = elf_parse(str(rel_elf_path)) if rel_elf_path.exists() else elf

        stacksize = ""
        priority = ""
        uid = ""
        gid = ""
        mode = ""
        heap_size = ""
        heap_start = ""
        for symbol in rel_elf.symbols:
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

        # _sheap is relative to the exec ELF, so we need to get it from there
        for symbol in elf.symbols:
            if symbol.name == "_sheap":
                heap_start = f"    .heapstart=0x{symbol.value:x},\n"
                break

        if heap_size == "":
            heap_start = ""

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
            f"{args.toolchain.get_phdr(args, elf)}"
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
        "--custom_layout",
        type=str,
        nargs="?",
        default=None,
        help="formatted as follows:"
        "app1, [app2, [app3]], flash start, flash size, ram start, ram size"
        "[, appX, flash startX, flash sizeX, ram startX, ram sizeX]"
        "app1, app2, and app3 will allocate memory from flash start and ram start. Multiple app groups can be parsed."
        "Use * to match all remaining apps.",
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

    if "cctc" in args.cc:
        args.toolchain = TaskingToolchain()
    else:
        args.toolchain = GnuToolchain()

    # Ensure custom_layout always exists and has * for remaining apps
    if not args.custom_layout:
        args.custom_layout = f"*,{hex(args.flash[0])},{hex(args.flash[1])},{hex(args.ram[0])},{hex(args.ram[1])}"
    elif "*" not in args.custom_layout:
        args.custom_layout += f",*,{hex(args.flash[0])},{hex(args.flash[1])},{hex(args.ram[0])},{hex(args.ram[1])}"

    return args


def run():
    args = parse_args()
    generate_elf_out(args)
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
