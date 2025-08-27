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

from . import utils
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


class TricoreCSA(gdb.Command):
    """Dump TriCore CSA list."""

    def csa2addr(self, csa):
        return (csa & 0x000F0000) << 12 | (csa & 0x0000FFFF) << 6

    def get_pcxi_from_tcb(self, tcb):
        return int(tcb["xcp"]["regs"])

    def dump_csa(self, pid, csa_addr):
        print(f"pid:{pid}")
        address = []
        is_upper = False
        line = ""
        while csa_addr != 0:
            offsets = upper_offsets if is_upper else lower_offsets
            pc = utils.read_uint(csa_addr + (REG_UPC if is_upper else REG_LPC) * 4)
            if pc:
                address.append(pc)
            print(f"CSA addr:{hex(csa_addr)} is {'upper' if is_upper else 'lower'}")
            for i, (name, offset) in enumerate(offsets, 1):
                val = utils.read_uint(csa_addr + offset * 4)
                line += f"{name}:0x{val:08X}  "
                if i % 4 == 0:
                    print(line)
                    line = ""

            pcxi = utils.read_uint(csa_addr)  # next CSA
            is_upper = bool(pcxi & PCXI_UL)
            csa_addr = self.csa2addr(pcxi & FCX_FREE_MASK)
        print(str(Backtrace(address)))

    def handle_all(self):
        for tcb in utils.get_tcbs():
            print(
                f"see tid:{tcb['pid']}, state={tcb['task_state']}, regs={tcb['xcp']['regs']}"
            )
            self.dump_csa(int(tcb["pid"]), self.get_pcxi_from_tcb(tcb))

    def handle_pid(self, pid):
        tcb = utils.get_tcb(pid)
        if not tcb:
            print(f"error: no tcb with pid={pid}")
            return
        self.dump_csa(pid, self.get_pcxi_from_tcb(tcb))

    def handle_pcxi(self, csa_addr):
        self.dump_csa(-1, csa_addr)

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-p",
            "-pid",
            type=int,
            dest="pid",
            default=None,
            help="Output the CSA chain of the specified pid",
        )

        parser.add_argument(
            "-u",
            "-pcxi",
            dest="pcxi",
            type=lambda s: int(s, 16),
            help="Output the CSA chain of the specified CSA addr",
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
            self.dont_repeat()
            self.parser = self.get_argparser()

    def invoke(self, args, from_tty):
        args = self.parse_argument(gdb.string_to_argv(args))
        if args is None:
            print("Error:Invalid arg")
            return
        if args.pid is not None:
            self.handle_pid(args.pid)
        elif args.pcxi is not None:
            self.handle_pcxi(args.pcxi)
        else:
            self.handle_all()
