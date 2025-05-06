############################################################################
# tools/pynuttx/nxtrace/trace.py
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

import functools
import logging
import sys

from .perfetto_trace import PerfettoTrace, TaskInfo, TaskState, TraceHead

try:
    from construct import Adapter, Bytes, Container, CString, Struct, this
except ModuleNotFoundError:
    print("Please execute the following command to install dependencies:")
    print("pip install construct")
    exit(1)

# Corresponds to the tstate_e enumeration type in nuttx

Tstate = None
logger = logging.getLogger(__name__)


class TaskNameCache:
    _instances = {}

    def __new__(cls, pid, name):
        cls._instances.setdefault(pid, name)
        return cls._instances[pid]

    @classmethod
    def find(cls, pid):
        return cls._instances.get(pid, "unknown")


class StringAdapter(Adapter):
    def _decode(self, obj, context, path):
        name = obj.split(b"\0", 1)[0].decode("utf-8")
        return name

    def _encode(self, obj, context, path):
        return obj.encode("utf-8") + b"\0"


def Tstate2State(state):
    if state == Tstate.TSTATE_TASK_INVALID:
        return TaskState.DEAD
    elif state <= Tstate.TSTATE_TASK_RUNNING:
        return TaskState.RUNNING
    else:
        return TaskState.INTERRUPTIBLE


def GenTraceHead(note):
    return TraceHead(note.nc_systime, note.nc_pid, note.nc_cpu)


