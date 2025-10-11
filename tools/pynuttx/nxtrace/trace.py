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
import re
import struct
import sys
from abc import ABC, abstractmethod
from typing import Any, List, Optional

from .perfetto_trace import PerfettoTrace, TaskInfo, TaskState, TraceHead

try:
    from construct import Adapter, Bytes, Container, CString, FixedSized, Struct, this
    from tqdm import tqdm
except ModuleNotFoundError:
    print("Please execute the following command to install dependencies:")
    print("pip install construct tqdm")
    exit(1)

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
    timestamp_ns = NoteFactory.cpu_cycles_to_ns(note.nc_systime)
    return TraceHead(timestamp_ns, note.nc_pid, note.nc_cpu)


class PluginContext:
    def __init__(self, ptrace: PerfettoTrace, parser, note_factory, plugin_name: str):
        self.ptrace = ptrace
        self.parser = parser
        self.note_factory = note_factory
        self.plugin_name = plugin_name


class NotePlugin(ABC):

    # Default attribute
    name = None

    @abstractmethod
    def can_handle(self, note_type: int) -> bool:
        """Check if the plugin can handle the note type"""
        pass

    @abstractmethod
    def setup(self, context: PluginContext) -> None:
        """Set up the plugin context"""
        pass

    @abstractmethod
    def process(
        self, note: Container, head: TraceHead, context: PluginContext
    ) -> Optional[Any]:
        """
        Process note data

        Args:
            note: Parsed note data
            head: Trace head information
            context: Plugin-specific context
        """
        pass

    @abstractmethod
    def teardown(self, context: PluginContext) -> None:
        """Teardown the plugin context"""
        pass

    def get_name(self) -> str:
        return getattr(self.__class__, "name", None) or self.__class__.__name__


class PluginManager:

    def __init__(self):
        self.plugins: List[NotePlugin] = []
        self.plugin_contexts: dict[str, PluginContext] = {}
        self.ptrace: Optional[PerfettoTrace] = None
        self.parser = None
        self.note_factory = None

    def register_plugin(self, plugin: NotePlugin):
        if not isinstance(plugin, NotePlugin):
            raise TypeError(f"Plugin must inherit from NotePlugin, got {type(plugin)}")

        self.plugins.append(plugin)
        logger.info(f"Registered plugin: {plugin.get_name()}")

    def register_plugins(self, plugins: List[NotePlugin]):
        for plugin in plugins:
            self.register_plugin(plugin)

    def set_context_data(self, ptrace: PerfettoTrace, parser, note_factory):
        self.ptrace = ptrace
        self.parser = parser
        self.note_factory = note_factory

        # create plugin context for each plugin
        self.plugin_contexts = {}
        for plugin in self.plugins:
            plugin_name = plugin.get_name()
            context = PluginContext(ptrace, parser, note_factory, plugin_name)
            self.plugin_contexts[plugin_name] = context

            try:
                plugin.setup(context)
                logger.debug(f"Created independent context for plugin: {plugin_name}")
            except Exception as e:
                logger.error(f"Plugin {plugin_name} setup failed: {e}")

    def teardown_all(self):
        for plugin in self.plugins:
            try:
                plugin_name = plugin.get_name()
                context = self.plugin_contexts.get(plugin_name)
                if context:
                    plugin.teardown(context)
                    logger.debug(f"Teardown completed for plugin: {plugin_name}")
            except Exception as e:
                logger.error(f"Plugin {plugin.get_name()} teardown failed: {e}")

    def process_note(self, note: Container, head: TraceHead) -> bool:
        if not self.ptrace:
            logger.warning("Plugin context data not set")
            return

        for plugin in self.plugins:
            if plugin.can_handle(note.nc_type):
                try:
                    plugin_name = plugin.get_name()
                    context = self.plugin_contexts[plugin_name]
                    plugin.process(note, head, context)
                except Exception as e:
                    logger.error(
                        f"Plugin {plugin.get_name()} failed to process note: {e}"
                    )

    def get_plugins_for_type(self, note_type: int) -> List[NotePlugin]:
        """Get all plugins that can handle the specified note type"""
        return [plugin for plugin in self.plugins if plugin.can_handle(note_type)]

    def get_plugin_context(self, plugin_name: str) -> Optional[PluginContext]:
        """Get the context of the specified plugin"""
        return self.plugin_contexts.get(plugin_name)

    def get_all_plugin_contexts(self) -> dict[str, PluginContext]:
        """Get the context of all plugins"""
        return self.plugin_contexts.copy()


