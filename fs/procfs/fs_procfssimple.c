/****************************************************************************
 * fs/procfs/fs_procfssimple.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <debug.h>
#include <nuttx/fs/procfs.h>
#include <nuttx/kmalloc.h>

#include "fs_heap.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum var_type_e
{
  VAR_U8,
  VAR_U16,
  VAR_U32,
  VAR_U64,
  VAR_ULONG,
  VAR_X8,
  VAR_X16,
  VAR_X32,
  VAR_X64,
  VAR_SIZE_T,
  VAR_BOOL,
};

struct simple_file_s
{
  struct procfs_file_s base;   /* Base open file structure */
  struct procfs_entry_s entry; /* Entry information */
  enum var_type_e type;        /* Type of variable */
  FAR void *value;             /* Pointer to variable */
  size_t size;                 /* Size of file */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int proc_simple_open(FAR struct file *filep, FAR const char *relpath,
                            int oflags, mode_t mode);
static int proc_simple_close(FAR struct file *filep);
static ssize_t proc_simple_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen);
static ssize_t proc_simple_write(FAR struct file *filep,
                                 FAR const char *buffer,
                                 size_t buflen);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct procfs_operations g_simple_operations =
{
  proc_simple_open,
  proc_simple_close,
  proc_simple_read,
  proc_simple_write,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: proc_simple_open
 ****************************************************************************/

static int proc_simple_open(FAR struct file *filep, FAR const char *relpath,
                            int oflags, mode_t mode)
{
  return OK;
}

/****************************************************************************
 * Name: proc_simple_close
 ****************************************************************************/

static int proc_simple_close(FAR struct file *filep)
{
  return OK;
}

/****************************************************************************
 * Name: proc_simple_read
 ****************************************************************************/

static ssize_t proc_simple_read(FAR struct file *filep, FAR char *buffer,
                                size_t buflen)
{
  FAR struct simple_file_s *entry;
  char line[32];
  size_t linesize;
  off_t offset;
  ssize_t ret;

  DEBUGASSERT(filep != NULL);

  if (buffer == NULL)
    {
      return -EINVAL;
    }

  if (buffer[0] == '\n')
    {
      return 0;
    }

  entry = (FAR struct simple_file_s *)filep->f_priv;
  DEBUGASSERT(entry != NULL);

  switch (entry->type)
  {
  case VAR_U8:
    linesize = procfs_snprintf(line, sizeof(line), "%" PRIu8 "\n",
                               *(FAR const uint8_t *)entry->value);
    break;
  case VAR_U16:
    linesize = procfs_snprintf(line, sizeof(line), "%" PRIu16 "\n",
                               *(FAR const uint16_t *)entry->value);
    break;
  case VAR_U32:
    linesize = procfs_snprintf(line, sizeof(line), "%" PRIu32 "\n",
                               *(FAR const uint32_t *)entry->value);
    break;
  case VAR_U64:
    linesize = procfs_snprintf(line, sizeof(line), "%" PRIu64 "\n",
                               *(FAR const uint64_t *)entry->value);
    break;
  case VAR_ULONG:
    linesize = procfs_snprintf(line, sizeof(line), "%lu\n",
                               *(FAR const unsigned long *)entry->value);
    break;
  case VAR_X8:
    linesize = procfs_snprintf(line, sizeof(line), "0x%" PRIx8 "\n",
                               *(FAR const uint8_t *)entry->value);
    break;
  case VAR_X16:
    linesize = procfs_snprintf(line, sizeof(line), "0x%" PRIx16 "\n",
                               *(FAR const uint16_t *)entry->value);
    break;
  case VAR_X32:
    linesize = procfs_snprintf(line, sizeof(line), "0x%" PRIx32 "\n",
                               *(FAR const uint32_t *)entry->value);
    break;
  case VAR_X64:
    linesize = procfs_snprintf(line, sizeof(line), "0x%" PRIx64 "\n",
                               *(FAR const uint64_t *)entry->value);
    break;
  case VAR_SIZE_T:
    linesize = procfs_snprintf(line, sizeof(line), "%zu\n",
                               *(FAR const size_t *)entry->value);
    break;
  case VAR_BOOL:
    linesize = procfs_snprintf(line, sizeof(line), "%d\n",
                               *(FAR const bool *)entry->value);
    break;
  default:
    return -EINVAL;
  }

  offset = filep->f_pos;
  ret = procfs_memcpy(line, linesize, buffer, buflen, &offset);
  if (ret > 0)
  {
    filep->f_pos += ret;
  }

  return ret;
}

/****************************************************************************
 * Name: proc_simple_write
 ****************************************************************************/

static ssize_t proc_simple_write(FAR struct file *filep,
                                 FAR const char *buffer,
                                 size_t buflen)
{
  FAR struct simple_file_s *entry;
  unsigned long long val;
  FAR char *endptr;

  DEBUGASSERT(filep != NULL);

  if (buffer == NULL || buflen == 0)
    {
      return -EINVAL;
    }

  if (buffer[0] == '\n')
    {
      return 0;
    }

  val = strtoull(buffer, &endptr, 0);
  if (endptr == buffer)
    {
      return -EINVAL;
    }

  entry = (FAR struct simple_file_s *)filep->f_priv;
  DEBUGASSERT(entry != NULL);

  switch (entry->type)
  {
  case VAR_U8:
    *(FAR uint8_t *)entry->value = (uint8_t)val;
    break;
  case VAR_U16:
    *(FAR uint16_t *)entry->value = (uint16_t)val;
    break;
  case VAR_U32:
    *(FAR uint32_t *)entry->value = (uint32_t)val;
    break;
  case VAR_U64:
    *(FAR uint64_t *)entry->value = (uint64_t)val;
    break;
  case VAR_ULONG:
    *(FAR unsigned long *)entry->value = (unsigned long)val;
    break;
  case VAR_X8:
    *(FAR uint8_t *)entry->value = (uint8_t)val;
    break;
  case VAR_X16:
    *(FAR uint16_t *)entry->value = (uint16_t)val;
    break;
  case VAR_X32:
    *(FAR uint32_t *)entry->value = (uint32_t)val;
    break;
  case VAR_X64:
    *(FAR uint64_t *)entry->value = (uint64_t)val;
    break;
  case VAR_SIZE_T:
    *(FAR size_t *)entry->value = (size_t)val;
    break;
  case VAR_BOOL:
    *(FAR bool *)entry->value = (bool)val;
    break;
  default:
    return -EINVAL;
  }

  return buflen;
}

/****************************************************************************
 * Name: procfs_create_int
 ****************************************************************************/

static FAR struct procfs_entry_s *
procfs_create_int(FAR const char *path, enum var_type_e type,
                  mode_t mode, FAR void *value)
{
  FAR struct simple_file_s *file;
  int ret;

  file = (FAR struct simple_file_s *)fs_heap_malloc(sizeof(*file));
  if (file == NULL)
    {
      ferr("ERROR: Failed to allocate simple_file_s\n");
      return NULL;
    }

  file->type = type;
  file->value = value;
  file->entry.pathpattern = path;
  file->entry.type = PROCFS_FILE_TYPE;
  file->entry.mode = mode;
  file->entry.ops = &g_simple_operations;
  file->entry.priv = file;

  ret = procfs_register(&file->entry);
  if (ret < 0)
    {
      fs_heap_free(file);
      ferr("ERROR : Failed to create file\n");
      return NULL;
    }

  return &file->entry;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct procfs_entry_s *
procfs_create_u8(FAR const char *path, mode_t mode,
                 FAR uint8_t *value)
{
  return procfs_create_int(path, VAR_U8, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_u16(FAR const char *path, mode_t mode,
                  FAR uint16_t *value)
{
  return procfs_create_int(path, VAR_U16, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_u32(FAR const char *path, mode_t mode,
                  FAR uint32_t *value)
{
  return procfs_create_int(path, VAR_U32, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_u64(FAR const char *path, mode_t mode,
                  FAR uint64_t *value)
{
  return procfs_create_int(path, VAR_U64, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_ulong(FAR const char *path, mode_t mode,
                    FAR unsigned long *value)
{
  return procfs_create_int(path, VAR_ULONG, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_x8(FAR const char *path, mode_t mode,
                 FAR uint8_t *value)
{
  return procfs_create_int(path, VAR_X8, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_x16(FAR const char *path, mode_t mode,
                  FAR uint16_t *value)
{
  return procfs_create_int(path, VAR_X16, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_x32(FAR const char *path, mode_t mode,
                  FAR uint32_t *value)
{
  return procfs_create_int(path, VAR_X32, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_x64(FAR const char *path, mode_t mode,
                  FAR uint64_t *value)
{
  return procfs_create_int(path, VAR_X64, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_size_t(FAR const char *path, mode_t mode,
                     FAR size_t *value)
{
  return procfs_create_int(path, VAR_SIZE_T, mode, value);
}

FAR struct procfs_entry_s *
procfs_create_bool(FAR const char *path, mode_t mode,
                   FAR bool *value)
{
  return procfs_create_int(path, VAR_BOOL, mode, value);
}
