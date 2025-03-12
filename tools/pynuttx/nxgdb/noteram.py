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
    from nxtrace.trace import NoteFactory
except SystemExit:
    pass

from . import utils


class NoteRam:
    def __init__(self, driver_name: str):
        """Initialize NoteRam object with driver structure"""
        self.driver = None
        self.buffer = None
        self.bufsize = 0
        self.head = 0
        self.read = 0

        self.driver = utils.gdb_eval_or_none(driver_name)
        if not self.driver:
            return
        self.head = int(self.driver["header"]["head"])
        self.read = int(self.driver["header"]["read"])
        self.bufsize = int(self.driver["bufsize"])
        self.buffer = (
            gdb.selected_inferior()
            .read_memory(self.driver["buffer"].cast("uintptr_t"), self.bufsize)
            .tobytes()
        )

    def events(self):
        """Generate events from circular buffer"""
        if not self.buffer or not self.bufsize:
            return

        uintptr_size = utils.sizeof("uintptr_t")
        buffer, bufsize, head, read = (
            self.buffer,
            self.bufsize,
            self.head,
            self.read,
        )
        while (unread := (head - read) % bufsize) > 0:
            event_len = int(buffer[read])
            if event_len <= 0 or event_len > unread:
                raise BufferError(
                    f"Invalid event length: {event_len}, available space {unread}"
                )
            end = read + event_len
            event = bytes(
                buffer[read:end]
                if end <= bufsize
                else buffer[read:bufsize] + buffer[: end % bufsize]
            )
            yield event
            read = (
                read + ((event_len + uintptr_size - 1) & ~(uintptr_size - 1))
            ) % bufsize


class NoteRamCommand(gdb.Command):
    """GDB command to parse and dump noteram datas"""

    def __init__(self):
        if not utils.get_field_nitems("struct noteram_driver_s", "buffer"):
            return
        super().__init__("noteram", gdb.COMMAND_USER)

    def init_note_factory(self, out_path):
        """Initialize NoteFactory"""
        if path := gdb.objfiles()[0].filename:
            NoteFactory.init_instance(ELFParser(path), out_path)
            return True
        return False

    def collect_notes(self, out_path):
        """Collect notes only if initialization succeeds"""
        if not self.init_note_factory(out_path):
            raise RuntimeError("NoteFactory initialization failed")

        notes = []
        noteram = NoteRam("g_noteram_driver")
        if not noteram.buffer:
            print("No valid noteram buffer")
            return notes

        for idx, event in enumerate(noteram.events()):
            try:
                if note := NoteFactory.parse(event):
                    notes.append(note)
            except Exception as e:
                print(f"Error parsing event {idx} ({event}): {e}")
        return notes

    def parse_arguments(self, argv):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-o",
            "--output-path",
            type=str,
            default="noteram.perfetto",
            help="Specify the output path for the Perfetto file",
        )
        try:
            args = parser.parse_args(argv)
        except SystemExit:
            return None
        return args

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        if not (args := self.parse_arguments(gdb.string_to_argv(args))):
            return
        out_path = path.abspath(args.output_path)
        notes = self.collect_notes(out_path)
        if notes:
            NoteFactory.dump(notes)
            NoteFactory.flush()
            print(f"Perfetto file saved to: {out_path}")
        else:
            print("No notes collected, skipping dump")

    def diagnose(self, *args, **kwargs):
        notes = self.collect_notes("diagnose_noteram.perfetto")
        if notes:
            NoteFactory.dump(notes)
            NoteFactory.flush()

        return {
            "title": "Noteram Report",
            "summary": "noteram dump",
            "command": "noteram",
            "result": "info",
            "message": notes,
        }