class SchedState:
    """Class to manage scheduling state"""

    def __init__(self):
        self.intr_nest = 0
        self.intr_nest_irq = -1
        self.pending_switch = False
        self.current_pid = -1
        self.current_priority = -1
        self.current_state = -1
        self.next_pid = -1
        self.next_priority = -1

    def reset_next_task(self):
        """Reset the next task information"""
        self.next_pid = -1
        self.next_priority = -1

    def switch_to_next(self):
        """Switch to the next task"""
        self.current_pid = self.next_pid
        self.current_priority = self.next_priority
        self.pending_switch = False

    def enter_interrupt(self, irq):
        """Enter interrupt"""
        self.intr_nest += 1
        self.intr_nest_irq = irq

    def exit_interrupt(self):
        """Exit interrupt"""
        self.intr_nest -= 1

    def is_in_interrupt(self):
        """Check if in interrupt"""
        return self.intr_nest > 0

    def has_pending_switch(self):
        """Check if there is a pending task switch"""
        return self.pending_switch


class NoteProcessor(ABC):
    """Note processor base class"""

    @abstractmethod
    def process(self, note, head, sched_state, ptrace, parser):
        """Process note"""
        pass


class TaskStartProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        TaskNameCache(note.nc_pid, note.name)
        task = TaskInfo(note.name, note.nc_pid, note.nc_priority)
        ptrace.sched_wakeup_new(head, task)


class TaskStopProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        sched_state.current_state = Tstate.TSTATE_TASK_INVALID


class TaskSuspendProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        sched_state.current_state = note.nsu_state


class TaskResumeProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        sched_state.next_pid = note.nc_pid
        sched_state.next_priority = note.nc_priority

        if sched_state.is_in_interrupt():
            current = TaskInfo(
                TaskNameCache.find(note.nc_pid), note.nc_pid, note.nc_priority
            )
            sched_state.pending_switch = True
            ptrace.sched_waking(head, current)
        else:
            self._perform_task_switch(head, sched_state, ptrace)

    def _perform_task_switch(
        self, head: TraceHead, sched_state: SchedState, ptrace: PerfettoTrace
    ):
        prev = TaskInfo(
            TaskNameCache.find(sched_state.current_pid),
            sched_state.current_pid,
            sched_state.current_priority,
        )
        next_task = TaskInfo(
            TaskNameCache.find(sched_state.next_pid),
            sched_state.next_pid,
            sched_state.next_priority,
        )
        state = Tstate2State(sched_state.current_state)
        ptrace.sched_switch(head, state, prev, next_task)
        sched_state.switch_to_next()


class IRQEnterProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace: PerfettoTrace, parser):
        if sched_state.intr_nest > 0:
            sched_state.exit_interrupt()
            ptrace.irq_exit(head, sched_state.intr_nest_irq, 0)

        sched_state.enter_interrupt(note.nih_irq)
        name = parser.addr2symbol(note.nih_handler) or f"0x{note.nih_handler:x}"
        ptrace.irq_entry(head, note.nih_irq, f"{note.nih_irq}: {name}", 0)


class IRQLeaveProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace: PerfettoTrace, parser):
        sched_state.exit_interrupt()
        ptrace.irq_exit(head, note.nih_irq, 0)

        if sched_state.has_pending_switch():
            self._handle_pending_switch(head, sched_state, ptrace)

    def _handle_pending_switch(self, head, sched_state, ptrace):
        state = Tstate2State(sched_state.current_state)
        prev = TaskInfo(
            TaskNameCache.find(sched_state.current_pid),
            sched_state.current_pid,
            sched_state.current_priority,
        )
        next_task = TaskInfo(
            TaskNameCache.find(sched_state.next_pid),
            sched_state.next_pid,
            sched_state.next_priority,
        )
        ptrace.sched_switch(head, state, prev, next_task)
        sched_state.switch_to_next()


class DumpBeginProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        if len(note.nev_data) > 0:
            ptrace.atrace_begin(head, str(note.nev_data))
        else:
            sym = parser.addr2symbol(note.nev_ip)
            ptrace.atrace_begin(head, sym if sym else f"0x{note.nev_ip:x}")


class DumpEndProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        if len(note.nev_data) > 0:
            ptrace.atrace_end(head, str(note.nev_data))
        else:
            sym = parser.addr2symbol(note.nev_ip)
            ptrace.atrace_end(head, sym if sym else f"0x{note.nev_ip:x}")


class DumpMarkProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        ptrace.atrace_instant(head, str(note.nev_data))


class DumpCounterProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        ptrace.atrace_int(head, str(note.name), note.value)


class NotePrintf:
    format_pattern = re.compile(
        r"%(?P<flags>[-+ #0]*)?(?P<width>\d+|\*)?(?:\.(?P<precision>\d+|\*))?"
        r"(?P<length>[hljztL]|ll|hh)?(?P<specifier>[diufFeEgGxXoscpn%])"
    )

    def _parse_format_params(self, parser, groups, data):
        offset = 0
        byteorder = parser.info["byteorder"]

        def get_value(key):
            nonlocal offset
            if groups[key] == "*":
                value = int.from_bytes(
                    data[offset : offset + 4],
                    byteorder=byteorder,
                    signed=True,
                )
                offset += 4
                return value
            elif groups[key]:
                return int(groups[key])
            return None

        return get_value("width"), get_value("precision"), offset

    def _build_format_spec(self, flags, width, precision, specifier, value):
        """
        Python format string syntax:
            format_spec ::= [options][width][grouping]["." precision][type]
            options     ::= [[fill]align][sign]["z"]["#"]["0"]
            align       ::= "<" | ">" | "=" | "^"
            sign        ::= "+" | "-" | " "
            width       ::= digit+
            precision   ::= digit+
            type        ::= "b" | "c" | "d" | "e" | "E" | "f" | "F" | "g"
                            | "G" | "n" | "o" | "s" | "x" | "X" | "%"
        """

        fmt = ""
        specifier_map = {"i": "d", "u": "d", "p": "#x", "A": "E", "a": "e"}
        specifier = specifier_map.get(specifier, specifier)

        # Process printf flags according to Python format priority
        if "-" in flags:
            fmt += "<"
        elif not isinstance(value, int):
            fmt += ">"

        if "+" in flags:
            fmt += "+"
        elif " " in flags:
            fmt += " "

        if "#" in flags and specifier in "o":
            value = f"0{value:o}"
            specifier = "s"
        elif "#" in flags:
            fmt += "#"

        if "0" in flags and width is not None:
            fmt += "0"

        # Python format string no support precision for int,
        # so we need to handle it manually
        if precision is not None and isinstance(value, int):
            if "<" not in fmt:
                fmt = ">" + fmt
            value = f"{value:0{precision}}"
            precision = None
            specifier = "s"

        if width is not None:
            fmt += str(width)

        if precision is not None:
            fmt += f".{precision}"

        # Build format string
        return f"{{:{fmt}{specifier}}}".format(value)

    def _extract_value(self, parser, groups, data, offset):
        # Get format parameters %[flags][width][.precision][length]specifier
        size_t = 4 if parser.info["size_t"] == "uint32" else 8
        flags = groups["flags"]
        width = groups["width"]
        precision = groups["precision"]
        length = groups["length"]
        specifier = groups["specifier"]
        value = None

        # Parse format parameters (width and precision)
        width, precision, param_offset = self._parse_format_params(
            parser, groups, data[offset:]
        )
        offset += param_offset

        # Extract value based on specifier
        if specifier == "c":
            value = data[offset]
            offset += 4
        elif specifier in "diuxXop":
            length_map = {"ll": 8, "l": size_t, "z": size_t, "t": size_t}
            length = length_map.get(length, size_t if specifier == "p" else 4)
            signed = specifier in ("d", "i")

            value = int.from_bytes(
                data[offset : offset + length],
                byteorder=parser.info["byteorder"],
                signed=signed,
            )
            offset += length
        elif specifier in "fFeEgGaA":
            length = 16 if length == "L" else 8
            value = struct.unpack("<d", data[offset : offset + length])[0]
            offset += length
        elif specifier == "s":
            string = data[offset:].split(b"\x00")[0]
            offset += len(string) + 1
            value = string.decode("utf-8", errors="ignore")

        formatted = self._build_format_spec(flags, width, precision, specifier, value)
        return formatted, offset

    def printf(self, parser, format, data):
        result = ""
        offset = 0
        end = 0

        # Find all format specifiers
        for match in self.format_pattern.finditer(format):
            if match.start() > end:
                result += format[end : match.start()]

            part = match.group(0)
            if part == "%%":
                result += "%"
            else:
                groups = match.groupdict()
                value, offset = self._extract_value(parser, groups, data, offset)
                result += value

            end = match.end()

        # Add remaining plain text
        if end < len(format):
            result += format[end:]

        return result


class DumpPrintfProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        result = parser.readstring(note.npt_fmt)
        result = NotePrintf().printf(parser, result, note.npt_data)
        ptrace.atrace_instant(head, result)


class DumpBinaryProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        ptrace.atrace_instant(head, note.nev_data)


class DumpThreadTimeProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace, parser):
        ptrace.atrace_int(head, "threadtime", note.elapsed)


class HeapAddProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace: PerfettoTrace, parser):
        # Use an implicit trace-global track (uuid = 0)
        ptrace.atrace_instant(
            head,
            f"Add heap: 0x{note.heap:x}, size: {note.size}, mem: 0x{note.mem:x}",
        )
        ptrace.trace_counter(None, head.ts, f"heap: 0x{note.heap:x}", 0)


class HeapRemoveProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace: PerfettoTrace, parser):
        # Use size of 0 to indicate heap removal
        ptrace.atrace_instant(
            head,
            f"Remove heap: 0x{note.heap:x}, size: {note.size}, mem: 0x{note.mem:x}",
        )
        ptrace.trace_counter(None, head.ts, f"heap: 0x{note.heap:x}", 0)


class HeapAllocProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace: PerfettoTrace, parser):
        ptrace.atrace_instant(head, f"Alloc: 0x{note.mem:x}, size: {note.size}")
        ptrace.trace_counter(None, head.ts, f"heap: 0x{note.heap:x}", note.used)


class HeapFreeProcessor(NoteProcessor):
    def process(self, note, head, sched_state, ptrace: PerfettoTrace, parser):
        ptrace.atrace_instant(head, f"Free: 0x{note.mem:x}, size: {note.size}")
        ptrace.trace_counter(None, head.ts, f"heap: 0x{note.heap:x}", note.used)


class NoteProcessorRegistry:

    def __init__(self):
        self.processors = {}

    def register_processor(self, note_type, processor):
        self.processors[note_type] = processor
        logger.debug(
            f"Registered processor {processor.__class__.__name__} for note type {note_type}"
        )

    def setup_default_processors(self, types):
        self.register_processor(types.NOTE_START, TaskStartProcessor())
        self.register_processor(types.NOTE_STOP, TaskStopProcessor())
        self.register_processor(types.NOTE_SUSPEND, TaskSuspendProcessor())
        self.register_processor(types.NOTE_RESUME, TaskResumeProcessor())
        self.register_processor(types.NOTE_IRQ_ENTER, IRQEnterProcessor())
        self.register_processor(types.NOTE_IRQ_LEAVE, IRQLeaveProcessor())
        self.register_processor(types.NOTE_DUMP_BEGIN, DumpBeginProcessor())
        self.register_processor(types.NOTE_DUMP_END, DumpEndProcessor())
        self.register_processor(types.NOTE_DUMP_MARK, DumpMarkProcessor())
        self.register_processor(types.NOTE_DUMP_COUNTER, DumpCounterProcessor())
        self.register_processor(types.NOTE_DUMP_PRINTF, DumpPrintfProcessor())
        self.register_processor(types.NOTE_DUMP_BINARY, DumpBinaryProcessor())
        self.register_processor(types.NOTE_DUMP_THREADTIME, DumpThreadTimeProcessor())
        self.register_processor(types.NOTE_HEAP_ADD, HeapAddProcessor())
        self.register_processor(types.NOTE_HEAP_REMOVE, HeapRemoveProcessor())
        self.register_processor(types.NOTE_HEAP_ALLOC, HeapAllocProcessor())
        self.register_processor(types.NOTE_HEAP_FREE, HeapFreeProcessor())

    def get_processor(self, note_type):
        return self.processors.get(note_type)


class DefaultNoteProcessorPlugin(NotePlugin):
    name = "DefaultNoteProcessor"

    def can_handle(self, note_type: int) -> bool:
        return True

    def setup(self, context: PluginContext) -> None:
        self.processor_registry = NoteProcessorRegistry()
        self.processor_registry.setup_default_processors(NoteFactory.types)
        context.sched_state = SchedState()

    def process(
        self, note: Container, head: TraceHead, context: PluginContext
    ) -> Optional[Any]:
        sched_state = context.sched_state
        ptrace = context.ptrace
        parser = context.parser

        processor = self.processor_registry.get_processor(note.nc_type)
        if processor:
            processor.process(note, head, sched_state, ptrace, parser)

        return None

    def teardown(self, context: PluginContext) -> None:
        pass


