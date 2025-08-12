############################################################################
# tools/pynuttx/nxgdb/memdump.py
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
import binascii
import os
import re
from collections import defaultdict
from typing import Dict, Generator, List, Protocol, Tuple

import gdb

from . import autocompeletion, backtrace, mm, utils


class MMNodeDump(Protocol):
    """Node information protocol for dump"""

    address: int  # Note that address should be in type of int
    nodesize: int
    seqno: int
    pid: int
    backtrace: Tuple[int]
    is_free: bool
    from_pool: bool
    overhead: int
    useraddress: int  # Address of user memory
    usersize: int = 0  # Size of the user data, excluding overhead

    def contains(self, addr: int) -> bool: ...

    def read_memory(self) -> memoryview: ...


def filter_node(
    pid=None,
    nodesize=None,
    used=None,
    free=None,
    seqmin=None,
    seqmax=None,
    orphan=None,
    no_pid=None,
    no_heap=None,
    no_pool=None,
) -> bool:
    return lambda node: (
        (pid is None or node.pid == pid)
        and (no_pid is None or node.pid != no_pid)
        and (nodesize is None or node.nodesize == nodesize)
        and (not used or not node.is_free)
        and (not free or node.is_free)
        and (seqmin is None or node.seqno >= seqmin)
        and (seqmax is None or node.seqno <= seqmax)
        and (not orphan or node.is_orphan)
    )


def dump_nodes(
    heaps: List[mm.MMHeap],
    filters=None,
) -> Generator[MMNodeDump, None, None]:
    no_heap = filters and filters.get("no_heap")
    no_pool = filters and filters.get("no_pool")

    if not no_heap:
        yield from (
            node
            for heap in heaps
            for node in filter(filter_node(**filters), heap.nodes)
        )

    if not no_pool:
        yield from (
            blk
            for pool in mm.get_pools(heaps)
            for blk in filter(filter_node(**filters), pool.blks)
        )


def group_nodes(
    nodes: List[MMNodeDump], grouped: Dict[MMNodeDump, List[MMNodeDump]] = None
) -> Dict[MMNodeDump, List[MMNodeDump]]:
    grouped = grouped or defaultdict(list)
    for node in nodes:
        # default to group by same PID, same size and same backtrace
        grouped[node].append(node)
    return grouped


def print_node(node: MMNodeDump, alive, count=1, formatter=None, no_backtrace=False):
    formatter = (
        formatter or "{:>1} {:>4} {:>12} {:>12} {:>12} {:>9} {:>14} {:>18} {:}\n"
    )
    gdb.write(
        formatter.format(
            "\x1b[33;1m*\x1b[m" if not alive else "",
            "P" if node.from_pool else "H",
            count,
            node.pid,
            node.nodesize,
            node.overhead,
            node.seqno,
            hex(node.address),
            "",
        )
    )

    if mm.MM_RECORD_STACK_DEPTH > 0 and not no_backtrace:
        leading = formatter.format("", "", "", "", "", "", "", "", "")[:-1]
        btformat = leading + "{1:<48}{2}\n"
        if node.backtrace and node.backtrace[0]:
            gdb.write(f"{backtrace.Backtrace(node.backtrace, formatter=btformat)}\n")


def print_header(formatter=None):
    formatter = (
        formatter or "{:>1} {:>4} {:>12} {:>12} {:>12} {:>9} {:>14} {:>18} {:}\n"
    )
    head = (
        "",
        "Pool",
        "CNT",
        "PID",
        "NodeSize",
        "Overhead",
        "Seqno",
        "Address",
        "Backtrace",
    )
    gdb.write(formatter.format(*head))


def get_heaps(args_heap: str = None) -> List[mm.MMHeap]:
    """Get the list of heaps, or a specific heap if args_heap is provided."""
    if args_heap is not None:
        try:
            # Check if the arg is heap name, instead of heap symbol or address.
            heaps = mm.get_heaps()
            heap = next((heap for heap in heaps if heap.name == args_heap), None)
            if heap is not None:
                return [heap]
        except gdb.MemoryError:
            pass
        finally:
            return [mm.MMHeap(utils.parse_arg(args_heap))]
    else:
        return mm.get_heaps()


