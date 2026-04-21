# Scheduler (sched/) Review Rules

Patterns mined from 170 review deltas in `sched/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| design-philosophy | 41 | 24% |
| coding-style | 33 | 19% |
| root-cause-fix | 31 | 18% |
| code-reuse | 15 | 9% |
| patch-organization | 13 | 8% |
| concurrency | 13 | 8% |
| error-handling | 7 | 4% |
| correctness | 6 | 4% |
| posix-compliance | 6 | 4% |
| variable-scope | 1 | 1% |
| other | 1 | 1% |
| thread-design | 1 | 1% |
| memory-management | 1 | 1% |
| performance | 1 | 1% |

## Severity Distribution

correctness: 75 | design: 67 | style: 26 | performance: 2

## Top Patterns

### 1. patch-organization/unrelated_changes_in_single_patch (2x) [design]

**Rule:** Each patch must address exactly one logical change; unrelated fixes (e.g. typo corrections) must be submitted as separate patches even if they touch nearby code.

**Example:** #4618899 — isrthread: community code synchronization to isrthread
**Transformation:** Removed the Kconfig typo fix (DYNAMINC→DYNAMIC) and its associated #ifdef changes in irq_attach.c from the irq_attach_thread refactor patch, keeping only the functionally related changes.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/Kconfig b/arch/Kconfig
index ba887d3d732..ce05dbfe747 100644
--- a/arch/Kconfig
+++ b/arch/Kconfig
@@ -1193,7 +1193,7 @@ config ARCH_MINIMAL_VECTORTABLE
 		IRQMAPPED_MAX, then hardware interrupt vector 42 is not used and
 		if it occurs will result in an unexpected interrupt crash.
 
-config ARCH_MINIMAL_VECTORTABLE_DYNAMINC
+config ARCH_MINIMAL_VECTORTABLE_DYNAMIC
 	bool "Dynaminc Minimal RAM usage for vector table"
 	default n
 	depends on ARCH_MINIMAL_VECTORTABLE
diff --git a/sched/irq/irq_attach.c b/sched/irq/irq_attach.c
index 1b5cfc25d16..93097ef9629 100644
--- a/sched/irq/irq_attach.c
+++ b/sched/irq/irq_attach.c
@@ -36,7 +36,7 @@
  * Private Data
  ****************************************************************************/
 
-#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMINC
+#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMIC
 static int g_irqmap_count = 1;
 #endif
 
@@ -44,7 +44,7 @@ static int g_irqmap_count = 1;
  * Public Data
  ****************************************************************************/
 
-#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMINC
+#ifdef CONFIG_ARCH_MINIMAL_VECTORTABLE_DYNAMIC
 
 /* This is the interrupt vector mapping table.  This must be provided by
  * architecture specific logic if CONFIG_ARCH_MINIMAL_VECTORTABLE is define
... (296 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/irq/irq_attach.c b/sched/irq/irq_attach.c
index 1b5cfc25d16..789895d8189 100644
--- a/sched/irq/irq_attach.c
+++ b/sched/irq/irq_attach.c
@@ -141,7 +141,7 @@ int irq_attach(int irq, xcpt_t isr, FAR void *arg)
       if (is_irqchain(ndx, isr))
         {
           ret = irqchain_attach(ndx, isr, arg);
-          leave_critical_section(flags);
+          spin_unlock_irqrestore(NULL, flags);
           return ret;
         }
 #endif
diff --git a/sched/irq/irq_attach_thread.c b/sched/irq/irq_attach_thread.c
index b2d2fa9b3b8..16c8e5fcbca 100644
--- a/sched/irq/irq_attach_thread.c
+++ b/sched/irq/irq_attach_thread.c
@@ -30,6 +30,7 @@
 #include <stdio.h>
 
 #include <nuttx/irq.h>
+#include <nuttx/arch.h>
 #include <nuttx/kthread.h>
 
 #include "irq/irq.h"
@@ -48,17 +49,15 @@
 struct irq_thread_info_s
 {
   xcpt_t handler;     /* Address of the interrupt handler */
-  xcpt_t isrthread;   /* Address of the interrupt thread handler */
   FAR void *arg;      /* The argument provided to the interrupt handler. */
-  sem_t sem;          /* irq sem used to notify irq thread */
-  pid_t threadid;     /* irq threadid */
+  FAR sem_t *sem;     /* irq sem used to notify irq thread */
 };
... (256 lines truncated)
```
</details>

### 2. coding-style/alignment_whitespace (2x) [style]

**Rule:** Continuation lines in multi-line expressions must align with the opening parenthesis or operator of the expression they continue.

**Example:** #872504 — nuttx/sched/sched: fix runtime error UBSan(division by zero)
**Transformation:** Adjusted the alignment of SI_LOAD_SHIFT continuation line from 5 spaces to 6 spaces indent to properly align with the opening parenthesis above.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/sched/sched_sysinfo.c b/sched/sched/sched_sysinfo.c
index 5562ebd0d3b..ffa9e3e1fe0 100644
--- a/sched/sched/sched_sysinfo.c
+++ b/sched/sched/sched_sysinfo.c
@@ -65,10 +65,17 @@ int sysinfo(FAR struct sysinfo *info)
 #ifdef CONFIG_SCHED_CPULOAD
   clock_cpuload(0, &cpuload);
 
-  info->loads[0] = ((cpuload.total - cpuload.active) <<
-                     SI_LOAD_SHIFT) / cpuload.total;
-  info->loads[1] = info->loads[0];
-  info->loads[2] = info->loads[0];
+  /* On the simulator, you may hit cpuload.total == 0, but probably never
+   * on real hardware.
+   */
+
+  if (cpuload.total)
+    {
+      info->loads[0] = ((cpuload.total - cpuload.active) <<
+                        SI_LOAD_SHIFT) / cpuload.total;
+      info->loads[1] = info->loads[0];
+      info->loads[2] = info->loads[0];
+    }
 #endif
 
 #ifdef MM_KERNEL_USRHEAP_INIT

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/sched/sched_sysinfo.c b/sched/sched/sched_sysinfo.c
index 5562ebd0d3b..024fd605a74 100644
--- a/sched/sched/sched_sysinfo.c
+++ b/sched/sched/sched_sysinfo.c
@@ -65,10 +65,17 @@ int sysinfo(FAR struct sysinfo *info)
 #ifdef CONFIG_SCHED_CPULOAD
   clock_cpuload(0, &cpuload);
 
