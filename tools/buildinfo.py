#!/usr/bin/env python3
############################################################################
# tools/buildinfo.py
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

"""Extract build information from NuttX ELF files.

Build information is stored as ELF notes inside a single SHT_NOTE section
named '.note.nuttx.buildinfo'. Each note's owner (n_name) is the entry key
(e.g. 'sysinfo', '.config') and its descriptor (n_desc) is the raw payload.
The section is added by the build system when CONFIG_BUILD_INFO is enabled.

Usage:
  buildinfo.py list <elf>                        List all buildinfo entries
  buildinfo.py dump <elf> [-n NAME]              Print entry/entries with header
  buildinfo.py extract <elf> [-n NAME] [-o PATH] Extract entry/entries to file(s)
  buildinfo.py summary <elf>                     Shortcut for 'dump -n summary'

Fallback without this tool:
  readelf -n nuttx                               # List all buildinfo notes
"""

import argparse
import os
import sys

try:
    from elftools.elf.elffile import ELFFile
except ModuleNotFoundError as e:
    print(
        f"Error: {e}. Please install: pip install pyelftools",
        file=sys.stderr,
    )
    sys.exit(1)

SECTION_NAME = ".note.nuttx.buildinfo"
SEPARATOR = b"\n" + b"-" * 64 + b"\n\n"


def _safe_suffix(suffix):
    """Reject suffixes that could traverse outside the output directory."""
    if not suffix or suffix in (".", "..") or os.path.isabs(suffix):
        return False
    if os.sep in suffix or (os.altsep and os.altsep in suffix):
        return False
    return True


def get_buildinfo_notes(elffile):
    """Return list of (key, data_bytes) for all buildinfo notes."""
    section = elffile.get_section_by_name(SECTION_NAME)
    if section is None:
        return []
    out = []
    for note in section.iter_notes():
        key = note["n_name"]
        desc = note["n_desc"]
        if isinstance(desc, str):
            desc = desc.encode("latin-1")
        out.append((key, desc))
    return out


def _select_notes(elffile, name):
    """Return notes to operate on. If name is None, return all."""
    notes = get_buildinfo_notes(elffile)
    if not notes:
        print(
            f"No buildinfo notes found in section '{SECTION_NAME}'.",
            file=sys.stderr,
        )
        return None
    if name is None:
        return notes
    matches = [n for n in notes if n[0] == name]
    if not matches:
        print(f"Buildinfo entry '{name}' not found.", file=sys.stderr)
        return None
    return matches


def cmd_list(args):
    """List all buildinfo entries with type and size."""
    with open(args.elf, "rb") as f:
        elffile = ELFFile(f)
        notes = get_buildinfo_notes(elffile)
        if not notes:
            print(
                f"No buildinfo notes found in section '{SECTION_NAME}'.",
                file=sys.stderr,
            )
            return 1
        for key, data in notes:
            print(f"  {key:<32s} {len(data)} bytes")
    return 0


def cmd_dump(args):
    """Print entry content to stdout with '==> name <==' header per entry.

    With no name, all entries are dumped back-to-back separated by a
    horizontal rule. Intended for human-readable text entries; a trailing
    newline is appended when missing so the separator renders cleanly.
    Use 'extract' for byte-exact output on binary entries.
    """
    with open(args.elf, "rb") as f:
        elffile = ELFFile(f)
        notes = _select_notes(elffile, args.name)
        if notes is None:
            return 1
        for i, (key, data) in enumerate(notes):
            if i > 0:
                sys.stdout.buffer.write(SEPARATOR)
            header = f"==> {key} <==\n".encode()
            sys.stdout.buffer.write(header)
            sys.stdout.buffer.write(data)
            if data and not data.endswith(b"\n"):
                sys.stdout.buffer.write(b"\n")
    return 0


def cmd_extract(args):
    """Extract entry/entries to file(s).

    With no name, all entries are written as <output>/<key>.
    With a name and -o pointing at a directory, write <output>/<name>.
    With a name and -o pointing at a file path, write to that file.
    """
    with open(args.elf, "rb") as f:
        elffile = ELFFile(f)
        notes = _select_notes(elffile, args.name)
        if notes is None:
            return 1

        output = args.output
        if args.name is None:
            # Multi-entry: output must be a directory (default ".").
            output_dir = output or "."
            os.makedirs(output_dir, exist_ok=True)
            for key, data in notes:
                if not _safe_suffix(key):
                    print(
                        f"Skipping unsafe entry name: {key}",
                        file=sys.stderr,
                    )
                    continue
                out_path = os.path.join(output_dir, key)
                with open(out_path, "wb") as out:
                    out.write(data)
                print(
                    f"Extracted {key} ({len(data)} bytes) -> {out_path}",
                    file=sys.stderr,
                )
        else:
            key, data = notes[0]
            if output is None:
                if not _safe_suffix(args.name):
                    print(
                        f"Refusing to write to unsafe path: {args.name}",
                        file=sys.stderr,
                    )
                    return 1
                out_path = args.name
            elif os.path.isdir(output):
                if not _safe_suffix(args.name):
                    print(
                        f"Refusing to write to unsafe path: {args.name}",
                        file=sys.stderr,
                    )
                    return 1
                out_path = os.path.join(output, args.name)
            else:
                out_path = output
            with open(out_path, "wb") as out:
                out.write(data)
            print(
                f"Extracted {key} ({len(data)} bytes) -> {out_path}",
                file=sys.stderr,
            )
    return 0


def cmd_summary(args):
    """Shortcut for dumping the summary entry."""
    args.name = "summary"
    return cmd_dump(args)


def main():
    parser = argparse.ArgumentParser(
        description="Extract build information from NuttX ELF files"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    # list
    p_list = subparsers.add_parser("list", help="List all buildinfo entries")
    p_list.add_argument("elf", help="Path to NuttX ELF file")

    # dump
    p_dump = subparsers.add_parser(
        "dump", help="Print entry/entries to stdout with header"
    )
    p_dump.add_argument("elf", help="Path to NuttX ELF file")
    p_dump.add_argument(
        "-n",
        "--name",
        default=None,
        help="Entry key. Omit to dump all.",
    )

    # extract
    p_extract = subparsers.add_parser(
        "extract", help="Extract entry/entries to file(s)"
    )
    p_extract.add_argument("elf", help="Path to NuttX ELF file")
    p_extract.add_argument(
        "-n",
        "--name",
        default=None,
        help="Entry key. Omit to extract all.",
    )
    p_extract.add_argument(
        "-o",
        "--output",
        default=None,
        help=(
            "Output path. With -n: target file, or target directory"
            " (writes <output>/<name>). Without -n: target directory"
            " (default '.')."
        ),
    )

    # summary
    p_summary = subparsers.add_parser(
        "summary", help="Show build summary (shortcut for 'dump -n summary')"
    )
    p_summary.add_argument("elf", help="Path to NuttX ELF file")

    args = parser.parse_args()

    commands = {
        "list": cmd_list,
        "dump": cmd_dump,
        "extract": cmd_extract,
        "summary": cmd_summary,
    }

    sys.exit(commands[args.command](args))


if __name__ == "__main__":
    main()
