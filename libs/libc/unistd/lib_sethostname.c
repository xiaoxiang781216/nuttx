/****************************************************************************
 * libs/libc/unistd/lib_sethostname.c
 *
 * SPDX-License-Identifier: BSD-3-Clause
 * SPDX-FileCopyrightText: 2015 Stavros Polymenis. All rights reserved.
 * SPDX-FileCopyrightText: 2015, 2016 Gregory Nutt. All rights reserved.
 * SPDX-FileContributor: Stavros Polymenis <sp@orbitalfox.com>
 * SPDX-FileContributor: Gregory Nutt <gnutt@nuttx.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name NuttX nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <string.h>
#include <unistd.h>

#include <nuttx/mutex.h>
#include <nuttx/tls_task.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sethostname
 *
 * Description:
 *
 *   sethostname() sets the hostname to the value given in the character
 *   array name. The len argument specifies the number of bytes in name.
 *   (Thus, name does not require a terminating null byte.)
 *
 * Conforming To
 *   SVr4, 4.4BSD (these interfaces first appeared in 4.2BSD). POSIX.1-2001
 *   specifies gethostname() but not sethostname().
 *
 * Input Parameters:
 *   name - The user buffer to providing the new host name.
 *   namelen - The size of the user buffer in bytes.
 *
 * Returned Value:
 *   Upon successful completion, 0 will be returned; otherwise, -1 will be
 *   returned.  No errors are defined;  errno variable is not set.
 *
 ****************************************************************************/

int sethostname(FAR const char *name, size_t namelen)
{
  FAR struct task_info_s *info = task_get_info();

  /* Save the new host name, truncating to HOST_NAME_MAX if necessary.  This
   * internal copy is always NUL terminated. The hostname is stored in
   * task_info_s.  Use the task lock to protect against concurrent access.
   */

  nxrmutex_lock(&info->ta_lock);
  strlcpy(info->ta_hostname, name, sizeof(info->ta_hostname));
  nxrmutex_unlock(&info->ta_lock);

  return 0;
}
