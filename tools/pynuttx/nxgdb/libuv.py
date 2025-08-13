############################################################################
# tools/pynuttx/nxgdb/libuv.py
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

from . import autocompeletion, lists, utils


@autocompeletion.complete
class UVDump(gdb.Command):
    """Dump libuv loop and handle informations"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-l",
            "--loop",
            type=str,
            required=True,
            help="Memory address of uv_loop structure",
        )
        parser.add_argument(
            "--handles",
            action="store_true",
            help="Dump handles information instead of loop data",
        )
        parser.add_argument(
            "--active",
            action="store_true",
            help="Filter only active handles (requires --handles)",
        )
        parser.add_argument(
            "--no-backtrace",
            action="store_true",
            help="Do not print backtrace (requires --handles)",
        )
        return parser

    def __init__(self):
        if not utils.get_symbol_value("CONFIG_LIBUV"):
            return
        super(UVDump, self).__init__("uvdump", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    def dump_loop_info(self, loop_ptr):
        """Dump uv_loop_t structure fields"""
        if not loop_ptr:
            return

        print(loop_ptr.dereference())

    def dump_uv_handles(self, loop_ptr, active_only=False, no_backtrace=False):
        """Dump handle information from uv_loop_t structure"""
        if not loop_ptr:
            return

        UV_HANDLE_FLAG = utils.enum("enum uv_handle_flag")

        formatter = "{:>8} {:>12} {:>18} {:}\n"
        headers = ("Flags", "Type", "Address", "Backtrace")
        gdb.write(formatter.format(*headers))
        gdb.write("-" * 50 + "\n")

        try:
            handle_queue_head = loop_ptr["handle_queue"].address
            handle_queue = lists.NxList(
                handle_queue_head, utils.lookup_type("uv_handle_t"), "handle_queue"
            )
            type_name_map = {
                member.value: member.name.lower()
                for member in utils.enum("uv_handle_type")
            }

            for uv_handle in handle_queue:
                handle_flags = int(uv_handle["flags"])
                is_active = handle_flags & UV_HANDLE_FLAG.HANDLE_ACTIVE.value

                if active_only and not is_active:
                    continue

                handle_type = int(uv_handle["type"])
                type_name = type_name_map.get(handle_type, f"<unknown:{handle_type}>")
                flag_str = (
                    "["
                    + ("R" if handle_flags & UV_HANDLE_FLAG.HANDLE_REF.value else "-")
                    + (
                        "A"
                        if handle_flags & UV_HANDLE_FLAG.HANDLE_ACTIVE.value
                        else "-"
                    )
                    + (
                        "I"
                        if handle_flags & UV_HANDLE_FLAG.HANDLE_INTERNAL.value
                        else "-"
                    )
                    + "]"
                )

                gdb.write(
                    formatter.format(
                        flag_str,
                        type_name,
                        f"0x{int(uv_handle):x}",
                        "",
                    )
                )

                if not no_backtrace:
                    backtrace = tuple(utils.BacktraceEntry(uv_handle["stack"]).get())
                    if backtrace and backtrace[0]:
                        leading = formatter.format("", "", "", "")[:-1]
                        bt_format = leading + "{1:<48}{2}\n"
                        gdb.write(f"{utils.Backtrace(backtrace, formatter=bt_format)}")
        except gdb.error:
            print("Error: Unable to access handle queue.")
            return

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        try:
            parsed_args = self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return

        if not parsed_args.handles and (parsed_args.active or parsed_args.no_backtrace):
            required = []
            if parsed_args.active:
                required.append("--active")
            if parsed_args.no_backtrace:
                required.append("--no-backtrace")

            print(f"Error: Options {', '.join(required)} require --handles")
            return

        loop = utils.gdb_eval_or_none(parsed_args.loop)
        if not loop:
            return

        loop_ptr = loop.cast(utils.lookup_type("uv_loop_t").pointer())
        if parsed_args.handles:
            self.dump_uv_handles(loop_ptr, parsed_args.active, parsed_args.no_backtrace)
        else:
            self.dump_loop_info(loop_ptr)
