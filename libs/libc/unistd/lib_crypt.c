/****************************************************************************
 * libs/libc/unistd/lib_crypt.c
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

#include <unistd.h>

#include <nuttx/tls_task.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

char *crypt(const char *key, const char *salt)
{
  FAR struct task_info_s *info = task_get_info();

  task_info_init_buffer(info->ta_passwd_buf, 128);
  return crypt_r(key, salt, info->ta_passwd_buf);
}