def parse_memdump_log(logfile, filters=None) -> Generator[MMNodeDump, None, None]:
    nodes = []

    class DumpNode(MMNodeDump):
        def __init__(self, address, nodesize, seqno, pid, backtrace, is_free, overhead):
            # C code dump the start address of the node, convert it the actual start address
            self.address = address - overhead
            self.nodesize = nodesize
            self.seqno = seqno
            self.pid = pid
            self.backtrace = backtrace
            self.overhead = overhead
            self.is_free = False
            self.from_pool = False

        def __repr__(self) -> str:
            return f"node@{self.address:#x}: size:{self.nodesize} seq:{self.seqno} pid:{self.pid} "

        def contains(self, addr: int) -> bool:
            return self.address <= addr < self.address + self.nodesize

        @property
        def prevnode(self):
            return next(
                (node for node in nodes if node.contains(self.address - 1)), None
            )

        @property
        def nextnode(self):
            address = self.address + self.nodesize  # address of the next node
            return next(
                (node for node in nodes if node.address == address),
                None,
            )

    with open(logfile, "r", errors="ignore") as f:
        for line in f:
            match = re.search(
                r"(\d+)\s+(\d+)\s+(\d+)\s+(\d+)((?:\s+0x[0-9a-fA-F]+)+)", line
            )
            if not match:
                continue

            try:
                pid = int(match.group(1))
                size = int(match.group(2))
                overhead = int(match.group(3))
                seq = int(match.group(4))
                addresses = match.group(5).split()
                addr = int(addresses[0], base=16)
                mem = tuple(int(addr, base=16) for addr in addresses[1:])
                nodes.append(DumpNode(addr, size, seq, pid, mem, False, overhead))
            except Exception as e:
                print(f"Error parsing line: {line}, {e}")

    return filter(filter_node(**filters), nodes) if filters else nodes