class NoteFactory:
    instance = None

    def __new__(cls, elf_parser):
        if cls.instance is None:
            cls.instance = super().__new__(cls)
            cls.instance.parser = elf_parser
            cls.init_instance(elf_parser)
        return cls.instance

    @classmethod
    def init_instance(cls, elf_parser, output, frequency_hz=1_000_000_000):
        if elf_parser is None:
            raise TypeError("Value of 'elf_parser' cannot be None")

        if frequency_hz is None:
            raise TypeError(
                "Value of 'frequency_hz' cannot be None. Please specify the CPU frequency in Hz."
            )

        cls.parser = elf_parser
        cls.ptrace = PerfettoTrace(output)
        cls.frequency_hz = frequency_hz
        cls.NCPUS = elf_parser.macro("CONFIG_SMP_NCPUS")
        cls.types = elf_parser.get_type("note_type_e")
        global Tstate
        Tstate = elf_parser.get_type("tstate_e")
        cls.note_common_s = cls.parser.get_type("note_common_s")
        cls.processor_registry = NoteProcessorRegistry()
        cls.processor_registry.setup_default_processors(cls.types)

    @classmethod
    @functools.lru_cache(maxsize=None)
    def _get_note_event_s(cls):
        struct = cls.parser.get_type("note_event_s")
        if struct is None:
            raise ValueError(
                "Failed to get type 'note_event_s', "
                "make sure 'CONFIG_SCHED_INSTRUMENTATION_DUMP' is enabled in NuttX config"
            )

        return Struct(*[field for field in struct.subcons if field.name != "nev_data"])

    @classmethod
    @functools.lru_cache(maxsize=None)
    def struct(cls, note_type):
        if note_type in [cls.types.NOTE_START, cls.types.NOTE_TASKNAME]:
            return Struct(
                "nst_cmn" / cls.note_common_s,
                "name"
                / FixedSized(
                    this.nst_cmn.nc_length - cls.note_common_s.sizeof(),
                    CString(encoding="utf-8"),
                ),
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
            cls.types.NOTE_DUMP_BINARY,
        ]:
            return cls._get_note_event_s(cls)
        elif note_type == cls.types.NOTE_DUMP_PRINTF:
            note_printf_s = cls.parser.get_type("note_printf_s")
            fixed_size = sum(
                field.sizeof()
                for field in note_printf_s.subcons
                if field.name != "npt_data"
            )
            return Struct(
                *[field for field in note_printf_s.subcons if field.name != "npt_data"],
                "npt_data" / Bytes(this.npt_cmn.nc_length - fixed_size),
            )
        elif note_type == cls.types.NOTE_DUMP_COUNTER:
            note_counter_s = cls.parser.get_type("note_counter_s")
            note_event_s = cls._get_note_event_s()
            return Struct(
                *note_event_s.subcons,
                *[field for field in note_counter_s.subcons if field.name != "name"],
                "name" / CString("utf8"),
            )
        elif note_type == cls.types.NOTE_DUMP_THREADTIME:
            note_threadtime_s = cls.parser.get_type("note_threadtime_s")
            note_event_s = cls._get_note_event_s()
            return Struct(
                *note_event_s.subcons,
                *note_threadtime_s.subcons,
            )
        elif note_type in [
            cls.types.NOTE_HEAP_ADD,
            cls.types.NOTE_HEAP_REMOVE,
            cls.types.NOTE_HEAP_ALLOC,
            cls.types.NOTE_HEAP_FREE,
        ]:
            return cls.parser.get_type("note_heap_s")
        else:
            logger.error(f"Unknown note type: {note_type}")
            return None

    @classmethod
    def _extract_common_fields(cls, note: Container) -> Container:
        for key in list(note.keys()):
            if key.endswith("_cmn") and isinstance(note[key], Container):
                cmn = note[key]
                for sub_key in cmn.keys():
                    if sub_key not in note:
                        note[sub_key] = cmn[sub_key]
                break
        return note

    @classmethod
    def parse(cls, data):
        notes = []
        view = memoryview(data)
        pos = 0
        header_size = cls.note_common_s.sizeof()

        total_size = len(view)
        last_pos = 0
        with tqdm(
            desc="Parse notes", unit="byte", total=total_size, leave=True
        ) as pbar:
            while len(view) - pos >= header_size:
                try:
                    common = cls.note_common_s.parse(view[pos : pos + header_size])

                    if (
                        common.nc_pid < 0
                        or common.nc_cpu > cls.NCPUS
                        or common.nc_cpu < 0
                        or common.nc_priority > 255
                        or common.nc_priority < 0
                        or common.nc_length <= 0
                    ):
                        logger.error(
                            f"Invalid note header at pos {pos}, skipping byte: {view[pos]}"
                        )
                        pos += 1
                        continue

                    total_len = common.nc_length
                    if len(view) - pos < total_len:
                        break

                    struct = cls.struct(common.nc_type)
                    note = struct.parse(view[pos : pos + total_len])
                    note = cls._extract_common_fields(note)
                    notes.append(note)
                    logger.debug(f"Parsed note type {common.nc_type} {note}")

                    pos += total_len
                    # update progress bar every 10KB
                    if pos - last_pos > 10240:
                        pbar.update(pos - last_pos)
                        last_pos = pos

                except Exception as e:
                    logger.error(
                        f"Parse error at pos {pos}: {e}, skipping byte: {view[pos]}"
                    )
                    pbar.update(1)
                    pos += 1
                    continue

        return notes, pos

    @classmethod
    def dump(
        cls,
        notes=None,
        output="trace.perfetto",
        plugin_manager: Optional[PluginManager] = None,
    ):
        if not plugin_manager:
            raise ValueError("Plugin manager is required")

        plugin_manager.set_context_data(cls.ptrace, cls.parser, cls)
        with tqdm(
            desc="Dump notes", unit="notes", total=len(notes), leave=True
        ) as pbar:
            for note in notes:
                logger.debug(note)
                head = GenTraceHead(note)
                plugin_manager.process_note(note, head)
                pbar.update(1)

    @classmethod
    def flush(cls):
        cls.ptrace.flush()

    @classmethod
    def cpu_cycles_to_ns(cls, cycles):
        """Convert CPU cycles to nanoseconds"""
        if not hasattr(cls, "frequency_hz") or cls.frequency_hz is None:
            raise RuntimeError(
                "CPU frequency not set. Please call init_instance with frequency_hz parameter."
            )

        ns = int(cycles * 1_000_000_000 // cls.frequency_hz)
        return ns

    @classmethod
    def get_frequency_hz(cls):
        """Get the current CPU frequency"""
        return getattr(cls, "frequency_hz", None)


class NoteParser:

    def __init__(
        self,
        parser,
        cache_size=0,
        output=None,
        plugins: Optional[List[NotePlugin]] = [DefaultNoteProcessorPlugin()],
        frequency_hz=1_000_000_000,
    ):
        self.notes = list()
        self.cache_size = cache_size
        self.buffer = bytearray()
        self.parser = parser
        self.output = output
        self.frequency_hz = frequency_hz

        # Initialize plugin manager
        self.plugin_manager = PluginManager()
        if plugins:
            self.plugin_manager.register_plugins(plugins)

        NoteFactory.init_instance(parser, output, frequency_hz)

    def register_plugin(self, plugin: NotePlugin):
        """Register a single plugin"""
        self.plugin_manager.register_plugin(plugin)

    def register_plugins(self, plugins: List[NotePlugin]):
        """Register multiple plugins"""
        self.plugin_manager.register_plugins(plugins)

    def dump(self, notes=None):
        output = self.output if self.output is sys.stdout else "trace.perfetto"
        notes = self.notes if notes is None else notes
        NoteFactory.dump(notes, output, self.plugin_manager)

    def flush(self):
        NoteFactory.flush()
        if hasattr(self, "plugin_manager"):
            self.plugin_manager.teardown_all()
        print(f"note parser flush to file: {self.output}")

    def parse(self, data):
        self.buffer.extend(data)
        parsed_notes, _ = NoteFactory.parse(self.buffer)

        notes = []
        for note in parsed_notes:
            if note.nc_type == NoteFactory.types.NOTE_TASKNAME:
                TaskNameCache(note.nc_pid, note.name)

            notes.append(note)
            self.notes.append(note)

            if self.cache_size > 0 and len(self.notes) > self.cache_size:
                self.notes.pop(0)

        return notes

    def parse_file(self, path):
        with open(path, "rb") as f:
            data = f.read()
            self.parse(data)
