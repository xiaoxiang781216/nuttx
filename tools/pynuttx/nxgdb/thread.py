############################################################################
# tools/pynuttx/nxgdb/thread.py
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
from enum import Enum, auto
from typing import Union

import gdb
from nxelf.elf import LiefELF
from nxreg.register import Registers, get_arch_name

from . import autocompeletion, utils
from .stack import Stack


def is_thread_command_supported():
    # Check if the native thread command is available by compare the number of threads.
    # It should have at least CONFIG_SMP_NCPUS of idle threads.
    return len(gdb.selected_inferior().threads()) > utils.get_ncpus()


class NxRegisters:
    saved_regs = None

    def __init__(self):
        elf = gdb.objfiles()[0]
        elf = LiefELF(elf.filename)

        def read_memory(addr, size):
            return bytes(gdb.selected_inferior().read_memory(addr, size))

        mapped_arch_name = get_arch_name()
        if not mapped_arch_name:
            raise ValueError("Architecture is not found in g_reg_table.\n")

        print(f"Register set for: {mapped_arch_name}")
        self.registers = Registers(elf, arch=mapped_arch_name, readmem=read_memory)

    def load(self, regs: Union[int, gdb.Value] = None):
        """Load registers from context register address"""
        self.registers.load(regs)
        for reg in self.registers:
            gdb.execute(f"set ${reg.name} = {reg.value}")

    def switch(self, pid):
        """Switch to the specified thread"""
        tcb = utils.get_tcb(pid)
        if not tcb:
            gdb.write(f"Thread {pid} not found\n")
            return

        if utils.task_is_running(tcb):
            # If the thread is running, then register is not in context but saved temporarily
            self.restore()
            return

        # Save current if this is the running thread, which is the case we never saved it before
        if not self.saved_regs:
            self.save()

        self.load(tcb["xcp"]["regs"])

    def save(self):
        """Save current registers"""
        if NxRegisters.saved_regs:
            # Already saved
            return

        registers = {}
        frame = gdb.newest_frame()
        for reg in self.registers:
            value = frame.read_register(reg.name)
            registers[reg.name] = value

        NxRegisters.saved_regs = registers

    def restore(self):
        if not NxRegisters.saved_regs:
            return

        for name, value in NxRegisters.saved_regs.items():
            gdb.execute(f"set ${name}={int(value)}")

        NxRegisters.saved_regs = None


g_registers = NxRegisters()


class RegInfoCommand(gdb.Command):
    """Display the register information"""

    def __init__(self):
        super().__init__("maintenance reginfo", gdb.COMMAND_USER)

    @utils.dont_repeat_decorator
    def invoke(self, arg: str, from_tty: bool) -> None:
        registers = g_registers.registers
        header = ("Name", "Rmt Nr", "Offset", "Tcb Reg Off")
        print(
            "Name: the register name GDB uses.\n"
            "Rmt Nr: the register number in RSP packet, also the position in tcb.xcp.regs \n"
            "Tcb Reg Off: the byte offset in tcb.xcp.regs"
        )
        formatter = "{:<20} {:<10} {:<10} {:<10}"
        print(formatter.format(*header))
        for register in registers:
            print(
                formatter.format(
                    register.name,
                    register.regnum,
                    register.goffset,
                    register.toffset,
                )
            )


