############################################################################
# tools/gdb/lists.py
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
import utils

list_node_type = utils.lookup_type("struct list_node")
sq_queue_type = utils.lookup_type("sq_queue_t")
dq_queue_type = utils.lookup_type("dq_queue_t")


class NxList:
    def __init__(self, head, container_type=None, member=None):
        """Initialize the list iterator. Optionally specify the container type and member name."""
        self.head = head
        if container_type and not member:
            raise ValueError("Must specify the member name in container.")

        self.current = head["next"]
        self.container_type = container_type
        self.member = member

    def __iter__(self):
        return self

    def __next__(self):
        if self.current == self.head:
            raise StopIteration

        node = self.current
        self.current = self.current["next"]
        return (
            utils.container_of(node, self.container_type, self.member)
            if self.container_type
            else node
        )


def list_check(head):
    """Check the consistency of a list"""
    nb = 0

    if head.type == list_node_type.pointer():
        head = head.dereference()
    elif head.type != list_node_type:
        raise gdb.GdbError("argument must be of type (struct list_node [*])")
    c = head
    try:
        gdb.write("Starting with: {}\n".format(c))
    except gdb.MemoryError:
        gdb.write("head is not accessible\n")
        return
    while True:
        p = c["prev"].dereference()
        n = c["next"].dereference()
        try:
            if p["next"] != c.address:
                gdb.write(
                    "prev.next != current: "
                    "current@{current_addr}={current} "
                    "prev@{p_addr}={p}\n".format(
                        current_addr=c.address,
                        current=c,
                        p_addr=p.address,
                        p=p,
                    )
                )
                return
        except gdb.MemoryError:
            gdb.write(
                "prev is not accessible: "
                "current@{current_addr}={current}\n".format(
                    current_addr=c.address, current=c
                )
            )
            return
        try:
            if n["prev"] != c.address:
                gdb.write(
                    "next.prev != current: "
                    "current@{current_addr}={current} "
                    "next@{n_addr}={n}\n".format(
                        current_addr=c.address,
                        current=c,
                        n_addr=n.address,
                        n=n,
                    )
                )
                return
        except gdb.MemoryError:
            gdb.write(
                "next is not accessible: "
                "current@{current_addr}={current}\n".format(
                    current_addr=c.address, current=c
                )
            )
            return
        c = n
        nb += 1
        if c == head:
            gdb.write("list is consistent: {} node(s)\n".format(nb))
            return


def sq_for_every(sq, entry=None):
    """Iterate over a singly linked list from the head or specified entry"""
    if sq.type == sq_queue_type.pointer():
        sq = sq.dereference()
    elif sq.type != sq_queue_type:
        gdb.write("Must be struct sq_queue not {}".format(sq.type))
        return

    if sq["head"] == 0:
        return

    if not entry:
        entry = sq["head"].dereference()

    while entry.address:
        yield entry.address
        entry = entry["flink"].dereference()


def sq_is_empty(sq):
    """Check if a singly linked list is empty"""
    if sq.type == sq_queue_type.pointer():
        sq = sq.dereference()
    elif sq.type != sq_queue_type:
        return False

    if sq["head"] == 0:
        return True
    else:
        return False


def sq_check(sq):
    """Check the consistency of a singly linked list"""
    nb = 0
    if sq.type == sq_queue_type.pointer():
        sq = sq.dereference()
    elif sq.type != sq_queue_type:
        gdb.write("Must be struct sq_queue not {}".format(sq.type))
        return

    if sq["head"] == 0:
        gdb.write("sq_queue head is empty {}\n".format(sq.address))
        return

    entry = sq["head"].dereference()
    try:
        while entry.address:
            nb += 1
            entry = entry["flink"].dereference()
    except gdb.MemoryError:
        gdb.write("entry address is unaccessible {}\n".format(entry.address))
        return

    gdb.write("sq_queue is consistent: {} node(s)\n".format(nb))


def dq_for_every(dq, entry=None):
    """Iterate over a doubly linked list"""
    if dq.type == dq_queue_type.pointer():
        dq = dq.dereference()
    elif dq.type != dq_queue_type:
        gdb.write("Must be struct dq_queue not {}".format(dq.type))
        return

    if dq["head"] == 0:
        return

    if not entry:
        entry = dq["head"].dereference()

    while entry.address:
        yield entry.address
        entry = entry["flink"].dereference()


def dq_check(dq):
    """Check the consistency of a doubly linked list"""
    nb = 0
    if dq.type == dq_queue_type.pointer():
        dq = dq.dereference()
    elif dq.type != dq_queue_type:
        gdb.write("Must be struct dq_queue not {}".format(dq.type))
        return

    if dq["head"] == 0:
        gdb.write("dq_queue head is empty {}\n".format(dq.address))
        return
    entry = dq["head"].dereference()
    try:
        while entry.address:
            nb += 1
            entry = entry["flink"].dereference()
    except gdb.MemoryError:
        gdb.write("entry address is unaccessible {}\n".format(entry.address))
        return

    gdb.write("dq_queue is consistent: {} node(s)\n".format(nb))


class ListCheck(gdb.Command):
    """Verify a list consistency"""

    def __init__(self):
        super(ListCheck, self).__init__(
            "list_check", gdb.COMMAND_DATA, gdb.COMPLETE_EXPRESSION
        )

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)
        if len(argv) != 1:
            raise gdb.GdbError("nx-list-check takes one argument")

        obj = gdb.parse_and_eval(argv[0])
        if obj.type == list_node_type.pointer():
            list_check(obj)
        elif obj.type == sq_queue_type.pointer():
            sq_check(obj)
        else:
            raise gdb.GdbError("Invalid argument type: {}".format(obj.type))


class ForeachListEntry(gdb.Command):
    """Dump list members for a given list"""

    def __init__(self):
        super(ForeachListEntry, self).__init__(
            "foreach_list_entry", gdb.COMMAND_DATA, gdb.COMPLETE_EXPRESSION
        )

    def invoke(self, arg, from_tty):
        argv = gdb.string_to_argv(arg)

        parser = argparse.ArgumentParser(description="Iterate the items in list")
        parser.add_argument("head", type=str, help="List head")
        parser.add_argument("type", type=str, help="Container type")
        parser.add_argument("member", type=str, help="Member name in container")
        try:
            args = parser.parse_args(argv)
        except SystemExit:
            gdb.write("Invalid arguments\n")
            return

        pointer = gdb.parse_and_eval(args.head)
        container_type = gdb.lookup_type(args.type)
        member = args.member
        list = NxList(pointer, container_type, member)
        for i, entry in enumerate(list):
            entry = entry.dereference()
            gdb.write(f"{i}: {entry.format_string(styling=True)}\n")


ListCheck()
ForeachListEntry()
