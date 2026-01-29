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

#include <inttypes.h>

#include <nuttx/lib/lib.h>
#include <nuttx/ascii.h>
#include <nuttx/streams.h>
#include <nuttx/clock.h>
#include <nuttx/panic_notifier.h>
#include <nuttx/nuttx.h>
#include <nuttx/syslog/syslog.h>
#include <debug.h>

#include "internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Architecture can overwrite the default XCPTCONTEXT alignment */

#ifndef XCPTCONTEXT_ALIGN
#  define XCPTCONTEXT_ALIGN 16
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int gdb_panic_callback(FAR struct notifier_block *nb,
                              unsigned long action, FAR void *data)
{
  FAR struct gdb_state_s *state =
    container_of(nb, struct gdb_state_s, nb);
  clock_t base;
  char c;
  ssize_t ret;

  UNUSED(data);

  if (action != PANIC_KERNEL_FINAL)
    {
      return 0;
    }

  _alert("Entering panic gdbstub mode, waiting for GDB connection...\n");
  _alert("Use: gdb-multiarch nuttx -ex \"target remote <device>\"\n");
  syslog_flush();

  if (state->panic_timeout > 0)
    {
      base = clock_systime_ticks();
      while (clock_systime_ticks() - base < SEC2TICK(state->panic_timeout))
        {
          ret = state->recv(state->priv, &c, 1);
          if (ret > 0)
            {
              break;
            }
        }

      if (ret <= 0)
        {
          _alert("Timeout after %d seconds, continuing panic flow\n",
                 state->panic_timeout);
          return 0;
        }
    }
  else
    {
      for (; ; )
        {
          ret = state->recv(state->priv, &c, 1);
          if (ret > 0)
            {
              break;
            }
        }
    }

  gdb_process(state, GDB_STOPREASON_NONE, NULL);
  return 0;
}

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
 *   init - The pointer to the initialization configuration structure.
 *
 * Returned Value:
 *   The pointer to the GDB state structure on success.
 *   NULL on failure.
 *
 ****************************************************************************/

FAR struct gdb_state_s *gdb_state_init(
    FAR const struct gdb_state_init_s *init)
{
  size_t pkt_size = init->pkt_size ? init->pkt_size :
                                      CONFIG_LIB_GDBSTUB_PKTSIZE;
  size_t alloc_size = sizeof(struct gdb_state_s) + pkt_size;
  FAR struct gdb_state_s *state;

  if (init->packet)
    {
      alloc_size += 2 * pkt_size + 4 + pkt_size;
    }

  state = lib_memalign(XCPTCONTEXT_ALIGN, alloc_size);
  if (state == NULL)
    {
      return NULL;
    }

  memset(state, 0, sizeof(struct gdb_state_s));
  state->send = init->send;
  state->recv = init->recv;
  state->priv = init->priv;
  state->monitor = init->monitor;
  state->pkt_size = pkt_size;

  if (init->packet)
    {
      state->tx_buf = state->pkt_buf + pkt_size;
      state->tx_size = 2 * pkt_size + 4;
      state->rx_buf = state->tx_buf + state->tx_size;
      state->rx_size = pkt_size;
    }

  if (init->panic_timeout != 0)
    {
      state->panic_timeout = init->panic_timeout;
      state->nb.notifier_call = gdb_panic_callback;
      panic_notifier_chain_register(&state->nb);
    }

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
      if (state->panic_timeout != 0)
        {
          panic_notifier_chain_unregister(&state->nb);
        }

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
  ret = gdb_bin2hex(&state->pkt_buf[1], state->pkt_size - 1,
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
