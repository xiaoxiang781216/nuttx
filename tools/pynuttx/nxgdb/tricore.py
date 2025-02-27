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
from .utils import Backtrace

FCX_FREE_MASK = (0xFFFF << 0) | (0xF << 16)
PCXI_UL = 1 << 20
REG_UPC = 3


class TricoreCSA(gdb.Command):
    """Dump TriCore CSA list."""

    def csa2addr(self, csa):
        return (csa & 0x000F0000) << 12 | (csa & 0x0000FFFF) << 6

    def get_pcxi_from_tcb(self, tcb):
        if utils.task_is_running(tcb):
            frame = gdb.newest_frame()
            pcxi = frame.read_register("pcx") & FCX_FREE_MASK
            return self.csa2addr(pcxi)
        else:
            return int(tcb["xcp"]["regs"])

    def dump_csa(self, pid, pcxi):
        print(f"pid:{pid}")
        address = []
        while pcxi != 0:
            addr = utils.read_uint(pcxi)
            pc = utils.read_uint(pcxi + REG_UPC * 4)
            is_upper = bool(addr & PCXI_UL)
            if pc:
                print(f"pcxi:{hex(pcxi)} upper:{is_upper} pc:0x{pc:X}")
                address.append(pc)

            pcxi = FCX_FREE_MASK & addr
            pcxi = self.csa2addr(pcxi)
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

    def handle_pcxi(self, pcxi):
        self.dump_csa(-1, pcxi)

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
