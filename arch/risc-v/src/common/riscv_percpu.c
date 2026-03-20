/****************************************************************************
 * arch/risc-v/src/common/riscv_percpu.c
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
#include <nuttx/irq.h>

#include <arch/barriers.h>
#include <arch/mode.h>

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "riscv_internal.h"
#include "riscv_percpu.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Per CPU save area. Access to this structure can be gained via the scratch
 * ([m/s]scratch) register. Prior to this, every CPU that
 * wishes to access this information must call riscv_percpu_add_hart() which
 * will set up [m/s]scratch to point to the CPUs own area
 */

struct riscv_percpu_s
{
  struct tcb_s       *tcb;       /* Current thread TCB */
  uintreg_t           hartid;    /* Hart ID */
  uintreg_t           irq_stack; /* Interrupt stack */
  uintreg_t           usp;       /* Area to store user sp */
  uintreg_t           ksp;       /* Area to load kernel sp */
};

typedef struct riscv_percpu_s riscv_percpu_t;

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Place g_percpu at the very beginning of the percpu section (offset 0).
 * CSR_SCRATCH stores the actual address of each CPU's g_percpu, serving
 * both as a direct riscv_percpu_t pointer (assembly) and as the percpu
 * section base for up_this_cpu_var() offset calculation (C code).
 */

#ifdef CONFIG_PERCPU_SECTION
#  define DEFINE_PER_CPU_FIRST(t, v) locate_data(".data..percpu..first") t v
#else
#  define DEFINE_PER_CPU_FIRST(t, v) t v[CONFIG_NCPUS]
#endif

static_assert(RISCV_PERCPU_TCB == offsetof(riscv_percpu_t, tcb),
              "RISCV_PERCPU_TCB offset is wrong");
static_assert(RISCV_PERCPU_HARTID == offsetof(riscv_percpu_t, hartid),
              "RISCV_PERCPU_HARTID offset is wrong");
static_assert(RISCV_PERCPU_IRQSTACK == offsetof(riscv_percpu_t, irq_stack),
              "RISCV_PERCPU_IRQSTACK offset is wrong");
static_assert(RISCV_PERCPU_USP == offsetof(riscv_percpu_t, usp),
              "RISCV_PERCPU_USP offset is wrong");
static_assert(RISCV_PERCPU_KSP == offsetof(riscv_percpu_t, ksp),
              "RISCV_PERCPU_KSP offset is wrong");

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_initialized;

/****************************************************************************
 * Public Data
 ****************************************************************************/

/* g_percpu must be at percpu section offset 0. Assembly code in
 * exception_common.S uses CSR_SCRATCH as a direct riscv_percpu_t pointer,
 * while up_this_cpu_var() uses it as the percpu section base address.
 * DEFINE_PER_CPU_FIRST guarantees this via .data..percpu..first section.
 */

DEFINE_PER_CPU_FIRST(riscv_percpu_t, g_percpu);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: riscv_percpu_init
 *
 * Description:
 *   Initialize percpu sections for all CPUs by copying .data and
 *   clearing .bss for each CPU's percpu region.
 *
 ****************************************************************************/

static void riscv_percpu_init(void)
{
  if (g_initialized)
    {
      return;
    }

  g_initialized = true;

#ifdef CONFIG_PERCPU_SECTION
  for (int cpu = 0; cpu < CONFIG_SMP_NCPUS; cpu++)
    {
      memcpy((void *)((uintptr_t)_sdata_percpu + PERCPU_OFFSET * cpu),
             (void *)_ldata_percpu,
             (uintptr_t)_edata_percpu - (uintptr_t)_sdata_percpu);
      memset((void *)((uintptr_t)_sbss_percpu + PERCPU_OFFSET * cpu),
             0, (uintptr_t)_ebss_percpu - (uintptr_t)_sbss_percpu);
    }
#endif
}

/****************************************************************************
 * Name: riscv_percpu_add_hart
 *
 * Description:
 *   Get add a hart to the per CPU area
 *
 * Input Parameters:
 *   hartid - Hart number
 *
 ****************************************************************************/

void riscv_percpu_add_hart(uintptr_t hartid)
{
  int cpu;
  riscv_percpu_t *percpu;

  riscv_percpu_init();

  cpu = riscv_hartid_to_cpuid(hartid);
  percpu = &per_cpu_var_smp(g_percpu, cpu);

  /* Assign hartid */

  percpu->hartid = hartid;

  /* Set interrupt stack (if any) */

#if CONFIG_ARCH_INTERRUPTSTACK > 15
  percpu->irq_stack = (uintptr_t)g_intstacktop - cpu * INT_STACK_SIZE;
#endif

  /* Set the scratch register to point to this CPU's percpu area.
   * Assembly code uses this as a direct pointer for fast access.
   * up_this_cpu_var() computes the percpu offset from this.
   */

  WRITE_CSR(CSR_SCRATCH, percpu);

  /* Make sure it sticks */

  UP_DSB();
}

/****************************************************************************
 * Name: riscv_percpu_get_hartid
 *
 * Description:
 *   Get harts own hartid by reading it from the per CPU area. This is safe
 *   to use from lower privilege modes (than M-mode).
 *
 * Returned Value:
 *   Hart id
 *
 ****************************************************************************/

uintptr_t riscv_percpu_get_hartid(void)
{
  uintptr_t scratch = READ_CSR(CSR_SCRATCH);
  DEBUGASSERT(scratch != 0);
  return ((riscv_percpu_t *)scratch)->hartid;
}

/****************************************************************************
 * Name: riscv_percpu_get_irqstack
 *
 * Description:
 *   Get harts own IRQ stack by reading it from the per CPU area.
 *
 * Returned Value:
 *   IRQ stack, or 0 if no IRQ stack is assigned
 *
 ****************************************************************************/

uintptr_t riscv_percpu_get_irqstack(void)
{
  uintptr_t scratch = READ_CSR(CSR_SCRATCH);
  DEBUGASSERT(scratch != 0);
  return ((riscv_percpu_t *)scratch)->irq_stack;
}

/****************************************************************************
 * Name: riscv_percpu_set_kstack
 *
 * Description:
 *   Set current kernel stack, so it can be taken quickly into use when a
 *   trap is taken. Do this whenever a new process moves to running state.
 *
 * Input Parameters:
 *   ksp - Pointer to the kernel stack
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void riscv_percpu_set_kstack(uintptr_t ksp)
{
  irqstate_t flags;
  uintptr_t scratch;

  /* This must be done with interrupts disabled */

  flags   = up_irq_save();
  scratch = READ_CSR(CSR_SCRATCH);
  DEBUGASSERT(scratch != 0);

  ((riscv_percpu_t *)scratch)->ksp = ksp;
  up_irq_restore(flags);
}

/****************************************************************************
 * Name: riscv_percpu_set_thread
 *
 * Description:
 *   Set current thread (tcb), so it can be found quickly when a trap is
 *   taken.
 *
 * Input Parameters:
 *   tcb - Pointer to the current thread's tcb
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void riscv_percpu_set_thread(struct tcb_s *tcb)
{
  irqstate_t flags;
  uintptr_t scratch;

  /* This must be done with interrupts disabled */

  flags   = up_irq_save();
  scratch = READ_CSR(CSR_SCRATCH);
  DEBUGASSERT(scratch != 0);

  ((riscv_percpu_t *)scratch)->tcb = tcb;
  up_irq_restore(flags);
}
