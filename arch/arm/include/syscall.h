/****************************************************************************
 * arch/arm/include/syscall.h
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

/* This file should never be included directly but, rather, only indirectly
 * through include/syscall.h or include/sys/sycall.h
 */

#ifndef __ARCH_ARM_INCLUDE_SYSCALL_H
#define __ARCH_ARM_INCLUDE_SYSCALL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#ifndef __ASSEMBLY__
#  include <stdint.h>
#endif

/****************************************************************************
 * Pre-processor Prototypes
 ****************************************************************************/

#define SYS_syscall 0x00

#if defined(__thumb__) || defined(__thumb2__) || \
    defined(__THUMB_AWARE__) || defined(__THUMB2_AWARE__)
#  define SYS_smhcall 0xab
#else
#  define SYS_smhcall 0x123456
#endif

/* Configuration ************************************************************/

/* Reserved syscall for special os feature */

#define CONFIG_SYS_RESERVED 11

/* Cortex-M system calls ****************************************************/

/* SYS call 1:
 *
 * void arm_fullcontextrestore(uint32_t *restoreregs) noreturn_function;
 */

#define SYS_restore_context       (1)

/* SYS call 2:
 *
 * void arm_switchcontext(uint32_t **saveregs, uint32_t *restoreregs);
 */

#define SYS_switch_context        (2)

/* SYS call 3: only valid when enable CONFIG_LIB_SYSCALL
 *
 * void arm_syscall_return(void);
 */

#define SYS_syscall_return        (3)

/* SYS call 4:
 *
 * void _assert(const char *filename, int linenum, const char *msg);
 */

#define SYS_assert_handler        (4)

/* SYS call 5: only valid when not in FLAT mode
 *
 * void up_task_start(main_t taskentry, int argc, char *argv[])
 *        noreturn_function;
 */

#define SYS_task_start            (5)

/* SYS call 6: only valid when not in FLAT mode
 *
 * void up_pthread_start((pthread_startroutine_t startup,
 *                        pthread_startroutine_t entrypt, pthread_addr_t arg)
 *        noreturn_function
 */

#define SYS_pthread_start         (6)

/* SYS call 7: only valid when not in FLAT mode
 *
 * void signal_handler(_sa_sigaction_t sighand,
 *                     int signo, siginfo_t *info,
 *                     void *ucontext);
 */

#define SYS_signal_handler        (7)

/* SYS call 8: only valid when not in FLAT mode
 *
 * void signal_handler_return(void);
 */

#define SYS_signal_handler_return (8)

/* SYS call 9:
 *
 * void ppccall(struct ppccall_param_s *param);
 */

#define SYS_ppccall               (9)

/* SYS call 10:
 *
 * void ppccall_handler_return(void);
 */
#define SYS_ppccall_return        (10)

#define ARM_SMCC_RES_A0           (0)
#define ARM_SMCC_RES_A1           (1)
#define ARM_SMCC_RES_A2           (2)
#define ARM_SMCC_RES_A3           (3)
#define ARM_SMCC_RES_A4           (4)
#define ARM_SMCC_RES_A5           (5)
#define ARM_SMCC_RES_A6           (6)
#define ARM_SMCC_RES_A7           (7)

#define ARM_SMCCC_STD_CALL        0UL
#define ARM_SMCCC_FAST_CALL       1UL
#define ARM_SMCCC_TYPE_SHIFT      31

#define ARM_SMCCC_SMC_32          0
#define ARM_SMCCC_SMC_64          1
#define ARM_SMCCC_CALL_CONV_SHIFT 30

#define ARM_SMCCC_OWNER_MASK      0x3F
#define ARM_SMCCC_OWNER_SHIFT     24

#define ARM_SMCCC_FUNC_MASK       0xFFFF

#define ARM_SMCCC_IS_FAST_CALL(smc_val) \
  ((smc_val) & (ARM_SMCCC_FAST_CALL << ARM_SMCCC_TYPE_SHIFT))
#define ARM_SMCCC_IS_64(smc_val) \
  ((smc_val) & (ARM_SMCCC_SMC_64 << ARM_SMCCC_CALL_CONV_SHIFT))
#define ARM_SMCCC_FUNC_NUM(smc_val)  ((smc_val) & ARM_SMCCC_FUNC_MASK)
#define ARM_SMCCC_OWNER_NUM(smc_val) \
  (((smc_val) >> ARM_SMCCC_OWNER_SHIFT) & ARM_SMCCC_OWNER_MASK)

