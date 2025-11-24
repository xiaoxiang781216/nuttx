############################################################################
# tools/pynuttx/nxgdb/tricore.py
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

import gdb

from . import autocompeletion, utils
from .backtrace import Backtrace

FCX_FREE_MASK = (0xFFFF << 0) | (0xF << 16)
PCXI_UL = 1 << 20
REG_LPC = 1
REG_UPC = 3

# Register offset in CSA

lower_offsets = [
    ("LPCXI", 0),
    ("LA11", 1),  # LPC
    ("A2", 2),
    ("A3", 3),
    ("D0", 4),
    ("D1", 5),
    ("D2", 6),
    ("D3", 7),
    ("A4", 8),
    ("A5", 9),
    ("A6", 10),
    ("A7", 11),
    ("D4", 12),
    ("D5", 13),
    ("D6", 14),
    ("D7", 15),
]
upper_offsets = [
    ("UPCXI", 0),
    ("PSW", 1),
    ("A10", 2),
    ("UA11", 3),  # UPC
    ("D8", 4),
    ("D9", 5),
    ("D10", 6),
    ("D11", 7),
    ("A12", 8),
    ("A13", 9),
    ("A14", 10),
    ("A15", 11),
    ("D12", 12),
    ("D13", 13),
    ("D14", 14),
    ("D15", 15),
]


def csa2addr(csa):
    return (csa & 0x000F0000) << 12 | (csa & 0x0000FFFF) << 6


class TricoreCSA:
    def __init__(self, addr, isupper=True):
        """
        Initialize TriCore CSA from memory address.
        Default is upper CSA, since tcb->xcp.regs points to upper CSA.

        :param addr: Memory address of the CSA
        :param upper: True for upper CSA, False for lower CSA

        """
        self.addr = addr
        self.isupper = isupper
        self.regs = {}
        for name, offset in upper_offsets if isupper else lower_offsets:
            try:
                self.regs[name] = utils.read_uint(addr + offset * 4)
            except Exception:
                print(
                    f"Error: failed to read register {name} at {hex(addr + offset * 4)}"
                )
                self.regs[name] = 0

    def dump_registers(self):
        """Dump all registers in the CSA."""
        line = ""
        for name, value in self.regs.items():
            line += f"{name}:0x{value:08X}  "
            if len(line) > 60:
                print(line)
                line = ""
        if line:
            print(line)

    def dump(self):
        """Dump CSA information."""
        print(self)
        self.dump_registers()

    @property
    def pc(self):
        """Get the PC value from the CSA."""
        return self.regs["UA11"] if self.isupper else self.regs["LA11"]

    @property
    def pcxi(self):
        """Get the PCXI value from the CSA."""
        return self.regs["UPCXI" if self.isupper else "LPCXI"]

    @property
    def next(self):
        """Get the next CSA from the current CSA."""
        pcxi = self.pcxi  # next CSA
        isupper = bool(pcxi & PCXI_UL)
        csa_addr = csa2addr(pcxi & FCX_FREE_MASK)
        return TricoreCSA(csa_addr, isupper) if csa_addr != 0 else None

    # iterator of all csa list from this one
    def __iter__(self):
        csa = self
        while csa:
            yield csa
            csa = csa.next

    # support using dict-like access
    def __getitem__(self, key):
        return self.regs[key]

    def __repr__(self):
        return f"{'Upper' if self.isupper else 'Lower'}CSA@{hex(self.addr)} PC=0x{self.pc:08X}"

    def __str__(self):
        return self.__repr__()


def csa_from_context(regs) -> TricoreCSA:
    """
    Get the TriCoreCSA from context regs.

    :param regs: The context registers address, usually from tcb->xcp.regs
    """
    # The CSA pointed by tcb->xcp.regs is always upper CSA
    # The lower CSA is stored above by one CSA size (0x40)
    # The lower CSA and upper CSA are linked together
    return TricoreCSA(int(regs) + 0x40, isupper=False)


def csa_from_tcb(tcb) -> TricoreCSA:
    """
    Get the TriCoreCSA from TCB.

    :param tcb: The TCB structure
    """
    if tcb is None:
        return None

    try:
        regs = int(tcb["xcp"]["regs"])
        # The CSA pointed by tcb->xcp.regs is always upper CSA
        return csa_from_context(regs)
    except gdb.error:
        return None


def csa_from_pcxi(pcxi) -> TricoreCSA:
    """
    Get the TriCoreCSA from PCXI value.

    :param pcxi: The PCXI value
    """
    isupper = bool(pcxi & PCXI_UL)
    csa_addr = csa2addr(pcxi & FCX_FREE_MASK)
    return TricoreCSA(csa_addr, isupper) if csa_addr != 0 else None


def dump_csa_chain(csa):
    """
    Dump the CSA chain starting from the given CSA.
    :param csa: The starting CSA
    """

    address = []
    for csa in list(csa):
        address.append(csa.pc)
        csa.dump()
    print(str(Backtrace(address)))


@autocompeletion.complete
class TricoreCSADump(gdb.Command):
    """Dump TriCore CSA list."""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        group = parser.add_mutually_exclusive_group()
        group.add_argument(
            "-p",
            "--pid",
            type=int,
            default=None,
            help="PID of the task to dump CSA",
        )

        group.add_argument(
            "-a",
            "--addr",
            help="The context register address from TCB",
        )

        group.add_argument(
            "--pcxi",
            help="The PCXI register value to dump the CSA chain",
        )
        return parser

    def parse_argument(self, argv):
        try:
            return self.parser.parse_args(argv)
        except SystemExit:
            return None

    def __init__(self):
        arch = gdb.selected_inferior().architecture()
        if arch.name().startswith("TriCore"):
            super().__init__("tricore-dumpcsa", gdb.COMMAND_USER)
            self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        args = self.parse_argument(gdb.string_to_argv(args))
        if args is None:
            return

        if args.pid is not None:
            csa = csa_from_tcb(utils.get_tcb(args.pid))
            dump_csa_chain(csa)
        elif args.pcxi is not None:
            pcxi = utils.parse_arg(args.pcxi)
            csa = csa_from_pcxi(pcxi)
            dump_csa_chain(csa)
        elif args.addr is not None:
            addr = utils.parse_arg(args.addr)
            csa = csa_from_context(addr)
            dump_csa_chain(csa)
        else:
            for tcb in utils.get_tcbs():
                print(
                    f"PID:{tcb['pid']}, state={tcb['task_state']}, regs={tcb['xcp']['regs']}"
                )
                csa = csa_from_tcb(tcb)
                dump_csa_chain(csa)
