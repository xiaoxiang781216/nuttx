############################################################################
# tools/pynuttx/nxgdb/stack.py
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

import traceback

import gdb

from . import utils

try:
    STACK_COLORATION_PATTERN = utils.get_symbol_value(
        "STACK_COLOR", locspec="up_create_stack"
    )
except gdb.error:
    STACK_COLORATION_PATTERN = 0xDEADBEEF


class Stack(object):
    def __init__(self, base, size, cursp):
        # We don't care about the stack growth here, base always point to the lower address!
        self._stack_base = base
        self._stack_top = base + size
        self._cur_sp = cursp
        self._stack_size = size

        self._sanity_check()

    def _sanity_check(self):
        # do some basic sanity checking to make sure we have a sane stack object
        if (
            self._stack_base > self._stack_top
            or not self._stack_size
            or self.is_stackof()
        ):

            gdb.write(
                f"base: {hex(self._stack_base)}, \
                size: {hex(self._stack_size)}, sp: {hex(self._cur_sp)}\n"
            )

            gdb.write("Inconsistant stack size...Maybe memory corruption?\n")
            gdb.write("dumping the stack:\n")

            ptr_4bytes = gdb.Value(self._stack_base).cast(
                utils.lookup_type("unsigned int").pointer()
            )

            for i in range(0, self._stack_size // 4):
                if i % 8 == 0:
                    gdb.write(f"{hex(self._stack_base + 4 * i)}: ")

                gdb.write(f"{hex(ptr_4bytes[i]):10} ")

                if i % 8 == 7:
                    gdb.write("\n")

            gdb.write("\n")
            gdb.GdbError(
                "pls check your stack size! sp:{0:x} base:{1:x}".format(
                    self._cur_sp, self._stack_base
                )
            )

    def cur_usage(self):
        return self._stack_top - self._cur_sp

    def check_stack_usage(self) -> int:
        np = utils.import_check("numpy", errmsg="Please pip install numpy\n")
        if not np:
            raise gdb.GdbError(
                "stack max usage check requires numpy, please install it via pip"
            )
        memory = gdb.selected_inferior().read_memory(self._stack_base, self._stack_size)

        color_size = utils.sizeof("int")
        pattern = int(STACK_COLORATION_PATTERN).to_bytes(color_size, byteorder="little")

        arr = np.frombuffer(memory, dtype=np.uint8)
        arr = arr.reshape(-1, color_size)
        pattern_arr = np.frombuffer(pattern * (len(arr)), dtype=np.uint8).reshape(
            -1, color_size
        )

        mismatch = np.any(arr != pattern_arr, axis=1)
        first_diff = np.argmax(mismatch)

        if not mismatch.any():
            used = 0
        else:
            used = self._stack_size - (first_diff * color_size)

        return used

    def max_usage(self):
        if not utils.get_symbol_value("CONFIG_STACK_COLORATION"):
            return 0
        return self.check_stack_usage()

    def avalaible(self):
        return self._stack_size - self.cur_usage()

    def maxdepth_backtrace(self):
        raise gdb.GdbError("Not implemented yet", traceback.print_stack())

    def is_stackof(self) -> bool:
        # we should notify the user if the stack overflow is about to happen as well!
        if utils.check_inferior_valid():
            return (self.check_stack_usage() / self._stack_size) >= 95
        return False


# Always refetch the stack infos, never cached as we may have threads created/destroyed
# dynamically!
def fetch_stacks():
    stacks = dict()

    for tcb in utils.get_tcbs():
        # We have no way to detect if we are in an interrupt context for now.
        # Originally we use `and not utils.in_interrupt_context()`
        sp = utils.get_sp(tcb)

        try:
            stacks[int(tcb["pid"])] = Stack(
                int(tcb["stack_base_ptr"]),
                int(tcb["adj_stack_size"]),
                sp,
            )

        except gdb.GdbError as e:
            pid = tcb["pid"]
            gdb.write(
                f"Failed to construction stack object for tcb {pid} due to: {e}\n"
            )

    return stacks


class StackUsage(gdb.Command):
    """Display the stack usage of each thread, similar to cat /proc/<pid>/stack"""

    def __init__(self):
        super().__init__("stack-usage", gdb.COMMAND_USER)
        self._stacks = []
        # format template
        self._fmt = (
            "{0: <4} | {1: <10} | {2: <10} | {3: <20} | {4: <10} | {5: <15} | {6: <15}"
        )

    def format_print(self, pid, stack, tcb):
        def gen_usage_str(x):
            usage = x / stack._stack_size
            res = f"{usage:.2%}"
            if usage > 0.8:
                res += "!"
            return res

        def gen_info_str(x):
            res = f"{str(x)} -> {gen_usage_str(x)}"
            return res

        if hasattr(self, "table"):
            self.table.add_row(
                [
                    pid,
                    utils.get_task_name(tcb),
                    hex(tcb["entry"]["pthread"]),
                    hex(stack._stack_base),
                    stack._stack_size,
                    str(stack.cur_usage()),
                    gen_usage_str(stack.cur_usage()),
                    str(stack.max_usage()),
                    gen_usage_str(stack.max_usage()),
                ]
            )
        else:
            gdb.write(
                self._fmt.format(
                    pid,
                    utils.get_task_name(tcb),
                    hex(tcb["entry"]["pthread"]),
                    hex(stack._stack_base),
                    stack._stack_size,
                    gen_info_str(stack.cur_usage()),
                    gen_info_str(stack.max_usage()),
                )
            )
            gdb.write("\n")

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        stacks = fetch_stacks()

        args = [int(arg) for arg in args.split()]

        pids = stacks.keys() if len(args) == 0 else args

        prettytable = utils.import_check(
            "prettytable",
            errmsg="Execute `pip install prettytable` for better printing result.\n",
        )
        if prettytable:
            self.table = prettytable.PrettyTable()
            self.table.align = "l"
            self.table.field_names = [
                "PID",
                "NAME",
                "Entry",
                "Base",
                "Size",
                "CurUsage",
                "CurUsage%",
                "MaxUsage",
                "MaxUsage%",
            ]
        else:
            gdb.write(
                self._fmt.format(
                    "Pid", "Name", "Entry", "Base", "Size", "CurUsage", "MaxUsage"
                )
            )
            gdb.write("\n")

        for pid in pids:
            stack = stacks.get(pid)

            if not stack:
                continue

            self.format_print(pid, stack, utils.get_tcb(pid))
        if hasattr(self, "table"):
            gdb.write(f"{self.table.get_string()}\n")

    def diagnose(self, *args, **kwargs):
        return {
            "title": "Stack Usage Report",
            "summary": "Stack usage report",
            "command": "stack-usage",
            "result": "info",
            "category": utils.DiagnoseCategory.memory,
            "message": gdb.execute("stack-usage", to_string=True),
        }