#define ARM_SMCCC_CALL_VAL(type, calling_convention, owner, func_num) \
  (((type) << ARM_SMCCC_TYPE_SHIFT) | \
  ((calling_convention) << ARM_SMCCC_CALL_CONV_SHIFT) | \
  (((owner) & ARM_SMCCC_OWNER_MASK) << ARM_SMCCC_OWNER_SHIFT) | \
  ((func_num) & ARM_SMCCC_FUNC_MASK))

#define ARM_SMCCC_OWNER_ARCH            0
#define ARM_SMCCC_OWNER_CPU             1
#define ARM_SMCCC_OWNER_SIP             2
#define ARM_SMCCC_OWNER_OEM             3
#define ARM_SMCCC_OWNER_STANDARD        4
#define ARM_SMCCC_OWNER_TRUSTED_APP     48
#define ARM_SMCCC_OWNER_TRUSTED_APP_END 49
#define ARM_SMCCC_OWNER_TRUSTED_OS      50
#define ARM_SMCCC_OWNER_TRUSTED_OS_END  63

#define ARM_SMCCC_QUIRK_NONE            0
#define ARM_SMCCC_QUIRK_QCOM_A6         1 /* Save/restore register a6 */

#define ARM_SMCCC_ARCH_FEATURES         0x80000001

#define ARM_SMCCC_RET_NOT_SUPPORTED     ((unsigned long)-1)

/* NuttX SPD (Secure Payload Dispatcher) SMC function IDs.
 * Used for BL32 <-> BL31 communication in TrustZone configurations.
 *
 * Yielding SMCs: world-switch / long-running ops, may be preempted.
 * Fast SMCs: short atomic ops, non-preemptible.
 */

#define ARM_SMCCC_NUTTX_SEND_SECURE_SGI \
  ARM_SMCCC_CALL_VAL(ARM_SMCCC_FAST_CALL, ARM_SMCCC_SMC_32, \
                     ARM_SMCCC_OWNER_TRUSTED_OS_END, 0x0000)

#define ARM_SMCCC_NUTTX_TEE_IDLE \
  ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_32, \
                     ARM_SMCCC_OWNER_TRUSTED_OS_END, 0x0001)

#define ARM_SMCCC_NUTTX_REE_IDLE \
  ARM_SMCCC_CALL_VAL(ARM_SMCCC_STD_CALL, ARM_SMCCC_SMC_32, \
                     ARM_SMCCC_OWNER_TRUSTED_OS_END, 0x0002)


#ifndef __ASSEMBLY__

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Result from SMC/HVC call
 * a0-a7 result values from registers 0 to 7
 */

struct arm_smccc_res
{
  unsigned long a0;
  unsigned long a1;
  unsigned long a2;
  unsigned long a3;
  unsigned long a4;
  unsigned long a5;
  unsigned long a6;
  unsigned long a7;
};

typedef struct arm_smccc_res arm_smccc_res_t;

enum arm_smccc_conduit
{
  SMCCC_CONDUIT_NONE,
  SMCCC_CONDUIT_SMC,
  SMCCC_CONDUIT_HVC,
};

/****************************************************************************
 * Inline functions
 ****************************************************************************/

/* SVC with SYS_ call number and no parameters */

#ifdef __ghs__
#  define sys_call0(nbr)                    \
({                                          \
  uintptr_t ret;                            \
  __asm__ __volatile__                      \
  (                                         \
    "mov r0, %2\n\t"                        \
    "svc %1\n\t"                            \
    "mov %0, r0"                            \
    : "=r"(ret)                             \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr) \
    : "memory", "r0"                        \
  );                                        \
  ret;                                      \
})
#else
#  define sys_call0(nbr)                                    \
({                                                          \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr); \
  __asm__ __volatile__                                      \
  (                                                         \
    "svc %1"                                                \
    : "=r"(reg0)                                            \
    : "i"(SYS_syscall), "r"(reg0)                           \
    : "memory"                                              \
  );                                                        \
  reg0;                                                     \
})
#endif

/* SVC with SYS_ call number and one parameter */

