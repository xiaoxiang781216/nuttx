/****************************************************************************
 * arch/zsp/src/zsp_sim/zsp_sim_hostfs.c
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

#include <nuttx/config.h>
#include <nuttx/fs/hostfs.h>

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <dirent.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ZSP simulator pre-opened IO channels.
 *
 * The ZSP simulator (zisimg5) provides POSIX-level semihosting with
 * pre-opened file descriptors for console IO at program startup:
 *   stdout fd = 1  (confirmed from h_stdio.h and runtime testing)
 *   stdin  fd = 3  (NOT 0; fd=0 causes "Invalid file pointer" SIGTRAP)
 *
 * These are unidirectional: fd=1 for write only, fd=3 for read only.
 *
 * The _open semihosting call (opcode 0x23) opens real host filesystem
 * files — there is no virtual console path (unlike ARM's ":tt").
 * Therefore console IO must use these pre-defined fds directly.
 *
 * TODO: Confirm with VeriSilicon whether these fd values are guaranteed
 * by documentation or could change across simulator versions.
 */

#define ZSP_STDOUT_FD  1
#define ZSP_STDIN_FD   3

/* Semihosting opcodes (from libc.a pr_routine.o disassembly) */

#define ZSP_SEMIHOST_WRITE  0x26
#define ZSP_SEMIHOST_READ   0x25

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: zsp_semihost_write
 *
 * Description:
 *   Write to host via semihosting. The simulator intercepts the magic
 *   value 0xf00df00d in a6 + stopdisp 0x3, then reads registers:
 *     r7  = opcode (0x26 for write)
 *     r1r0 = fd
 *     a4  = buffer pointer
 *     r3r2 = byte count
 *
 *   This file must be compiled with -mno_zopt because the zopt assembler
 *   optimizer renames hardcoded registers inside inline asm.
 *
 ****************************************************************************/

static ssize_t zsp_semihost_write(int fd, const void *buf, size_t count)
{
  __asm__ __volatile__(
    "\tmov32 r0, %0\n"
    "\tmov32 a4, %1\n"
    "\tmov32 r2, %2\n"
    "\tmov16 r7, 0x26\n"
    "\tmov32 a6, 0xf00df00d\n"
    "\tstopdisp 0x3\n"
    "\tbits %%ireq, 14\n"
    : : "r"(fd), "r"(buf), "r"(count)
    : "memory", "r0", "r2", "a4", "a6", "r7"
  );

  return count;
}

/****************************************************************************
 * Name: zsp_semihost_read
 *
 * Description:
 *   Read from host via semihosting. Same protocol as write but with
 *   opcode 0x25. The simulator reads from the console input stream.
 *
 ****************************************************************************/

static ssize_t zsp_semihost_read(int fd, void *buf, size_t count)
{
  __asm__ __volatile__(
    "\tmov32 r0, %0\n"
    "\tmov32 a4, %1\n"
    "\tmov32 r2, %2\n"
    "\tmov16 r7, 0x25\n"
    "\tmov32 a6, 0xf00df00d\n"
    "\tstopdisp 0x3\n"
    "\tbits %%ireq, 14\n"
    : : "r"(fd), "r"(buf), "r"(count)
    : "memory", "r0", "r2", "a4", "a6", "r7"
  );

  return count;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: host_open
 *
 * Description:
 *   Open a host file. For console devices, returns pre-defined stdout fd
 *   directly. The ZSP simulator has no virtual console open path (unlike
 *   ARM semihosting ":tt") — console channels are available at startup
 *   without explicit open.
 *
 ****************************************************************************/

int host_open(const char *pathname, int flags, int mode)
{
  return ZSP_STDOUT_FD;
}

/****************************************************************************
 * Name: host_close
 *
 * Description:
 *   Close a host file. Pre-defined console fds (stdout=1, stdin=3) are
 *   persistent simulator resources and cannot be closed, so this is a
 *   no-op.
 *
 ****************************************************************************/

int host_close(int fd)
{
  return 0;
}

/****************************************************************************
 * Name: host_read
 *
 * Description:
 *   Read from host console via semihosting using pre-defined stdin fd.
 *
 ****************************************************************************/

ssize_t host_read(int fd, void *buf, size_t count)
{
  return zsp_semihost_read(ZSP_STDIN_FD, buf, count);
}

/****************************************************************************
 * Name: host_write
 *
 * Description:
 *   Write to host console via semihosting using pre-defined stdout fd.
 *
 ****************************************************************************/

ssize_t host_write(int fd, const void *buf, size_t count)
{
  return zsp_semihost_write(ZSP_STDOUT_FD, buf, count);
}

/****************************************************************************
 * Name: host_lseek
 *
 * Description:
 *   Seek is not supported for console IO.
 *
 ****************************************************************************/

off_t host_lseek(int fd, off_t pos, off_t offset, int whence)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: host_ioctl
 *
 * Description:
 *   No ioctl operations supported for simulator console.
 *
 ****************************************************************************/

int host_ioctl(int fd, int request, unsigned long arg)
{
  return -ENOTTY;
}

/****************************************************************************
 * Name: host_fstat
 *
 * Description:
 *   Not supported for simulator console.
 *
 ****************************************************************************/

int host_fstat(int fd, struct stat *buf)
{
  return -ENOSYS;
}

int host_fchstat(int fd, const struct stat *buf, int flags)
{
  return -ENOSYS;
}

int host_ftruncate(int fd, off_t length)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: host_sync
 *
 * Description:
 *   Flush host file buffers. Semihosting IO is unbuffered on the ZSP
 *   side (each write goes directly to the simulator), so this is a no-op.
 *
 ****************************************************************************/

void host_sync(int fd)
{
}

/****************************************************************************
 * Name: host_opendir / host_readdir / host_rewinddir / host_closedir
 *
 * Description:
 *   Directory operations are not supported. The ZSP simulator semihosting
 *   does not provide directory enumeration.
 *
 ****************************************************************************/

void *host_opendir(const char *name)
{
  return NULL;
}

int host_readdir(void *dirp, struct dirent *entry)
{
  return -ENOENT;
}

void host_rewinddir(void *dirp)
{
}

int host_closedir(void *dirp)
{
  return 0;
}

/****************************************************************************
 * Name: host_statfs / host_stat / host_chstat
 *
 * Description:
 *   Filesystem metadata operations are not supported.
 *
 ****************************************************************************/

int host_statfs(const char *path, struct statfs *buf)
{
  return -ENOSYS;
}

int host_stat(const char *path, struct stat *buf)
{
  return -ENOSYS;
}

int host_chstat(const char *path, const struct stat *buf, int flags)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: host_unlink / host_mkdir / host_rmdir / host_rename
 *
 * Description:
 *   Filesystem modification operations are not supported.
 *
 ****************************************************************************/

int host_unlink(const char *pathname)
{
  return -ENOSYS;
}

int host_mkdir(const char *pathname, int mode)
{
  return -ENOSYS;
}

int host_rmdir(const char *pathname)
{
  return -ENOSYS;
}

int host_rename(const char *oldpath, const char *newpath)
{
  return -ENOSYS;
}