-  info->loads[0] = ((cpuload.total - cpuload.active) <<
-                     SI_LOAD_SHIFT) / cpuload.total;
-  info->loads[1] = info->loads[0];
-  info->loads[2] = info->loads[0];
+  /* On the simulator, you may hit cpuload.total == 0, but probably never
+   * on real hardware.
+   */
+
+  if (cpuload.total)
+    {
+      info->loads[0] = ((cpuload.total - cpuload.active) <<
+                         SI_LOAD_SHIFT) / cpuload.total;
+      info->loads[1] = info->loads[0];
+      info->loads[2] = info->loads[0];
+    }
 #endif
 
 #ifdef MM_KERNEL_USRHEAP_INIT

```
</details>

### 3. design-philosophy/integrate_into_existing_framework (2x) [design]

**Rule:** New functionality must integrate into the existing framework (here sched_note) rather than bolting ad-hoc entry points onto a peripheral driver; follow the established patterns for note types, data structures, Kconfig gating, and no-op macros.

**Example:** #830393 — trace: and note dump
**Transformation:** Replaced ad-hoc ioctl-based print commands in notectl_driver with proper sched_note instrumentation dump APIs (sched_note_string, sched_note_dump, sched_note_printf, etc.) integrated into the existing note framework with Kconfig, note types, and data structures.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/note/notectl_driver.c b/drivers/note/notectl_driver.c
index 05ed3babf69..d6827610af5 100644
--- a/drivers/note/notectl_driver.c
+++ b/drivers/note/notectl_driver.c
@@ -209,6 +209,21 @@ static int notectl_ioctl(struct file *filep, int cmd, unsigned long arg)
         break;
 #endif
 
+      case NOTECTL_PRINTSTART:
+        break;
+
+      case NOTECTL_PRINTSTOP:
+        break;
+
+      case NOTECTL_PRINTSUSPEND:
+        break;
+
+      case NOTECTL_PRINTRESUME:
+        break;
+
+      case NOTECTL_PRINTMESSAGE:
+        break;
+
       default:
           break;
     }
@@ -239,3 +254,8 @@ int notectl_register(void)
 {
   return register_driver("/dev/notectl", &notectl_fops, 0666, NULL);
 }
+
+int notectl_print(int cmd, const char *str)
+{
+  return notectl_ioctl(NULL, cmd, (unsigned long)str);
+}
... (28 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/sched_note.h b/include/nuttx/sched_note.h
index 1271ea50334..f96901eba4b 100644
--- a/include/nuttx/sched_note.h
+++ b/include/nuttx/sched_note.h
@@ -147,6 +147,11 @@ enum note_type_e
   NOTE_IRQ_ENTER       = 20,
   NOTE_IRQ_LEAVE       = 21
 #endif
+#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
+  ,
+  NOTE_DUMP_STRING     = 22,
+  NOTE_DUMP_BINARY     = 23
+#endif
 };
 
 /* This structure provides the common header of each note */
@@ -318,6 +323,29 @@ struct note_irqhandler_s
 };
 #endif /* CONFIG_SCHED_INSTRUMENTATION_IRQHANDLER */
 
+#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP
+struct note_string_s
+{
+  struct note_common_s nst_cmn; /* Common note parameters */
+  char nst_data[1];             /* String data terminated by '\0' */
+};
+
+#define SIZEOF_NOTE_STRING(n) (sizeof(struct note_string_s) + \
+                               (n) * sizeof(char))
+
+struct note_binary_s
+{
+  struct note_common_s nbi_cmn; /* Common note parameters */
+  uint32_t nbi_module;          /* Module number */
+  uint8_t  nbi_event;           /* Event number */
... (433 lines truncated)
```
</details>

### 4. coding-style/comment_capitalization (1x) [style]

**Rule:** In multi-sentence block comments, only the first word after a period or the start of the comment block should be capitalized; mid-sentence words following 'And' conjunction should remain lowercase

**Example:** #6953927 — sched/timerexpiration: fix timer & tickless, first nest did no update.
**Transformation:** Changed mid-sentence 'Cancel' to lowercase 'cancel' in block comment

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/sched/sched_timerexpiration.c b/sched/sched/sched_timerexpiration.c
index ba6ac2a24b7..d8d430ec949 100644
--- a/sched/sched/sched_timerexpiration.c
+++ b/sched/sched/sched_timerexpiration.c
@@ -563,9 +563,12 @@ void nxsched_reassess_timer(void)
   clock_t ticks;
   clock_t next;
 