#ifdef __ghs__
#  define sys_call1(nbr, parm1)              \
({                                           \
  uintptr_t ret;                             \
  __asm__ __volatile__                       \
  (                                          \
    "mov r0, %2\n\t"                         \
    "mov r1, %3\n\t"                         \
    "svc %1\n\t"                             \
    "mov %0, r0"                             \
    : "=r"(ret)                              \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr), \
      "r"((uintptr_t)parm1)                  \
    : "memory", "r0", "r1"                   \
  );                                         \
  ret;                                       \
})
#else
#  define sys_call1(nbr, parm1)                               \
({                                                            \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1); \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);   \
  __asm__ __volatile__                                        \
  (                                                           \
    "svc %1"                                                  \
    : "=r"(reg0)                                              \
    : "i"(SYS_syscall), "r"(reg0), "r"(reg1)                  \
    : "memory"                                                \
  );                                                          \
  reg0;                                                       \
})
#endif

/* SVC with SYS_ call number and two parameters */

#ifdef __ghs__
#  define sys_call2(nbr, parm1, parm2)             \
({                                                 \
  uintptr_t ret;                                   \
  __asm__ __volatile__                             \
  (                                                \
    "mov r0, %2\n\t"                               \
    "mov r1, %3\n\t"                               \
    "mov r2, %4\n\t"                               \
    "svc %1\n\t"                                   \
    "mov %0, r0"                                   \
    : "=r"(ret)                                    \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr),       \
      "r"((uintptr_t)parm1), "r"((uintptr_t)parm2) \
    : "memory", "r0", "r1", "r2"                   \
  );                                               \
  ret;                                             \
})
#else
#  define sys_call2(nbr, parm1, parm2)                        \
({                                                            \
  register uintptr_t reg2 __asm__("r2") = (uintptr_t)(parm2); \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1); \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);   \
  __asm__ __volatile__                                        \
  (                                                           \
    "svc %1"                                                  \
    : "=r"(reg0)                                              \
    : "i"(SYS_syscall), "r"(reg0), "r"(reg1), "r"(reg2)       \
    : "memory"                                                \
  );                                                          \
  reg0;                                                       \
})
#endif

/* SVC with SYS_ call number and three parameters */

#ifdef __ghs__
#  define sys_call3(nbr, parm1, parm2, parm3)                       \
({                                                                  \
  uintptr_t ret;                                                    \
  __asm__ __volatile__                                              \
  (                                                                 \
    "mov r0, %2\n\t"                                                \
    "mov r1, %3\n\t"                                                \
    "mov r2, %4\n\t"                                                \
    "mov r3, %5\n\t"                                                \
    "svc %1\n\t"                                                    \
    "mov %0, r0"                                                    \
    : "=r"(ret)                                                     \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr), "r"((uintptr_t)parm1), \
      "r"((uintptr_t)parm2), "r"((uintptr_t)parm3)                  \
    : "memory", "r0", "r1", "r2", "r3"                              \
  );                                                                \
  ret;                                                              \
})
#else
#  define sys_call3(nbr, parm1, parm2, parm3)                      \
({                                                                 \
  register uintptr_t reg3 __asm__("r3") = (uintptr_t)(parm3);      \
  register uintptr_t reg2 __asm__("r2") = (uintptr_t)(parm2);      \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1);      \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);        \
  __asm__ __volatile__                                             \
  (                                                                \
    "svc %1"                                                       \
    : "=r"(reg0)                                                   \
    : "i"(SYS_syscall), "r"(reg0), "r"(reg1), "r"(reg2), "r"(reg3) \
    : "memory"                                                     \
  );                                                               \
  reg0;                                                            \
})
#endif

/* SVC with SYS_ call number and four parameters */

#ifdef __ghs__
#  define sys_call4(nbr, parm1, parm2, parm3, parm4)                      \
({                                                                        \
  uintptr_t ret;                                                          \
  __asm__ __volatile__                                                    \
  (                                                                       \
    "push {r4}\n\t"                                                       \
    "mov r0, %2\n\t"                                                      \
    "mov r1, %3\n\t"                                                      \
    "mov r2, %4\n\t"                                                      \
    "mov r3, %5\n\t"                                                      \
    "mov r4, %6\n\t"                                                      \
    "svc %1\n\t"                                                          \
    "pop {r4}\n\t"                                                        \
    "mov %0, r0"                                                          \
    : "=r"(ret)                                                           \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr), "r"((uintptr_t)parm1),       \
      "r"((uintptr_t)parm2), "r"((uintptr_t)parm3), "r"((uintptr_t)parm4) \
    : "memory", "r0", "r1", "r2", "r3"                                    \
  );                                                                      \
  ret;                                                                    \
})
#else
#  define sys_call4(nbr, parm1, parm2, parm3, parm4)          \
({                                                            \
  register uintptr_t reg4 __asm__("r4") = (uintptr_t)(parm4); \
  register uintptr_t reg3 __asm__("r3") = (uintptr_t)(parm3); \
  register uintptr_t reg2 __asm__("r2") = (uintptr_t)(parm2); \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1); \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);   \
  __asm__ __volatile__                                        \
  (                                                           \
    "svc %1"                                                  \
    : "=r"(reg0)                                              \
    : "i"(SYS_syscall), "r"(reg0), "r"(reg1), "r"(reg2),      \
      "r"(reg3), "r"(reg4)                                    \
    : "memory"                                                \
  );                                                          \
  reg0;                                                       \
})
#endif

