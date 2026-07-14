#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Generate footprint-compatible JSON from nm output.

Produces a directory tree consumable by plot.py for sunburst
visualization. No project-specific logic; works with any NuttX ELF
analyzed by nm.

Usage:
    arm-none-eabi-nm --print-size --size-sort --radix=d --line-numbers \
        nuttx.elf > nuttx.nmlines
    python3 tools/footprint/footprint.py nuttx.nmlines -o footprint.json
    python3 tools/footprint/plot.py footprint.json --html footprint.html
"""

import argparse
import json
import sys
from pathlib import Path

SEG_OF_TYPE = {
    "T": "text",
    "t": "text",
    "W": "text",
    "w": "text",
    "R": "rodata",
    "r": "rodata",
    "D": "data",
    "d": "data",
    "V": "data",
    "v": "data",
    "B": "bss",
    "b": "bss",
}


def parse_nm(path):
    syms = []
    seen = set()
    with open(path, errors="ignore") as f:
        for line in f:
            parts = line.rstrip("\n").split("\t", 1)
            fields = parts[0].split()
            if len(fields) < 4:
                continue
            try:
                addr = int(fields[0])
                size = int(fields[1])
            except ValueError:
                continue
            if not size or addr in seen:
                continue
            seen.add(addr)
            seg = SEG_OF_TYPE.get(fields[2])
            if seg is None:
                continue
            name = fields[3]
            src = parts[1].strip().rsplit(":", 1)[0] if len(parts) > 1 else ""
            syms.append((addr, size, seg, name, src))
    return syms


def build_tree(syms, total_size):
    root = {
        "name": "Root",
        "identifier": "root",
        "size": total_size,
        "children": [],
    }
    cache = {"root": root}

    for addr, size, seg, name, src in syms:
        if not src:
            src = ":"
        path = Path(src.replace("\\", "/"), name)
        parts = list(path.parts)

        parent = root
        cur_id = ""
        for i, part in enumerate(parts):
            cur_id = cur_id + "/" + part if cur_id else part
            if cur_id in cache:
                parent = cache[cur_id]
                parent["size"] = parent.get("size", 0) + size
            else:
                node = {
                    "name": part,
                    "identifier": cur_id,
                    "size": size,
                    "children": [],
                }
                if i == len(parts) - 1:
                    node["address"] = addr
                    node["section"] = seg
                    del node["children"]
                parent["children"].append(node)
                cache[cur_id] = node
                parent = node

    accounted = sum(c.get("size", 0) for c in root["children"])
    hidden = total_size - accounted
    if hidden > 0:
        root["children"].append(
            {"name": "(hidden)", "identifier": "(hidden)", "size": hidden}
        )

    return root


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("nmlines", help="nm output file")
    parser.add_argument(
        "-o",
        "--output",
        default=None,
        help="Output JSON path (default: stdout)",
    )
    args = parser.parse_args()

    syms = parse_nm(args.nmlines)
    total_size = sum(s for _, s, _, _, _ in syms)
    tree = build_tree(syms, total_size)
    data = {"symbols": tree, "total_size": total_size}

    if args.output:
        with open(args.output, "w") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print(
            "wrote %s (%d symbols, %d KB)"
            % (args.output, len(syms), total_size // 1024)
        )
    else:
        json.dump(data, sys.stdout, indent=2, ensure_ascii=False)


if __name__ == "__main__":
    main()
