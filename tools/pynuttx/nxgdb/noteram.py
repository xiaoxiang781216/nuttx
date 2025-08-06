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


@autocompeletion.complete
class NoteRamCommand(gdb.Command):
    """GDB command to parse and dump noteram datas"""

    def __init__(self):
        if not utils.get_field_nitems("struct noteram_driver_s", "buffer"):
            return
        super().__init__("noteram", gdb.COMMAND_USER)

    def collect_notes(self, driver_name, out_path, save_path=None):
        """Collect notes only if initialization succeeds"""

        noteram = NoteRam(driver_name)
        if not noteram.buffer:
            print("No valid noteram buffer")
            return None

        if save_path:
            with open(save_path, "wb") as f:
                f.write(noteram.buffer)
            print(f"Raw trace data saved to: {path.abspath(save_path)}")

        elf_parser = ELFParser(gdb.objfiles()[0].filename)
        note_parser = NoteParser(elf_parser, output=out_path)
        notes = note_parser.parse(noteram.buffer)
        note_parser.dump()
        note_parser.flush()
        return notes

    def parse_arguments(self, args):
        try:
            return self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return None

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        args = self.parse_arguments(args)

        try:
            self.collect_notes(
                args.driver, path.abspath(args.output_path), args.save_data
            )
        except Exception as e:
            print(f"Error parsing notes: {e}")

    def diagnose(self, *args, **kwargs):
        try:
            notes = self.collect_notes("diagnose_noteram.perfetto")
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