/* SVC with SYS_ call number and five parameters */

#ifdef __ghs__
#  define sys_call5(nbr, parm1, parm2, parm3, parm4, parm5)                \
({                                                                         \
  uintptr_t ret;                                                           \
  __asm__ __volatile__                                                     \
  (                                                                        \
    "push {r3-r5}\n\t"                                                     \
    "push {%7}\n\t"                                                        \
    "push {%6}\n\t"                                                        \
    "mov r0, %2\n\t"                                                       \
    "mov r1, %3\n\t"                                                       \
    "mov r2, %4\n\t"                                                       \
    "mov r3, %5\n\t"                                                       \
    "pop {r4-r5}\n\t"                                                      \
    "svc %1\n\t"                                                           \
    "pop {r3-r5}\n\t"                                                      \
    "mov %0, r0"                                                           \
    : "=r"(ret)                                                            \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr), "r"((uintptr_t)parm1),        \
      "r"((uintptr_t)parm2), "r"((uintptr_t)parm3), "r"((uintptr_t)parm4), \
      "r"((uintptr_t)parm5)                                                \
    : "memory", "r0", "r1", "r2"                                           \
  );                                                                       \
  ret;                                                                     \
})
#else
#  define sys_call5(nbr, parm1, parm2, parm3, parm4, parm5)   \
({                                                            \
  register uintptr_t reg5 __asm__("r5") = (uintptr_t)(parm5); \
  register uintptr_t reg4 __asm__("r4") = (uintptr_t)(parm4); \
  register uintptr_t reg3 __asm__("r3") = (uintptr_t)(parm3); \
  register uintptr_t reg2 __asm__("r2") = (uintptr_t)(parm2); \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1); \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);   \
  __asm__ __volatile__                                        \
  (                                                           \
    "svc %1"                                                  \
    : "=r"(reg0)                                              \
    : "i"(SYS_syscall), "r"(reg0), "r"(reg1), "r"(reg2),      \
      "r"(reg3), "r"(reg4), "r"(reg5)                         \
    : "memory"                                                \
  );                                                          \
  reg0;                                                       \
})
#endif

/* SVC with SYS_ call number and six parameters */

#ifdef __ghs__
#  define sys_call6(nbr, parm1, parm2, parm3, parm4, parm5, parm6)         \
({                                                                         \
  uintptr_t ret;                                                           \
  __asm__ __volatile__                                                     \
  (                                                                        \
    "push {r2-r6}\n\t"                                                     \
    "push {%8}\n\t"                                                        \
    "push {%7}\n\t"                                                        \
    "push {%6}\n\t"                                                        \
    "push {%5}\n\t"                                                        \
    "mov r0, %2\n\t"                                                       \
    "mov r1, %3\n\t"                                                       \
    "mov r2, %4\n\t"                                                       \
    "pop {r3-r6}\n\t"                                                      \
    "svc %1\n\t"                                                           \
    "pop {r2-r6}\n\t"                                                      \
    "mov %0, r0"                                                           \
    : "=r"(ret)                                                            \
    : "i"(SYS_syscall), "r"((uintptr_t)nbr), "r"((uintptr_t)parm1),        \
      "r"((uintptr_t)parm2), "r"((uintptr_t)parm3), "r"((uintptr_t)parm4), \
      "r"((uintptr_t)parm5), "r"((uintptr_t)parm6)                         \
    : "memory", "r0", "r1"                                                 \
  );                                                                       \
  ret;                                                                     \
})
#else
#  define sys_call6(nbr, parm1, parm2, parm3, parm4, parm5, parm6) \
({                                                                 \
  register uintptr_t reg6 __asm__("r6") = (uintptr_t)(parm6);      \
  register uintptr_t reg5 __asm__("r5") = (uintptr_t)(parm5);      \
  register uintptr_t reg4 __asm__("r4") = (uintptr_t)(parm4);      \
  register uintptr_t reg3 __asm__("r3") = (uintptr_t)(parm3);      \
  register uintptr_t reg2 __asm__("r2") = (uintptr_t)(parm2);      \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1);      \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);        \
  __asm__ __volatile__                                             \
  (                                                                \
    "svc %1"                                                       \
    : "=r"(reg0)                                                   \
    : "i"(SYS_syscall), "r"(reg0), "r"(reg1), "r"(reg2),           \
      "r"(reg3), "r"(reg4), "r"(reg5), "r"(reg6)                   \
    : "memory"                                                     \
  );                                                               \
  reg0;                                                            \
})
#endif

