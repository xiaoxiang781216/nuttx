#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""Fast footprint diff: compare two footprint JSONs by identifier.

Drop-in replacement for Zephyr fpdiff.py that uses flat dict lookup
instead of anytree traversal. O(n) instead of O(n^2).

Usage:
    python3 tools/footprint/fpdiff_fast.py old.json new.json
"""

import argparse
import json


def flatten_tree(node, prefix=""):
    """Flatten tree into {identifier: (name, size, path)} dict."""
    result = {}
    identifier = node.get("identifier", "")
    name = node.get("name", "")
    path = prefix + "/" + name if prefix else name

    if "children" not in node:
        result[identifier] = (node.get("name", ""), node.get("size", 0), path)
        return result

    size = node.get("size", 0)
    if not node.get("children"):
        result[identifier] = (node.get("name", ""), size, path)
        return result

    children_total = 0
    for child in node["children"]:
        sub = flatten_tree(child, path)
        result.update(sub)
        cid = child.get("identifier", "")
        children_total += sub.get(cid, (None, 0, None))[1]

    result[identifier] = (node.get("name", ""), size, path)
    return result


def main():
    parser = argparse.ArgumentParser(
        description="Compare footprint sizes of two builds (fast)."
    )
    parser.add_argument("file1", help="First (old/baseline)")
    parser.add_argument("file2", help="Second (new)")
    parser.add_argument(
        "--top", type=int, default=30, help="Show top N changes (default 30)"
    )
    args = parser.parse_args()

    with open(args.file1) as f:
        data1 = json.load(f)
    with open(args.file2) as f:
        data2 = json.load(f)

    old = flatten_tree(data1["symbols"])
    new = flatten_tree(data2["symbols"])

    old_ids = set(old)
    new_ids = set(new)

    removed = [(old[i][2], old[i][0], old[i][1]) for i in old_ids - new_ids]
    added = [(new[i][2], new[i][0], new[i][1]) for i in new_ids - old_ids]
    changed = []
    for i in old_ids & new_ids:
        old_sz = old[i][1]
        new_sz = new[i][1]
        if old_sz != new_sz:
            entry = (new[i][2], new[i][0], old_sz, new_sz, new_sz - old_sz)
            changed.append(entry)

    changed.sort(key=lambda x: -abs(x[4]))
    removed.sort(key=lambda x: -x[2])
    added.sort(key=lambda x: -x[2])

    old_total = data1["total_size"]
    new_total = data2["total_size"]
    delta = new_total - old_total

    print("old: %s B (%.1f KB)" % (format(old_total, ","), old_total / 1024))
    print("new: %s B (%.1f KB)" % (format(new_total, ","), new_total / 1024))
    print("delta: %+s B (%+.1f KB)" % (format(delta, "+,"), delta / 1024))
    print()

    if changed:
        print("changed (top %d):" % min(args.top, len(changed)))
        print("  %-60s %10s %10s %10s" % ("path/name", "old", "new", "delta"))
        print("  " + "-" * 92)
        for path, name, old_sz, new_sz, d in changed[: args.top]:
            short = path[-58:] if len(path) > 58 else path
            print(
                "  %-60s %10s %10s %+10s"
                % (
                    short,
                    format(old_sz, ","),
                    format(new_sz, ","),
                    format(d, "+,"),
                )
            )
        print()

    if removed:
        print("removed (top %d):" % min(args.top, len(removed)))
        for path, name, sz in removed[: args.top]:
            short = path[-70:] if len(path) > 70 else path
            print("  -%-70s %10s" % (short, format(sz, ",")))
        print()

    if added:
        print("added (top %d):" % min(args.top, len(added)))
        for path, name, sz in added[: args.top]:
            short = path[-70:] if len(path) > 70 else path
            print("  +%-70s %10s" % (short, format(sz, ",")))


if __name__ == "__main__":
    main()
