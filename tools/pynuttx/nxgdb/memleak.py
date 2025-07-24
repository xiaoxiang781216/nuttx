############################################################################
# tools/pynuttx/nxgdb/memleak.py
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
import bisect
import json
import time
from os import path
from typing import Dict, Generator, List, Tuple

import gdb

from . import autocompeletion, backtrace, memdump, mm, utils


class GlobalNode(memdump.MMNodeDump):
    def __init__(self, address: int, nodesize: int):
        self.address = address
        self.nodesize = nodesize
        self.pid = None
        self.seqno = None
        self.overhead = 0
        self.backtrace = ()
        self.usersize = nodesize

    def __repr__(self):
        return f"GlobalVar@{self.address:x}:{self.nodesize}Bytes"

    def contains(self, addr: int) -> bool:
        pass

    def read_memory(self) -> memoryview:
        return gdb.selected_inferior().read_memory(self.address, self.nodesize)


@autocompeletion.complete
class MMLeak(gdb.Command):
    """Dump memory manager heap"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(
            description="Check if any memory is leaked in the system.",
        )
        parser.add_argument(
            "-a",
            "--address",
            type=str,
            default=None,
            metavar="symbol",
            help="Dump all good nodes reference by this block. The address must be a member of this block",
        )
        return parser

    def __init__(self):
        self.elf = utils.import_check(
            "elftools.elf.elffile", "ELFFile", "Please pip install pyelftools\n"
        )
        if not self.elf:
            return

        super().__init__("mm leak", gdb.COMMAND_USER)
        utils.alias("memleak", "mm leak")
        self.parser = self.get_argparser()

    def global_nodes(self) -> List[GlobalNode]:
        cache = path.join(
            path.dirname(path.abspath(gdb.objfiles()[0].filename)),
            f"{utils.get_elf_md5()}-globals.json",
        )

        nodes: List[GlobalNode] = []

        if path.isfile(cache):
            with open(cache, "r") as f:
                variables = json.load(f)
                for var in variables:
                    nodes.append(GlobalNode(var["address"], var["size"]))
                return nodes

        longsize = utils.get_long_type().sizeof
        for objfile in gdb.objfiles():
            elf = self.elf.load_from_path(objfile.filename)
            symtab = elf.get_section_by_name(".symtab")
            symbols = filter(
                lambda s: s["st_info"]["type"] == "STT_OBJECT"
                and s["st_size"] >= longsize,
                symtab.iter_symbols(),
            )

            for symbol in symbols:
                size = symbol["st_size"] // longsize * longsize
                address = symbol["st_value"]
                nodes.append(GlobalNode(address, size))

        with open(cache, "w") as f:
            variables = [
                {"address": node.address, "size": node.nodesize} for node in nodes
            ]
            str = utils.jsonify(variables)
            f.write(str)

        return nodes

    def pointers(
        self, node: memdump.MMNodeDump, regions, checked_ptr
    ) -> Generator[int, None, None]:
        # Return all possible pointers stored in this node
        longsize = utils.get_long_type().sizeof
        memory = node.read_memory()
        size = len(memory)
        prev_mem = None
        while size > 0:
            size -= longsize
            mem = memory[size : size + longsize]
            if mem == prev_mem:
                continue

            prev_mem = mem
            ptr = int.from_bytes(mem, "little")
            if ptr in checked_ptr:
                continue

            checked_ptr[ptr] = True

            if any(region["start"] <= ptr < region["end"] for region in regions):
                yield ptr

    def collect_leaks(
        self, heaps: List[mm.MMHeap]
    ) -> Dict[memdump.MMNodeDump, List[memdump.MMNodeDump]]:
        t = time.time()
        print("Loading globals from elf...", flush=True, end="")
        good_nodes = self.global_nodes()  # Global memory are all good.
        print(f" {time.time() - t:.2f}s", flush=True, end="\n")

        nodes_dict: Dict[int, memdump.MMNodeDump] = {}
        sorted_addr = set()
        t = time.time()
        print("Gather memory nodes...", flush=True, end="")

        heap_guards = [
            item
            for heap in heaps
            for start, end in heap.regions
            for item in (start, end)
        ]

        for node in memdump.dump_nodes(
            heaps, {"no_pid": mm.PID_MM_MEMPOOL, "used": True}
        ):
            if node in heap_guards:
                continue

            nodes_dict[node.useraddress] = node
            sorted_addr.add(node.useraddress)

        sorted_addr = sorted(sorted_addr)
        print(f" {time.time() - t:.2f}s", flush=True, end="\n")

        regions = [
            {"start": start.address, "end": end.address}
            for heap in heaps
            for start, end in heap.regions
        ]

        checked_ptr = {}

        print("Leak analyzing...", flush=True, end="")
        t = time.time()
        for good in good_nodes:
            if not sorted_addr:  # All nodes are checked
                break
            for ptr in self.pointers(good, regions, checked_ptr):
                if not (idx := bisect.bisect_right(sorted_addr, ptr)):
                    continue

                node = nodes_dict[sorted_addr[idx - 1]]
                if node.contains(ptr):
                    del sorted_addr[idx - 1]
                    good_nodes.append(node)

        print(f" {time.time() - t:.2f}s", flush=True, end="\n")

        return memdump.group_nodes((nodes_dict[addr] for addr in sorted_addr))

    def _iterate_leaks(
        self, nodes: Dict[memdump.MMNodeDump, List[memdump.MMNodeDump]]
    ) -> Generator[Tuple[memdump.MMNodeDump, bool, int], None, None]:
        pids = [int(tcb["pid"]) for tcb in utils.get_tcbs()]

        def is_pid_alive(pid):
            return pid in pids

        for node in nodes.keys():
            count = len(nodes[node])
            yield node, is_pid_alive(node.pid), count

    def dump_good_nodes(self, address):
        global_nodes = self.global_nodes()
        node = next(
            (
                node
                for node in global_nodes
                if node.address <= address < node.address + node.nodesize
            ),
            None,
        ) or memdump.find_address(address)
        if not node:
            print(f"{address:#x} Not found in global variables or heap nodes.")
            return

        regions = [
            {"start": start.address, "end": end.address}
            for heap in memdump.get_heaps()
            for start, end in heap.regions
        ]

        print(
            f"Address found in {node}, size: {node.nodesize} bytes\nMark below as all good."
        )
        for ptr in self.pointers(node, regions, {}):
            print(f"{ptr:#x}")

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool) -> None:
        pargs = None
        try:
            pargs = self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            pass

        if pargs and pargs.address is not None:
            self.dump_good_nodes(utils.parse_arg(pargs.address))
            return

        heaps = memdump.get_heaps("g_mmheap")

        leak_nodes = self.collect_leaks(heaps)
        memdump.print_header()
        total_blk = total_size = 0
        for node, alive, count in self._iterate_leaks(leak_nodes):
            total_blk += count
            total_size += count * node.nodesize
            memdump.print_node(node, alive, count=count)

        print(f"Leaked {total_blk} blks, {total_size} bytes")

    def diagnose(self, *args, **kwargs):
        heaps = memdump.get_heaps("g_mmheap")
        leak_nodes = self.collect_leaks(heaps)
        total_blk = total_size = 0
        data = []
        for node, alive, count in self._iterate_leaks(leak_nodes):
            total_blk += count
            total_size += count * node.nodesize
            info = {
                "count": count,
                "pid": node.pid,
                "size": node.nodesize,
                "address": node.address,
                "seqno": node.seqno,
                "alive": alive,
                "backtrace": [],
            }

            if mm.MM_RECORD_STACK_DEPTH > 0 and node.backtrace and node.backtrace[0]:
                info["backtrace"] = backtrace.Backtrace(node.backtrace)
            data.append(info)

        return {
            "title": "Memory Leak Report",
            "summary": f"Total {total_blk} blks, {total_size} bytes leaked",
            "result": "fail" if total_blk else "pass",
            "category": utils.DiagnoseCategory.memory,
            "command": "mm leak",
            "data": data,
        }
