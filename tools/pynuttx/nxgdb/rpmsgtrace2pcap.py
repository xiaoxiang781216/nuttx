############################################################################
# tools/pynuttx/nxgdb/rpmsgtrace2pcap.py
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
from os import path
from typing import Optional

import gdb
from rpmsgtrace2pcap import note_binary_parser

from . import autocompeletion, utils
from .noteram import NoteRam


@autocompeletion.complete
class NoteToPcapCommand(gdb.Command):
    """GDB command for converting noteram data to pcap format"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-d",
            "--driver",
            type=str,
            default="g_rpmsg_note_driver",
            help="Noteram driver name",
        )
        parser.add_argument(
            "-o",
            "--output",
            type=str,
            metavar="file",
            default="rpmsgtrace2pcap.perfetto",
            help="Output pcap file path",
        )
        parser.add_argument(
            "-s", "--save-raw", type=str, help="Save raw trace data to path"
        )
        parser.add_argument(
            "-f", "--freq", type=float, help="Frequency value for trace processing"
        )
        return parser

    def __init__(self):
        if not utils.get_field_nitems("struct noteram_driver_s", "buffer"):
            return
        super().__init__("rpmsgtrace2pcap", gdb.COMMAND_USER)
        self.noteram = None
        self.parser = self.get_argparser()

    def parse_arguments(self, args):
        try:
            return self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return None

    def convert_notes(
        self,
        driver_name: str,
        out_path: str,
        save_path: Optional[str] = None,
        freq: Optional[float] = None,
    ) -> Optional[bytes]:

        # Initialize converter
        self.noteram = NoteRam(driver_name)
        self.noteram.collect_notes(out_path, save_path)

        # Call note_binary_parser function and pass freq parameter
        note_binary_parser(self.noteram.elf_parser, self.noteram.note_parser, freq=freq)

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        args = self.parse_arguments(args)
        if not args:
            return

        try:
            self.convert_notes(
                args.driver,
                path.abspath(args.output),
                args.save_raw,
                args.freq,
            )
        except Exception as e:
            print(f"Error in note conversion: {e}")

    def diagnose(self, *args, **kwargs):
        return {
            "title": "NoteToPcap Report",
            "summary": "Note to pcap conversion",
            "command": "rpmsgtrace2pcap",
            "result": "info",
            "category": utils.DiagnoseCategory.system,
            "message": gdb.execute("rpmsgtrace2pcap", to_string=True),
        }
