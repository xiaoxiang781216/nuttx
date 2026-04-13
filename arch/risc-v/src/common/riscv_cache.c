/****************************************************************************
 * arch/risc-v/src/common/riscv_cache.c
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
#include <nuttx/cache.h>

#include <stdint.h>
#include <sys/types.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CACHE_LINE_SIZE     CONFIG_RISCV_CACHE_LINE_SIZE
#define CACHE_LINE_MASK     (CACHE_LINE_SIZE - 1)
#define CACHE_LINE_ALIGN(a) ((a) & ~(uintptr_t)CACHE_LINE_MASK)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * D-Cache Operations
 ****************************************************************************/

#ifdef CONFIG_ARCH_DCACHE

/****************************************************************************
 * Name: DCACHE_OP_RANGE
 *
 * Description:
 *   Apply a CBO operation to every cache line in [start, end).
 *   start is aligned down to cache line boundary before iteration.
 *   A fence rw,rw is issued after all operations to ensure completion.
 *   Implemented as a macro to avoid function pointer overhead.
 *
 ****************************************************************************/

#  define DCACHE_OP_RANGE(start, end, op)                            \
     do                                                              \
       {                                                             \
         for (start = CACHE_LINE_ALIGN(start); start < (end);        \
              start += CACHE_LINE_SIZE)                              \
           {                                                         \
             op(start);                                              \
           }                                                         \
         __asm__ volatile ("fence rw, rw" ::: "memory");             \
       }                                                             \
     while (0)

/****************************************************************************
 * Name: cbo_clean
 *
 * Description:
 *   Write-back the cache line containing addr to memory.
 *   The cache line remains valid (not invalidated).
 *   Zicbom extension instruction.
 *
 ****************************************************************************/

static inline void cbo_clean(uintptr_t addr)
{
  __asm__ volatile ("cbo.clean 0(%0)" :: "r"(addr) : "memory");
}

/****************************************************************************
 * Name: cbo_inval
 *
 * Description:
 *   Invalidate the cache line containing addr.
 *   WARNING: Any dirty (unflushed) data in this cache line will be lost.
 *   Zicbom extension instruction.
 *
 ****************************************************************************/

static inline void cbo_inval(uintptr_t addr)
{
  __asm__ volatile ("cbo.inval 0(%0)" :: "r"(addr) : "memory");
}

/****************************************************************************
 * Name: cbo_flush
 *
 * Description:
 *   Write-back and then invalidate the cache line containing addr.
 *   Equivalent to cbo_clean + cbo_inval in one instruction.
 *   Zicbom extension instruction.
 *
 ****************************************************************************/

static inline void cbo_flush(uintptr_t addr)
{
  __asm__ volatile ("cbo.flush 0(%0)" :: "r"(addr) : "memory");
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: up_get_dcache_linesize
 *
 * Description:
 *   Return the D-Cache line size, configured via RISCV_CACHE_LINE_SIZE.
 *
 ****************************************************************************/

size_t up_get_dcache_linesize(void)
{
  return CACHE_LINE_SIZE;
}

/****************************************************************************
 * Name: up_invalidate_dcache
 *
 * Description:
 *   Invalidate D-Cache lines in [start, end) using cbo.inval.
 *   Typical use: after DMA receive, discard stale cached data so
 *   CPU reads fresh data from memory written by the DMA device.
 *   WARNING: Dirty data in the range will be lost.
 *
 ****************************************************************************/

void up_invalidate_dcache(uintptr_t start, uintptr_t end)
{
  DCACHE_OP_RANGE(start, end, cbo_inval);
}

/****************************************************************************
 * Name: up_clean_dcache
 *
 * Description:
 *   Write-back D-Cache lines in [start, end) using cbo.clean.
 *   Dirty data is written to memory, cache lines remain valid.
 *   Typical use: before DMA transmit, ensure device can read
 *   the latest data from memory.
 *
 ****************************************************************************/

void up_clean_dcache(uintptr_t start, uintptr_t end)
{
  DCACHE_OP_RANGE(start, end, cbo_clean);
}

/****************************************************************************
 * Name: up_flush_dcache
 *
 * Description:
 *   Write-back and invalidate D-Cache lines in [start, end)
 *   using cbo.flush. Combines clean + invalidate in one step.
 *   Typical use: bidirectional DMA buffers where CPU and device
 *   both read and write the same memory region.
 *
 ****************************************************************************/

void up_flush_dcache(uintptr_t start, uintptr_t end)
{
  DCACHE_OP_RANGE(start, end, cbo_flush);
}

/****************************************************************************
 * Name: up_flush_dcache_all
 *
 * Description:
 *   No-op. Zicbom has no set/way operations, so full cache flush is not
 *   possible. Kept because sched/assert.c unconditionally calls this
 *   function when CONFIG_ARCH_DCACHE is enabled.
 *
 ****************************************************************************/

void up_flush_dcache_all(void)
{
}

#endif /* CONFIG_ARCH_DCACHE */

/****************************************************************************
 * I-Cache Operations
 ****************************************************************************/

#ifdef CONFIG_ARCH_ICACHE

/****************************************************************************
 * Name: up_get_icache_linesize
 *
 * Description:
 *   Return the I-Cache line size, configured via RISCV_CACHE_LINE_SIZE.
 *
 ****************************************************************************/

size_t up_get_icache_linesize(void)
{
  return CACHE_LINE_SIZE;
}

/****************************************************************************
 * Name: up_invalidate_icache
 *
 * Description:
 *   Invalidate I-Cache for the address range [start, end).
 *   RISC-V has no per-address I-Cache invalidate instruction.
 *   fence.i is the only standard mechanism and it acts globally
 *   on the entire I-Cache pipeline.
 *
 ****************************************************************************/

void up_invalidate_icache(uintptr_t start, uintptr_t end)
{
  __asm__ volatile ("fence rw, rw" ::: "memory");
  __asm__ volatile ("fence.i" ::: "memory");
}

/****************************************************************************
 * Name: up_invalidate_icache_all
 *
 * Description:
 *   Invalidate the entire I-Cache using fence.i.
 *
 ****************************************************************************/

void up_invalidate_icache_all(void)
{
  __asm__ volatile ("fence rw, rw" ::: "memory");
  __asm__ volatile ("fence.i" ::: "memory");
}

/****************************************************************************
 * Name: up_coherent_dcache
 *
 * Description:
 *   Ensure I/D cache coherence for [addr, addr+len).
 *   First clean D-Cache (write modified code to memory), then
 *   fence.i to make I-Cache re-fetch from memory.
 *   Typical use: after writing new code to memory (e.g. dynamic
 *   loading), ensure subsequent instruction fetches see the new code.
 *
 ****************************************************************************/

void up_coherent_dcache(uintptr_t addr, size_t len)
{
  up_clean_dcache(addr, addr + len);
  __asm__ volatile ("fence rw, rw" ::: "memory");
  __asm__ volatile ("fence.i" ::: "memory");
}

#endif /* CONFIG_ARCH_ICACHE */
