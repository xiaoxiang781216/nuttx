############################################################################
# tools/pynuttx/nxgdb/noteram.py
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

try:
    from nxelf.elf import ELFParser
    from nxtrace.trace import NoteParser
except SystemExit:
    pass

from . import autocompeletion, utils


class NoteRam:
    def __init__(self, driver_name: str):
        """Initialize NoteRam object with driver structure"""

        self.driver = utils.gdb_eval_or_none(driver_name)
        self.elf_parser = None
        self.note_parser = None
        if not self.driver:
            return

        noteram_driver_s = utils.lookup_type("struct noteram_driver_s")
        if self.driver.type.code == gdb.TYPE_CODE_PTR:
            self.driver = self.driver.cast(noteram_driver_s.pointer())

        head = int(self.driver["header"]["head"])
        tail = int(self.driver["header"]["tail"])
        bufsize = int(self.driver["bufsize"])
        address = int(self.driver["buffer"])

        if head == tail:
            self.buffer = b""
            return

        gdbif = gdb.selected_inferior()
        if head > tail:
            available = head - tail
            rawdata = gdbif.read_memory(address + tail, available).tobytes()
        else:
            available = bufsize - tail + head
            remaining = bufsize - tail
            rawdata = gdbif.read_memory(address + tail, remaining).tobytes()
            rawdata += gdbif.read_memory(address, available - remaining).tobytes()

        self.buffer = self._process_events(rawdata)

    def _process_events(self, rawdata):
        """Process all trace data with alignment"""

        uintptr_size = utils.sizeof("uintptr_t")
        tracedata = bytes()
        offset = 0

        while offset < len(rawdata):
            notelen = int(rawdata[offset])
            if notelen <= 0 or offset + notelen > len(rawdata):
                raise BufferError(f"Invalid event length: {notelen}")

            event = rawdata[offset : offset + notelen]
            offset += (notelen + uintptr_size - 1) & ~(uintptr_size - 1)
            tracedata += event

        return tracedata

    def collect_notes(
        self, out_path: str, save_path: Optional[str] = None
    ) -> Optional[bytes]:
        """
        Collect notes and return notes data
        Returns None if initialization fails
        """
        if not self.buffer:
            print("No valid noteram buffer")
            return None

        if save_path:
            with open(save_path, "wb") as f:
                f.write(self.buffer)
            print(f"Raw trace data saved to: {path.abspath(save_path)}")

        self.elf_parser = ELFParser(gdb.objfiles()[0].filename)
        self.note_parser = NoteParser(self.elf_parser, output=out_path)
        notes = self.note_parser.parse(self.buffer)
        self.note_parser.dump()
        self.note_parser.flush()

        return notes


@autocompeletion.complete
class NoteRamCommand(gdb.Command):
    """GDB command to parse and dump noteram datas"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-d",
            "--driver",
            type=str,
            metavar="driver",
            default="g_noteram_driver",
            help="Specify the noteram driver name",
        )
        parser.add_argument(
            "-o",
            "--output-path",
            type=str,
            metavar="file",
            default="noteram.perfetto",
            help="Specify the output path for the Perfetto file",
        )
        parser.add_argument(
            "-s",
            "--save_data",
            type=str,
            help="Specify the save path for the raw trace data",
        )
        return parser

    def __init__(self):
        if not utils.get_field_nitems("struct noteram_driver_s", "buffer"):
            return
        super().__init__("noteram", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    def parse_arguments(self, args):
        try:
            return self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return None

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        args = self.parse_arguments(args)

        try:
            noteram = NoteRam(args.driver)
            noteram.collect_notes(path.abspath(args.output_path), args.save_data)
        except Exception as e:
            print(f"Error parsing notes: {e}")

    def diagnose(self, *args, **kwargs):
        try:
            noteram = NoteRam("diagnose_noteram.perfetto")
            notes = noteram.collect_notes()
        except Exception as e:
            notes = f"No notes collected {e}"

        return {
            "title": "Noteram Report",
            "summary": "noteram dump",
            "command": "noteram",
            "result": "info",
            "category": utils.DiagnoseCategory.system,
            "message": notes,
        }
