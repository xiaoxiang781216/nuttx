############################################################################
# tools/pynuttx/nxgdb/fs/utils.py
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

from .. import utils

FSNODEFLAG_TYPE_MASK = 0x0000000F

CONFIG_PSEUDOFS_FILE = utils.lookup_type("struct fs_pseudofile_s") is not None
CONFIG_PSEUDOFS_ATTRIBUTES = utils.has_field("struct inode", "i_mode")

CONFIG_FS_BACKTRACE = utils.has_field("struct fd", "f_backtrace")
CONFIG_FS_SHMFS = utils.lookup_type("struct shmfs_object_s") is not None

CONFIG_NFILE_DESCRIPTORS_PER_BLOCK = utils.get_field_nitems(
    "struct fdlist", "fl_prefds"
)

# see fs_mount.c
CONFIG_DISABLE_MOUNTPOINT = utils.lookup_type("struct fsmap_t") is None

# see fs_romfs.c
CONFIG_FS_ROMFS_CACHE_NODE = utils.has_field("struct romfs_dir_s", "firstnode")

# see lfs_vfs.c
CONFIG_FS_LITTLEFS = utils.lookup_type("struct littlefs_file_s") is not None

# see yaffs_vfs.c
CONFIG_FS_YAFFS = utils.lookup_type("struct yaffs_file_s") is not None

# see fatfs_vfs.c
CONFIG_FS_FATFS = utils.lookup_type("struct fatfs_file_s") is not None

# see fs_Irofs.c
CONFIG_FS_LROFS = utils.lookup_type("struct lrofs_dir_s") is not None