-  if (!g_timernested)
+  if (g_timernested <= 1)
     {
-      /* Cancel the timer and get the current time */
+      /* If the timer is nested deep, should skip the timer reassessment.
+       * And Cancel the timer and get the current time. when not nested or
+       * in first level of nesting.
+       */
 
       nxsched_timer_tick_cancel(&ticks);
       next = nxsched_timer_update(ticks, true);

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/sched/sched_timerexpiration.c b/sched/sched/sched_timerexpiration.c
index ba6ac2a24b7..fc40d852e82 100644
--- a/sched/sched/sched_timerexpiration.c
+++ b/sched/sched/sched_timerexpiration.c
@@ -563,9 +563,12 @@ void nxsched_reassess_timer(void)
   clock_t ticks;
   clock_t next;
 
-  if (!g_timernested)
+  if (g_timernested <= 1)
     {
-      /* Cancel the timer and get the current time */
+      /* If the timer is nested deep, should skip the timer reassessment.
+       * And cancel the timer and get the current time. when not nested or
+       * in first level of nesting.
+       */
 
       nxsched_timer_tick_cancel(&ticks);
       next = nxsched_timer_update(ticks, true);

```
</details>

### 5. error-handling/critical_section_leak_on_early_exit (1x) [correctness]

**Rule:** When adding a critical section around a loop body, every exit path (break, error return, early exit) must release the lock; place leave_critical_section immediately before each break rather than relying on a single release point at the bottom.

**Example:** #6422303 — waittid: add critical_section to avoid switch out
**Transformation:** PS1 placed enter_critical_section after DEBUGASSERT and leave_critical_section only after nxsig_waitinfo, missing all intermediate break paths. FINAL moves enter to top of while loop and adds leave_critical_section before every break statement, ensuring the critical section is released on all exit paths.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/sched/sched_waitid.c b/sched/sched/sched_waitid.c
index d069a561598..2e409981e1c 100644
--- a/sched/sched/sched_waitid.c
+++ b/sched/sched/sched_waitid.c
@@ -88,6 +88,7 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
 #endif
   int errcode = OK;
   sigset_t set;
+  irqstate_t flags;
   int ret;
 
   /* Create a signal set that contains only SIGCHLD */
@@ -107,6 +108,7 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
        */
 
       DEBUGASSERT(!retains || rtcb->group->tg_children);
+      flags = enter_critical_section();
       if (idtype == P_ALL)
         {
           /* We are waiting for any child to exit */
@@ -204,6 +206,7 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
       /* Wait for any death-of-child signal */
 
       ret = nxsig_waitinfo(&set, info);
+      leave_critical_section(flags);
       if (ret < 0)
         {
           errcode = -ret;

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/sched/sched_waitid.c b/sched/sched/sched_waitid.c
index d069a561598..332bc20699a 100644
--- a/sched/sched/sched_waitid.c
+++ b/sched/sched/sched_waitid.c
@@ -88,6 +88,7 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
 #endif
   int errcode = OK;
   sigset_t set;
+  irqstate_t flags;
   int ret;
 
   /* Create a signal set that contains only SIGCHLD */
@@ -99,6 +100,8 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
 
   while (errcode == OK)
     {
+      flags = enter_critical_section();
+
 #ifdef CONFIG_SCHED_CHILD_STATUS
       /* Check if the task has already died. Signals are not queued in
        * NuttX.  So a possibility is that the child has died and we
@@ -118,6 +121,7 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
                */
 
               exited_child(rtcb, child, info);
+              leave_critical_section(flags);
               break;
             }
         }
@@ -142,6 +146,7 @@ int waittcb(FAR struct tcb_s *rtcb, idtype_t idtype, int options,
                */
 
               exited_child(rtcb, child, info);
+              leave_critical_section(flags);
               break;
... (34 lines truncated)
```
</details>

### 6. correctness/overflow_safe_comparison (1x) [correctness]

**Rule:** When comparing distances in pointer/address arithmetic, compute the known-safe difference (where the invariant guarantees no underflow) rather than adding to the smaller operand (which may overflow). Also, if a feature is gated by a config with default 0 (effectively disabled), prefer a sensible non-zero default so users get value without manual tuning.

**Example:** #6414024 — assert.c: limit stack print length
**Transformation:** Changed condition from `dump_end > sp + MAX_LENGTH` (potential unsigned overflow) to `top - sp > MAX_LENGTH` (safe, since top >= sp is invariant); fixed indentation to NuttX 6-space style; changed Kconfig default from 0 to 4096 to make feature active by default

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/misc/assert.c b/sched/misc/assert.c
index ece5e5bf873..e48c592d967 100644
--- a/sched/misc/assert.c
+++ b/sched/misc/assert.c
@@ -179,6 +179,7 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
                            uintptr_t base, size_t size, size_t used)
 {
   uintptr_t top = base + size;
+  uintptr_t dump_end = top;
 
   _alert("%s Stack:\n", tag);
   _alert("  base: 0x%" PRIxPTR "\n", base);
@@ -195,7 +196,13 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
           sp -= DUMP_STRIDE;
         }
 
-      stack_dump(sp, top);
+#if CONFIG_ARCH_STACKDUMP_MAX_LENGTH > 0
+    if (dump_end > sp + CONFIG_ARCH_STACKDUMP_MAX_LENGTH)
+      {
+        dump_end = sp + CONFIG_ARCH_STACKDUMP_MAX_LENGTH;
+      }
+#endif
+      stack_dump(sp, dump_end);
     }
   else
     {

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/Kconfig b/arch/Kconfig
index 872f7ff24f4..8c1fe76fa79 100644
--- a/arch/Kconfig
+++ b/arch/Kconfig
@@ -1220,7 +1220,7 @@ config ARCH_HAVE_COPYUSERCONTEXT
 config ARCH_STACKDUMP_MAX_LENGTH
 	int "The maximum length for dump stack on assertions"
 	depends on ARCH_STACKDUMP
-	default 0
+	default 4096
 
 config ARCH_USBDUMP
 	bool "Dump USB trace data"
diff --git a/sched/misc/assert.c b/sched/misc/assert.c
index ece5e5bf873..b1e372c5511 100644
--- a/sched/misc/assert.c
+++ b/sched/misc/assert.c
@@ -179,6 +179,7 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
                            uintptr_t base, size_t size, size_t used)
 {
   uintptr_t top = base + size;
+  uintptr_t dump_end = top;
 
   _alert("%s Stack:\n", tag);
   _alert("  base: 0x%" PRIxPTR "\n", base);
@@ -195,7 +196,14 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
           sp -= DUMP_STRIDE;
         }
 