/* semihosting(SMH) call with call number and one parameter */

#ifdef __ghs__
#  ifdef CONFIG_ARCH_ARM_M

#    define smh_call(nbr, parm1)                                   \
({                                                                 \
  uintptr_t ret;                                                   \
  __asm__ __volatile__                                             \
  (                                                                \
    "mov r0, %2\n\t"                                               \
    "mov r1, %3\n\t"                                               \
    "bkpt %1\n\t"                                                  \
    "mov %0, r0"                                                   \
    : "=r"(ret)                                                    \
    : "i"(SYS_smhcall), "r"((uintptr_t)nbr), "r"((uintptr_t)parm1) \
    : "memory", "r0", "r1"                                         \
  );                                                               \
  ret;                                                             \
})
#  else
#    define smh_call(nbr, parm1)                                   \
({                                                                 \
  uintptr_t ret;                                                   \
  __asm__ __volatile__                                             \
  (                                                                \
    "mov r0, %2\n\t"                                               \
    "mov r1, %3\n\t"                                               \
    "svc %1\n\t"                                                   \
    "mov %0, r0"                                                   \
    : "=r"(ret)                                                    \
    : "i"(SYS_smhcall), "r"((uintptr_t)nbr), "r"((uintptr_t)parm1) \
    : "memory", "r0", "r1"                                         \
  );                                                               \
  ret;                                                             \
})
#  endif
#else
#  ifdef CONFIG_ARCH_ARM_M

#    define smh_call(nbr, parm1)                              \
({                                                            \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1); \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);   \
  __asm__ __volatile__                                        \
  (                                                           \
    "bkpt %1"                                                 \
    : "=r"(reg0)                                              \
    : "i"(SYS_smhcall), "r"(reg0), "r"(reg1)                  \
    : "memory"                                                \
  );                                                          \
  reg0;                                                       \
})
#  else
#    define smh_call(nbr, parm1)                              \
({                                                            \
  register uintptr_t reg1 __asm__("r1") = (uintptr_t)(parm1); \
  register uintptr_t reg0 __asm__("r0") = (uintptr_t)(nbr);   \
  __asm__ __volatile__                                        \
  (                                                           \
    "svc %1"                                                  \
    : "=r"(reg0)                                              \
    : "i"(SYS_smhcall), "r"(reg0), "r"(reg1)                  \
    : "memory"                                                \
  );                                                          \
  reg0;                                                       \
})
#  endif
#endif

/****************************************************************************
 * Name: up_assert
 ****************************************************************************/

#define up_assert(filename, linenum, msg) \
    sys_call3(SYS_assert_handler, (uintptr_t)filename, \
              (uintptr_t)linenum, (uintptr_t)msg);

/****************************************************************************
 * Public Data
 ****************************************************************************/

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Make HVC calls
 *
 * param a0 function identifier
 * param a1-a7 parameters registers
 * param res results
 */

void arm_smccc_hvc(unsigned long a0, unsigned long a1,
                   unsigned long a2, unsigned long a3,
                   unsigned long a4, unsigned long a5,
                   unsigned long a6, unsigned long a7,
                   struct arm_smccc_res *res);

/* Make SMC calls
 *
 * param a0 function identifier
 * param a1-a7 parameters registers
 * param res results
 */

void arm_smccc_smc(unsigned long a0, unsigned long a1,
                   unsigned long a2, unsigned long a3,
                   unsigned long a4, unsigned long a5,
                   unsigned long a6, unsigned long a7,
                   struct arm_smccc_res *res);

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __ARCH_ARM_INCLUDE_SYSCALL_H */
