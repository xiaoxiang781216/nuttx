/****************************************************************************
 * libs/libc/gdbstub/debugpoint.c
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

#include <nuttx/ascii.h>

#include "internal.h"

#ifdef CONFIG_ARCH_HAVE_DEBUG

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_SMP
static struct smp_call_data_s g_gdb_smp_call_data;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_SMP

/****************************************************************************
 * Name: gdb_smp_debugpoint_add
 *
 * Description:
 *  Add debug point to all cpu.
 *
 ****************************************************************************/

static int gdb_smp_debugpoint_add(FAR void *arg)
{
  FAR struct gdb_debugpoint_s *point = arg;
  return up_debugpoint_add(point->type, point->addr, point->size,
                           point->callback, point->arg);
}

/****************************************************************************
 * Name: gdb_smp_debugpoint_remove
 *
 * Description:
 *  Remove debug point to all cpu.
 *
 ****************************************************************************/

static int gdb_smp_debugpoint_remove(FAR void *arg)
{
  FAR struct gdb_debugpoint_s *point = arg;
  return up_debugpoint_remove(point->type, point->addr, point->size);
}

#endif /* CONFIG_SMP */

/****************************************************************************
 * Name: gdb_debugpoint_callback
 *
 * Description:
 *  The debugpoint callback is used by GDB to request.
 *
 ****************************************************************************/

static void gdb_debugpoint_callback(int type, FAR void *addr,
                                    size_t size, FAR void *arg)
{
  int stopreason;

  switch (type)
    {
      case DEBUGPOINT_BREAKPOINT:
        stopreason = GDB_STOPREASON_BREAKPOINT;
        break;
      case DEBUGPOINT_WATCHPOINT_RO:
        stopreason = GDB_STOPREASON_WATCHPOINT_RO;
        break;
      case DEBUGPOINT_WATCHPOINT_WO:
        stopreason = GDB_STOPREASON_WATCHPOINT_WO;
        break;
      case DEBUGPOINT_WATCHPOINT_RW:
        stopreason = GDB_STOPREASON_WATCHPOINT_RW;
        break;
      case DEBUGPOINT_STEPPOINT:
        stopreason = GDB_STOPREASON_STEPPOINT;
        gdb_debugpoint_remove(GDB_STOPREASON_STEPPOINT, NULL, 0);
        break;
      default:
        return;
    }

  gdb_process(arg, stopreason, addr);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_debugpoint_add
 ****************************************************************************/

int gdb_debugpoint_add(int type, FAR void *addr, size_t size,
                       debug_callback_t callback, FAR void *arg)
{
#ifdef CONFIG_SMP
  struct gdb_debugpoint_s point;
  point.type = type;
  point.addr = addr;
  point.size = size;
  point.callback = callback;
  point.arg = arg;

  nxsched_smp_call_init(&g_gdb_smp_call_data, gdb_smp_debugpoint_add,
                        &point);
  return nxsched_smp_call_async((1 << CONFIG_SMP_NCPUS) - 1,
                                &g_gdb_smp_call_data);
#else
  return up_debugpoint_add(type, addr, size, callback, arg);
#endif
}

/****************************************************************************
 * Name: gdb_debugpoint_remove
 ****************************************************************************/

int gdb_debugpoint_remove(int type, FAR void *addr, size_t size)
{
#ifdef CONFIG_SMP
  struct gdb_debugpoint_s point;
  point.type = type;
  point.addr = addr;
  point.size = size;

  nxsched_smp_call_init(&g_gdb_smp_call_data, gdb_smp_debugpoint_remove,
                        &point);
  return nxsched_smp_call_async((1 << CONFIG_SMP_NCPUS) - 1,
                                &g_gdb_smp_call_data);
#else
  return up_debugpoint_remove(type, addr, size);
#endif
}

/****************************************************************************
 * Name: gdb_debugpoint
 *
 * Description:
 *   The debugpoint packet is used by GDB to request information from
 *
 * Input Parameters:
 *   state  - The pointer to the GDB state structure.
 *   enable - Enable or disable debugpoint.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Comand Format: Z/z type,addr,length
 *        Response Format: OK
 *   Z is set breakpoint.
 *   z is clear breakpoint.
 *   type: 0 is software breakpoint.
 *         1 is hardware breakpoint.
 *         2 is write watchpoint.
 *         3 is read watchpoint.
 *         4 is read/write watchpoint.
 *   length: is the length of watchpoint.
 *           if is breakpoint, length is instruction type.
 *
 ****************************************************************************/

int gdb_debugpoint(FAR struct gdb_state_s *state, bool enable)
{
  uintptr_t type;
  uintptr_t addr;
  uintptr_t size;
  int ret;

  state->pkt_next += 1;
  ret = gdb_expect_integer(state, &type);
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_expect_separator(state, ',');
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_expect_integer(state, &addr);
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_expect_separator(state, ',');
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_expect_integer(state, &size);
  if (ret < 0)
    {
      return ret;
    }

  switch (type)
    {
      case 0: /* just use hardware break */
      case 1:
        type = DEBUGPOINT_BREAKPOINT;
        break;
      case 2:
        type = DEBUGPOINT_WATCHPOINT_WO;
        break;
      case 3:
        type = DEBUGPOINT_WATCHPOINT_RO;
        break;
      case 4:
        type = DEBUGPOINT_WATCHPOINT_RW;
        break;
      default:
        return -EPROTONOSUPPORT;
    }

  if (enable)
    {
      ret = gdb_debugpoint_add(type, (FAR void *)addr, size,
                               gdb_debugpoint_callback, state);
    }
  else
    {
      ret = gdb_debugpoint_remove(type, (FAR void *)addr, size);
    }

  if (ret < 0)
    {
      return ret;
    }

  return gdb_send_ok_packet(state);
}

/****************************************************************************
 * Name: gdb_step
 *
 * Description:
 *   The handle step packet is used by GDB to request information from
 *
 * Input Parameters:
 *   state  - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Comand Format: s
 *        Response Format: OK
 *
 ****************************************************************************/

int gdb_step(FAR struct gdb_state_s *state)
{
  int ret = gdb_debugpoint_add(GDB_STOPREASON_STEPPOINT, NULL, 0,
                               gdb_debugpoint_callback, state);

  if (ret < 0)
    {
      return ret;
    }

  return gdb_send_ok_packet(state);
}

/****************************************************************************
 * Name: gdb_continue
 *
 * Description:
 *   The handle continue packet is used by GDB to request information from
 *
 * Input Parameters:
 *   state  - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Comand Format: c
 *        Response Format: OK
 *
 ****************************************************************************/

int gdb_continue(FAR struct gdb_state_s *state)
{
  return gdb_send_ok_packet(state);
}

#endif /* CONFIG_ARCH_HAVE_DEBUG */
