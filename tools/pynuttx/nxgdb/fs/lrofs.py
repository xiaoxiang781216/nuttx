############################################################################
# tools/pynuttx/nxgdb/fs/lrofs.py
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

import nxgdb.utils as utils
from nxgdb.fs.inode import Inode


def dump_lrofs_mpt(mpt):
    sector_size = mpt.lm_hwsectorsize
    sector_num = mpt.lm_hwnsectors
    volume_size = mpt.lm_volsize
    xip_base = mpt.lm_xipbase
    buffer_base = mpt.lm_buffer
    print(
        f" HW sector size: {sector_size} HW sector number: {sector_num}\n"
        f" Volume size: {volume_size} XIP_addr: {hex(xip_base)}"
        f" Buffer_addr: {hex(buffer_base)}"
    )


def dump_lrofs_files(node, level=1, prefix="", maxlevel=4096):
    if level > maxlevel:
        return

    if node.ln_count > 0:
        initial_indent = prefix + "├── "
        newprefix = prefix + "│   "
        dirfix = "/"
    else:
        initial_indent = prefix + "└── "
        newprefix = prefix + "    "
        dirfix = ""

    name = node.ln_name.string(length=node.ln_namesize)
    print(
        f"{initial_indent}{name}{dirfix} offset:{node.ln_offset} next:{node.ln_next}"
        f" size:{node.ln_size} child_count:{node.ln_count}"
    )

    for child in utils.ArrayIterator(node.ln_child, node.ln_count):
        dump_lrofs_files(child, level + 1, newprefix, maxlevel)


def dump_lrofs_cache(node: Inode, path):
    mpt = node.i_private.cast(utils.lookup_type("struct lrofs_mountpt_s").pointer())
    root = mpt.lm_root.cast(utils.lookup_type("struct lrofs_nodeinfo_s").pointer())
    print(f"Lrofs {path} mount point information: {hex(mpt)}")
    dump_lrofs_mpt(mpt)
    dump_lrofs_files(root)
