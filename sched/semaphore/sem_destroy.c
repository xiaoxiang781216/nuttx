/****************************************************************************
 * sched/semaphore/sem_destroy.c
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

#include <errno.h>
#include <assert.h>

#include "semaphore/semaphore.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsem_destroy
 *
 * Description:
 *   This function is used to destroy the un-named semaphore indicated by
 *   'sem'.  Only a semaphore that was created using nxsem_init() may be
 *   destroyed using nxsem_destroy(); the effect of calling nxsem_destroy()
 *   with a named semaphore is undefined.  The effect of subsequent use of
 *   the semaphore sem is undefined until sem is re-initialized by another
 *   call to nxsem_init().
 *
 *   The effect of destroying a semaphore upon which other processes are
 *   currently blocked is undefined.
 *
 * Input Parameters:
 *   sem - Semaphore to be destroyed.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *
 ****************************************************************************/

int nxsem_destroy(FAR sem_t *sem)
{
  int32_t old;
  bool mutex;
  FAR atomic_t *val;
  int32_t new;

  DEBUGASSERT(sem != NULL);

  mutex = NXSEM_IS_MUTEX(sem);
  val = mutex ? NXSEM_MHOLDER(sem) : NXSEM_COUNT(sem);

  /* A destroyed mutex keeps NXSEM_MBLOCKING_BIT set with no holder.  This is
   * the same encoding that nxsem_reset() uses, and the lock-free fast paths
   * in nxmutex_wait() / nxmutex_post() cannot complete their cmpxchg against
   * it, so they fall through to the slow paths where SEM_FLAG_DESTROYED is
   * checked first and -EINVAL is returned.  Do not reset the holder to a
   * plain NXSEM_NO_MHOLDER here:  that would make a destroyed mutex look
   * free to the fast paths again.
   */

  new = mutex ? (NXSEM_NO_MHOLDER | NXSEM_MBLOCKING_BIT) : 1;

  /* There is really no particular action that we need
   * take to destroy a semaphore.  We will just reset
   * the count to some reasonable value (0) and release
   * ownership.
   *
   * Check if other threads are waiting on the semaphore.
   * In this case, the behavior is undefined.  We will:
   * leave the count unchanged but still return OK.
   */

  old = atomic_read(val);
  do
    {
      if (mutex)
        {
          if (NXSEM_MBLOCKING(old))
            {
              break;
            }
        }
      else if(old < 0)
        {
          break;
        }
    }
  while (!atomic_try_cmpxchg_release(val, &old, new));

  /* Release holders of the semaphore */

  nxsem_destroyholder(sem);

  /* Mark the semaphore as destroyed.  The priority protocol is dropped as
   * before, but SEM_TYPE_MUTEX is preserved so that NXSEM_IS_MUTEX() keeps
   * telling the truth about a destroyed object.
   */

  sem->flags = (sem->flags & SEM_TYPE_MUTEX) | SEM_FLAG_DESTROYED;
  return OK;
}