@autocompeletion.complete
class MMDump(gdb.Command):
    """Dump memory manager heap"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-a",
            "--address",
            type=str,
            metavar="symbol",
            default=None,
            help="Find the node that contains the address and exit",
        )

        parser.add_argument(
            "-l",
            "--log",
            type=str,
            metavar="file",
            default=None,
            help="Use the memdump log file generated by memdump command on device instead of live dump",
        )

        parser.add_argument(
            "--heap",
            type=str,
            metavar="symbol",
            default=None,
            help="Which heap to inspect",
        )

        parser.add_argument(
            "-p", "--pid", type=int, default=None, help="Thread PID, -1 for mempool"
        )
        parser.add_argument(
            "-i", "--min", type=int, default=None, help="Minimum sequence number"
        )
        parser.add_argument(
            "-x", "--max", type=int, default=None, help="Maximum sequence number"
        )
        parser.add_argument("--free", action="store_true", help="Free flag")
        parser.add_argument("--biggest", action="store_true", help="biggest allocated")
        parser.add_argument(
            "--orphan", action="store_true", help="Filter nodes that are orphan"
        )
        parser.add_argument(
            "--top", type=int, default=None, help="biggest top n, default to all"
        )
        parser.add_argument(
            "--size", type=int, default=None, help="Node block size filter."
        )
        parser.add_argument(
            "--no-pool",
            "--nop",
            action="store_true",
            help="Exclude dump from memory pool",
        )
        parser.add_argument(
            "--no-heap", "--noh", action="store_true", help="Exclude heap dump"
        )
        parser.add_argument(
            "--no-group", "--nog", action="store_true", help="Do not group the nodes"
        )
        parser.add_argument(
            "--no-backtrace",
            "--nob",
            action="store_true",
            help="Do not print backtrace",
        )
        parser.add_argument(
            "--no-reverse",
            "--nor",
            action="store_true",
            help="Do not reverse the sort result",
        )
        parser.add_argument(
            "--no-pid", type=int, default=None, help="Exclude nodes from this PID"
        )

        # add option to sort the node by size or count
        parser.add_argument(
            "--sort",
            type=str,
            choices=["size", "nodesize", "count", "seq", "address"],
            default="count",
            help="sort the node by size(nodesize * count), nodesize,  count or sequence number",
        )
        return parser

    def __init__(self):
        super().__init__("mm dump", gdb.COMMAND_USER)
        # define memdump as mm dump
        utils.alias("memdump", "mm dump")
        self.parser = self.get_argparser()

    def parse_args(self, arg):
        try:
            return self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

    def find_address(self, addr, heaps: List[mm.MMHeap] = None, log=None):
        """Find the node that contains the address from memdump log or live dump."""
        addr = int(utils.parse_and_eval(addr))
        if log:
            nodes = parse_memdump_log(log)
            node = next((node for node in nodes if node.contains(addr)), None)
        else:
            # Find pool firstly
            node = next(
                (blk for pool in mm.get_pools(heaps) if (blk := pool.find(addr))), None
            )

            # Try heap if not found in pool
            node = node or next(
                (node for heap in heaps if (node := heap.find(addr))), None
            )

        return addr, node

    def collect_nodes(self, heaps: List[mm.MMHeap], log=None, filters=None):
        if log:
            nodes = parse_memdump_log(log, filters=filters)
        else:
            nodes = dump_nodes(heaps, filters)

        return nodes

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool) -> None:
        if not (args := self.parse_args(arg)):
            return

        print_header()

        pids = [int(tcb["pid"]) for tcb in utils.get_tcbs()]

        def printnode(node, count):
            print_node(node, node.pid in pids, count, no_backtrace=args.no_backtrace)

        heaps = get_heaps(args.heap)

        # Find the node by address, find directly and then quit
        if args.address:
            addr, node = self.find_address(args.address, heaps, args.log)
            if not node:
                print(f"Address {addr:#x} not found in any heap")
            else:
                source = "Pool" if node.from_pool else "Heap"
                printnode(node, 1)
                print(
                    f"{addr: #x} is {'free' if node.is_free else 'used'}, found belongs to {source} - {node}"
                )

                if node.prevnode:
                    print(f"prevnode: {node.prevnode}")
                if node.nextnode:
                    print(f"nextnode: {node.nextnode}")
            return

        filters = {
            "pid": args.pid,
            "nodesize": args.size,
            "used": not args.free,
            "free": args.free,
            "seqmin": args.min,
            "seqmax": args.max,
            "orphan": args.orphan,
            "no_heap": args.no_heap,
            "no_pool": args.no_pool,
            "no_pid": args.no_pid,
        }

        nodes = self.collect_nodes(heaps, log=args.log, filters=filters)

        sort_method = {
            "count": lambda node: 1,
            "size": lambda node: node.nodesize,
            "nodesize": lambda node: node.nodesize,
            "seq": lambda node: node.seqno,
            "address": lambda node: node.address,
        }

        def sort_nodes(nodes, sort=None):
            sort = sort or args.sort
            nodes = sorted(nodes, key=sort_method[sort], reverse=not args.no_reverse)
            if args.top is not None:
                nodes = nodes[: args.top] if args.top > 0 else nodes[args.top :]
            return nodes

        if args.biggest:
            # Dump the biggest node is same as sort by nodesize and do not group them
            args.sort = "nodesize"
            args.no_group = True

        if args.no_group:
            # Print nodes without grouping
            nodes = list(nodes)

            for node in sort_nodes(nodes):
                printnode(node, 1)

            gdb.write(f"Total blks: {len(nodes)}\n")
        else:
            # Group the nodes and then print

            grouped: Dict[MMNodeDump, MMNodeDump] = defaultdict(list)
            grouped = group_nodes(nodes)

            # Replace the count and size to count grouped nodes
            sort_method["count"] = lambda node: len(grouped[node])
            sort_method["size"] = lambda node: node.nodesize * len(grouped[node])
            total_blk = total_size = 0
            for node in sort_nodes(grouped.keys()):
                count = len(grouped[node])
                total_blk += count
                if node.pid != mm.PID_MM_MEMPOOL:
                    total_size += count * node.nodesize
                printnode(node, count)

            print(f"Total {total_blk} blks, {total_size} bytes")


@autocompeletion.complete
class MMfrag(gdb.Command):
    """Show memory fragmentation rate and analyze fragmentation causes"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "--heap",
            type=str,
            default=None,
            help="Which heap to inspect",
        )
        parser.add_argument(
            "-v",
            "--verbose",
            action="store_true",
            help="Show detailed fragmentation causes (orphan nodes between free nodes)",
        )
        parser.add_argument(
            "--threshold",
            type=int,
            default=512,
            help="Minimum total size of adjacent free nodes to consider (bytes, default: 512)",
        )
        parser.add_argument(
            "--top", type=int, default=None, help="Number of top fragments to show"
        )
        parser.add_argument(
            "--no-backtrace",
            action="store_true",
            help="Do not print backtrace",
        )
        return parser

    def __init__(self):
        super().__init__("mm frag", gdb.COMMAND_USER)
        utils.alias("memfrag", "mm frag")
        self.parser = self.get_argparser()

    def show_fragmentation_rate(self, heap: mm.MMHeap):
        """Calculate and display fragmentation rate for a heap"""
        free_nodes = list(
            sorted(heap.nodes_free(), key=lambda node: node.nodesize, reverse=True)
        )
        if not free_nodes:
            gdb.write(f"{heap}: no free nodes\n")
            return

        total_free_size = sum(node.nodesize for node in free_nodes)
        remaining = total_free_size
        frag_rate = 0.0

        for node in free_nodes:
            frag_rate += (1 - (node.nodesize / remaining)) * (
                node.nodesize / total_free_size
            )
            remaining -= node.nodesize

        frag_rate *= 1000
        gdb.write(
            f"{heap.name}@{heap.address:#x}, fragmentation rate:{frag_rate:.2f},"
            f" heapsize: {heap.heapsize}, free size: {total_free_size},"
            f" free count: {len(free_nodes)}, largest: {free_nodes[0].nodesize}\n"
        )

    def show_memory_fragments(self, heap: mm.MMHeap, args):
        """Analyze and display fragmentation causes (orphan nodes between free nodes)"""
        formatter = "{:>4} {:>8} {:>12} {:>12} {:>12} {:>14} {:>9} {:>18} {:}\n"

        def print_fragment_header():
            head = (
                "Pool",
                "PID",
                "PrevSize",
                "NodeSize",
                "NextSize",
                "TotalFreeSize",
                "Seqno",
                "Address",
                "Backtrace",
            )
            gdb.write(formatter.format(*head))

        def print_fragment_node(node: MMNodeDump, total_free, no_backtrace=False):
            gdb.write(
                formatter.format(
                    "P" if node.from_pool else "H",
                    node.pid,
                    node.prevnode.nodesize,
                    node.nodesize,
                    node.nextnode.nodesize,
                    total_free,
                    node.seqno,
                    hex(node.address),
                    "",
                )
            )
            if (
                mm.MM_RECORD_STACK_DEPTH > 0
                and not no_backtrace
                and node.backtrace
                and node.backtrace[0]
            ):
                leading = formatter.format("", "", "", "", "", "", "", "", "")[:-1]
                bt_format = leading + "{1:<48}{2}\n"
                gdb.write(
                    f"{backtrace.Backtrace(node.backtrace, formatter=bt_format)}\n"
                )

        def collect_significant_fragments():
            heap_guards = [item for start, end in heap.regions for item in (start, end)]

            for node in heap.nodes_used():
                if node in heap_guards:
                    continue
                if not (node.is_prev_free and node.nextnode and node.nextnode.is_free):
                    continue
                total_free = sum(n.nodesize for n in [node.prevnode, node.nextnode])
                if total_free < args.threshold:
                    continue

                yield node, total_free

        fragments = list(collect_significant_fragments())
        if not fragments:
            gdb.write(
                f"No significant memory fragments found in {heap.name} "
                f"(threshold: {args.threshold} bytes)\n"
            )
            return

        gdb.write("-" * 106 + "\n")
        print_fragment_header()

        fragments.sort(key=lambda x: x[1], reverse=True)
        for node, total_free in fragments[: args.top]:
            print_fragment_node(node, total_free, args.no_backtrace)

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        parsed_args = self.parser.parse_args(gdb.string_to_argv(args))
        if not parsed_args:
            return

        heaps = get_heaps(parsed_args.heap)
        for heap in heaps:
            self.show_fragmentation_rate(heap)
            if parsed_args.verbose:
                self.show_memory_fragments(heap, parsed_args)

    def diagnose(self, *args, **kwargs):
        return {
            "title": "Memory Fragments Report",
            "summary": "Display fragmentation causes (orphan nodes between free nodes)",
            "command": "mm frag",
            "result": "info",
            "category": utils.DiagnoseCategory.memory,
            "message": gdb.execute("mm frag --verbose", to_string=True),
        }


