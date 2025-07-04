############################################################################
# tools/pynuttx/nxgdb/tlsdump.py
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

from . import autocompeletion, utils
from .lists import NxDQueue

CONFIG_TLS_NELEM = utils.get_field_nitems("struct tls_info_s", "tl_elem")


@autocompeletion.complete
class TlsDump(gdb.Command):
    """Dump and check the integrity of tls_info and task_info"""

    def get_argparser(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-p",
            "--pid",
            type=int,
            help="Dump the tls info of the thread/task represented by this pid",
            default=None,
        )
        parser.add_argument(
            "-c",
            "--check",
            action="store_true",
            help="Integrity check.All threads in a task must share the same task_info",
        )
        return parser

    def __init__(self):
        if not CONFIG_TLS_NELEM:
            print("TLS is not enabled in the current configuration")
            return
        super().__init__("tlsdump", gdb.COMMAND_USER)
        self.parser = self.get_argparser()

    def parse_arguments(self, argv):
        try:
            args = self.parser.parse_args(argv)
        except SystemExit:
            return None

        return args

    def check_corruption(self, tcb):
        """integrity check"""

        try:
            tcb_s = utils.lookup_type("struct tcb_s").pointer()
            if not tcb or tcb.type != tcb_s:
                print("No tcb found, or the tcb type is invalid")
                return True

            # Get the task_info of the task, and compare it with the task_info of each thread
            # If the tcb is a task, get the task_info of the task
            # If the tcb is a thread, get the task_info of the task to which the thread belongs
            task = (
                tcb
                if utils.get_tcb_type(tcb) == "TASK"
                else utils.get_tcb(tcb.group.tg_pid)
            )
            if not task or not utils.get_tcb_type(task):
                print("Can not find the task within the group")
                return True

            tls_info_s = utils.lookup_type("struct tls_info_s").pointer()
            task_info = task.stack_alloc_ptr.cast(tls_info_s).tl_task
            corrupted = False
            # Traverse all threads under this task through the group linked list
            for tcb in NxDQueue(task.group.tg_members, "struct tcb_s", "member"):
                # Get the task_info of this thread
                info = tcb.stack_alloc_ptr.cast(tls_info_s).tl_task
                # Only report when corrupted
                if info != task_info:
                    pid = int(tcb.pid)
                    print(
                        f"PID:{pid} is corrupted, task_info addr:{hex(task_info)}, got {hex(info)}"
                    )
                    corrupted = True
            return corrupted
        except gdb.error:
            print("Error occurred during integrity check")
            return True

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        args = self.parse_arguments(gdb.string_to_argv(args))
        if not args:
            return

        # tlsdump -c
        # Do integrity check
        if args.check:
            corrupted = any(self.check_corruption(tcb) for tcb in utils.get_tcbs())
            print(f"Check: {'FAILED' if corrupted else 'PASS'}")
            return

        # tlsdump / tlsdump -p pid
        tls_info_s = utils.lookup_type("struct tls_info_s").pointer()
        CONFIG_TLS_TASK_NELEM = utils.get_field_nitems("struct task_info_s", "ta_telem")
        pid = args.pid
        tcbs = [utils.get_tcb(pid)] if pid is not None else utils.get_tcbs()
        for tcb in tcbs:
            if not tcb or not (task_type := utils.get_tcb_type(tcb)):
                continue
            tls_info = tcb.stack_alloc_ptr.cast(tls_info_s)
            task_info = tls_info.tl_task
            print(f"PID:{tcb.pid}, {task_type}, task_info addr:{hex(task_info)}")

            if task_type == "TASK":
                print("task tls elements:")
                for i in range(CONFIG_TLS_TASK_NELEM):
                    tls = task_info.ta_telem[i]
                    print(f"{i}: {hex(tls)}")

            print("thread tls elements:")
            for i in range(CONFIG_TLS_NELEM):
                tls = tls_info.tl_elem[i]
                print(f"{i}: {hex(tls)}")

    def diagnose(self, *args, **kwargs):
        corrupted = any(self.check_corruption(tcb) for tcb in utils.get_tcbs())

        return {
            "title": "tlsdump report",
            "summary": "integrity check",
            "result": "failed" if corrupted else "pass",
            "category": utils.DiagnoseCategory.system,
            "command": "tlsdump",
            "data": gdb.execute("tlsdump", to_string=True),
        }
