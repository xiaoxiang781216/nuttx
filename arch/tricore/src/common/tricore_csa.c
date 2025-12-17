/****************************************************************************
 * arch/tricore/src/common/tricore_csa.c
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

#include <sys/types.h>
#include <stdint.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/tls.h>
#include <nuttx/spinlock.h>
#include <arch/barriers.h>
#include <arch/irq.h>
#include <sched/sched.h>

#include "tricore_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tricore_alloc_csa
 ****************************************************************************/

uintptr_t *tricore_alloc_csa(struct tcb_s *tcb, uintptr_t pc,
                             uintptr_t sp, uintptr_t psw,
                             bool irqsave)
{
  uintptr_t *plcsa;
  uintptr_t *pucsa;

  /* If the task corresponding to tcb is in running state, it must be in
   * interrupt context to ensure the correct management of csa.
   */

  DEBUGASSERT(tcb != nxsched_self() || up_interrupt_context());

  /* Init new uppercsa and lowcsa */

  if (tcb->xcp.regs == NULL)
    {
      pucsa = tcb->stack_base_ptr;
    }
  else
    {
      pucsa = tcb->xcp.regs +  XCPTCONTEXT_REGS;
    }

  plcsa = pucsa + TC_CONTEXT_REGS;

  memset(pucsa, 0, TC_CONTEXT_SIZE);
  memset(plcsa, 0, TC_CONTEXT_SIZE);

  /* Save the function entry point */

  plcsa[REG_LPC] = pc;
  pucsa[REG_SP]  = sp;
  pucsa[REG_PSW] = psw;

  plcsa[REG_LPCXI] = (PCXI_UL | tricore_addr2csa(pucsa));

  /* Determine whether to enable interrupt */

  if (!irqsave)
    {
      plcsa[REG_LPCXI] |= PCXI_PIE;
    }

  return pucsa;
}

/****************************************************************************
 * Name: tricore_reclaim_csa
 ****************************************************************************/

void tricore_reclaim_csa(uintptr_t pcxi)
{
  uintptr_t *tail;

  while ((pcxi & FCX_FREE) != 0)
    {
      tail = tricore_csa2addr(pcxi);
      pcxi = tail[0];
      tail[0] = tricore_addr2csa(tail + TC_CONTEXT_REGS);
    }
}

/****************************************************************************
 * Name: tricore_get_csainfo
 ****************************************************************************/

void tricore_get_csainfo(csa_info_t *info)
{
  FAR struct tcb_s *rtcb = nxsched_self();
  uintptr_t fcx, ucx;
  irqstate_t flags;
  int hash_ndx;
  int ndx;

  info->used = 0;
  hash_ndx = PIDHASH(rtcb->pid);

  flags = spin_lock_irqsave(&g_pidhashlock);

  for (ndx = 0; ndx < g_npidhash; ndx++)
    {
      if (g_pidhash[ndx])
        {
          if (ndx != hash_ndx)
            {
              ucx = tricore_addr2csa(g_pidhash[ndx]->xcp.regs) & FCX_FREE;
            }
          else
            {
              ucx = __mfcr(CPU_PCXI) & FCX_FREE;
              UP_DSB();
            }

          while (ucx != 0)
            {
              info->used++;
              ucx = tricore_csa2addr(ucx)[REG_UPCXI] & FCX_FREE;
            }

#ifndef CONFIG_DISABLE_SIGNALS
          ucx = tricore_addr2csa(g_pidhash[ndx]->xcp.saved_regs) & FCX_FREE;

          while (ucx != 0)
            {
              info->used++;
              ucx = tricore_csa2addr(ucx)[REG_UPCXI] & FCX_FREE;
            }
#endif
        }
    }

  spin_unlock_irqrestore(&g_pidhashlock, flags);

  fcx = __mfcr(CPU_FCX) & FCX_FREE;
  UP_DSB();

  info->free = 0;
  while (fcx != 0)
    {
      info->free++;
      fcx = tricore_csa2addr(fcx)[REG_UPCXI] & FCX_FREE;
    }
}

/****************************************************************************
 * Name: tricore_region_csainit
 ****************************************************************************/

void tricore_region_csainit(void *regionbase, ssize_t nbytes)
{
  uintptr_t *curcsa_ptr;
  uintptr_t *nextcsa_ptr;
  uintptr_t  regionend;
  size_t     nwords;

  if (nbytes > 0)
    {
      regionend = (uintptr_t)regionbase + nbytes;
      curcsa_ptr = (uintptr_t *)STACK_ALIGN_UP((uintptr_t)regionbase);
      regionend = STACK_ALIGN_DOWN(regionend);
      nwords = (regionend - (uintptr_t)curcsa_ptr) >> 2;

      nextcsa_ptr = curcsa_ptr;
      while (nwords-- > 0)
        {
          if (TC_CONTEXT_ALIGNED(++nextcsa_ptr))
            {
              curcsa_ptr[0] = tricore_addr2csa(nextcsa_ptr);
              curcsa_ptr = nextcsa_ptr;
            }
        }
    }
}