-      stack_dump(sp, top);
+#if CONFIG_ARCH_STACKDUMP_MAX_LENGTH > 0
+      if (top - sp > CONFIG_ARCH_STACKDUMP_MAX_LENGTH)
+        {
+          dump_end = sp + CONFIG_ARCH_STACKDUMP_MAX_LENGTH;
+        }
... (7 lines truncated)
```
</details>

### 7. design-philosophy/sensible_default_config (1x) [design]

**Rule:** New features that add configurable limits should ship with sensible non-zero defaults so they are effective immediately, rather than defaulting to disabled and requiring every user to manually enable them

**Example:** #6416053 — assert.c: limit stack print length
**Transformation:** Changed ARCH_STACKDUMP_MAX_LENGTH Kconfig default from 0 (disabled/unlimited) to 4096, so the stack dump length limit is active out-of-the-box without requiring explicit user configuration

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/misc/assert.c b/sched/misc/assert.c
index 120b474bd8d..47238cb93e2 100644
--- a/sched/misc/assert.c
+++ b/sched/misc/assert.c
@@ -200,6 +200,7 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
                            uintptr_t base, size_t size, size_t used)
 {
   uintptr_t top = base + size;
+  uintptr_t dump_end = top;
 
   _alert("%s Stack:\n", tag);
   _alert("  base: %p\n", (FAR void *)base);
@@ -216,7 +217,14 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
           sp -= DUMP_STRIDE;
         }
 
-      stack_dump(sp, top);
+#if CONFIG_ARCH_STACKDUMP_MAX_LENGTH > 0
+      if (top - sp > CONFIG_ARCH_STACKDUMP_MAX_LENGTH)
+        {
+          dump_end = sp + CONFIG_ARCH_STACKDUMP_MAX_LENGTH;
+        }
+#endif
+
+      stack_dump(sp, dump_end);
     }
   else
     {

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/Kconfig b/arch/Kconfig
index d09e44250e5..5c1ebb5ab9f 100644
--- a/arch/Kconfig
+++ b/arch/Kconfig
@@ -1219,7 +1219,7 @@ config ARCH_HAVE_COPYUSERCONTEXT
 config ARCH_STACKDUMP_MAX_LENGTH
 	int "The maximum length for dump stack on assertions"
 	depends on ARCH_STACKDUMP
-	default 0
+	default 4096
 
 config ARCH_USBDUMP
 	bool "Dump USB trace data"
diff --git a/sched/misc/assert.c b/sched/misc/assert.c
index 120b474bd8d..47238cb93e2 100644
--- a/sched/misc/assert.c
+++ b/sched/misc/assert.c
@@ -200,6 +200,7 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
                            uintptr_t base, size_t size, size_t used)
 {
   uintptr_t top = base + size;
+  uintptr_t dump_end = top;
 
   _alert("%s Stack:\n", tag);
   _alert("  base: %p\n", (FAR void *)base);
@@ -216,7 +217,14 @@ static void dump_stackinfo(FAR const char *tag, uintptr_t sp,
           sp -= DUMP_STRIDE;
         }
 
-      stack_dump(sp, top);
+#if CONFIG_ARCH_STACKDUMP_MAX_LENGTH > 0
+      if (top - sp > CONFIG_ARCH_STACKDUMP_MAX_LENGTH)
+        {
+          dump_end = sp + CONFIG_ARCH_STACKDUMP_MAX_LENGTH;
+        }
... (7 lines truncated)
```
</details>

### 8. error-handling/capture_return_value (1x) [correctness]

**Rule:** When a function's signature changes to return a value, all call sites must capture and use that return value to maintain correct program state

**Example:** #6223684 — arch: nxsched_switch_context may cause context switch and change this this_task(), which is not expected
**Transformation:** Capture return value of nxsched_switch_context() into tcb variable across all arch-specific doirq/syscall call sites

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm/src/arm/arm_doirq.c b/arch/arm/src/arm/arm_doirq.c
index 9c6a4470074..33b39bcd170 100644
--- a/arch/arm/src/arm/arm_doirq.c
+++ b/arch/arm/src/arm/arm_doirq.c
@@ -101,7 +101,7 @@ uint32_t *arm_doirq(int irq, uint32_t *regs)
 
       /* Update scheduler parameters */
 
-      nxsched_switch_context(*running_task, tcb);
+      tcb = nxsched_switch_context(*running_task, tcb);
 
       /* Record the new "running" task when context switch occurred.
        * g_running_tasks[] is only used by assertion logic for reporting
diff --git a/arch/arm/src/arm/arm_syscall.c b/arch/arm/src/arm/arm_syscall.c
index 444e9f91183..a8b875d43dc 100644
--- a/arch/arm/src/arm/arm_syscall.c
+++ b/arch/arm/src/arm/arm_syscall.c
@@ -87,7 +87,7 @@ uint32_t *arm_syscall(uint32_t *regs)
 
         /* Update scheduler parameters */
 
-        nxsched_switch_context(*running_task, tcb);
+        tcb = nxsched_switch_context(*running_task, tcb);
 
       case SYS_restore_context:
 #ifdef CONFIG_ARCH_ADDRENV
diff --git a/arch/arm/src/armv6-m/arm_doirq.c b/arch/arm/src/armv6-m/arm_doirq.c
index e1657c34b92..e6e8faa3139 100644
--- a/arch/arm/src/armv6-m/arm_doirq.c
+++ b/arch/arm/src/armv6-m/arm_doirq.c
@@ -121,7 +121,7 @@ uint32_t *arm_doirq(int irq, uint32_t *regs)
        * so only the scheduling information needs to be updated in doirq.
        */
 