class NoteFactory:
    def __new__(cls, elf_parser):
        if cls.instance is None:
            cls.instance = super().__new__(cls)
            cls.instance.parser = elf_parser
            cls.init_instance(elf_parser)
        return cls.instance

    @classmethod
    def init_instance(cls, elf_parser, output):
        if elf_parser is None:
            raise TypeError("Value of 'elf_parser' cannot be None")

        cls.parser = elf_parser
        cls.ptrace = PerfettoTrace(output)
        cls.NCPUS = elf_parser.macro("CONFIG_SMP_NCPUS")
        cls.types = elf_parser.get_type("note_type_e")
        global Tstate
        Tstate = elf_parser.get_type("tstate_e")
        cls.note_common_s = cls.parser.get_type("note_common_s")

    @classmethod
    @functools.lru_cache(maxsize=None)
    def struct(cls, note_type):
        note_event_s = Struct(
            *[
                field
                for field in cls.parser.get_type("note_event_s").subcons
                if field.name != "nev_data"
            ]
        )
        cls.note_event_s = Struct(
            *[field for field in note_event_s.subcons if field.name != "nev_data"],
            "nev_data" / Bytes(this.nev_cmn.nc_length - note_event_s.sizeof()),
        )

        if note_type == cls.types.NOTE_START:
            return Struct(
                "nst_cmn" / cls.note_common_s,
                "name" / CString("utf8"),
            )
        elif note_type == cls.types.NOTE_STOP:
            return cls.parser.get_type("note_stop_s")
        elif note_type == cls.types.NOTE_SUSPEND:
            return cls.parser.get_type("note_suspend_s")
        elif note_type == cls.types.NOTE_RESUME:
            return cls.parser.get_type("note_resume_s")
        elif note_type in [cls.types.NOTE_IRQ_ENTER, cls.types.NOTE_IRQ_LEAVE]:
            return cls.parser.get_type("note_irqhandler_s")
        elif note_type in [cls.types.NOTE_SYSCALL_ENTER, cls.types.NOTE_SYSCALL_LEAVE]:
            return cls.parser.get_type("note_syscall_s")
        elif note_type in [
            cls.types.NOTE_DUMP_BEGIN,
            cls.types.NOTE_DUMP_END,
            cls.types.NOTE_DUMP_MARK,
        ]:
            return cls.note_event_s
        elif note_type == cls.types.NOTE_DUMP_PRINTF:
            note_printf_s = cls.parser.get_type("note_printf_s")
            return Struct(
                *[field for field in note_printf_s.subcons if field.name != "npt_data"],
                "npt_data" / Bytes(this.npt_cmn.nc_length - note_printf_s.sizeof()),
            )
        elif note_type == cls.types.NOTE_DUMP_COUNTER:
            note_counter_s = cls.parser.get_type("note_counter_s")

            return Struct(
                *note_event_s.subcons,
                *[field for field in note_counter_s.subcons if field.name != "name"],
                "name" / CString("utf8"),
            )
        elif note_type == cls.types.NOTE_DUMP_THREADTIME:
            note_threadtime_s = cls.parser.get_type("note_threadtime_s")
            return Struct(
                *note_event_s.subcons,
                *note_threadtime_s.subcons,
            )
        else:
            logger.error(f"Unknown note type: {note_type}")
            return None

    @classmethod
    def parse(cls, data):
        common = cls.note_common_s.parse(data)
        struct = cls.struct(common.nc_type)
        note = struct.parse(data[: common.nc_length])
        logger.debug(note)

        class Note(Container):
            def __getattr__(self, name):
                if name not in self.keys():
                    for key in self.keys():
                        if isinstance(self[key], Container):
                            return getattr(self[key], name)
                return self[name]

        return Note(note)

    @classmethod
    def dump(cls, notes=None, output="trace.perfetto"):
        sched_switch = {
            "intr_nest": 0,
            "pendingswitch": False,
            "current_pid": -1,
            "current_priority": -1,
            "current_state": -1,
            "next_pid": -1,
            "next_priority": -1,
        }

        for note in notes:
            logger.debug(note)
            head = GenTraceHead(note)

            if note.nc_type == cls.types.NOTE_START:
                TaskNameCache(note.nc_pid, note.name)
                task = TaskInfo(note.name, note.nc_pid, note.nc_priority)
                cls.ptrace.sched_wakeup_new(head, task)
            elif note.nc_type == cls.types.NOTE_STOP:
                sched_switch["current_state"] = Tstate.TSTATE_TASK_INVALID
            elif note.nc_type == cls.types.NOTE_SUSPEND:
                sched_switch["current_state"] = note.nsu_state
            elif note.nc_type == cls.types.NOTE_RESUME:
                sched_switch["next_pid"] = note.nc_pid
                sched_switch["next_priority"] = note.nc_priority

                # If we are in an interrupt, we need to wake up and suspend the task
                if sched_switch["intr_nest"]:
                    current = TaskInfo(
                        TaskNameCache.find(note.nc_pid), note.nc_pid, note.nc_priority
                    )
                    sched_switch["pendingswitch"] = True
                    cls.ptrace.sched_waking(head, current)
                else:
                    # If there is no interrupt, we need to switch the task
                    prev = TaskInfo(
                        TaskNameCache.find(sched_switch["current_pid"]),
                        sched_switch["current_pid"],
                        sched_switch["current_priority"],
                    )
                    next = TaskInfo(
                        TaskNameCache.find(sched_switch["next_pid"]),
                        sched_switch["next_pid"],
                        sched_switch["next_priority"],
                    )
                    state = Tstate2State(sched_switch["current_state"])
                    cls.ptrace.sched_switch(head, state, prev, next)
                    sched_switch["current_pid"] = sched_switch["next_pid"]
                    sched_switch["current_priority"] = sched_switch["next_priority"]
            elif note.nc_type == cls.types.NOTE_IRQ_ENTER:
                sched_switch["intr_nest"] += 1
                cls.ptrace.irq_entry(
                    head, note.nih_irq, f"{note.nih_irq}, {note.nih_handler}", 0
                )
            elif note.nc_type == cls.types.NOTE_IRQ_LEAVE:
                sched_switch["intr_nest"] -= 1
                cls.ptrace.irq_exit(head, note.nih_irq, 0)

                if sched_switch["pendingswitch"]:
                    # If there is a suspended task, perform task switching when the interrupt exits
                    state = Tstate2State(sched_switch["current_state"])
                    prev = TaskInfo(
                        TaskNameCache.find(sched_switch["current_pid"]),
                        sched_switch["current_pid"],
                        sched_switch["current_priority"],
                    )
                    next = TaskInfo(
                        TaskNameCache.find(sched_switch["next_pid"]),
                        sched_switch["next_pid"],
                        sched_switch["next_priority"],
                    )
                    cls.ptrace.sched_switch(head, state, prev, next)
                    sched_switch["pendingswitch"] = False
                    sched_switch["current_pid"] = sched_switch["next_pid"]
                    sched_switch["current_priority"] = sched_switch["next_priority"]
            elif note.nc_type == cls.types.NOTE_DUMP_BEGIN:
                if len(note.nev_data) > 0:
                    cls.ptrace.atrace_begin(head, str(note.nev_data))
                else:
                    sym = cls.parser.addr2symbol(note.nev_ip)
                    cls.ptrace.atrace_begin(head, sym if sym else f"0x{note.nev_ip:x}")
            elif note.nc_type == cls.types.NOTE_DUMP_END:
                if len(note.nev_data) > 0:
                    cls.ptrace.atrace_end(head, str(note.nev_data))
                else:
                    sym = cls.parser.addr2symbol(note.nev_ip)
                    cls.ptrace.atrace_end(head, sym if sym else f"0x{note.nev_ip:x}")
            elif note.nc_type == cls.types.NOTE_DUMP_MARK:
                cls.ptrace.atrace_instant(head, str(note.nev_data))
            elif note.nc_type == cls.types.NOTE_DUMP_COUNTER:
                cls.ptrace.atrace_int(head, str(note.name), note.value)
            elif note.nc_type == cls.types.NOTE_DUMP_THREADTIME:
                cls.ptrace.atrace_int(head, "threadtime", note.elapsed)

            logger.debug(sched_switch)

    @classmethod
    def flush(cls):
        cls.ptrace.flush()


class NoteParser:

    def __init__(self, parser, cache_size=0, output=None):
        self.notes = list()
        self.cache_size = cache_size
        self.buffer = bytearray()
        self.parser = parser
        self.output = output
        NoteFactory.init_instance(parser, output)

    def dump(self, notes=None):
        output = self.output if self.output is sys.stdout else "trace.perfetto"
        notes = self.notes if notes is None else notes
        NoteFactory.dump(notes, output)

    def flush(self):
        NoteFactory.flush()
        print(f"note parser flush to file: {self.output}")

    def parse(self, data):
        self.buffer.extend(data)
        notes = list()
        while len(self.buffer):
            try:
                note = NoteFactory.parse(self.buffer)
            except Exception as e:
                logger.error(f"skip bytes: {self.buffer[0]} {e}")
                self.buffer = self.buffer[1:]
                continue

            if note is None:
                break

            notes.append(note)
            self.notes.append(note)
            self.buffer = self.buffer[note.nc_length :]
            if self.cache_size > 0 and len(notes) >= self.cache_size:
                self.notes.pop(0)

        return notes

    def parse_file(self, path):
        with open(path, "rb") as f:
            data = f.read()
            self.parse(data)
