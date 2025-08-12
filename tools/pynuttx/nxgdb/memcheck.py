############################################################################
# tools/pynuttx/nxgdb/memcheck.py
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

import argparse
import traceback
from collections import defaultdict
from typing import Dict, List

import gdb

from . import autocompeletion, memdump, mm, utils


def is_heap_node_corrupted(heap: mm.MMHeap, node: mm.MMNode) -> str:
    # Must be in this heap
    if not heap.contains(node.address):
        return f"node@{hex(node.address)} not in heap"

    # Check next node
    if node.nodesize > node.MM_SIZEOF_ALLOCNODE:
        nextnode = node.nextnode
        if not heap.contains(nextnode.address):
            return f"nexnode@{hex(nextnode.address)} not in heap"
        if node.is_free:
            if not nextnode.is_prev_free:
                # This node is free, then next node must have prev free set
                return f"nextnode@{hex(nextnode.address)} not marked as prev free"

            if nextnode.prevsize != node.nodesize:
                return f"nextnode @{hex(nextnode.address)} prevsize not match"

    if node.is_free:
        if node.nodesize < node.MM_MIN_CHUNK:
            return f"nodesize {int(node.nodesize)} too small"

        if node.flink and node.flink.blink != node:
            return f"flink not intact: {hex(node.flink.address)}, node: {hex(node.address)}"

        if node.blink.flink != node:
            return (
                f"blink not intact: {hex(node.blink.flink)}, node: {hex(node.address)}"
            )

        # Node should be in correctly sorted order
        if (blinksize := mm.MMNode(node.blink).nodesize) > node.nodesize:
            return f"blink node not in sorted order: {blinksize} > {node.nodesize}"

        fnode = mm.MMNode(node.flink) if node.flink else None
        if fnode and fnode.nodesize and fnode.nodesize < node.nodesize:
            return f"flink node not in sorted order: {fnode.nodesize} < {node.nodesize}"
    else:
        # Node is allocated.
        if node.nodesize < node.MM_SIZEOF_ALLOCNODE:
            return f"nodesize {node.nodesize} too small"

    return ""


def check_heap(heap: mm.MMHeap) -> Dict[int, List[str]]:  # noqa: C901
    """Check heap integrity and return list of issues in string"""
    issues = defaultdict(list)  # key: address, value: list of issues

    try:
        # Check nodes in physical memory order
        for node in heap.nodes:
            corrupted = is_heap_node_corrupted(heap, node)
            if corrupted:
                issues[node.address].append(corrupted)

        # Check free list
        for node in utils.ArrayIterator(heap.mm_nodelist):
            # node is in type of gdb.Value, struct mm_freenode_s
            while node:
                address = int(node.address)
                if node["flink"] and not heap.contains(node["flink"]):
                    issues[address].append(f"flink {hex(node['flink'])} not in heap")
                    break

                if address in issues or node["size"] == 0:
                    # This node is already checked or size is 0, which is a node in node table
                    node = node["flink"]
                    continue

                # Check if this node is corrupted
                corrupted = is_heap_node_corrupted(heap, mm.MMNode(node))
                if corrupted:
                    issues[address].append(corrupted)
                    break

                # Continue to it's flink
                node = node["flink"]

    except Exception as e:
        gdb.write(f"Error happened during heap check: {e}\n")
        try:
            gdb.write(f" heap: {heap}\n")
            gdb.write(f"current node: {node}")
            if node.prevnode:
                gdb.write(f" prev node: {node.prevnode}")
            if node.nextnode:
                gdb.write(f" next node: {node.nextnode}")

            gdb.write("\n")
        except gdb.error as e:
            gdb.write(f"Error happened during report: {e}\n")
        traceback.print_exc()

    return issues


def check_pool(mpool: mm.MemPoolMultiple) -> Dict[int, List[str]]:
    """Check pool integrity and return list of issues in string"""

    def check_queue(queue) -> List[str]:
        """Check queue integrity and return list of issues in string"""
        nodes = set()
        issues = list()
        try:
            entry = queue.head
            if entry == 0:
                # Empty queue, nothing to check
                return issues

            while entry:
                if int(entry) in nodes:
                    issues.append(
                        f"*(sq_queue_t*){hex(queue.address)} "
                        f"is circular at *(sq_entry_t*){hex(entry)}"
                    )
                    return issues
                nodes.add(int(entry))
                entry = entry.flink

            if int(queue.tail) not in nodes:
                issues.append(f"*(sq_queue_t*){hex(queue.address)} tail not in squeue")

            if queue.tail.flink != 0:
                issues.append(f"*(sq_queue_t*){hex(queue.address)} tail flink not 0")

        except gdb.MemoryError as e:
            issues.append(f"MemoryError happened during squeue check: {e}")

        except Exception as e:
            gdb.write(f"Error happened during squeue check: {e}\n")
            issues.append(f"Error happened during squeue check: {e}")

        return issues

    issues = defaultdict(list)

    # chunk_queue must be valid
    if mpool.chunk_queue:
        if ret := check_queue(mpool.chunk_queue):
            issues[mpool.chunk_queue.address].extend(ret)

    # pools
    for pool in mpool.pools:
        # pool queue
        if pool.queue and (ret := check_queue(pool.queue)):
            issues[pool.queue.address].extend(ret)

        # pool iqueue
        if pool.iqueue and (ret := check_queue(pool.iqueue)):
            issues[pool.iqueue.address].extend(ret)

        # pool equeue
        if pool.equeue and (ret := check_queue(pool.equeue)):
            issues[pool.equeue.address].extend(ret)

    return issues


def dump_issues(issues: Dict[int, List[str]]) -> None:
    for address, reasons in issues.items():
        strings = "\n".join(reasons)
        gdb.write(f"{len(reasons)} issues @{hex(address)}: " f"{strings}\n")


@autocompeletion.complete
class MMCheck(gdb.Command):
    """Check memory manager and pool integrity"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "--heap",
            type=str,
            metavar="symbol",
            default=None,
            help="Only check this heap if specified, default to check all heaps",
        )
        return parser

    def parse_args(self, arg):
        try:
            return self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

    def __init__(self):
        super().__init__("mm check", gdb.COMMAND_USER)
        utils.alias("memcheck", "mm check")
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool) -> None:
        try:
            if not (args := self.parse_args(arg)):
                return
            issues = defaultdict(list)
            heaps = memdump.get_heaps(args.heap)
            for heap in heaps:
                issues = check_heap(heap)
                if issues:
                    print(f"Found {len(issues)} issues in heap {heap}")
                    dump_issues(issues)

                mpool = heap.mm_mpool
                if not mpool:
                    continue

                issues = check_pool(mpool)
                if issues:
                    print(f"Found {len(issues)} issues in pool {mpool}")
                    dump_issues(issues)

        except Exception as e:
            print(f"Error happened during check: {e}")
            traceback.print_exc()

        print("Check done.")

    def diagnose(self, *args, **kwargs):
        output = gdb.execute("mm check", to_string=True)
        fail = "issues in" in output
        return {
            "title": "Memory Corruption Check",
            "summary": (
                "Memory corruption found" if fail else "No obvious memory corruption"
            ),
            "command": "memcheck",
            "result": "fail" if fail else "pass",
            "category": utils.DiagnoseCategory.memory,
            "data": output,
        }
