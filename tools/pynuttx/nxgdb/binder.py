############################################################################
# tools/pynuttx/nxgdb/binder.py
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
############################################################################

import argparse
from typing import Optional

import gdb

from . import fs, lists, utils


def get_servicemanager() -> Optional[gdb.Value]:
    """Get ServiceManager instance from servicemanager process."""
    pid = utils.get_pid_by_name("servicemanager")
    if pid is None:
        return None

    for frame in utils.get_thread_frames(int(pid)):
        name = utils.get_frame_func_name(frame)
        if "servicemanager_main" in name:
            value = utils.get_frame_variables(frame)
            if manager := value.get("manager"):
                return utils.Value(manager).m_ptr

    return None


def get_binder_device() -> Optional[gdb.Value]:
    """Get binder device from filesystem."""
    for node, _ in filter(fs.fstype_filter("driver"), fs.foreach_inode()):
        name = fs.get_inode_name(node)
        if name and "binder" in name:
            return node.i_private.cast(
                utils.lookup_type("struct binder_device").pointer()
            )
    return None


def get_process_self(pid: int) -> Optional[gdb.Value]:
    """Get ProcessState for a given process ID."""
    index = utils.parse_and_eval("android::gProcessIndex")
    if index is None:
        return None

    value = utils.get_task_tls(pid, index)
    if value is None:
        return None

    proc = utils.lookup_type("android::sp<android::ProcessState>")
    return value.cast(proc.pointer()).m_ptr


def get_services() -> list[tuple[str, gdb.Value]]:
    """Get all registered services from ServiceManager.

    Returns:
        List of tuples containing (name, Service)
    """
    service_manager = get_servicemanager()
    if service_manager is None:
        return []

    service = service_manager.mNameToService
    begin = service.__begin_
    end = service.__end_
    size = int(end - begin)

    if size < 0:
        return []

    services = []
    for i in range(size):
        pair = utils.Value(begin + i)
        name = str(pair.first).strip('"')
        services.append((name, pair.second))
    return services


def get_proc_list() -> lists.NxList | list:
    """Get iterator of binder processes."""
    device = get_binder_device()
    if device is None:
        return []
    return lists.NxList(device.binder_procs_list, "struct binder_proc", "proc_node")