-      nxsched_switch_context(*running_task, tcb);
... (655 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/arm/arm_doirq.c b/arch/arm/src/arm/arm_doirq.c
index 9c6a4470074..33b39bcd170 100644
--- a/arch/arm/src/arm/arm_doirq.c
+++ b/arch/arm/src/arm/arm_doirq.c
@@ -101,7 +101,7 @@ uint32_t *arm_doirq(int irq, uint32_t *regs)
 
       /* Update scheduler parameters */
 
-      nxsched_switch_context(*running_task, tcb);
+      tcb = nxsched_switch_context(*running_task, tcb);
 
       /* Record the new "running" task when context switch occurred.
        * g_running_tasks[] is only used by assertion logic for reporting
diff --git a/arch/arm/src/arm/arm_syscall.c b/arch/arm/src/arm/arm_syscall.c
index 444e9f91183..a8b875d43dc 100644
--- a/arch/arm/src/arm/arm_syscall.c
+++ b/arch/arm/src/arm/arm_syscall.c
@@ -87,7 +87,7 @@ uint32_t *arm_syscall(uint32_t *regs)
 
         /* Update scheduler parameters */
 
-        nxsched_switch_context(*running_task, tcb);
+        tcb = nxsched_switch_context(*running_task, tcb);
 
       case SYS_restore_context:
 #ifdef CONFIG_ARCH_ADDRENV
diff --git a/arch/arm/src/armv6-m/arm_doirq.c b/arch/arm/src/armv6-m/arm_doirq.c
index e1657c34b92..e6e8faa3139 100644
--- a/arch/arm/src/armv6-m/arm_doirq.c
+++ b/arch/arm/src/armv6-m/arm_doirq.c
@@ -121,7 +121,7 @@ uint32_t *arm_doirq(int irq, uint32_t *regs)
        * so only the scheduling information needs to be updated in doirq.
        */
 
-      nxsched_switch_context(*running_task, tcb);
... (655 lines truncated)
```
</details>

### 9. patch-organization/remove_unrelated_changes_and_debug_prints (1x) [correctness]

**Rule:** A patch must do exactly one thing: no debug prints, no unrelated config changes; each stream pointer must be explicitly cast to its concrete type when passing through a generic interface

**Example:** #6242586 — sched/coredump: Flush and close the stream
**Transformation:** Removed debug _alert() prints, dropped unrelated CONFIG_BOARD_MEMORY_RANGE ifdef removals, added explicit type casts on stream close calls, and changed close calls to discard void return value

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/misc/coredump.c b/sched/misc/coredump.c
index bf8858645d0..10349678187 100644
--- a/sched/misc/coredump.c
+++ b/sched/misc/coredump.c
@@ -127,16 +127,31 @@ static bool g_stream_initialized;
  ****************************************************************************/
 
 /****************************************************************************
- * Name: elf_flush
+ * Name: elf_emit_finish
  *
  * Description:
- *   Flush the out stream
+ *   Flush and close the out stream
  *
  ****************************************************************************/
 
-static int elf_flush(FAR struct elf_dumpinfo_s *cinfo)
+static int elf_emit_finish(FAR struct elf_dumpinfo_s *cinfo)
 {
-  return lib_stream_flush(cinfo->stream);
+  int ret;
+  ret = lib_stream_flush(cinfo->stream);
+  if (ret < 0)
+    {
+      return ret;
+    }
+
+#ifdef CONFIG_BOARD_COREDUMP_BLKDEV
+  ret = lib_blkoutstream_close(cinfo->stream);
+#elif defined(CONFIG_BOARD_COREDUMP_MTDDEV)
+  ret = lib_mtdoutstream_close(cinfo->stream);
+#elif defined(CONFIG_BOARD_COREDUMP_MEMDEV)
+  ret = lib_filesistream_close(cinfo->stream);
+#endif
... (61 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/misc/coredump.c b/sched/misc/coredump.c
index bf8858645d0..d2f39a9b4ab 100644
--- a/sched/misc/coredump.c
+++ b/sched/misc/coredump.c
@@ -127,16 +127,31 @@ static bool g_stream_initialized;
  ****************************************************************************/
 
 /****************************************************************************
- * Name: elf_flush
+ * Name: elf_emit_finish
  *
  * Description:
- *   Flush the out stream
+ *   Flush and close the out stream
  *
  ****************************************************************************/
 
-static int elf_flush(FAR struct elf_dumpinfo_s *cinfo)
+static int elf_emit_finish(FAR struct elf_dumpinfo_s *cinfo)
 {
-  return lib_stream_flush(cinfo->stream);
+  int ret;
+  ret = lib_stream_flush(cinfo->stream);
+  if (ret < 0)
+    {
+      return ret;
+    }
+
+#ifdef CONFIG_BOARD_COREDUMP_BLKDEV
+  lib_blkoutstream_close((FAR struct lib_blkoutstream_s *)cinfo->stream);
+#elif defined(CONFIG_BOARD_COREDUMP_MTDDEV)
+  lib_mtdoutstream_close((FAR struct lib_mtdoutstream_s *)cinfo->stream);
+#elif defined(CONFIG_BOARD_COREDUMP_MEMDEV)
+  lib_filesistream_close((FAR struct lib_filesistream_s *)cinfo->stream);
+#endif
... (18 lines truncated)
```
</details>

### 10. design-philosophy/header_dependency_colocation (1x) [correctness]

**Rule:** When a header uses a macro, that macro must be defined in (or explicitly included by) the same header — never rely on transitive inclusion from an unrelated header

**Example:** #6195354 — percpu: remove all per_cpu_var use per_cpu_var_smp
**Transformation:** Moved this_cpu() macro definition from include/nuttx/sched.h into include/nuttx/percpu.h where it is actually used, eliminating an implicit cross-header dependency

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/procfs/fs_procfscritmon.c b/fs/procfs/fs_procfscritmon.c
index 70e1d596c9c..8c2f854992d 100644
--- a/fs/procfs/fs_procfscritmon.c
+++ b/fs/procfs/fs_procfscritmon.c
@@ -183,21 +183,21 @@ static ssize_t critmon_read_cpu(FAR struct critmon_file_s *attr,
 #endif
 #if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0
 #  ifdef CONFIG_SMP
-  clock_t *premp_max = &per_cpu_var(g_premp_max, cpu);
+  clock_t *premp_max = &per_cpu_var_smp(g_premp_max, cpu);
 #  else
   clock_t *premp_max = &this_cpu_var(g_premp_max);
 #  endif
 #endif
 #if CONFIG_SCHED_CRITMONITOR_MAXTIME_CSECTION >= 0
 #  ifdef CONFIG_SMP
-  clock_t *crit_max = &per_cpu_var(g_crit_max, cpu);
+  clock_t *crit_max = &per_cpu_var_smp(g_crit_max, cpu);
 #  else
   clock_t *crit_max = &this_cpu_var(g_crit_max);
 #  endif
 #endif
 #if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT >= 0
 #  ifdef CONFIG_SMP
-  clock_t *busywait_max = &per_cpu_var(g_busywait_max, cpu);
+  clock_t *busywait_max = &per_cpu_var_smp(g_busywait_max, cpu);
 #  else
   clock_t *busywait_max = &this_cpu_var(g_busywait_max);
 #  endif
diff --git a/include/nuttx/percpu.h b/include/nuttx/percpu.h
index 1647dd84268..313e00fb091 100644
--- a/include/nuttx/percpu.h
+++ b/include/nuttx/percpu.h
@@ -37,21 +37,19 @@
 #  define DEFINE_PER_CPU(t, v)         t v[CONFIG_NCPUS]
... (138 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/procfs/fs_procfscritmon.c b/fs/procfs/fs_procfscritmon.c
index 70e1d596c9c..8c2f854992d 100644
--- a/fs/procfs/fs_procfscritmon.c
+++ b/fs/procfs/fs_procfscritmon.c
@@ -183,21 +183,21 @@ static ssize_t critmon_read_cpu(FAR struct critmon_file_s *attr,
 #endif
 #if CONFIG_SCHED_CRITMONITOR_MAXTIME_PREEMPTION >= 0
 #  ifdef CONFIG_SMP
-  clock_t *premp_max = &per_cpu_var(g_premp_max, cpu);
+  clock_t *premp_max = &per_cpu_var_smp(g_premp_max, cpu);
 #  else
   clock_t *premp_max = &this_cpu_var(g_premp_max);
 #  endif
 #endif
 #if CONFIG_SCHED_CRITMONITOR_MAXTIME_CSECTION >= 0
 #  ifdef CONFIG_SMP
-  clock_t *crit_max = &per_cpu_var(g_crit_max, cpu);
+  clock_t *crit_max = &per_cpu_var_smp(g_crit_max, cpu);
 #  else
   clock_t *crit_max = &this_cpu_var(g_crit_max);
 #  endif
 #endif
 #if CONFIG_SCHED_CRITMONITOR_MAXTIME_BUSYWAIT >= 0
 #  ifdef CONFIG_SMP
-  clock_t *busywait_max = &per_cpu_var(g_busywait_max, cpu);
+  clock_t *busywait_max = &per_cpu_var_smp(g_busywait_max, cpu);
 #  else
   clock_t *busywait_max = &this_cpu_var(g_busywait_max);
 #  endif
diff --git a/include/nuttx/percpu.h b/include/nuttx/percpu.h
index 1647dd84268..e391e85da9b 100644
--- a/include/nuttx/percpu.h
+++ b/include/nuttx/percpu.h
@@ -33,25 +33,33 @@
  * Pre-processor Definitions
... (173 lines truncated)
```
</details>

### 11. coding-style/global_variable_naming_prefix (1x) [style]

**Rule:** Global variables must carry a module-specific prefix to avoid name collisions and clarify ownership; avoid unnecessary casts when the type already matches; align continuation arguments to the opening parenthesis

**Example:** #5028449 — sched_perf: Fix perf build error with CONFIG_SMP
**Transformation:** Renamed global variable from generic `g_call_data` to module-prefixed `g_perf_call_data`, and removed unnecessary `(FAR void *)` cast on event argument, fixed alignment of continuation lines

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/sched/sched_perf.c b/sched/sched/sched_perf.c
index 10d59711010..aa7d03ddca9 100644
--- a/sched/sched/sched_perf.c
+++ b/sched/sched/sched_perf.c
@@ -163,6 +163,9 @@ static struct pmu_s g_perf_swevent =
   .ops = &g_perf_swevent_ops,
 };
 
+#ifdef CONFIG_SMP
+static struct smp_call_data_s g_call_data;
+#endif
 /****************************************************************************
  * Private Functions
  ****************************************************************************/
@@ -1406,7 +1409,7 @@ static int perf_task_function_call(FAR struct tcb_s *tcb,
 #ifdef CONFIG_SMP
   if (tcb->cpu != this_cpu())
     {
-      return nxsched_smp_call_single(tcb->cpu, func, event, true);
+      return nxsched_smp_call_single(tcb->cpu, func, event);
     }
 #endif
 
@@ -1436,7 +1439,7 @@ static int perf_cpu_function_call(int cpu, FAR struct perf_event_s *event,
 #ifdef CONFIG_SMP
   if (cpu != this_cpu())
     {
-      return nxsched_smp_call_single(cpu, func, event, true);
+      return nxsched_smp_call_single(cpu, func, event);
     }
 #endif
 
@@ -2343,8 +2346,10 @@ static void perf_swevent_timer_handle(wdparm_t arg)
 
   if (event->cpu != -1 && event->cpu != cpu)
... (11 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/sched/sched_perf.c b/sched/sched/sched_perf.c
index 7b67a6c0ad1..dc1faa2b443 100644
--- a/sched/sched/sched_perf.c
+++ b/sched/sched/sched_perf.c
@@ -163,6 +163,9 @@ static struct pmu_s g_perf_swevent =
   .ops = &g_perf_swevent_ops,
 };
 
+#ifdef CONFIG_SMP
+static struct smp_call_data_s g_perf_call_data;
+#endif
 /****************************************************************************
  * Private Functions
  ****************************************************************************/
@@ -1406,7 +1409,7 @@ static int perf_task_function_call(FAR struct tcb_s *tcb,
 #ifdef CONFIG_SMP
   if (tcb->cpu != this_cpu())
     {
-      return nxsched_smp_call_single(tcb->cpu, func, event, true);
+      return nxsched_smp_call_single(tcb->cpu, func, event);
     }
 #endif
 
@@ -1436,7 +1439,7 @@ static int perf_cpu_function_call(int cpu, FAR struct perf_event_s *event,
 #ifdef CONFIG_SMP
   if (cpu != this_cpu())
     {
-      return nxsched_smp_call_single(cpu, func, event, true);
+      return nxsched_smp_call_single(cpu, func, event);
     }
 #endif
 
@@ -2343,8 +2346,9 @@ static void perf_swevent_timer_handle(wdparm_t arg)
 
   if (event->cpu != -1 && event->cpu != cpu)
... (10 lines truncated)
```
</details>

### 12. design-philosophy/separation_of_concerns_retry_loop (1x) [design]

**Rule:** Keep low-level functions single-responsibility and stateless; push retry/loop policy to callers who understand the intent. A cancel primitive should report status (EBUSY), not embed wait-and-retry logic.

**Example:** #6043164 — sched/wqueue: Ensure wq_cancel_sync semantics.
**Transformation:** Moved retry loop from inside work_qcancel() to callers (work_cancel_sync/work_cancel_sync_wq). work_qcancel now returns -EBUSY with break on first busy worker instead of internally looping with sem_wait. Callers decide retry policy.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/wqueue/kwork_cancel.c b/sched/wqueue/kwork_cancel.c
index c59ed867b2a..33268e2ef5f 100644
--- a/sched/wqueue/kwork_cancel.c
+++ b/sched/wqueue/kwork_cancel.c
@@ -46,64 +46,71 @@ static int work_qcancel(FAR struct kwork_wqueue_s *wqueue, bool sync,
                         FAR struct work_s *work)
 {
   irqstate_t flags;
-  FAR sem_t *sync_wait = NULL;
+  FAR sem_t *sync_wait;
   int ret = -EINVAL;
 
   if (wqueue && work)
     {
-      ret = OK;
-      /* Cancelling the work is simply a matter of removing the work
-       * structure from the work queue. This must be done with interrupts
-       * disabled because new work is typically added to the work queue from
-       * interrupt handlers.
-       */
-
-      flags = spin_lock_irqsave(&wqueue->lock);
-
-      if (!work_available(work))
+      do
         {
-          /* If the head of the pending queue has changed, we should reset
-           * the wqueue timer.
+          sync_wait = NULL;
+          ret = OK;
+          /* Cancelling the work is simply a matter of removing the work
+           * structure from the work queue. This must be done with interrupts
+           * disabled because new work is typically added to the work queue
+           * from interrupt handlers.
            */
... (80 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/wqueue/kwork_cancel.c b/sched/wqueue/kwork_cancel.c
index c59ed867b2a..0f25c1e5eaf 100644
--- a/sched/wqueue/kwork_cancel.c
+++ b/sched/wqueue/kwork_cancel.c
@@ -94,6 +94,8 @@ static int work_qcancel(FAR struct kwork_wqueue_s *wqueue, bool sync,
                 {
                   worker[wndx].wait_count++;
                   sync_wait = &worker[wndx].wait;
+                  ret = -EBUSY;
+                  break;
                 }
             }
         }
@@ -169,13 +171,30 @@ int work_cancel_wq(FAR struct kwork_wqueue_s *wqueue,
 
 int work_cancel_sync(int qid, FAR struct work_s *work)
 {
-  return work_qcancel(work_qid2wq(qid), true, work);
+  struct kwork_wqueue_s *wqueue = work_qid2wq(qid);
+  int ret;
+
+  do
+    {
+      ret = work_qcancel(wqueue, true, work);
+    }
+  while (ret == -EBUSY);
+
+  return ret;
 }
 
 int work_cancel_sync_wq(FAR struct kwork_wqueue_s *wqueue,
                         FAR struct work_s *work)
 {
-  return work_qcancel(wqueue, true, work);
+  int ret;
... (12 lines truncated)
```
</details>

### 13. coding-style/incorrect_far_on_value_type (1x) [style]

**Rule:** The FAR qualifier is only for pointers to data in far memory segments; never apply FAR to scalar value types like pid_t, int, or other non-pointer types.

**Example:** #6016077 — fix nxsig_tcbdispatch use-after-free
**Transformation:** Removed incorrect FAR pointer qualifier from pid_t value types and updated comments to say 'pid' instead of 'TCB'.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/group/group_signal.c b/sched/group/group_signal.c
index e2fe450c37..5ee85b32b8 100644
--- a/sched/group/group_signal.c
+++ b/sched/group/group_signal.c
@@ -46,10 +46,10 @@
 struct group_signal_s
 {
   FAR siginfo_t *siginfo; /* Signal to be dispatched */
-  FAR struct tcb_s *dtcb; /* Default, valid TCB */
-  FAR struct tcb_s *utcb; /* TCB with this signal unblocked */
-  FAR struct tcb_s *atcb; /* This TCB was awakened */
-  FAR struct tcb_s *ptcb; /* This TCB received the signal */
+  FAR pid_t dpid; /* Default, valid TCB */
+  FAR pid_t upid; /* TCB with this signal unblocked */
+  FAR pid_t apid; /* This TCB was awakened */
+  FAR pid_t ppid; /* This TCB received the signal */
 };
 #endif
 
@@ -91,9 +91,9 @@ static int group_signal_handler(pid_t pid, FAR void *arg)
        * default.
        */
 
-      if (!info->dtcb)
+      if (!info->dpid)
         {
-          info->dtcb = tcb;
+          info->dpid = pid;
         }
 
       /* Is the thread waiting for this signal (in this case, the signal is
@@ -101,7 +101,7 @@ static int group_signal_handler(pid_t pid, FAR void *arg)
        */
 
       ret = nxsig_ismember(&tcb->sigwaitmask, info->siginfo->si_signo);
... (115 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/group/group_signal.c b/sched/group/group_signal.c
index e2fe450c37..c7915d7ef6 100644
--- a/sched/group/group_signal.c
+++ b/sched/group/group_signal.c
@@ -46,10 +46,10 @@
 struct group_signal_s
 {
   FAR siginfo_t *siginfo; /* Signal to be dispatched */
-  FAR struct tcb_s *dtcb; /* Default, valid TCB */
-  FAR struct tcb_s *utcb; /* TCB with this signal unblocked */
-  FAR struct tcb_s *atcb; /* This TCB was awakened */
-  FAR struct tcb_s *ptcb; /* This TCB received the signal */
+  pid_t dpid;             /* Default, valid pid */
+  pid_t upid;             /* pid with this signal unblocked */
+  pid_t apid;             /* This pid was awakened */
+  pid_t ppid;             /* This pid received the signal */
 };
 #endif
 
@@ -91,9 +91,9 @@ static int group_signal_handler(pid_t pid, FAR void *arg)
        * default.
        */
 
-      if (!info->dtcb)
+      if (!info->dpid)
         {
-          info->dtcb = tcb;
+          info->dpid = pid;
         }
 
       /* Is the thread waiting for this signal (in this case, the signal is
@@ -101,7 +101,7 @@ static int group_signal_handler(pid_t pid, FAR void *arg)
        */
 
       ret = nxsig_ismember(&tcb->sigwaitmask, info->siginfo->si_signo);
... (115 lines truncated)
```
</details>

### 14. root-cause-fix/fix_essential_not_symptom (1x) [correctness]

**Rule:** Each patch should contain only the minimal correct fix; do not bundle unrelated or unnecessary changes (like gratuitous casts) with a genuine bug fix.

**Example:** #5457491 — coredump: fixed coredump warning issue
**Transformation:** Reviewer accepted the printf format-string argument fix but rejected the unnecessary cast added to elf_exist_hdr, keeping only the real bug fix.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/misc/coredump.c b/sched/misc/coredump.c
index 6879190eed7..efea901d1ff 100644
--- a/sched/misc/coredump.c
+++ b/sched/misc/coredump.c
@@ -853,7 +853,7 @@ static void coredump_dump_mem(pid_t pid)
   FAR void *stream = &g_memstream;
   int ret;
 
-  if (elf_exist_hdr(&g_meminstream))
+  if (elf_exist_hdr((FAR struct lib_instream_s *)&g_meminstream))
     {
       _alert("Coredump memory device already exist:%s\n",
              CONFIG_BOARD_COREDUMP_DEVPATH);
@@ -1028,7 +1028,8 @@ int coredump_initialize(void)
                               O_RDONLY, 0666);
   if (ret < 0)
     {
-      _alert("%s Coredump instream could not be opened:%d\n", ret);
+      _alert("%s Coredump instream could not be opened:%d\n",
+             CONFIG_BOARD_COREDUMP_DEVPATH, ret);
       return ret;
     }
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/misc/coredump.c b/sched/misc/coredump.c
index 66067ef0b05..63582d3954d 100644
--- a/sched/misc/coredump.c
+++ b/sched/misc/coredump.c
@@ -1039,7 +1039,8 @@ int coredump_initialize(void)
                               O_RDONLY, 0666);
   if (ret < 0)
     {
-      _alert("%s Coredump instream could not be opened:%d\n", ret);
+      _alert("%s Coredump instream could not be opened:%d\n",
+             CONFIG_BOARD_COREDUMP_DEVPATH, ret);
       return ret;
     }
 

```
</details>

### 15. concurrency/unconditional_critical_section (1x) [correctness]

**Rule:** Critical sections protecting scheduler state and context switches must be unconditional; IRQ nesting on single-CPU can cause the same races as SMP if the critical section is compiled out.

**Example:** #5405411 — sched/signal: always add critical_section when context switch
**Transformation:** Removed #ifdef CONFIG_SMP guards around enter/leave_critical_section() so critical section is always taken, even on single-CPU with IRQ nesting enabled.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/sched/signal/sig_timedwait.c b/sched/signal/sig_timedwait.c
index 1563f51437b..dcceafc4976 100644
--- a/sched/signal/sig_timedwait.c
+++ b/sched/signal/sig_timedwait.c
@@ -78,20 +78,20 @@
 static void nxsig_timeout(wdparm_t arg)
 {
   FAR struct tcb_s *wtcb = (FAR struct tcb_s *)(uintptr_t)arg;
-#ifdef CONFIG_SMP
   irqstate_t flags;
 
   /* We must be in a critical section in order to call up_switch_context()
-   * below.  If we are running on a single CPU architecture, then we know
-   * interrupts a disabled an there is no need to explicitly call
-   * enter_critical_section().  However, in the SMP case,
+   * below.  If we are running on a single CPU architecture,  And there is 
+   * no irq nested support, then we know interrupts a disabled an there is
+   * no need to explicitly call enter_critical_section().  But if single CPU
+   * and irqnest enabled, we still need enter_critical_section() to ensure
+   * sched and tasks consistency.  However, in the SMP case,
    * enter_critical_section() does much more than just disable interrupts on
    * the local CPU; it also manages spinlocks to assure the stability of the
    * TCB that we are manipulating.
    */
 
   flags = enter_critical_section();
-#endif
 
   /* There may be a race condition -- make sure the task is
    * still waiting for a signal
@@ -127,9 +127,7 @@ static void nxsig_timeout(wdparm_t arg)
         }
     }
 
-#ifdef CONFIG_SMP
... (6 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/sched/signal/sig_timedwait.c b/sched/signal/sig_timedwait.c
index 1563f51437b..305c6cafacd 100644
--- a/sched/signal/sig_timedwait.c
+++ b/sched/signal/sig_timedwait.c
@@ -78,20 +78,20 @@
 static void nxsig_timeout(wdparm_t arg)
 {
   FAR struct tcb_s *wtcb = (FAR struct tcb_s *)(uintptr_t)arg;
-#ifdef CONFIG_SMP
   irqstate_t flags;
 
   /* We must be in a critical section in order to call up_switch_context()
-   * below.  If we are running on a single CPU architecture, then we know
-   * interrupts a disabled an there is no need to explicitly call
-   * enter_critical_section().  However, in the SMP case,
+   * below.  If we are running on a single CPU architecture,  And there is
+   * no irq nested support, then we know interrupts a disabled an there is
+   * no need to explicitly call enter_critical_section().  But if single CPU
+   * and irqnest enabled, we still need enter_critical_section() to ensure
+   * sched and tasks consistency.  However, in the SMP case,
    * enter_critical_section() does much more than just disable interrupts on
    * the local CPU; it also manages spinlocks to assure the stability of the
    * TCB that we are manipulating.
    */
 
   flags = enter_critical_section();
-#endif
 
   /* There may be a race condition -- make sure the task is
    * still waiting for a signal
@@ -127,9 +127,7 @@ static void nxsig_timeout(wdparm_t arg)
         }
     }
 
-#ifdef CONFIG_SMP
... (6 lines truncated)
```
</details>