@autocompeletion.complete
class SetRegs(gdb.Command):
    """Load registers from TCB context memory address.
    Usage: setregs [regs]

    Etc: setregs
         setregs tcb->xcp.regs
         setregs g_pidhash[0]->xcp.regs

    Default to load from g_running_tasks if no args are provided.
    If the memory address is NULL, it will not set registers.
    """

    def get_argparser(self):
        parser = argparse.ArgumentParser(
            description="Set registers to the specified values"
        )

        parser.add_argument(
            "regs",
            nargs="?",
            default="",
            metavar="symbol",
            help="The memory address to load register values, use g_running_tasks.xcp.regs if not specified",
        )
        return parser

    def __init__(self):
        super().__init__("setregs", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    @utils.dont_repeat_decorator
    def invoke(self, arg, from_tty):
        try:
            args = self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

        if args and args.regs:
            regs = utils.parse_and_eval(f"{args.regs}").cast(
                utils.lookup_type("char").pointer()
            )
        else:
            try:
                current_regs = utils.parse_and_eval("g_running_tasks[0].xcp.regs")
            except gdb.error as e:
                gdb.write(f"Failed to parse running tasks: {e}\n")
                return

            regs = current_regs.cast(utils.lookup_type("char").pointer())

        if regs == 0:
            gdb.write("regs is NULL\n")
            return

        g_registers.save()
        g_registers.load(regs)


class Nxinfothreads(gdb.Command):
    """Display information of all threads"""

    def __init__(self):
        super().__init__("info nxthreads", gdb.COMMAND_USER)

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        npidhash = utils.parse_and_eval("g_npidhash")
        pidhash = utils.parse_and_eval("g_pidhash")
        statenames = utils.parse_and_eval("nxsched_get_stateinfo::g_statenames")

        if utils.is_target_smp():
            gdb.write(
                "%-5s %-4s %-4s %-4s %-21s %-80s %-30s\n"
                % ("Index", "Tid", "Pid", "Cpu", "Thread", "Info", "Frame")
            )
        else:
            gdb.write(
                "%-5s %-4s %-4s %-21s %-80s %-30s\n"
                % ("Index", "Tid", "Pid", "Thread", "Info", "Frame")
            )

        for i, tcb in enumerate(utils.ArrayIterator(pidhash, npidhash)):
            if not tcb:
                continue

            pid = tcb["group"]["tg_pid"]
            tid = tcb["pid"]
            pc = utils.get_pc(tcb)
            thread = f"Thread {hex(tcb)}"
            index = f"*{i}" if utils.task_is_running(tcb) else f" {i}"

            statename = statenames[tcb["task_state"]].string()
            statename = f'\x1b{"[32;1m" if statename == "Running" else "[33;1m"}{statename}\x1b[m'

            if tcb["task_state"] == utils.parse_and_eval("TSTATE_WAIT_SEM"):
                mutex = tcb["waitobj"].cast(utils.lookup_type("sem_t").pointer())
                if utils.sem_is_mutex(mutex):
                    statename = f"Waiting,Mutex:{utils.mutex_get_holder(mutex)}"

            try:
                """Maybe tcb not have name member, or name is not utf-8"""
                info = (
                    "(Name: \x1b[31;1m%s\x1b[m, State: %s, Priority: %d, Stack: %d)"
                    % (
                        utils.get_task_name(tcb),
                        statename,
                        tcb["sched_priority"],
                        tcb["adj_stack_size"],
                    )
                )
            except gdb.error and UnicodeDecodeError:
                info = "(Name: Not utf-8, State: %s, Priority: %d, Stack: %d)" % (
                    statename,
                    tcb["sched_priority"],
                    tcb["adj_stack_size"],
                )

            line = gdb.find_pc_line(pc)
            if line.symtab:
                func = gdb.execute(f"info symbol {pc} ", to_string=True)
                frame = "\x1b[34;1m0x%x\x1b[\t\x1b[33;1m%s\x1b[m at %s:%d" % (
                    pc,
                    func.split()[0] + "()",
                    line.symtab,
                    line.line,
                )
            else:
                frame = "No symbol with pc"

            if utils.is_target_smp():
                cpu = f"{tcb['cpu']}"
                gdb.write(
                    "%-5s %-4s %-4s %-4s %-21s %-80s %-30s\n"
                    % (index, tid, pid, cpu, thread, info, frame)
                )
            else:
                gdb.write(
                    "%-5s %-4s %-4s %-21s %-80s %-30s\n"
                    % (index, tid, pid, thread, info, frame)
                )


class Nxthread(gdb.Command):
    """Switch to a specified thread"""

    def __init__(self):
        super().__init__("nxthread", gdb.COMMAND_USER)

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        npidhash = utils.parse_and_eval("g_npidhash")
        pidhash = utils.parse_and_eval("g_pidhash")
        arg = args.split(" ")
        arglen = len(arg)

        if arg[0] == "":
            pass
        elif arg[0] == "apply":
            if arglen <= 1:
                gdb.write("Please specify a thread ID list\n")
            elif arglen <= 2:
                gdb.write("Please specify a command following the thread ID list\n")

            elif arg[1] == "all":
                for i, tcb in enumerate(utils.ArrayIterator(pidhash, npidhash)):
                    if tcb == 0:
                        continue
                    try:
                        gdb.write(f"Thread {i} {tcb['name'].string()}\n")
                    except gdb.error and UnicodeDecodeError:
                        gdb.write(f"Thread {i}\n")

                    if not utils.task_is_running(tcb):
                        gdb.execute(f"setregs g_pidhash[{i}]->xcp.regs")

                    cmd_arg = ""
                    for cmd in arg[2:]:
                        cmd_arg += cmd + " "

                    gdb.write(gdb.execute(f"{cmd_arg}\n", to_string=True))
                    g_registers.restore()
            else:
                threadlist = []
                i = 0
                cmd = ""
                for i in range(1, arglen):
                    if arg[i].isnumeric():
                        threadlist.append(int(arg[i]))
                    else:
                        cmd += arg[i] + " "

                if len(threadlist) == 0 or cmd == "":
                    gdb.write("Please specify a thread ID list and command\n")
                else:
                    for i in threadlist:
                        if i >= npidhash:
                            break

                        if pidhash[i] == 0:
                            continue

                        try:
                            gdb.write(f"Thread {i} {pidhash[i]['name'].string()}\n")
                        except gdb.error and UnicodeDecodeError:
                            gdb.write(f"Thread {i}\n")

                        if not utils.task_is_running(utils.get_tcb(i)):
                            gdb.execute(f"setregs g_pidhash[{i}]->xcp.regs")

                        gdb.write(gdb.execute(f"{cmd}\n", to_string=True))
                        g_registers.restore()

        else:
            if (
                arg[0].isnumeric()
                and int(arg[0]) < npidhash
                and pidhash[int(arg[0])] != 0
            ):
                if utils.task_is_running(pidhash[int(arg[0])]):
                    g_registers.restore()
                else:
                    gdb.execute("setregs g_pidhash[%s]->xcp.regs" % arg[0])
            else:
                gdb.write(f"Invalid thread id {arg[0]}\n")


class Nxcontinue(gdb.Command):
    """Restore the registers and continue the execution"""

    def __init__(self):
        super().__init__("nxcontinue", gdb.COMMAND_USER)
        if not is_thread_command_supported():
            gdb.execute("define c\n nxcontinue \n end\n")
            gdb.write(
                "\n\x1b[31;1m if use thread command, please don't use 'continue', use 'c' instead !!!\x1b[m\n"
            )

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        g_registers.restore()
        gdb.execute("continue")


class Nxstep(gdb.Command):
    """Restore the registers and step the execution"""

    def __init__(self):
        super().__init__("nxstep", gdb.COMMAND_USER)
        if not is_thread_command_supported():
            gdb.execute("define s\n nxstep \n end\n")
            gdb.write(
                "\x1b[31;1m if use thread command, please don't use 'step', use 's' instead !!!\x1b[m\n"
            )

    def invoke(self, args, from_tty):
        g_registers.restore()
        gdb.execute("step")


class TaskType(Enum):
    TASK = 0
    PTHREAD = 1
    KTHREAD = 2


class TaskSchedPolicy(Enum):
    FIFO = 0
    RR = 1
    SPORADIC = 2


class TaskState(Enum):
    Invalid = 0
    Waiting_Unlock = auto()
    Ready = auto()
    if utils.get_symbol_value("CONFIG_SMP"):
        Assigned = auto()
    Running = auto()
    Inactive = auto()
    Waiting_Semaphore = auto()
    Waiting_Signal = auto()
    if not utils.get_symbol_value(
        "CONFIG_DISABLE_MQUEUE"
    ) or not utils.get_symbol_value("CONFIG_DISABLE_MQUEUE_SYSV"):
        Waiting_MQEmpty = auto()
        Waiting_MQFull = auto()
    if utils.get_symbol_value("CONFIG_PAGING"):
        Waiting_PagingFill = auto()
    if utils.get_symbol_value("CONFIG_SIG_SIGSTOP_ACTION"):
        Stopped = auto()


class Ps(gdb.Command):
    def __init__(self):
        super().__init__("ps", gdb.COMMAND_USER)
        self._fmt_wxl = "{0: <{width}}"
        # By default we align to the right, whcih respects the nuttx foramt
        self._fmt_wx = "{0: >{width}}"
        self._char_ptr_ptr_type = None
        self._mutex_t_ptr_type = None
        self._sem_t_ptr_type = None
        self._pthread_tcb_s_ptr_type = None
        self.macros = {
            "TCB_FLAG_POLICY_MASK": utils.get_symbol_value("TCB_FLAG_POLICY_MASK"),
            "TCB_FLAG_POLICY_SHIFT": utils.get_symbol_value("TCB_FLAG_POLICY_SHIFT"),
            "TCB_FLAG_TTYPE_MASK": utils.get_symbol_value("TCB_FLAG_TTYPE_MASK"),
            "TCB_FLAG_TTYPE_SHIFT": utils.get_symbol_value("TCB_FLAG_TTYPE_SHIFT"),
            "TCB_FLAG_EXIT_PROCESSING": utils.get_symbol_value(
                "TCB_FLAG_EXIT_PROCESSING"
            ),
            "TCB_FLAG_TTYPE_PTHREAD": utils.get_symbol_value("TCB_FLAG_TTYPE_PTHREAD"),
            "_SIGSET_NELEM": utils.get_symbol_value("_SIGSET_NELEM"),
            "CONFIG_SCHED_CPULOAD_NONE": utils.get_symbol_value(
                "CONFIG_SCHED_CPULOAD_NONE"
            ),
            "CONFIG_SMP": utils.get_symbol_value("CONFIG_SMP"),
        }

    def get_cached_type(self, type_name):
        if type_name == "char_ptr_ptr" and not self._char_ptr_ptr_type:
            self._char_ptr_ptr_type = gdb.lookup_type("char").pointer().pointer()
        elif type_name == "mutex_t_ptr" and not self._mutex_t_ptr_type:
            self._mutex_t_ptr_type = utils.lookup_type("mutex_t").pointer()
        elif type_name == "sem_t_ptr" and not self._sem_t_ptr_type:
            self._sem_t_ptr_type = utils.lookup_type("sem_t").pointer()
        elif type_name == "pthread_tcb_s_ptr" and not self._pthread_tcb_s_ptr_type:
            self._pthread_tcb_s_ptr_type = utils.lookup_type(
                "struct pthread_tcb_s"
            ).pointer()

        return getattr(self, f"_{type_name}_type")

    def parse_and_show_info(self, tcb):
        def eval2str(cls, x):
            return cls(int(x)).name

        pid = int(tcb["pid"])
        group = int(tcb["group"]["tg_pid"])
        priority = int(tcb["sched_priority"])
        flags = int(tcb["flags"])

        policy = eval2str(
            TaskSchedPolicy,
            (flags & self.macros.get("TCB_FLAG_POLICY_MASK"))
            >> self.macros.get("TCB_FLAG_POLICY_SHIFT"),
        )

        task_type = eval2str(
            TaskType,
            (flags & self.macros.get("TCB_FLAG_TTYPE_MASK"))
            >> self.macros.get("TCB_FLAG_TTYPE_SHIFT"),
        )

        npx = "P" if (flags & self.macros.get("TCB_FLAG_EXIT_PROCESSING")) else "-"

        waiter = ""
        if tcb["waitobj"]:
            waitobj = tcb["waitobj"].cast(self.get_cached_type("sem_t_ptr"))
            if utils.sem_is_mutex(waitobj):
                mutex = tcb["waitobj"].cast(utils.lookup_type("sem_t").pointer())
                waiter = str(utils.mutex_get_holder(mutex))

        state_and_event = eval2str(TaskState, (tcb["task_state"]))
        if waiter:
            state_and_event += "@MutexHolder: " + waiter
        state_and_event = state_and_event.split("_")

        # Append a null str here so we don't need to worry
        # about the number of elements as we only want the first two
        state, event = (
            state_and_event if len(state_and_event) > 1 else state_and_event + [""]
        )

        sigmask = "{0:#0{1}x}".format(
            sum(
                int(tcb["sigprocmask"]["_elem"][i] << i)
                for i in range(self.macros.get("_SIGSET_NELEM"))
            ),
            self.macros.get("_SIGSET_NELEM") * 8 + 2,
        )[
            2:
        ]  # exclude "0x"

        st = Stack(
            int(tcb["stack_base_ptr"]),
            int(tcb["adj_stack_size"]),
            utils.get_sp(tcb),
        )

        stacksz = st._stack_size
        used = st.max_usage()
        filled = "{0:.2%}".format(used / stacksz)

        cpu = int(tcb["cpu"]) if self.macros.get("CONFIG_SMP") else 0

        # For a task we need to display its cmdline arguments, while for a thread we display
        # pointers to its entry and argument
        cmd = ""
        name = utils.get_task_name(tcb)

        if (flags & self.macros.get("TCB_FLAG_TTYPE_MASK")) == self.macros.get(
            "TCB_FLAG_TTYPE_PTHREAD"
        ):
            entry = tcb["entry"]["main"]
            ptcb = tcb.cast(utils.lookup_type("struct pthread_entry_s").pointer())
            arg = ptcb["arg"]
            cmd = f"{name} {hex(entry)} {hex(arg)}"
        elif pid < utils.get_ncpus():
            cmd = name
        else:
            # For tasks other than pthreads, hence need to get its command line
            # arguments from
            tls_info_type = utils.lookup_type("struct tls_info_s").pointer()
            stack_ptr = tcb["stack_alloc_ptr"].cast(tls_info_type)
            argv = tcb["stack_alloc_ptr"] + stack_ptr["tl_size"]
            args = []
            parg = argv.cast(self.get_cached_type("char_ptr_ptr")) + 1
            while parg.dereference():
                args.append(parg.dereference().string())
                parg += 1
            cmd = " ".join([name] + args)

        if not self.macros.get("CONFIG_SCHED_CPULOAD_NONE"):
            g_cpuload_total = int(utils.parse_and_eval("g_cpuload_total"))
            load = "{0:.1%}".format(
                int(tcb["ticks"]) / g_cpuload_total if g_cpuload_total else 0
            )
        else:
            load = "Dis."

        if hasattr(self, "table"):
            self.table.add_row(
                [
                    pid,
                    group,
                    cpu,
                    priority,
                    policy,
                    task_type,
                    npx,
                    state,
                    event,
                    sigmask,
                    stacksz,
                    used,
                    filled,
                    load,
                    cmd,
                ]
            )
        else:
            gdb.write(
                " ".join(
                    (
                        self._fmt_wx.format(pid, width=5),
                        self._fmt_wx.format(group, width=5),
                        self._fmt_wx.format(cpu, width=3),
                        self._fmt_wx.format(priority, width=3),
                        self._fmt_wxl.format(policy, width=8),
                        self._fmt_wxl.format(task_type, width=7),
                        self._fmt_wx.format(npx, width=3),
                        self._fmt_wxl.format(state, width=8),
                        self._fmt_wxl.format(event, width=9),
                        self._fmt_wxl.format(sigmask, width=8),
                        self._fmt_wx.format(stacksz, width=7),
                        self._fmt_wx.format(used, width=7),
                        self._fmt_wx.format(filled, width=6),
                        self._fmt_wx.format(load, width=6),
                        cmd,
                    )
                )
            )
            gdb.write("\n")

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        prettytable = utils.import_check(
            "prettytable",
            errmsg="Execute `pip install prettytable` for better printing result.\n",
        )
        if prettytable:
            self.table = prettytable.PrettyTable()
            self.table.align = "l"
            self.table.field_names = [
                "PID",
                "GROUP",
                "CPU",
                "PRI",
                "POLICY",
                "TYPE",
                "NPX",
                "STATE",
                "EVENT",
                "SIGMASK",
                "STACK",
                "USED",
                "FILLED",
                "LOAD",
                "COMMAND",
            ]
        else:
            gdb.write(
                " ".join(
                    (
                        self._fmt_wx.format("PID", width=5),
                        self._fmt_wx.format("GROUP", width=5),
                        self._fmt_wx.format("CPU", width=3),
                        self._fmt_wx.format("PRI", width=3),
                        self._fmt_wxl.format("POLICY", width=8),
                        self._fmt_wxl.format("TYPE", width=7),
                        self._fmt_wx.format("NPX", width=3),
                        self._fmt_wxl.format("STATE", width=8),
                        self._fmt_wxl.format("EVENT", width=9),
                        self._fmt_wxl.format(
                            "SIGMASK", width=utils.get_symbol_value("_SIGSET_NELEM") * 8
                        ),
                        self._fmt_wx.format("STACK", width=7),
                        self._fmt_wx.format("USED", width=7),
                        self._fmt_wx.format("FILLED", width=3),
                        self._fmt_wx.format("LOAD", width=6),
                        "COMMAND",
                    )
                )
            )
            gdb.write("\n")

        for tcb in utils.get_tcbs():
            try:
                self.parse_and_show_info(tcb)
            except gdb.error as e:
                gdb.write(f"[Error] GDB error while processing TCB: {e}\n")
            except Exception as e:
                gdb.write(f"[Error] Unexpected error: {e}\n")
        if hasattr(self, "table"):
            gdb.write(f"{self.table.get_string()}\n")

    def diagnose(self, *args, **kwargs):
        return {
            "title": "Thread Information",
            "summary": "Thread information",
            "command": "ps",
            "result": "info",
            "category": utils.DiagnoseCategory.sched,
            "message": gdb.execute("ps", to_string=True),
        }
