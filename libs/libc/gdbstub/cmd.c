/****************************************************************************
 * libs/libc/gdbstub/cmd.c
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

#include <inttypes.h>
#include <elf.h>
#include <errno.h>
#include <stdio.h>
#include <sys/param.h>
#include <unistd.h>

#include "internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PID0_REPLACE INT32_MAX

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_find_reginfo
 ****************************************************************************/

static FAR const struct reginfo_s *gdb_find_reginfo(int regnum)
{
  size_t i;
  for (i = 0; i < g_tcbinfo.regs_num; i++)
    {
      if (g_tcbinfo.u.reginfo[i].regnum == regnum  &&
          g_tcbinfo.u.reginfo[i].goffset != REGINFO_OFFSET_INVALID)
        {
          return &g_tcbinfo.u.reginfo[i];
        }
    }

  return NULL;
}

/****************************************************************************
 * Name: gdb_get_thread
 *
 * Description:
 *   Get the all thread id.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 ****************************************************************************/

static void gdb_get_thread(FAR struct tcb_s *tcb, FAR void *arg)
{
  int pid = tcb->pid == 0 ? PID0_REPLACE : tcb->pid;
  FAR struct gdb_state_s *state = arg;
  state->pkt_len += sprintf(&state->pkt_buf[state->pkt_len], "%x,", pid);
}

/****************************************************************************
 * Name: gdb_get_memory
 *
 * Description:
 *  Get data from memory.
 *
 * Input Parameters:
 *   state   - The state structure.
 *   buf     - The buffer to store the encoded data.
 *   buf_len - The length of the buffer.
 *   addr    - The address to read from.
 *   len     - The length of the data to read.
 *   format  - The formatting function to use.
 *
 * Returned Value:
 *   The number of bytes written to buf on success.
 *   Negative value on error.
 *
 ****************************************************************************/