@autocompeletion.complete
class MMMap(gdb.Command):
    """Generate memory map image to visualize memory layout"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-o",
            "--output",
            type=str,
            metavar="file",
            default=None,
            help="img output file",
        )
        parser.add_argument(
            "--heap",
            type=str,
            metavar="symbol",
            help="Which heap's pool to show",
            default=None,
        )
        return parser

    def __init__(self):
        self.np = utils.import_check("numpy", errmsg="Please pip install numpy\n")
        self.plt = utils.import_check(
            "matplotlib", "pyplot", errmsg="Please pip install matplotlib\n"
        )
        self.math = utils.import_check("math")
        if not self.np or not self.plt or not self.math:
            return

        super().__init__("mm map", gdb.COMMAND_USER)
        utils.alias("memmap", "mm map")
        self.parser = self.get_argparser()

    def save_memory_map(self, nodes: List[MMNodeDump], output_file):
        mallinfo = sorted(nodes, key=lambda node: node.address)
        start = mallinfo[0].address
        size = mallinfo[-1].address - start
        order = self.math.ceil(size**0.5)
        img = self.np.zeros([order, order])

        for node in mallinfo:
            addr = node.address
            size = node.nodesize
            start_index = addr - start
            end_index = start_index + size
            img.flat[start_index:end_index] = 1 + self.math.log2(node.seqno + 1)

        self.plt.imsave(output_file, img, cmap=self.plt.get_cmap("Greens"))

    def parse_arguments(self, argv):
        try:
            args = self.parser.parse_args(argv)
        except SystemExit:
            return None

        return args

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        if not (args := self.parse_arguments(gdb.string_to_argv(args))):
            return

        for heap in get_heaps(args.heap):
            name = heap.name or f"heap@{heap.address:#x}"
            output = args.output or f"{name}.png"
            self.save_memory_map(heap.nodes_used(), output)
            gdb.write(f"Memory map saved to {output}\n")


@autocompeletion.complete
class MMVisualize(gdb.Command):
    """Generates a memory treemap, showing all backtrace statistics"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-o",
            "--output",
            type=str,
            metavar="file",
            default="mm_visualize",
            help="html output file",
        )
        parser.add_argument(
            "--heap",
            type=str,
            metavar="symbol",
            help="Which heap's pool to show",
            default=None,
        )
        return parser

    def __init__(self):
        self.backtrace_depth = mm.MM_RECORD_STACK_DEPTH
        if self.backtrace_depth <= 0:
            gdb.write(
                "Without mm record backtrace enabled, visualization is not possible\n"
            )
            return

        super().__init__("mm visualize", gdb.COMMAND_USER)
        utils.alias("memvisualize", "mm visualize")
        self.px = utils.import_check(
            "plotly.express", errmsg="Please pip install plotly\n"
        )
        self.pd = utils.import_check("pandas", errmsg="Please pip install pandas\n")
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        try:
            args = self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return

        nodes = mm.get_nodes_dict()
        df = self.pd.DataFrame.from_records(nodes)

        # Only show used nodes, exclude all free nodes
        df = df[~df["free"]]

        # Merge all nodes with the same pid and backtrace
        df = (
            df.groupby(["backtrace", "pid"], as_index=False)
            .agg(
                {
                    "name": lambda x: x.iloc[0],
                    "address": "any",
                    "size": "sum",
                    "pid": "first",
                    "from_pool": "first",
                }
            )
            .reset_index()
        )

        for i in range(self.backtrace_depth):
            df[f"stack_{i}"] = df["backtrace"].apply(
                lambda x: (
                    "Unkown" if i >= len(x) or not x[i] else utils.Symbol(x[i]).func
                )
            )

        # Drop the backtrace column
        df = df.drop(columns=["backtrace"])

        # Reorder the backtrace to ensure it is displayed in the correct format in the UI.
        # For example, a,b,c,0,U,U,U,U generates U,U,U,U,U,a,b,c
        stack_columns = [f"stack_{i}" for i in range(8)]
        for index, row in df.iterrows():
            stack_values = row[stack_columns].tolist()
            unknowns = [val for val in stack_values if val == "Unkown"]
            non_unknowns = [val for val in stack_values if val != "Unkown"]
            new_stack_values = unknowns + non_unknowns
            df.loc[index, stack_columns] = new_stack_values

        # Generate the treemap
        stack_cols = [f"stack_{i}" for i in range(self.backtrace_depth - 1, -1, -1)]
        fig = self.px.treemap(
            df,
            path=stack_cols,
            values="size",
            hover_data=["pid", "from_pool"],
            title="Memory Allocation",
        )

        fig.update_layout(
            margin=dict(t=50, l=25, r=25, b=25),
            coloraxis_colorbar=dict(title="Size (bytes)"),
        )
        fig.update_traces(
            maxdepth=3,
            texttemplate="%{label}<br>%{value:,d} bytes",
            textposition="middle center",
            marker=dict(line=dict(width=1, color="DarkGray")),
        )

        path = args.output + ".html"
        fig.write_html(path)
        gdb.write(f"Memory map saved to Memory visualizations saved to {path}\n")


