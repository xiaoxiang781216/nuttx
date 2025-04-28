/****************************************************************************
 * binfmt/binfmt_copyattr.c
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

#include <string.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/kmalloc.h>
#include <nuttx/binfmt/binfmt.h>

#include "binfmt.h"

#if defined(CONFIG_ARCH_ADDRENV) && defined(CONFIG_BUILD_KERNEL) \
  && !defined(CONFIG_BINFMT_DISABLE)

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: binfmt_copyattr
 *
 * Description:
 *   Copies the POSIX spawn attributes to a new memory location to ensure
 *   accessibility in the new process address environment.
 *
 * Input Parameters:
 *   copy - Pointer to the location where the copied attributes will be
 *   stored.
 *   attr - Pointer to the original POSIX spawn attributes.
 *
 * Returned Value:
 *   Returns OK on success, or -ENOMEM if memory allocation fails.
 *
 ****************************************************************************/

int binfmt_copyattr(FAR const posix_spawnattr_t **copy,
                    FAR const posix_spawnattr_t *attr)
{
  if (attr == NULL)
    {
      *copy = NULL;
      return OK;
    }

  *copy = kmm_malloc(sizeof(posix_spawnattr_t));
  if (*copy == NULL)
    {
      berr("ERROR: Failed to allocate memory for attr copy\n");
      return -ENOMEM;
    }

  memcpy((FAR void *)*copy, attr, sizeof(posix_spawnattr_t));
  return OK;
}

/****************************************************************************
 * Name: binfmt_freeattr
 *
 * Description:
 *   Release the memory allocated for the copied POSIX spawn attributes.
 *
 * Input Parameters:
 *   copy - Pointer to the copied POSIX spawn attributes.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void binfmt_freeattr(FAR const posix_spawnattr_t *copy)
{
  if (copy != NULL)
    {
      /* Free the argument buffer */

      kmm_free((FAR void *)copy);
    }
}
#endif
