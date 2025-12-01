/****************************************************************************
 * libs/libc/grp/lib_getgrbuf.c
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

#include <nuttx/config.h>

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <grp.h>

#include <nuttx/tls_task.h>

#include "grp/lib_grp.h"
#include "libc.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: getgrbuf
 *
 * Description:
 *   libc/grp internal helper function for getgrgid and getgrnam to allocate
 *   and setup a group structure once a matching entry has been found.
 *
 * Input Parameters:
 *   gid    - Value to set the group structure's gr_gid field to.
 *   name   - Value to set the group structure's gr_name field to.
 *   passwd - Value to set the group structure's passwd field to.
 *
 * Returned Value:
 *   A pointer to a statically allocated group structure, or NULL if an
 *   error occurs, in which case errno is set appropriately.
 *
 ****************************************************************************/

FAR struct group *getgrbuf(gid_t gid, FAR const char *name,
                           FAR const char *passwd)
{
  FAR struct task_info_s *info = task_get_info();
  FAR struct group *result;
  FAR char *newbuf;
  size_t buflen;
  int err;

  buflen = sizeof(FAR char **) + strlen(name) + 1 + strlen(passwd) + 1;

  newbuf = lib_realloc(info->ta_group_buffer, buflen);
  if (!newbuf)
    {
      err = ENOMEM;
      goto error;
    }

  info->ta_group_buffer = newbuf;
  err = getgrbuf_r(gid, name, passwd, &info->ta_group,
                   info->ta_group_buffer, buflen, &result);

  if (err)
    {
      goto error;
    }

  return result;

error:
  set_errno(err);
  return NULL;
}
