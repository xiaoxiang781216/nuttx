/****************************************************************************
 * include/nuttx/semaphore_type.h
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

#ifndef __INCLUDE_NUTTX_SEMAPHORE_TYPE_H
#define __INCLUDE_NUTTX_SEMAPHORE_TYPE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <semaphore.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Initializers */

#ifdef CONFIG_PRIORITY_INHERITANCE
#  if CONFIG_SEM_PREALLOCHOLDERS > 0
/* semcount, flags, waitlist, hhead */

#    define NXSEM_INITIALIZER(c, f) \
       {{(c)}, (f), SEM_WAITLIST_INITIALIZER, NULL}
#  else
/* semcount, flags, waitlist, holder[2] */

#    define NXSEM_INITIALIZER(c, f) \
       {{(c)}, (f), SEM_WAITLIST_INITIALIZER, SEMHOLDER_INITIALIZER}
#  endif
#else /* CONFIG_PRIORITY_INHERITANCE */
/* semcount, flags, waitlist */

#  define NXSEM_INITIALIZER(c, f) \
     {{(c)}, (f), SEM_WAITLIST_INITIALIZER}
#endif /* CONFIG_PRIORITY_INHERITANCE */

/* Macros to retrieve sem count and to check if nxsem is mutex */

#define NXSEM_COUNT(s)        ((FAR atomic_t *)&(s)->val.semcount)
#define NXSEM_IS_MUTEX(s)     (((s)->flags & SEM_TYPE_MUTEX) != 0)

/* Mutex related helper macros */

#define NXSEM_MBLOCKING_BIT   ((int32_t)0x80000000)
#define NXSEM_NO_MHOLDER      ((int32_t)0x7fffffff)

/* Macro to retrieve mutex's atomic holder's ptr */

#define NXSEM_MHOLDER(s)      ((FAR atomic_t *)&(s)->val.mholder)

/* Check if holder value (TID) is not NO_HOLDER or RESET */

#define NXSEM_MACQUIRED(h)    (((h) & NXSEM_NO_MHOLDER) != NXSEM_NO_MHOLDER)

/* Check if mutex is acquired and blocks some other task */

#define NXSEM_MBLOCKING(h)    (((h) & NXSEM_MBLOCKING_BIT) != 0)

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

#ifdef CONFIG_FS_NAMED_SEMAPHORES
/* This is the named semaphore inode */

struct inode;
struct nsem_inode_s
{
  /* This must be the first element of the structure.  In sem_close() this
   * structure must be cast compatible with sem_t.
   */

  sem_t ns_sem;                     /* The contained semaphore */

  /* Inode payload unique to named semaphores. */

  FAR struct inode *ns_inode;       /* Containing inode */
};
#endif

#endif /* __INCLUDE_NUTTX_SEMAPHORE_TYPE_H */