static ssize_t gdb_get_memory(FAR struct gdb_state_s *state,
                              FAR void *buf, size_t buf_len,
                              uintptr_t addr, size_t len,
                              gdb_format_func_t format)
{
  ssize_t ret = -EINVAL;

  if (gdb_is_valid_region(state, addr, len, PF_R))
    {
      if (format)
        {
          return format(buf, buf_len, (FAR const void *)addr, len);
        }
      else
        {
          ret = MIN(len, buf_len);
          gdb_memcpy(buf, (FAR const void *)addr, ret);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: gdb_put_memory
 *
 * Description:
 *   Put data from buf into memory.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *   buf     - The buffer containing the encoded data.
 *   buf_len - The length of the buffer.
 *   addr    - The address to write to.
 *   len     - The length of the data to write.
 *   format  - The formatting function to use.
 *
 * Returned Value:
 *   The number of bytes written to addr on success.
 *   Negative value on error.
 *
 ****************************************************************************/

static ssize_t gdb_put_memory(FAR struct gdb_state_s *state,
                              FAR const void *buf, size_t buf_len,
                              uintptr_t addr, size_t len,
                              gdb_format_func_t format)
{
  ssize_t ret = -EINVAL;

  if (gdb_is_valid_region(state, addr, len, PF_W))
    {
      if (format)
        {
          return format((FAR void *)addr, len, buf, buf_len);
        }
      else
        {
          ret = MIN(len, buf_len);
          gdb_memcpy((FAR void *)addr, buf, ret);
        }
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gdb_update_regcache
 *
 * Description:
 *   Get the registers of the specified task.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 ****************************************************************************/

void gdb_update_regcache(FAR struct gdb_state_s *state)
{
  if (state->pid == gettid())
    {
      if (up_interrupt_context() && running_regs())
        {
          up_copyusercontext(state->running_regs, running_regs(),
                             XCPTCONTEXT_SIZE);
        }
      else
        {
          up_saveusercontext(state->running_regs);
        }
    }
  else
    {
      FAR struct tcb_s *tcb = nxsched_get_tcb(state->pid);
      if (tcb != NULL)
        {
          up_copyusercontext(state->running_regs, tcb->xcp.regs,
                             XCPTCONTEXT_SIZE);
          nxsched_put_tcb(tcb);
        }
      else
        {
          memset(state->running_regs, 0, XCPTCONTEXT_SIZE);
        }
    }

  state->xcpregs = state->running_regs;
}

/****************************************************************************
 * Name: gdb_read_registers
 *
 * Description:
 *   Read Registers Command Format: g.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   Zero on success.
 *   Negative value on error.
 *
 * Note: Comand Format: g.
 *       Response Format: xxxxxxxxyyyyyyyyy...
 ****************************************************************************/

int gdb_read_registers(FAR struct gdb_state_s *state)
{
  FAR const char *xcpregs = state->xcpregs;
  ssize_t offset = 0; /* Offset in g/G packet */
  size_t i;
  int ret;

  memset(state->pkt_buf, '0', state->pkt_size);

  for (i = 0; i < g_tcbinfo.regs_num; i++)
    {
      FAR const struct reginfo_s *reg = &g_tcbinfo.u.reginfo[i];
      uintptr_t value = 0;

      if (reg->goffset == REGINFO_OFFSET_INVALID)
        {
          continue;
        }
      else if (reg->goffset != REGINFO_OFFSET_AUTO)
        {
          offset = reg->goffset;
        }

      if (reg->toffset != REGINFO_OFFSET_INVALID)
        {
          memcpy(&value, xcpregs + reg->toffset, reg->size);
          ret = gdb_bin2hex(&state->pkt_buf[offset * 2],
                            state->pkt_size - offset * 2,
                            &value, reg->size);
          if (ret < 0)
            {
              return ret;
            }
        }

      offset += reg->size;
    }

  state->pkt_len = offset * 2;
  gdb_send_packet(state);
  return 0;
}

/****************************************************************************
 * Name: gdb_write_registers
 *
 * Description:
 *   Write Registers Command Format: G XX...
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   Zero on success.
 *   Negative value on error.
 *
 *   Note: This function is not really change the register values.
 *         Comand Format: Gxxxxxxxxyyyyyyyyy
 *         Response Format: OK
 ****************************************************************************/

int gdb_write_registers(FAR struct gdb_state_s *state)
{
  FAR char *xcpregs = state->xcpregs;
  ssize_t offset = 0; /* Offset in g/G packet */
  size_t i;
  int ret;

  for (i = 0; i < g_tcbinfo.regs_num; i++)
    {
      FAR const struct reginfo_s *reg = &g_tcbinfo.u.reginfo[i];
      uintptr_t value = 0;

      offset = reg->goffset > 0 ? reg->goffset : offset;
      ret = gdb_hex2bin(&value, sizeof(value),
                        state->pkt_buf + offset, state->pkt_len - offset);
      if (ret < 0)
        {
          /* xxxx means to register value not available. */

          continue;
        }

      memcpy(xcpregs + reg->toffset, &value, reg->size);
      offset += reg->size;
    }

  gdb_send_ok_packet(state);
  return 0;
}

/****************************************************************************
 * Name: gdb_read_register
 *
 * Description:
 *   Read a Register Command Format: p n.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   Zero on success.
 *   Negative value on error.
 *
 * Note: Comand Format: Pn.
 *       Response Format: OK
 ****************************************************************************/

int gdb_read_register(FAR struct gdb_state_s *state)
{
  FAR const char *xcpregs = state->xcpregs;
  FAR const struct reginfo_s *reg;
  uintptr_t value = 0;
  uintptr_t regnum;
  int ret;

  state->pkt_next++;
  ret = gdb_expect_integer(state, &regnum);
  if (ret < 0)
    {
      return ret;
    }

  reg = gdb_find_reginfo(regnum);
  if (reg == NULL)
    {
      return -EINVAL;
    }

  /* If toffset equal to REGINFO_OFFSET_INVALID, return register value zero.
   * See g_reginfo[] of xtensa arch for an example of this situation.
   */

  if (reg->toffset != REGINFO_OFFSET_INVALID)
    {
      memcpy(&value, xcpregs + reg->toffset, reg->size);
    }

  ret = gdb_bin2hex(state->pkt_buf, state->pkt_size,
                    &value, reg->size);
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = ret;
  gdb_send_packet(state);
  return 0;
}

/****************************************************************************
 * Name: gdb_write_register
 *
 * Description:
 *   Write a Register Command Format: P n.
 *   Note that this won't work for all architectures because the GDB remote
 *   register number is not always the same as the NuttX register number.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   Zero on success.
 *   Negative value on error.
 *
 * Note: Comand Format: Pn.
 *       Response Format: OK or negative value for error number.
 ****************************************************************************/

int gdb_write_register(FAR struct gdb_state_s *state)
{
  FAR char *xcpregs = state->xcpregs;
  FAR const struct reginfo_s *reg;
  uintptr_t value = 0;
  uintptr_t regnum;
  int ret;

  state->pkt_next++;

  ret = gdb_expect_integer(state, &regnum);
  if (ret < 0)
    {
      return ret;
    }

  reg = gdb_find_reginfo(regnum);
  if (reg == NULL)
    {
      return -EINVAL;
    }

  ret = gdb_expect_separator(state, '=');
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_hex2bin(&value, sizeof(uintptr_t),
                    state->pkt_next, gdb_remaining_len(state));
  if (ret < 0)
    {
      return ret;
    }

  memcpy(xcpregs + reg->toffset, &value, reg->size);

  gdb_send_ok_packet(state);
  return 0;
}

/****************************************************************************
 * Name: gdb_read_memory
 *
 * Description:
 *   Read Memory Command Format: m addr,length
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   The number of bytes read if successful.
 *   Negative value on error.
 *
 * Note: Command Format: mAAAAAAAAA,LLLLLLLL
 *       Response Format: XXXXXXXXYYYYYYYYY...
 ****************************************************************************/

int gdb_read_memory(FAR struct gdb_state_s *state)
{
  uintptr_t addr;
  size_t length;
  int ret;

  state->pkt_next++;
  ret = gdb_expect_addr_length(state, &addr, &length);
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_get_memory(state, state->pkt_buf, state->pkt_size,
                       addr, length, gdb_bin2hex);
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = ret;
  gdb_send_packet(state);

  return ret;
}

/****************************************************************************
 * Name: gdb_read_bin_memory
 *
 * Description:
 *   Read Memory (Binary) Command Format: x addr,length
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   The number of bytes read if successful.
 *   Negative value on error.
 *
 * Note : Command Format: xAAAAAAAAA,LLLLLLLL
 *        Response Format: bXXXXXXXXYYYYYYYYY...
 ****************************************************************************/

int gdb_read_bin_memory(FAR struct gdb_state_s *state)
{
  uintptr_t addr;
  size_t length;
  int ret;

  state->pkt_next++;
  ret = gdb_expect_addr_length(state, &addr, &length);
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_buf[0] = 'b';
  ret = gdb_get_memory(state, state->pkt_buf + 1, state->pkt_size - 1,
                       addr, length, NULL);
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = ret + 1;
  gdb_send_packet(state);
  return ret + 1;
}

/****************************************************************************
 * Name: gdb_write_memory
 *
 * Description:
 *   Write Memory Command Format: M addr,length:XX..
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   The number of bytes written if successful.
 *   Negative value on error.
 *
 * Note : Command Format: MAAAAAAAAA,LLLLLLLL:XXXXXXXXX...
 *        Response Format: OK
 ****************************************************************************/

int gdb_write_memory(FAR struct gdb_state_s *state)
{
  uintptr_t addr;
  size_t length;
  int ret;

  state->pkt_next++;
  ret = gdb_expect_addr_length(state, &addr, &length);
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_put_memory(state, state->pkt_next, gdb_remaining_len(state),
                       addr, length, gdb_hex2bin);
  if (ret < 0)
    {
      return ret;
    }

  gdb_send_ok_packet(state);
  return ret;
}

/****************************************************************************
 * Name: gdb_write_bin_memory
 *
 * Description:
 *   Write Memory (Binary) Command Format: X addr,length:XX..
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   The number of bytes written if successful.
 *   Negative value on error.
 *
 * Note : Command Format: XAAAAAAAAA,LLLLLLLL:XXXXXXXXX...
 *        Response Format: OK
 ****************************************************************************/

int gdb_write_bin_memory(FAR struct gdb_state_s *state)
{
  uintptr_t addr;
  size_t length;
  int ret;

  state->pkt_next++;
  ret = gdb_expect_addr_length(state, &addr, &length);
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_expect_separator(state, ':');
  if (ret < 0)
    {
      return ret;
    }

  ret = gdb_put_memory(state, state->pkt_next, gdb_remaining_len(state),
                       addr, length, NULL);
  if (ret < 0)
    {
      return ret;
    }

  gdb_send_ok_packet(state);
  return ret;
}

/****************************************************************************
 * Name: gdb_query
 *
 * Description:
 *  The query packet is used by GDB to request information from the stub.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Comand Format: qSTRING
 *        STRING:is the query string.
 ****************************************************************************/

int gdb_query(FAR struct gdb_state_s *state)
{
  static const char thread_extra_info[] = "ThreadExtraInfo";
  static const char fthread_info[] = "fThreadInfo";
  static const char sthread_info[] = "sThreadInfo";
  static const char supported_info[] = "Supported";
  static const char r_cmd[] = "Rcmd";

  if (memcmp(&state->pkt_buf[1], fthread_info,
              sizeof(fthread_info) - 1) == 0)
    {
      state->pkt_buf[0] = 'm';
      state->pkt_len = 1;
      nxsched_foreach(gdb_get_thread, state);
      state->pkt_len--;
      state->pkt_buf[state->pkt_len] = 0;
      gdb_send_packet(state);
    }
  else if (memcmp(&state->pkt_buf[1], sthread_info,
                   sizeof(sthread_info) - 1) == 0)
    {
      state->pkt_buf[0] = 'l';
      state->pkt_len = 1;
      gdb_send_packet(state);
    }
  else if (memcmp(&state->pkt_buf[1], supported_info,
                  sizeof(supported_info) - 1) == 0)
    {
#ifdef CONFIG_ARCH_HAVE_DEBUG
      state->pkt_len = sprintf(state->pkt_buf,
                               "binary-upload+;hwbreak+;PacketSize=%zx",
                               state->pkt_size);
#else
      state->pkt_len = sprintf(state->pkt_buf,
                               "binary-upload+;PacketSize=%zx",
                               state->pkt_size);
#endif
      gdb_send_packet(state);
    }
  else if (memcmp(&state->pkt_buf[1], thread_extra_info,
                   sizeof(thread_extra_info) - 1) == 0)
    {
      FAR struct tcb_s *tcb;
      char thread_state[32];
      char thread_info[128];
      uintptr_t pid;
      int ret;

      state->pkt_next += sizeof(thread_extra_info) + 1;
      ret = gdb_expect_integer(state, &pid);
      if (ret < 0)
        {
          return ret;
        }

      if (pid == PID0_REPLACE)
        {
          pid = 0;
        }

      tcb = nxsched_get_tcb(pid);
      if (tcb == NULL)
        {
          return -EINVAL;
        }

      nxsched_get_stateinfo(tcb, thread_state, sizeof(thread_state));
      snprintf(thread_info, sizeof(thread_info),
               "Name: %s, State: %s, Priority: %d, Stack: %zu",
                get_task_name(tcb), thread_state, tcb->sched_priority,
                tcb->adj_stack_size);

      nxsched_put_tcb(tcb);
      ret = gdb_bin2hex(state->pkt_buf, state->pkt_size,
                        thread_info, strlen(thread_info));

      state->pkt_len = ret;
      gdb_send_packet(state);
    }
  else if (state->monitor != NULL &&
           memcmp(&state->pkt_buf[1], r_cmd, sizeof(r_cmd) - 1) == 0)
    {
      ssize_t len = state->pkt_len - sizeof(r_cmd) - 1; /* skip the 'Rcmd,' */
      char cmd[128];
      int ret;

      len = gdb_hex2bin(cmd, len / 2,
                        &state->pkt_buf[sizeof(r_cmd) + 1], len);
      if (len < 0)
        {
          return len;
        }

      cmd[len] = '\0';
      ret = state->monitor(state, cmd);
      if (ret < 0)
        {
          return ret;
        }
      else
        {
          gdb_send_ok_packet(state);
        }
    }
  else
    {
      return -EPROTONOSUPPORT;
    }

  return 0;
}

/****************************************************************************
 * Name: gdb_is_thread_active
 *
 * Description:
 *  The is thread active packet is used by GDB to request information from
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Comand Format: T<id>
 *        id:is the thread id.
 *        Response Format: OK
 ****************************************************************************/

int gdb_is_thread_active(FAR struct gdb_state_s *state)
{
  FAR struct tcb_s *tcb;
  uintptr_t pid;
  int ret;

  state->pkt_next++;
  ret = gdb_expect_integer(state, &pid);
  if (ret < 0)
    {
      return ret;
    }

  if (pid == PID0_REPLACE)
    {
      pid = 0;
    }

  tcb = nxsched_get_tcb(pid);
  if (tcb == NULL)
    {
      return -EINVAL;
    }

  nxsched_put_tcb(tcb);
  state->pid = pid;
  gdb_send_ok_packet(state);
  return ret;
}

/****************************************************************************
 * Name: gdb_thread_context
 *
 * Description:
 *  The thread context packet is used by GDB to request information from
 *  the stub.
 *  Set thread for subsequent operations (m, M, g, G, etc)
 *  Thread ID of -1 means all all thread, 0 means any thread.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Comand Format: Hg<id>
 *                       Hc<id>
 *        Response Format: OK
 ****************************************************************************/

int gdb_thread_context(FAR struct gdb_state_s *state)
{
  FAR struct tcb_s *tcb;
  uintptr_t pid;
  int ret;
  char c = state->pkt_buf[1];

  if (c != 'g' && c != 'c')
    {
      return -EINVAL;
    }

  state->pkt_next += 2;   /* Skip Hg/Hc */

  ret = gdb_expect_integer(state, &pid);
  if (ret < 0)
    {
      return ret;
    }

  if (pid == 0 || pid == -1)
    {
      /* '-1' to indicate all threads, or '0' to pick any thread. */

      pid = state->pid;   /* Keep unchanged */
    }
  else if (pid == PID0_REPLACE)
    {
      pid = 0;
    }

  tcb = nxsched_get_tcb(pid);
  if (tcb == NULL)
    {
      return -EINVAL;
    }

  nxsched_put_tcb(tcb);
  state->pid = pid;

  gdb_update_regcache(state);

  gdb_send_ok_packet(state);
  return 0;
}

/****************************************************************************
 * Name: gdb_send_signal_packet
 *
 * Description:
 *   Send a signal packet (S AA).
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *   signal  - The signal to send.
 *
 * Returned Value:
 *   Zero on success.
 *   Negative value on error.
 *
 ****************************************************************************/

int gdb_send_signal_packet(FAR struct gdb_state_s *state,
                           unsigned char signal)
{
  int ret;

  state->pkt_buf[0] = 'S';
  ret = gdb_bin2hex(&state->pkt_buf[1], state->pkt_size - 1,
                    &signal, 1);
  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = 1 + ret;
  return gdb_send_packet(state);
}

/****************************************************************************
 * Name: gdb_send_stop
 *
 * Description:
 *  The stop packet is used by GDB to request information from the stub.
 *
 * Input Parameters:
 *   state   - The pointer to the GDB state structure.
 *
 * Returned Value:
 *   0  if successful.
 *   Negative value on error.
 *
 * Note : Response Format: T AA n1:r1;n2:r2;...
 *        The program received signal number AA
 *        n is thread id in current.
 *        r is stop reason.
 *
 ****************************************************************************/

int gdb_send_stop(FAR struct gdb_state_s *state, int stopreason,
                  FAR void *stopaddr)
{
  int ret;
  int pid;

  state->pid = pid = gettid();
  if (pid == 0)
    {
      pid = PID0_REPLACE;
    }

retry:
  switch (stopreason)
    {
      case GDB_STOPREASON_WATCHPOINT_RO:
        ret = sprintf(state->pkt_buf, "T05thread:%x;rwatch:%" PRIxPTR ";",
                      pid, (uintptr_t)stopaddr);
        break;
      case GDB_STOPREASON_WATCHPOINT_WO:
        ret = sprintf(state->pkt_buf, "T05thread:%x;awatch:%" PRIxPTR ";",
                      pid, (uintptr_t)stopaddr);
        break;
      case GDB_STOPREASON_WATCHPOINT_RW:
        ret = sprintf(state->pkt_buf, "T05thread:%x;watch:%" PRIxPTR ";",
                      pid, (uintptr_t)stopaddr);
        break;
      case GDB_STOPREASON_BREAKPOINT:
        ret = sprintf(state->pkt_buf, "T05thread:%x;hwbreak:;",
                      pid);
        break;
      case GDB_STOPREASON_STEPPOINT:
        if (state->last_stopreason == GDB_STOPREASON_WATCHPOINT_RW ||
            state->last_stopreason == GDB_STOPREASON_WATCHPOINT_WO)
          {
            stopreason = state->last_stopreason;
            stopaddr = state->last_stopaddr;
            goto retry;
          }

      case GDB_STOPREASON_CTRLC:
      default:
        ret = sprintf(state->pkt_buf, "T05thread:%x;", pid);
    }

  if (ret < 0)
    {
      return ret;
    }

  state->pkt_len = ret;
  return gdb_send_packet(state);
}