@autocompeletion.complete
class MMFree(gdb.Command):
    """Show heap statistics, same as device command free"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)

        parser.add_argument(
            "--heap",
            type=str,
            default=None,
            metavar="symbol",
            help="Which heap to inspect",
        )

        parser.add_argument(
            "-t",
            "--thread-usage",
            action="store_true",
            help="Show memory usage of each thread",
        )
        return parser

    def __init__(self):
        super().__init__("mm free", gdb.COMMAND_USER)
        utils.alias("free", "mm free")
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        try:
            args = self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return

        heaps = get_heaps(args.heap)

        formatter = "{:<20} {:<10} {:<10} {:<10} {:<10} {:<10} {:<10} {:<10}"
        header = (
            "name",
            "total",
            "used",
            "free",
            "maxused",
            "maxfree",
            "nused",
            "nfree",
        )

        print(formatter.format(*header))
        mm_heap_s = utils.lookup_type("struct mm_heap_s")

        # Print summary of memory usage
        summary = defaultdict(lambda: {"size": 0, "count": 0})
        mempool_free = 0

        for heap in heaps:
            heap_free = heap_used = 0
            total_size = max_free = nused = nfree = 0

            # Heap nodes
            for node in heap.nodes:
                nodesize = node.nodesize
                total_size += nodesize

                if node.is_free:
                    nfree += 1
                    heap_free += nodesize
                    max_free = max(max_free, nodesize)
                else:
                    heap_used += nodesize
                    nused += 1
                    summary[node.pid]["size"] += node.nodesize
                    summary[node.pid]["count"] += 1

            mempool_total = 0
            mempool_maxused = 0
            mempool_used = 0
            mempool_nused = 0
            mempool_nfree = 0
            mempool_free = 0

            if mm_pool := heap.mm_mpool:
                mpool = mm.MemPoolMultiple(mm_pool)

                for chunk in mpool.chunks:
                    mempool_total += int(chunk.end) - int(chunk.start)

                for pool in mpool.pools:
                    mempool_maxused += pool.total
                    for blk in pool.blks:
                        if blk.is_free:
                            mempool_free += blk.nodesize
                            mempool_nfree += 1
                        else:
                            mempool_used += blk.nodesize
                            mempool_nused += 1
                            summary[blk.pid]["size"] += node.nodesize
                            summary[blk.pid]["count"] += 1

            total = heap.heapsize + mm_heap_s.sizeof
            heap_used += mm_heap_s.sizeof  # struct overhead

            print(
                formatter.format(
                    heap.name,
                    total,
                    heap_used - mempool_free,
                    heap_free + mempool_free,
                    int(heap.mm_maxused),
                    max_free,
                    nused,
                    nfree,
                )
            )

            if mempool_total:
                print(
                    formatter.format(
                        f"{heap.name}@mempool",
                        mempool_total,
                        mempool_used,
                        mempool_total - mempool_used,
                        mempool_maxused,
                        mempool_total - mempool_maxused,
                        mempool_nused,
                        mempool_nfree,
                    )
                )

        if args.thread_usage:
            print("\nSummary of thread memory usage:")
            formatter = "{:<10} {:<30} {:<16} {:<10} {:<10}"
            print(formatter.format("PID", "TaskName", "Size", "Size(kB)", "Count"))
            for pid, data in sorted(
                summary.items(), key=lambda x: x[1]["size"], reverse=True
            ):
                print(
                    formatter.format(
                        pid,
                        utils.get_task_name(pid) or "<noname>",
                        data["size"],
                        f"{data['size'] / 1024:.1f}kB",
                        data["count"],
                    )
                )

    def diagnose(self, *args, **kwargs):
        return {
            "title": "Memory Free Information",
            "summary": "Memory free information",
            "command": "free",
            "result": "info",
            "category": utils.DiagnoseCategory.memory,
            "message": gdb.execute("mm free --thread-usage", to_string=True),
        }


@autocompeletion.complete
class NxMemoryRange(gdb.Command):
    """Show RAM range of heap and sections"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument("--heap-only", action="store_true", help="Heap only")
        parser.add_argument(
            "--globals-only", action="store_true", help="Global variables only"
        )
        return parser

    def __init__(self):
        super().__init__("mm range", gdb.COMMAND_USER)
        utils.alias("memrange", "mm range")
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, arg, from_tty):
        try:
            args = self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

        memrange = mm.get_memrange(None, args.heap_only, args.globals_only)
        if not memrange:
            print("No memory range found")
            return

        header = ("start", "end", "size", "size(kB)")
        formatter = "{:<20} {:<20} {:<20} {:<20}"
        print(formatter.format(*header))
        for start, end in memrange:
            length = end - start
            print(
                formatter.format(hex(start), hex(end), length, f"{length / 1024: .1f}")
            )


