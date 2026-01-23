/****************************************************************************
 * libs/libc/gdbstub/lib_gdbstub.c
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

#include <errno.h>
#include <unistd.h>

#include <nuttx/lib/lib.h>
#include <nuttx/ascii.h>
#include <nuttx/streams.h>

#include "internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Architecture can overwrite the default XCPTCONTEXT alignment */

#ifndef XCPTCONTEXT_ALIGN
#  define XCPTCONTEXT_ALIGN 16
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_state_init
 *
 * Description:
 *   Initialize the GDB state structure.
 *
 * Input Parameters:
 *   send    - The pointer to the send function.
 *   recv    - The pointer to the receive function.
 *   monitor - The pointer to the monitor_cmd function.
 *   priv    - The pointer to the private data.
 *
 * Returned Value:
 *   The pointer to the GDB state structure on success.
 *   NULL on failure.
 *
 ****************************************************************************/

FAR struct gdb_state_s *gdb_state_init(gdb_send_func_t send,
                                       gdb_recv_func_t recv,
                                       gdb_monitor_func_t monitor,
                                       FAR void *priv)
{
  FAR struct gdb_state_s *state = lib_memalign(XCPTCONTEXT_ALIGN,
                                               sizeof(struct gdb_state_s));

  if (state == NULL)
    {
      return NULL;
    }

  memset(state, 0, sizeof(struct gdb_state_s));
  state->send = send;
  state->recv = recv;
  state->priv = priv;
  state->monitor = monitor;

#ifdef CONFIG_LIB_GDBSTUB_DEBUG
  lib_syslograwstream_open(&state->stream);
#endif

  return state;
}

/****************************************************************************
 * Name: gdb_state_uninit
 *
 * Description:
 *   Uninitialize the GDB state structure.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 ****************************************************************************/

void gdb_state_uninit(FAR struct gdb_state_s *state)
{
  if (state != NULL)
    {
      lib_free(state);
    }
}

/****************************************************************************
 * Name: gdb_console_message
 *
 * Description:
 *   Send a message to the GDB console (via O XX... packet).
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *    Zero on success.
 *    Negative value on error.
 *
 ****************************************************************************/

int gdb_console_message(FAR struct gdb_state_s *state, FAR const char *msg)
{
  ssize_t ret;

  if (state == NULL || msg == NULL)
    {
      return -EINVAL;
    }

  state->pkt_buf[0] = 'O';
  ret = gdb_bin2hex(&state->pkt_buf[1], sizeof(state->pkt_buf) - 1,
                    msg, strlen(msg));
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = 1 + ret;
  return gdb_send_packet(state);
}

/****************************************************************************
 * Name: gdb_process
 *
 * Description:
 *   Main debug loop. Handles commands.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   Zero if successful.
 *   Negative value on error.
 *
 ****************************************************************************/

int gdb_process(FAR struct gdb_state_s *state, int stopreason,
                FAR void *stopaddr)
{
  int ret;

  if (stopreason != GDB_STOPREASON_NONE)
    {
      state->pid = gettid();
      gdb_update_regcache(state);
      gdb_send_stop(state, stopreason, stopaddr);
    }

  while ((ret = gdb_recv_packet(state)) >= 0)
    {
      /* Handle one letter commands */

      switch (state->pkt_buf[0])
        {
          case '?': /* gdbserial status */
            ret = gdb_send_signal_packet(state, 0x00);
            break;
          case 'g': /* Read registers */
            ret = gdb_read_registers(state);
            break;
          case 'G': /* Write registers */
            ret = gdb_write_registers(state);
            break;
          case 'p': /* Read one register */
            ret = gdb_read_register(state);
            break;
          case 'P': /* Write one register */
            ret = gdb_write_register(state);
            break;
          case 'm': /* Read memory */
            ret = gdb_read_memory(state);
            break;
          case 'M': /* Write memory */
            ret = gdb_write_memory(state);
            break;
          case 'x': /* Read binary memory */
            ret = gdb_read_bin_memory(state);
            break;
          case 'X': /* Write binary memory */
            ret = gdb_write_bin_memory(state);
            break;
          case 'q': /* Query command */
            ret = gdb_query(state);
            break;
          case 'T': /* Query thread */
            ret = gdb_is_thread_active(state);
            break;
          case 'H': /* Thread related */
            ret = gdb_thread_context(state);
            break;
          case 'k': /* Kill request */
            ret = -ECONNRESET;
            break;
          case ASCII_ETX: /* Ctrl C */
            ret = gdb_send_ok_packet(state);
            break;
#ifdef CONFIG_ARCH_HAVE_DEBUG
          case 'Z': /* Insert breakpoint/watchpoint */
            ret = gdb_debugpoint(state, true);
            break;
          case 'z': /* Remove breakpoint/watchpoint */
            ret = gdb_debugpoint(state, false);
            break;
          case 's': /* Single step */
            ret = gdb_step(state);
            if (ret < 0)
              {
                break;
              }

            goto out;
          case 'c': /* Continue */
            ret = gdb_continue(state);
            if (ret < 0)
              {
                break;
              }

            goto out;
#endif
          default:
            ret = -EPROTONOSUPPORT;
      }

      if (ret == -EPROTONOSUPPORT)
        {
          state->pkt_len = 0;
          gdb_send_packet(state);
        }
      else if (ret == -ECONNRESET)
        {
          break;
        }
      else if (ret < 0)
        {
          gdb_send_error_packet(state, 0x00);
        }
    }

#ifdef CONFIG_ARCH_HAVE_DEBUG
out:
#endif
  state->last_stopreason = stopreason;
  state->last_stopaddr = stopaddr;
  return ret;
}
