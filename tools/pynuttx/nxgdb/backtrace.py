import argparse
import re
import shlex
from typing import List, Union

import gdb

from . import autocompeletion, utils


class Backtrace:
    """
    Convert addresses to backtrace
    Usage:
    backtrace = Backtrace(addresses=[0x4001, 0x4002, 0x4003])

    # Access converted backtrace
    addr, func, file, line = backtrace[0]
    remaining = backtrace[1:]  # Return list of (addr, func, file, line)

    # Iterate over backtrace
    for addr, func, file, line in backtrace:
        print(addr, func, file, line)

    # Append more addresses to convert
    backtrace.append(0x40001234)

    # Print backtrace
    print(str(backtrace))

    # Format backtrace to string
    print("\n".join(backtrace.formatted))

    # Custom formatter
    backtrace = Backtrace(addresses=[0x4001, 0x4002, 0x4003], formatter="{:<6} {:<20} {}")
    """

    def __init__(
        self,
        address: List[Union[gdb.Value, int]] = [],
        formatter="{:<5} {:<36} {}\n",
        break_null=True,
    ):
        self.formatter = formatter  # Address, Function, Source
        self._formatted = None  # Cached formatted backtrace
        self.backtrace = []
        for addr in address:
            if break_null and not addr:
                break
            self.append(int(addr))

    def __eq__(self, value: "Backtrace") -> bool:
        return self.backtrace == value.backtrace

    def __hash__(self) -> int:
        return hash(tuple(self.backtrace))

    def append(self, addr: Union[gdb.Value, int]) -> None:
        """Append an address to the backtrace"""
        if result := utils.Symbol(addr):
            self.backtrace.append(
                (result.address, result.func, result.filename, result.line)
            )
            self._formatted = None  # Clear cached result

    @property
    def formatted(self):
        """Return the formatted backtrace string list"""
        if not self._formatted:
            self._formatted = [
                self.formatter.format(hex(addr), func, f"{file}:{line}")
                for addr, func, file, line in self.backtrace
            ]

        return self._formatted

    def __repr__(self) -> str:
        return f"Backtrace: {len(self.backtrace)} items"

    def __str__(self) -> str:
        return "".join(self.formatted)

    def __iter__(self):
        for item in self.backtrace:
            yield item

    def __getitem__(self, index):
        return self.backtrace.__getitem__(index)

    def toJSON(self):
        return [
            {"address": addr, "function": func, "source": source, "line": line}
            for addr, func, source, line in self.backtrace
        ]


class BacktraceEntry:
    backtrace_entry_s = utils.lookup_type("struct backtrace_entry_s")

    def __init__(self, entry):
        if int(entry) == 0:
            self.entry = None
        else:
            self.entry = utils.Value(entry).cast(self.backtrace_entry_s.pointer())

    def get(self):
        if not self.entry:
            return list()

        stack = utils.ArrayIterator(self.entry.stack, self.entry.depth)
        return [int(addr) for addr in stack]

    def format(self, formatter: str = "{:<5} {:<36} {}\n"):
        if not self.entry:
            return ""

        stack = utils.ArrayIterator(self.entry.stack, self.entry.depth)
        return str(Backtrace(stack))


@autocompeletion.complete
class Addr2Line(gdb.Command):
    """Convert addresses or expressions

    Usage: addr2line address1 address2 expression1
    Example: addr2line 0x1234 0x5678
             addr2line "0x1234 + pointer->abc" &var var->field function_name var
             addr2line $pc $r1 "$r2 + var"
             addr2line [24/08/29 20:51:02] [CPU1] [209] [ap] sched_dumpstack: backtrace| 0: 0x402cd484 0x4028357e
             addr2line -f crash.log
             addr2line -f crash.log -p 123
    """

    formatter = "{:<20} {:<32} {}\n"

    def get_argparser(self):
        parser = argparse.ArgumentParser(
            description="Convert addresses or expressions to source code location"
        )
        parser.add_argument(
            "-f", "--file", type=str, metavar="file", help="Crash log to analyze."
        )
        parser.add_argument(
            "-p",
            "--pid",
            type=int,
            help="Only dump specified task backtrace from crash file.",
        )
        return parser

    def __init__(self):
        super().__init__("addr2line", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    def print_backtrace(self, addresses, pid=None):
        if pid:
            gdb.write(f"\nBacktrace of {pid}\n")
        backtraces = Backtrace(addresses, formatter=self.formatter, break_null=False)
        gdb.write(str(backtraces))

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        if not args:
            gdb.write(Addr2Line.__doc__ + "\n")
            return
        pargs = None
        try:
            pargs, _ = self.parser.parse_known_args(gdb.string_to_argv(args))
        except SystemExit:
            pass

        gdb.write(self.formatter.format("Address", "Symbol", "Source"))

        if pargs and pargs.file:
            pattern = re.compile(
                r".*sched_dumpstack: backtrace\|\s*(\d+)\s*:\s*((?:(0x)?[0-9a-fA-F]+\s*)+)"
            )
            addresses = {}
            with open(pargs.file, "r") as f:
                for line in f:
                    match = pattern.match(line)
                    if not match:
                        continue

                    pid = match.group(1)
                    if pargs.pid is not None and pargs.pid != int(pid):
                        continue

                    addresses.setdefault(pid, [])
                    addresses[pid].extend(
                        [int(addr, 16) for addr in match.group(2).split()]
                    )

            for pid, addr in addresses.items():
                self.print_backtrace(addr, pid)
        else:
            addresses = []
            for arg in shlex.split(args.replace(",", " ")):
                if utils.is_decimal(arg):
                    addresses.append(int(arg))
                elif utils.is_hexadecimal(arg):
                    addresses.append(int(arg, 16))
                else:
                    try:
                        var = utils.parse_and_eval(f"{arg}")
                        addresses.append(var)
                    except gdb.error as e:
                        gdb.write(f"Ignore {arg}: {e}\n")
            self.print_backtrace(addresses)


@autocompeletion.complete
class BacktracePool(gdb.Command):
    """Display the global backtrace information"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument("-d", "--detail", action="store_true")
        parser.add_argument("-t", "--top", type=int, help="Display the top N backtrace")
        return parser

    def __init__(self):
        super().__init__("backtracepool", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    def invoke(self, args, from_tty):
        g_backtrace_pool = utils.parse_and_eval("g_backtrace_pool")
        args = self.parser.parse_args(gdb.string_to_argv(args))

        formatter = "{:>8} {:>8} {:>10} {:}\n"
        btformat = formatter.format("", "", "", "")[:-1] + "{1:<48}{2}\n"
        gdb.write(formatter.format("slot", "depth", "refcount", "stack"))

        backtrace_pool = []
        for i in range(g_backtrace_pool.capacity):
            if not g_backtrace_pool.bucket[i]:
                continue
            entry = BacktraceEntry(g_backtrace_pool.bucket[i])
            entry.index = i
            backtrace_pool.append(entry)

        backtrace_pool.sort(key=lambda x: x.entry.ref, reverse=True)

        if args.top:
            backtrace_pool = backtrace_pool[: args.top]

        for entry in backtrace_pool:
            stack = ""
            if args.detail:
                stack = f"\n{entry.format(btformat)}"
            else:
                stack = " ".join(hex(addr) for addr in entry.get())
            gdb.write(
                formatter.format(entry.index, entry.entry.depth, entry.entry.ref, stack)
            )

        gdb.write(
            f"capacity: {g_backtrace_pool.capacity}, used: {g_backtrace_pool.used}\n"
        )