@autocompeletion.complete
class NxDumpRAM(gdb.Command):
    """Dump memory to file, similar to GDB dump memory"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-o",
            "--output",
            help="Memory dump output directory",
            metavar="file",
            default="memdump",
        )
        parser.add_argument("-r", "--memrange", type=str, default=None)
        parser.add_argument("--heap-only", action="store_true", help="Heap only")
        parser.add_argument(
            "--globals-only", action="store_true", help="Global variables only"
        )
        return parser

    def __init__(self):
        super().__init__("dump ram", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool) -> None:
        try:
            args = self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

        memrange = mm.get_memrange(args.memrange, args.heap_only, args.globals_only)
        if not memrange:
            print("No memory range found")
            return

        # Enable trust-readonly flag to try to read from elf file if possible
        gdb.execute("set trust-readonly-sections on")

        print(f"Dumping memory to {args.output}")
        os.makedirs(args.output, exist_ok=True)

        for start, end in memrange:
            print(f"Dumping memory range {start:#x} - {end:#x}")
            try:
                data = gdb.selected_inferior().read_memory(start, end - start)
                output = os.path.join(args.output, f"{start:#x}.bin")
            except gdb.MemoryError:
                print(f"Failed to read memory range {start:#x} - {end:#x}")
                continue

            with open(output, "wb") as f:
                f.write(data)


@autocompeletion.complete
class NxMemoryFind(gdb.Command):
    """Find memory address by pattern"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument("pattern", type=str, help="Pattern to search")
        parser.add_argument("-r", "--memrange", type=str, default=None)
        parser.add_argument("--heap-only", action="store_true", help="Heap only")
        parser.add_argument(
            "--globals-only", action="store_true", help="Global variables only"
        )
        return parser

    def __init__(self):
        super().__init__("mm find", gdb.COMMAND_USER)
        utils.alias("memfind", "mm find")
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool) -> None:
        try:
            args = self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

        if args.pattern.startswith('"') and args.pattern.endswith('"'):
            # Search for string
            value = bytes(args.pattern[1:-1], "utf-8")
        else:
            # Convert to a number
            value = utils.parse_arg(args.pattern)
            value = value.to_bytes((value.bit_length() + 7) // 8, "little")

        print(f"Searching for pattern {binascii.hexlify(value)} in memory")
        pattern = re.compile(value)

        memrange = mm.get_memrange(args.memrange, args.heap_only, args.globals_only)
        for start, end in memrange:
            try:
                data = bytes(gdb.selected_inferior().read_memory(start, end - start))
            except gdb.MemoryError:
                print(f"Failed to read memory range {start:#x} - {end:#x}")
                continue

            matches = [match.start() for match in pattern.finditer(data)]
            for offset in matches:
                print(f"Found pattern @ {offset + start:#x}")
        print("Done")