class BinderDump(gdb.Command):
    __doc__ = "Dump Android binder state"

    def __init__(self):
        super().__init__("binderdump", gdb.COMMAND_USER)
        self.parser = self.get_argparse()

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        args = self.parse_arguments(gdb.string_to_argv(args))
        if args is None:
            return

        try:
            if args.list:
                self.dump_services()

            if args.process_state:
                pid = args.pid if args.pid else -1
                self.dump_process_state(pid)

            if args.driver:
                pid = args.pid if args.pid else -1
                self.dump_driver(pid)

        except Exception as e:
            print(f"Error: {e}")

    def dump_services(self):
        services = get_services()
        if not services:
            print("No services found")
            return

        print(f"Found: {len(services)} services")
        print(
            f"{'Index':<8} {'Name':<40} {'Binder':<18} {'Isolated':<10} {'Priority':<10} {'PID':<8}"
        )
        print("-" * 100)

        for idx, (name, svc) in enumerate(services):
            service = utils.Value(svc)
            binder_ptr = str(service.binder.m_ptr)
            print(
                f"{idx:<8} {name:<40} {binder_ptr:<18} "
                f"{str(service.allowIsolated):<10} "
                f"{service.dumpPriority:<10} {service.debugPid:<8}"
            )

    def dump_process_state(self, pid: int):
        if pid > 0:
            self.dump_process_state_for_pid(pid)
        elif pid == -1:
            procs = get_proc_list()
            found = False

            for proc in procs:
                if self.dump_process_state_for_pid(proc.pid):
                    found = True

            if not found:
                print("No ProcessState found for any binder process")
        else:
            print(f"Invalid pid value: {pid}")

    def dump_driver(self, pid: Optional[int] = None):
        pid = pid if pid is not None else -1
        procs = get_proc_list()
        found = False
        for proc in procs:
            if pid != -1 and proc.pid != pid:
                continue

            found = True
            tcb = utils.get_tcb(proc.pid)
            name = tcb.name.string()
            print(f"Found binder process {name} (pid {proc.pid})")
            self.dump_structure(proc, "struct binder_proc", 1)

            for thread in lists.NxList(
                proc.threads, "struct binder_thread", "thread_node"
            ):
                self.dump_structure(thread, "struct binder_thread", 2)
                txn = thread.transaction_stack
                while txn:
                    self.dump_structure(txn, "struct binder_transaction", 3)
                    txn = txn.from_parent

            field = utils.get_type_field("struct binder_node", "rb_node")
            if field and proc.nodes:
                offset = field.bitpos // 8
                for node in lists.NxList(proc.nodes):
                    item = self.container_of_with_offset(
                        node, "struct binder_node", offset
                    )
                    self.dump_structure(item, "struct binder_node", 2)

            if proc.refs_by_desc:
                for ref in lists.NxList(
                    proc.refs_by_desc, "struct binder_ref", "rb_node_desc"
                ):
                    self.dump_structure(ref, "struct binder_ref", 2)

        if not found and pid != -1:
            print(f"Process {pid} not found in binder driver")

    def dump_process_state_for_pid(self, pid: int) -> bool:
        procs = get_process_self(pid)
        proc = procs.cast(utils.lookup_type("android::ProcessState").pointer())
        print(f"Found ProcessState for pid {pid}")
        self.dump_structure(proc, "android::ProcessState", 1)
        return True

    @staticmethod
    def get_anonymous_fields(type_obj: gdb.Type) -> set:
        anonymous_fields = set()
        for field in type_obj.fields():
            if field.name is None and field.type.code in (
                gdb.TYPE_CODE_UNION,
                gdb.TYPE_CODE_STRUCT,
            ):
                for sub_field in field.type.fields():
                    if sub_field.name:
                        anonymous_fields.add(sub_field.name)
        return anonymous_fields

    @staticmethod
    def dump_anonymous_member(
        obj: gdb.Value, field_type, indent: str, max_name_len: int
    ):
        type_names = {
            gdb.TYPE_CODE_UNION: "<anonymous union>",
            gdb.TYPE_CODE_STRUCT: "<anonymous struct>",
        }

        if field_type.code not in type_names:
            return

        print(f"  {indent}{type_names[field_type.code]:<{max_name_len}} :")
        inner_name_len = max(max_name_len - 2, 10)

        for member_field in field_type.fields():
            if member_field.name:
                try:
                    value_str = str(obj[member_field.name])
                except Exception as e:
                    value_str = f"<error: {e}>"

                print(
                    f"    {indent}{member_field.name:<{inner_name_len}} : {value_str}"
                )

    @staticmethod
    def dump_regular_field(obj: gdb.Value, name: str, indent: str, max_name_len: int):
        try:
            value = obj[name]
            value_str = str(value) if value is not None else "<None>"
        except Exception as e:
            value_str = f"<error: {e}>"

        print(f"  {indent}{str(name):<{max_name_len}} : {value_str}")

    def dump_structure(self, obj: gdb.Value, struct_type: str, indent_level: int = 0):
        indent = "  " * indent_level
        typ = utils.lookup_type(struct_type)
        fields = typ.fields()

        if not fields:
            print(f"{indent}No fields found for {struct_type}")
            return

        anon_fields = self.get_anonymous_fields(typ)
        max_name_len = max((len(str(f.name)) for f in fields if f.name), default=0)

        print(f"{indent}{struct_type}")

        for field in fields:
            name = field.name
            if name and "::" in name:
                continue

            if name is None:
                try:
                    self.dump_anonymous_member(obj, field.type, indent, max_name_len)
                except Exception as e:
                    print(f"  {indent}{'<anonymous>':<{max_name_len}} : <error: {e}>")
                continue

            if name not in anon_fields:
                self.dump_regular_field(obj, name, indent, max_name_len)

        print()

    @staticmethod
    def container_of_with_offset(ptr: gdb.Value, typeobj, offset: int) -> gdb.Value:
        """Get container structure pointer given a member pointer and offset."""
        if isinstance(typeobj, str):
            typeobj = utils.lookup_type(typeobj).pointer()
        addr = utils.Value(ptr).cast(utils.long_type)
        return utils.Value(addr - offset).cast(typeobj)

    def get_argparse(self):
        parser = argparse.ArgumentParser(description=self.__doc__)
        parser.add_argument(
            "-l",
            "--list",
            action="store_true",
            help="Dump all services",
        )
        parser.add_argument(
            "-d",
            "--driver",
            action="store_true",
            help="Dump binder driver",
        )
        parser.add_argument(
            "-s",
            "--process-state",
            action="store_true",
            help="Dump ProcessState info",
        )
        parser.add_argument(
            "-p",
            "--pid",
            type=int,
            help="Dump services of a specific process",
        )

        return parser

    def parse_arguments(self, argv):
        try:
            args = self.parser.parse_args(argv)
        except SystemExit:
            return None

        return args
