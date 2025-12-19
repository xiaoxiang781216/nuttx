/****************************************************************************
 * include/nuttx/tls.h
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

#ifndef __INCLUDE_NUTTX_TLS_H
#define __INCLUDE_NUTTX_TLS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/compiler.h>
#include <nuttx/sched.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Configuration ************************************************************/

#ifdef CONFIG_TLS_ALIGNED
#  ifndef CONFIG_TLS_LOG2_MAXSTACK
#    error CONFIG_TLS_LOG2_MAXSTACK is not defined
#  endif
#endif

#ifndef CONFIG_TLS_NELEM
#  define CONFIG_TLS_NELEM 0
#endif

/* TLS Definitions **********************************************************/

#ifdef CONFIG_TLS_ALIGNED
#  define TLS_STACK_ALIGN  (1L << CONFIG_TLS_LOG2_MAXSTACK)
#  define TLS_STACK_MASK   (TLS_STACK_ALIGN - 1)
#  define TLS_MAXSTACK     (TLS_STACK_ALIGN)
#  define TLS_INFO(sp)     ((FAR struct tls_info_s *)((sp) & ~TLS_STACK_MASK))
#endif

/* Values for struct tls_info_s tl_flags bits */

#define TLS_FLAG_HEAP_CHECK (1 << 0)  /* Bit 0: Heap check */
#define TLS_FLAG_HEAP_DUMP  (1 << 1)  /* Bit 1: Heap dump */

#if (defined(CONFIG_BUILD_FLAT) && !defined(CONFIG_ARCH_ADDRENV)) || defined(__KERNEL__)
#  define TLS_FROM_TCB
#endif

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/* struct tls_cleanup_s *****************************************************/

/* This structure describes one element of the pthread cleanup stack */

typedef CODE void (*tls_cleanup_t)(FAR void *arg);

struct tls_cleanup_s
{
  tls_cleanup_t tc_cleaner; /* Cleanup callback address */
  FAR void *tc_arg;         /* Argument that accompanies the callback */
};

/* When TLS is enabled, up_createstack() will align allocated stacks to the
 * TLS_STACK_ALIGN value.  An instance of the following structure will be
 * implicitly positioned at the "lower" end of the stack.  Assuming a
 * "push down" stack, this is at the "far" end of the stack (and can be
 * clobbered if the stack overflows).
 *
 * If an MCU has a "push up" then that TLS structure will lie at the top
 * of the stack and stack allocation and initialization logic must take
 * care to preserve this structure content.
 *
 * The stack memory is fully accessible to user mode threads.  TLS is not
 * available from interrupt handlers (nor from the IDLE thread).
 *
 * The following diagram represent the typical stack layout:
 *
 *      Push Down             Push Up
 *   +-------------+      +-------------+ <- Stack memory allocation
 *   | Task Data*  |      | Task Data*  |
 *   +-------------+      +-------------+
 *   |  TLS Data   |      |  TLS Data   |
 *   +-------------+      +-------------+
 *   |  Arguments  |      |  Arguments  |
 *   +-------------+      +-------------+ |
 *   |             |      |             | v
 *   | Available   |      | Available   |
 *   |   Stack     |      |   Stack     |
 *   |             |      |             |
 *   |             |      |             |
 *   |             | ^    |             |
 *   +-------------+ |    +-------------+
 *
 *  Task data is a pointer that pointed to a user space memory region.
 */

struct tls_info_s
{
  FAR struct task_info_s     *tl_task;

#if defined(CONFIG_TLS_NELEM) && CONFIG_TLS_NELEM > 0
  uintptr_t                   tl_elem[CONFIG_TLS_NELEM]; /* TLS elements */
#endif

  /* tl_tos   - The index to the next available entry at the top of the
   *            stack.
   * tl_stack - The pre-allocated clean-up stack memory.
   */

#if CONFIG_TLS_NCLEANUP > 0
  struct tls_cleanup_s        tl_stack[CONFIG_TLS_NCLEANUP];
  uint8_t                     tl_tos;
#endif

  uint8_t                     tl_cpstate; /* Cancellation state */
  uint8_t                     tl_flags;   /* Thread local flags */

#ifdef CONFIG_CANCELLATION_POINTS
  int16_t                     tl_cpcount; /* Nested cancellation point count */
#endif

  uint16_t                    tl_size;    /* Actual size with alignments */
  int                         tl_errno;   /* Per-thread error number */
  FAR char                  **tl_argv;    /* Arguments first string */
  pid_t                       tl_tid;     /* Thread ID */

  /* Robust mutex support ***************************************************/

#if !defined(CONFIG_DISABLE_PTHREAD) && !defined(CONFIG_PTHREAD_MUTEX_UNSAFE)
  FAR struct pthread_mutex_s *tl_mhead;   /* List of mutexes held by thread  */
#endif
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: tls_get_info
 *
 * Description:
 *   Return a reference to the tls_info_s structure.  This is used as part
 *   of the internal implementation of tls_get/set_elem() and ONLY for the
 *   where CONFIG_TLS_ALIGNED is *not* defined
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   A reference to the thread-specific tls_info_s structure is return on
 *   success.  NULL would be returned in the event of any failure.
 *
 ****************************************************************************/

#ifdef TLS_FROM_TCB
#  define tls_get_info() ((FAR struct tls_info_s *)this_task()->stack_alloc_ptr)
#elif defined(CONFIG_TLS_ALIGNED)
#  define tls_get_info() TLS_INFO(up_getsp())
#elif defined(up_tls_info)
#  define tls_get_info() up_tls_info()
#else
FAR struct tls_info_s *tls_get_info(void);
#endif

/****************************************************************************
 * Name: tls_destruct
 *
 * Description:
 *   Destruct all TLS data element associated with allocated key
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

#if defined(CONFIG_TLS_NELEM) && CONFIG_TLS_NELEM > 0
void tls_destruct(void);
#endif

#if CONFIG_TLS_NCLEANUP > 0
/****************************************************************************
 * Name: tls_cleanup_push
 *
 * Description:
 *   Push a new entry onto the cleanup stack
 *
 * Input Parameters:
 *   tls     - The TLS data structure
 *   routine - The cleanup routine to be called
 *   arg     - An argument that will be passed to the cleanup routine
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void tls_cleanup_push(FAR struct tls_info_s *tls,
                      tls_cleanup_t routine, FAR void *arg);

/****************************************************************************
 * Name: tls_cleanup_pop
 *
 * Description:
 *   Pop the top entry from the cleanup stack and execute the cleanup
 *
 * Input Parameters:
 *   tls     - The TLS data structure
 *   execute - Execute the cleanup routine (true) or just remove the
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void tls_cleanup_pop(FAR struct tls_info_s *tls, int execute);

/****************************************************************************
 * Name: tls_cleanup_popall
 *
 * Description:
 *   Pop and execute all cleanup stack entries
 *
 * Input Parameters:
 *   tls     - The TLS data structure
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void tls_cleanup_popall(FAR struct tls_info_s *tls);

#else
#  define tls_cleanup_push(tls, routine, arg) ((void)(tls), (void)(routine), (void)(arg))
#  define tls_cleanup_pop(tls, execute) ((void)(tls), (void)(execute))
#  define tls_cleanup_popall(tls) ((void)(tls))
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_TLS_H */
