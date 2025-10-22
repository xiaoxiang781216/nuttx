/****************************************************************************
 * libs/libc/pwd/lib_getspnam.c
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

#include <pwd.h>
#include <shadow.h>
#include <string.h>

#include <nuttx/tls.h>

#include "pwd/lib_pwd.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR struct spwd *getspnam(FAR const char *name)
{
  FAR struct spwd *result = NULL;

#ifdef CONFIG_LIBC_PASSWD_FILE
  FAR struct task_info_s *info = task_get_info();

  task_info_init_buffer(info->ta_passwd_buffer, CONFIG_LIBC_PASSWD_LINESIZE);
  getspnam_r(name, &info->ta_spwd, info->ta_passwd_buffer,
             CONFIG_LIBC_PASSWD_LINESIZE, &result);
#else
  if (strcmp(name, ROOT_NAME) == 0)
    {
      FAR struct task_info_s *info = task_get_info();
      size_t nsize = sizeof(ROOT_NAME);
      size_t psize = sizeof(ROOT_PWDP);
      result = &info->ta_spwd;

      task_info_init_buffer(info->ta_passwd_buffer,
                            CONFIG_LIBC_PASSWD_LINESIZE);
      result->sp_namp = info->ta_passwd_buffer;
      result->sp_pwdp = &info->ta_passwd_buffer[nsize];

      strlcpy(result->sp_namp, ROOT_NAME, nsize);
      strlcpy(result->sp_pwdp, ROOT_PWDP, psize);
    }
#endif

  return result;
}
