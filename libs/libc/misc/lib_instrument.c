/****************************************************************************
 * libs/libc/misc/lib_instrument.c
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

#include <nuttx/atomic.h>
#include <nuttx/instrument.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Avoid instrument bootstrap */

#define MAIGC_NUMBMER 0x5a5a5a5a

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Use static to avoid instrument bootstrap */

static atomic_t g_magic;
static sq_queue_t g_instrument_queue;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __cyg_profile_func_enter
 ****************************************************************************/

void noinstrument_function
__cyg_profile_func_enter(FAR void *this_fn, FAR void *call_site)
{
  FAR struct instrument_s *instrument;
  FAR sq_entry_t *entry;

  /* Atomically increment g_magic and check the old value.
   * If old value != MAGIC, either not initialized or already in a
   * callback (recursion). Restore and return immediately.
   */

  if (atomic_add_relaxed(&g_magic, 1) != MAIGC_NUMBMER)
    {
      atomic_sub_relaxed(&g_magic, 1);
      return;
    }

  sq_for_every(&g_instrument_queue, entry)
    {
      instrument = (FAR struct instrument_s *)entry;
      if (instrument->enter)
        {
          instrument->enter(this_fn, call_site, instrument->arg);
        }
    }

  atomic_sub_relaxed(&g_magic, 1);
}

/****************************************************************************
 * Name: __cyg_profile_func_exit
 ****************************************************************************/

void noinstrument_function
__cyg_profile_func_exit(FAR void *this_fn, FAR void *call_site)
{
  FAR struct instrument_s *instrument;
  FAR sq_entry_t *entry;

  /* Atomically increment g_magic and check the old value.
   * If old value != MAGIC, either not initialized or already in a
   * callback (recursion). Restore and return immediately.
   */

  if (atomic_add_relaxed(&g_magic, 1) != MAIGC_NUMBMER)
    {
      atomic_sub_relaxed(&g_magic, 1);
      return;
    }

  sq_for_every(&g_instrument_queue, entry)
    {
      instrument = (FAR struct instrument_s *)entry;
      if (instrument->leave)
        {
          instrument->leave(this_fn, call_site, instrument->arg);
        }
    }

  atomic_sub_relaxed(&g_magic, 1);
}

/****************************************************************************
 * Name: instrument_register
 *
 * Description: register instrument, it will be called
 *              when function enter or exit.
 *
 * Input Parameters:
 *   entry - instrument entry structure.
 * Notice:
 *  use CONFIG_ARCH_INSTRUMENT_ALL must mark _start or entry
 *  noinstrument_function, becuase bss not set.
 *  Make sure your callbacks are not instrumented recursively.
 *
 ****************************************************************************/

void noinstrument_function
instrument_register(FAR struct instrument_s *entry)
{
  if (entry != NULL)
    {
      sq_addlast((FAR sq_entry_t *)entry, &g_instrument_queue);
      atomic_set(&g_magic, MAIGC_NUMBMER);
    }
}
