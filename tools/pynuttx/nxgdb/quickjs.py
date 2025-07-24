############################################################################
# tools/pynuttx/nxgdb/quickjs.py
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

from . import autocompeletion, backtrace, utils


@autocompeletion.complete
class QjsDump(gdb.Command):
    """Dump the information of backtrace of JSObject in QuickJS"""

    def parse_arguments(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-a",
            "--address",
            type=str,
            metavar="symbol",
            default=None,
            help="Variable or address of a JSObject.",
        )
        return parser

    def __init__(self):
        if utils.has_field("JSObject", "backtrace"):
            super().__init__("qjsdump", gdb.COMMAND_USER)
            self.parser = self.parse_arguments()

    def parse_args(self, arg):
        try:
            return self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool):
        if not (args := self.parse_args(arg)):
            return
        qjs_object = self.get_object(args.address)
        if qjs_object is None:
            gdb.write("invalid jsobject ptr\n")
            return

        if not utils.has_field(qjs_object, "backtrace"):
            gdb.write("this object has not a backtrace\n")
            return

        backtrace = qjs_object["backtrace"]
        jsnative_backtrace = backtrace["js_backtrace"].string()
        gdb.write(f"### dump JSObject backtrace {qjs_object.address} ###\n")
        gdb.write(f"js native backtrace:\n {jsnative_backtrace}\n")
        gdb.write("C/C++ backtrace: \n")
        self.print_jsbacktrace_stack(backtrace)

    def get_object(self, address: str) -> gdb.Value:
        if not address:
            return None

        addr = utils.Value(utils.parse_arg(address))
        return addr.cast(utils.lookup_type("JSObject").pointer()) if addr else None

    def print_jsbacktrace_stack(self, js_backtrace: gdb.Value):
        if utils.has_field(js_backtrace, "backtrace") is not None:
            backtrace_ptr = js_backtrace["backtrace"]
            cnt = int(js_backtrace["cnt"])

            if cnt == 0:
                gdb.write("No backtrace entries found.\n")
                return

            formatter = "{:>1} {:>4} {:>12} {:>12} {:>12} {:>9} {:>14} {:>18} {:}\n"
            leading = formatter.format("", "", "", "", "", "", "", "", "")[:-1]
            btformat = leading + "{1:<48}{2}\n"

            for bt in utils.ArrayIterator(backtrace_ptr, maxlen=cnt):
                gdb.write("BackTrace Entry:\n")
                gdb.write(
                    f"{backtrace.Backtrace(list(utils.ArrayIterator(bt['buffers'], bt['nptrs'])), formatter=btformat)}\n"
                )
