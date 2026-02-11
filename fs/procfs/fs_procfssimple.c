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

struct simple_file_s
{
  struct procfs_file_s base;   /* Base open file structure */
  struct procfs_entry_s entry; /* Entry information */
  FAR void *value;             /* Pointer to variable or priv data */
  procfs_read_t read;          /* Read callback */
  procfs_write_t write;        /* Write callback */
};

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PROCFS_DEFINE_READ(name, type, fmt)                              \
static ssize_t procfs_read_##name(FAR struct file *filep,                \
                                  FAR char *buffer, size_t buflen,       \
                                  FAR void *priv)                        \
{                                                                        \
  char line[32];                                                         \
  size_t linesize;                                                       \
  off_t offset = filep->f_pos;                                           \
  ssize_t ret;                                                           \
  linesize = procfs_snprintf(line, sizeof(line), fmt "\n",               \
                             *(FAR const type *)priv);                   \
  ret = procfs_memcpy(line, linesize, buffer, buflen, &offset);          \
  if (ret > 0)                                                           \
    {                                                                    \
      filep->f_pos += ret;                                               \
    }                                                                    \
                                                                         \
  return ret;                                                            \
}

#define PROCFS_DEFINE_WRITE(name, type)                                  \
static ssize_t procfs_write_##name(FAR struct file *filep,               \
                                   FAR const char *buffer,               \
                                   size_t buflen, FAR void *priv)        \
{                                                                        \
  FAR char *endptr;                                                      \
  unsigned long long val = strtoull(buffer, &endptr, 0);                 \
  if (endptr == buffer)                                                  \
    {                                                                    \
      return -EINVAL;                                                    \
    }                                                                    \
                                                                         \
  *(FAR type *)priv = (type)val;                                         \
  return buflen;                                                         \
}

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

  return entry->read(filep, buffer, buflen, entry->value);
}

/****************************************************************************
 * Name: proc_simple_write
 ****************************************************************************/

static ssize_t proc_simple_write(FAR struct file *filep,
                                 FAR const char *buffer,
                                 size_t buflen)
{
  FAR struct simple_file_s *entry;

  DEBUGASSERT(filep != NULL);

  if (buffer == NULL || buflen == 0)
    {
      return -EINVAL;
    }

  if (buffer[0] == '\n')
    {
      return 0;
    }

  entry = (FAR struct simple_file_s *)filep->f_priv;
  DEBUGASSERT(entry != NULL);

  return entry->write(filep, buffer, buflen, entry->value);
}

PROCFS_DEFINE_READ(u8, uint8_t, "%" PRIu8)
PROCFS_DEFINE_WRITE(u8, uint8_t)
PROCFS_DEFINE_READ(u16, uint16_t, "%" PRIu16)
PROCFS_DEFINE_WRITE(u16, uint16_t)
PROCFS_DEFINE_READ(u32, uint32_t, "%" PRIu32)
PROCFS_DEFINE_WRITE(u32, uint32_t)
PROCFS_DEFINE_READ(u64, uint64_t, "%" PRIu64)
PROCFS_DEFINE_WRITE(u64, uint64_t)
PROCFS_DEFINE_READ(ulong, unsigned long, "%lu")
PROCFS_DEFINE_WRITE(ulong, unsigned long)
PROCFS_DEFINE_READ(x8, uint8_t, "0x%" PRIx8)
PROCFS_DEFINE_WRITE(x8, uint8_t)
PROCFS_DEFINE_READ(x16, uint16_t, "0x%" PRIx16)
PROCFS_DEFINE_WRITE(x16, uint16_t)
PROCFS_DEFINE_READ(x32, uint32_t, "0x%" PRIx32)
PROCFS_DEFINE_WRITE(x32, uint32_t)
PROCFS_DEFINE_READ(x64, uint64_t, "0x%" PRIx64)
PROCFS_DEFINE_WRITE(x64, uint64_t)
PROCFS_DEFINE_READ(size, size_t, "%zu")
PROCFS_DEFINE_WRITE(size, size_t)
PROCFS_DEFINE_READ(bool, bool, "%d")
PROCFS_DEFINE_WRITE(bool, bool)

/****************************************************************************
 * Name: procfs_create_var
 ****************************************************************************/

static FAR struct procfs_entry_s *
procfs_create_var(FAR const char *path, mode_t mode, FAR void *value,
                  procfs_read_t read, procfs_write_t write)
{
  FAR struct simple_file_s *file;
  int ret;

  file = (FAR struct simple_file_s *)fs_heap_malloc(sizeof(*file));
  if (file == NULL)
    {
      ferr("ERROR: Failed to allocate simple_file_s\n");
      return NULL;
    }

  file->value = value;
  file->read = read;
  file->write = write;
  file->entry.pathpattern = path;
  file->entry.type = PROCFS_FILE_TYPE;
  file->entry.mode = mode;
  file->entry.ops = &g_simple_operations;
  file->entry.priv = file;

  ret = procfs_register(&file->entry);
  if (ret < 0)
    {
      fs_heap_free(file);
      ferr("ERROR: Failed to create file\n");
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
  return procfs_create_var(path, mode, value,
                           procfs_read_u8, procfs_write_u8);
}

FAR struct procfs_entry_s *
procfs_create_u16(FAR const char *path, mode_t mode,
                  FAR uint16_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_u16, procfs_write_u16);
}

FAR struct procfs_entry_s *
procfs_create_u32(FAR const char *path, mode_t mode,
                  FAR uint32_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_u32, procfs_write_u32);
}

FAR struct procfs_entry_s *
procfs_create_u64(FAR const char *path, mode_t mode,
                  FAR uint64_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_u64, procfs_write_u64);
}

FAR struct procfs_entry_s *
procfs_create_ulong(FAR const char *path, mode_t mode,
                    FAR unsigned long *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_ulong, procfs_write_ulong);
}

FAR struct procfs_entry_s *
procfs_create_x8(FAR const char *path, mode_t mode,
                 FAR uint8_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_x8, procfs_write_x8);
}

FAR struct procfs_entry_s *
procfs_create_x16(FAR const char *path, mode_t mode,
                  FAR uint16_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_x16, procfs_write_x16);
}

FAR struct procfs_entry_s *
procfs_create_x32(FAR const char *path, mode_t mode,
                  FAR uint32_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_x32, procfs_write_x32);
}

FAR struct procfs_entry_s *
procfs_create_x64(FAR const char *path, mode_t mode,
                  FAR uint64_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_x64, procfs_write_x64);
}

FAR struct procfs_entry_s *
procfs_create_size_t(FAR const char *path, mode_t mode,
                     FAR size_t *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_size, procfs_write_size);
}

FAR struct procfs_entry_s *
procfs_create_bool(FAR const char *path, mode_t mode,
                   FAR bool *value)
{
  return procfs_create_var(path, mode, value,
                           procfs_read_bool, procfs_write_bool);
}

FAR struct procfs_entry_s *
procfs_create_custom(FAR const char *path, mode_t mode,
                     procfs_read_t read, procfs_write_t write,
                     FAR void *priv)
{
  return procfs_create_var(path, mode, priv, read, write);
}
