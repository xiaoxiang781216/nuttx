/****************************************************************************
 * libs/libc/fuse/lib_fuse_loop.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

#include <fuse_lowlevel.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int fuse_session_loop(FAR struct fuse_session *se)
{
  struct fuse_buf buf;
  int ret = 0;

  memset(&buf, 0, sizeof(buf));

  while (!fuse_session_exited(se))
    {
      ret = fuse_session_receive_buf(se, &buf);

      if (ret == -EINTR)
        {
          continue;
        }

      if (ret <= 0)
        {
          break;
        }

      fuse_session_process_buf(se, &buf);
    }

  fuse_buf_free(&buf);

  /* No error, just the length of the most recently read request */

  if (ret > 0)
    {
      ret = 0;
    }

  return ret;
}
