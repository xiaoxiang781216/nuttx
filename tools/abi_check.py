#!/usr/bin/env python3
# tools/abi_check.py
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

import os
import re
import subprocess as subp
import sys

struct_re = re.compile(r"struct\s+(\w+)\s+{")
member_re = re.compile(r"{(.*?)};", re.DOTALL)
file_re = re.compile(r"/\*\s*<[\da-f]+>\s+([\w\/\.\-_]+\.h:\d+)\s*\*/", re.DOTALL)


def member_mismatch(member1, member2):
    member1 = member1.replace("_Bool", "bool")
    member1 = member1.replace(" ", "")
    member2 = member2.replace("_Bool", "bool")
    member2 = member2.replace(" ", "")

    return member1 != member2


def print_struct(name, member, fileinfo):
    print(f"struct {name} {{")
    print(member)
    print(f"}}; at {fileinfo}")


def main(argv):
    p = subp.Popen(
        ["pahole", "-M", "--sort", "-I", argv[0]],
        env=os.environ.copy(),
        stdout=subp.PIPE,
        stderr=subp.PIPE,
        text=True,
    )
    out, err = p.communicate()
    if p.returncode != 0:
        print(f"Error: {err}")
        sys.exit(1)

    struct_blocks = re.split(r"/\* Used at:", out)
    structs = {}
    for block in struct_blocks:
        name_match = struct_re.search(block)
        if not name_match:
            continue

        struct_name = name_match.group(1)
        if not struct_name:
            continue

        member_match = member_re.search(block)
        if not member_match:
            continue

        file_match = file_re.search(block)
        if not file_match:
            continue

        members = member_match.group(1)
        fileinfo = file_match.group(1)
        if struct_name in structs and member_mismatch(structs[struct_name][0], members):
            print_struct(struct_name, structs[struct_name][0], structs[struct_name][1])
            print("------")
            print_struct(struct_name, members, fileinfo)
            print("")

        else:
            structs[struct_name] = (members, fileinfo)


def usage():
    print("Usage: struct_check.py <elffile>")
    sys.exit(1)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        usage()

    main(sys.argv[1:])
