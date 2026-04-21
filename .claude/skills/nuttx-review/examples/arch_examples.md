# Architecture (arch/) Review Rules

Patterns mined from 342 review deltas in `arch/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| design-philosophy | 98 | 29% |
| coding-style | 71 | 21% |
| root-cause-fix | 48 | 14% |
| code-reuse | 45 | 13% |
| patch-organization | 43 | 13% |
| correctness | 13 | 4% |
| concurrency | 7 | 2% |
| variable-scope | 4 | 1% |
| posix-compliance | 4 | 1% |
| performance | 3 | 1% |
| memory-management | 3 | 1% |
| error-handling | 2 | 1% |
| thread-design | 1 | 0% |

## Severity Distribution

correctness: 127 | design: 152 | style: 61 | performance: 2

## Top Patterns

### 1. patch-organization/incomplete_feature_patch (2x) [design]

**Rule:** A feature patch must be self-contained: include documentation updates, follow Kconfig subsystem naming conventions (MM_KASAN_ prefix), add all necessary arch support code (e.g. MMU/TBI for SW_TAGS), and use consistent indentation (spaces not tabs in NuttX Makefiles).

**Example:** #3637068 — kasan: Implementation of Kasan based on software tags.
**Transformation:** Added missing documentation (kasan.rst), fixed Kconfig naming from KASAN_GENERIC/KASAN_SW_TAGS to MM_KASAN_GENERIC/MM_KASAN_SW_TAGS with proper subsystem prefix, added necessary arm64 MMU TBI support code, and fixed Makefile indentation from tabs to spaces.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm64/src/Toolchain.defs b/arch/arm64/src/Toolchain.defs
index 214b481d5e1..91addb9ffcc 100644
--- a/arch/arm64/src/Toolchain.defs
+++ b/arch/arm64/src/Toolchain.defs
@@ -81,7 +81,11 @@ ifeq ($(CONFIG_MM_UBSAN_TRAP_ON_ERROR),y)
 endif
 
 ifeq ($(CONFIG_MM_KASAN_ALL),y)
-  ARCHOPTIMIZATION += -fsanitize=kernel-address
+	ifeq ($(CONFIG_KASAN_GENERIC),y)
+    ARCHOPTIMIZATION += -fsanitize=kernel-address
+	else ifeq ($(CONFIG_KASAN_SW_TAGS),y)
+		ARCHOPTIMIZATION += -fsanitize=kernel-hwaddress
+	endif
 endif
 
 # Instrumentation options
diff --git a/arch/sim/src/sim/sim_heap.c b/arch/sim/src/sim/sim_heap.c
index 21d9228efec..39da824159b 100644
--- a/arch/sim/src/sim/sim_heap.c
+++ b/arch/sim/src/sim/sim_heap.c
@@ -230,6 +230,7 @@ struct mm_heap_s *mm_initialize(const char *name,
   procfs_register_meminfo(&heap->mm_procfs);
 #endif
 
+  kasan_enable();
   return heap;
 }
 
diff --git a/boards/arm64/qemu/qemu-armv8a/configs/nsh/defconfig b/boards/arm64/qemu/qemu-armv8a/configs/nsh/defconfig
index 102b2e3cc90..e9453fde58e 100644
--- a/boards/arm64/qemu/qemu-armv8a/configs/nsh/defconfig
+++ b/boards/arm64/qemu/qemu-armv8a/configs/nsh/defconfig
@@ -41,6 +41,7 @@ CONFIG_IDLETHREAD_STACKSIZE=8192
 CONFIG_INIT_ENTRYPOINT="nsh_main"
... (695 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/Documentation/guides/kasan.rst b/Documentation/guides/kasan.rst
index 3f3b1aeb047..14c29e9e80a 100644
--- a/Documentation/guides/kasan.rst
+++ b/Documentation/guides/kasan.rst
@@ -8,17 +8,23 @@ Overview
 Kernel Address Sanitizer (KASAN) is a dynamic memory safety error detector
 designed to find out-of-bounds and use-after-free bugs.
 
-The current version of NuttX has one modes:
+The current version of NuttX has two modes:
 
 1. Generic KASAN
+2. Software Tag-Based KASAN
 
-Generic KASAN, enabled with CONFIG_MM_KASAN, is the mode intended for
+Generic KASAN, enabled with CONFIG_MM_KASAN_GENERIC, is the mode intended for
 debugging, similar to linux user level ASan. This mode is supported on many CPU
 architectures, but it has significant performance and memory overheads.
 The current NuttX Generic KASAN can support memory out of bounds detection
 allocated by the default NuttX heap allocator,which depends on CONFIG_MM_DEFAULT_MANAGER
 or CONFIG_MM_TLSF_MANAGER, and detection of out of bounds with global variables.
 
+Software Tag-Based KASAN or SW_TAGS KASAN, enabled with CONFIG_MM_KASAN_SW_TAGS,
+can be used for both debugging, This mode is only supported for arm64,
+but its moderate memory overhead allows using it for testing on
+memory-restricted devices with real workloads.
+
 Support
 -------
 
@@ -27,20 +33,30 @@ Architectures
 
 Generic KASAN is supported on x86_64, arm, arm64, riscv, xtensa and so on.
 
+Software Tag-Based KASAN modes are supported only on arm64.
... (682 lines truncated)
```
</details>

### 2. patch-organization/unrelated_changes_in_patch (2x) [design]

**Rule:** Each patch should do exactly one thing; toolchain-specific additions (IAR VPATH, ARMCLANG vector objects) that are independent of the core linker option abstraction must be submitted as separate patches.

**Example:** #904757 — arm64: ARMv8-A/R support
**Transformation:** Removed unrelated IAR toolchain VPATH additions and arm_vectors.o EXTRA_LIBS line that were mixed into the ARMCLANG linker abstraction patch.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm64/src/Makefile b/arch/arm64/src/Makefile
index 4dd63205b57..bb70a41e84f 100644
--- a/arch/arm64/src/Makefile
+++ b/arch/arm64/src/Makefile
@@ -70,36 +70,67 @@ UOBJS = $(UAOBJS) $(UCOBJS)
 KBIN = libkarch$(LIBEXT)
 BIN  = libarch$(LIBEXT)
 
-LDFLAGS += $(addprefix -T,$(call CONVERT_PATH,$(ARCHSCRIPT))) $(EXTRALINKCMDS)
+$(foreach lib,$(notdir $(wildcard $(APPDIR)$(DELIM)staging$(DELIM)*$(LIBEXT))), \
+  $(foreach elib,$(EXTRA_LIBS), \
+    $(if $(filter $(notdir $(elib)),$(lib)), \
+      $(eval NAMEFULL_LIBS+=$(elib)), \
+      $(if $(filter $(notdir $(elib)),$(patsubst lib%$(LIBEXT),-l%,$(lib))), \
+        $(eval NAMESPEC_LIBS+=$(elib)) \
+       ) \
+     ) \
+   ) \
+ )
+
+EXTRA_LIBS := $(filter-out $(NAMEFULL_LIBS) $(NAMESPEC_LIBS),$(EXTRA_LIBS))
+EXTRA_LIBS += $(wildcard $(APPDIR)$(DELIM)staging$(DELIM)*$(LIBEXT))
 
 # Override in Make.defs if linker is not 'ld'
 
-ifeq ($(LD),$(CC))
-  LDSTARTGROUP ?= -Wl,--start-group
-  LDENDGROUP   ?= -Wl,--end-group
-  LDFLAGS      := $(addprefix -Xlinker ,$(LDFLAGS))
-  LDFLAGS      += $(CFLAGS)
+ifeq ($(CONFIG_ARM_TOOLCHAIN_ARMCLANG),)
+  ifeq ($(LD),$(CC))
+    LDSTARTGROUP ?= -Wl,--start-group
+    LDENDGROUP   ?= -Wl,--end-group
+    LDFLAGS      := $(addprefix -Xlinker ,$(LDFLAGS))
... (88 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm64/src/Makefile b/arch/arm64/src/Makefile
index 4dd63205b57..4fc0a56fd22 100644
--- a/arch/arm64/src/Makefile
+++ b/arch/arm64/src/Makefile
@@ -70,30 +70,54 @@ UOBJS = $(UAOBJS) $(UCOBJS)
 KBIN = libkarch$(LIBEXT)
 BIN  = libarch$(LIBEXT)
 
-LDFLAGS += $(addprefix -T,$(call CONVERT_PATH,$(ARCHSCRIPT))) $(EXTRALINKCMDS)
+$(foreach lib,$(notdir $(wildcard $(APPDIR)$(DELIM)staging$(DELIM)*$(LIBEXT))), \
+  $(foreach elib,$(EXTRA_LIBS), \
+    $(if $(filter $(notdir $(elib)),$(lib)), \
+      $(eval NAMEFULL_LIBS+=$(elib)), \
+      $(if $(filter $(notdir $(elib)),$(patsubst lib%$(LIBEXT),-l%,$(lib))), \
+        $(eval NAMESPEC_LIBS+=$(elib)) \
+       ) \
+     ) \
+   ) \
+ )
+
+EXTRA_LIBS := $(filter-out $(NAMEFULL_LIBS) $(NAMESPEC_LIBS),$(EXTRA_LIBS))
+EXTRA_LIBS += $(wildcard $(APPDIR)$(DELIM)staging$(DELIM)*$(LIBEXT))
 
 # Override in Make.defs if linker is not 'ld'
 
-ifeq ($(LD),$(CC))
-  LDSTARTGROUP ?= -Wl,--start-group
-  LDENDGROUP   ?= -Wl,--end-group
-  LDFLAGS      := $(addprefix -Xlinker ,$(LDFLAGS))
-  LDFLAGS      += $(CFLAGS)
+ifeq ($(CONFIG_ARM_TOOLCHAIN_ARMCLANG),)
+  ifeq ($(LD),$(CC))
+    LDSTARTGROUP ?= -Wl,--start-group
+    LDENDGROUP   ?= -Wl,--end-group
+    LDFLAGS      := $(addprefix -Xlinker ,$(LDFLAGS))
... (75 lines truncated)
```
</details>

### 3. patch-organization/incomplete_architecture_coverage (2x) [correctness]

**Rule:** When adding a new context field (like JB_FLAG) to a setjmp/longjmp register set, it must be added consistently across ALL architecture variants, not just the one you tested on; also never submit commented-out code as part of a feature patch.

**Example:** #883355 — sim: set timer irq as real timer
**Transformation:** Expanded IRQ flags save/restore from x86-only (PS1) to all architectures (x86_64, x86, ARM, ARM64), added proper function declaration in irq.h, and removed debug comment-outs of unrelated code (sim_timer_update, audio_register).

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/include/setjmp.h b/arch/sim/include/setjmp.h
index 20f8bdfbfee..78534697423 100644
--- a/arch/sim/include/setjmp.h
+++ b/arch/sim/include/setjmp.h
@@ -73,7 +73,8 @@
 #elif defined(CONFIG_HOST_X86) || defined(CONFIG_SIM_M32)
   /* Storage order: %ebx, %esi, %edi, %ebp, sp, and return PC */
 
-#  define XCPTCONTEXT_REGS    6
+#  define FLAGS_XCPT_REGS     2
+#  define XCPTCONTEXT_REGS    (6 + FLAGS_XCPT_REGS)
 #  define XCPTCONTEXT_SIZE    (4 * XCPTCONTEXT_REGS)
 
 #  ifdef __ASSEMBLY__
@@ -93,6 +94,8 @@
 #    define JB_EBP            (3)
 #    define JB_SP             (4)
 #    define JB_PC             (5)
+#    define JB_FLAG0          (6)
+#    define JB_FLAG1          (7)
 
 #  endif /* __ASSEMBLY__ */
 
diff --git a/arch/sim/src/sim/posix/sim_hostirq.c b/arch/sim/src/sim/posix/sim_hostirq.c
index 1426b0e3a02..ccedb14909c 100644
--- a/arch/sim/src/sim/posix/sim_hostirq.c
+++ b/arch/sim/src/sim/posix/sim_hostirq.c
@@ -101,6 +101,15 @@ void up_irq_restore(uint64_t flags)
   pthread_sigmask(SIG_SETMASK, &nmask.sigset, NULL);
 }
 
+uint64_t up_irq_flags(void)
+{
+  union sigset_u omask;
+
... (123 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/include/irq.h b/arch/sim/include/irq.h
index 5d1b343f81c..93397018d6e 100644
--- a/arch/sim/include/irq.h
+++ b/arch/sim/include/irq.h
@@ -115,6 +115,7 @@ int up_cpu_index(void);
  * leave_critical section(), are probably what you really want.
  */
 
+irqstate_t up_irq_flags(void);
 irqstate_t up_irq_save(void);
 void up_irq_restore(irqstate_t flags);
 
diff --git a/arch/sim/include/setjmp.h b/arch/sim/include/setjmp.h
index 20f8bdfbfee..7e510ef005c 100644
--- a/arch/sim/include/setjmp.h
+++ b/arch/sim/include/setjmp.h
@@ -37,7 +37,7 @@
 #if defined(CONFIG_HOST_X86_64) && !defined(CONFIG_SIM_M32)
   /* Storage order: %rbx, %rsp, %rbp, %r12, %r13, %r14, %r15, %rip */
 
-#  define XCPTCONTEXT_REGS    8
+#  define XCPTCONTEXT_REGS    9
 #  define XCPTCONTEXT_SIZE    (8 * XCPTCONTEXT_REGS)
 
 #  ifdef __ASSEMBLY__
@@ -50,6 +50,7 @@
 #    define JB_R14            (5*8)
 #    define JB_R15            (6*8)
 #    define JB_RSI            (7*8)
+#    define JB_FLAG           (8*8)
 
 #  else
 
@@ -61,6 +62,7 @@
 #    define JB_R14            (5)
... (225 lines truncated)
```
</details>

### 4. patch-organization/incomplete_arch_coverage (2x) [correctness]

**Rule:** When fixing a bug in arch-specific code that is duplicated across architectures, apply the same fix to every architecture copy in a single patch.

**Example:** #856553 — up_nputs: fix AddressSanitizer: global-buffer-overflow problem
**Transformation:** Expanded condition-order fix from sim-only to all 12 architecture implementations of up_nputs.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/src/sim/up_nputs.c b/arch/sim/src/sim/up_nputs.c
index 72e444996b8..9e92abf85b3 100644
--- a/arch/sim/src/sim/up_nputs.c
+++ b/arch/sim/src/sim/up_nputs.c
@@ -51,7 +51,7 @@
 
 void up_nputs(const char *str, size_t len)
 {
-  while (*str && len-- > 0)
+  while (len-- > 0 && *str)
     {
       up_putc(*str++);
     }

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/common/arm_nputs.c b/arch/arm/src/common/arm_nputs.c
index 316ef260d3b..1246e326c07 100644
--- a/arch/arm/src/common/arm_nputs.c
+++ b/arch/arm/src/common/arm_nputs.c
@@ -39,7 +39,7 @@
 
 void up_nputs(const char *str, size_t len)
 {
-  while (*str && len-- > 0)
+  while (len-- > 0 && *str)
     {
       up_putc(*str++);
     }
diff --git a/arch/arm64/src/common/arm64_nputs.c b/arch/arm64/src/common/arm64_nputs.c
index 41277451bf9..39587cafa12 100644
--- a/arch/arm64/src/common/arm64_nputs.c
+++ b/arch/arm64/src/common/arm64_nputs.c
@@ -39,7 +39,7 @@
 
 void up_nputs(const char *str, size_t len)
 {
-  while (*str && len-- > 0)
+  while (len-- > 0 && *str)
     {
       up_putc(*str++);
     }
diff --git a/arch/avr/src/common/up_nputs.c b/arch/avr/src/common/up_nputs.c
index 2c1e0c7667a..afc0049df62 100644
--- a/arch/avr/src/common/up_nputs.c
+++ b/arch/avr/src/common/up_nputs.c
@@ -51,7 +51,7 @@
 
 void up_nputs(const char *str, size_t len)
 {
-  while (*str && len-- > 0)
... (174 lines truncated)
```
</details>

### 5. coding-style/naming_convention_consistency (2x) [style]

**Rule:** Function names in arch/sim must follow the established short-form naming pattern of peer functions (e.g. `up_buttonevent`, `up_kbdevent`), and new files must not carry stale header comments copied from template files.

**Example:** #839929 — arch/sim: support simulator keyboard devices
**Transformation:** Renamed `up_keyboard_event` to `up_kbdevent` to match existing NuttX sim naming pattern (e.g. `up_buttonevent`), and fixed copy-paste error in file header comment from `up_touchscreen.c` to `up_keyboard.c`.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/Kconfig b/arch/sim/Kconfig
index fe5afbad7fb..85b8832d22c 100644
--- a/arch/sim/Kconfig
+++ b/arch/sim/Kconfig
@@ -341,6 +341,14 @@ config SIM_NOINPUT
 	bool "No input device"
 
 endchoice # Input Device
+
+config SIM_KEYBOARD
+	bool "X11 keyboard"
+	select INPUT_KEYBOARD
+	depends on SIM_X11FB
+	---help---
+		Support an X11 mouse-based keyboard emulation.  Also needs INPUT=y
+
 endif # if INPUT
 
 endmenu
diff --git a/arch/sim/src/Makefile b/arch/sim/src/Makefile
index ab3801d286d..3943b1876d0 100644
--- a/arch/sim/src/Makefile
+++ b/arch/sim/src/Makefile
@@ -131,6 +131,10 @@ else ifeq ($(CONFIG_SIM_AJOYSTICK),y)
 else ifeq ($(CONFIG_SIM_BUTTONS),y)
   HOSTSRCS += up_x11eventloop.c
 endif
+
+ifeq ($(CONFIG_SIM_KEYBOARD),y)
+  CSRCS += up_keyboard.c
+endif
 endif
 
 ifeq ($(CONFIG_SIM_IOEXPANDER),y)
diff --git a/arch/sim/src/sim/up_internal.h b/arch/sim/src/sim/up_internal.h
... (187 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/Kconfig b/arch/sim/Kconfig
index 4904370843f..f699e0f5301 100644
--- a/arch/sim/Kconfig
+++ b/arch/sim/Kconfig
@@ -341,6 +341,14 @@ config SIM_NOINPUT
 	bool "No input device"
 
 endchoice # Input Device
+
+config SIM_KEYBOARD
+	bool "X11 keyboard"
+	select INPUT_KEYBOARD
+	depends on SIM_X11FB
+	---help---
+		Support an X11 mouse-based keyboard emulation.  Also needs INPUT=y
+
 endif # if INPUT
 
 endmenu
diff --git a/arch/sim/src/Makefile b/arch/sim/src/Makefile
index 640370c0701..74672c22b7c 100644
--- a/arch/sim/src/Makefile
+++ b/arch/sim/src/Makefile
@@ -135,6 +135,10 @@ else ifeq ($(CONFIG_SIM_AJOYSTICK),y)
 else ifeq ($(CONFIG_SIM_BUTTONS),y)
   HOSTSRCS += up_x11eventloop.c
 endif
+
+ifeq ($(CONFIG_SIM_KEYBOARD),y)
+  CSRCS += up_keyboard.c
+endif
 endif
 
 ifeq ($(CONFIG_FS_FAT),y)
diff --git a/arch/sim/src/sim/up_internal.h b/arch/sim/src/sim/up_internal.h
... (186 lines truncated)
```
</details>

### 6. design-philosophy/proper_multicore_abstraction (1x) [design]

**Rule:** When adding multi-core support, generalize the architecture (symbols, macros, entry points) so each core is a first-class citizen, rather than duplicating single-core code behind #ifdef blocks with runtime core-index checks

**Example:** #4793749 — tc4xx: add multi cores startup support 1/3
**Transformation:** Replaced duplicated #ifdef CONFIG_TRICORE_BL/AP blocks with core-index checks and commented-out code by removing core-0 hardcoding entirely: linker symbols made generic (__USTACK0 → __USTACK), IRQ handler parameterized by CONFIG_CPU_COREID, nx_start() called unconditionally per-core, earlyserialinit guarded by USE_EARLYSERIALINIT

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/tricore/src/common/tricore_main.c b/arch/tricore/src/common/tricore_main.c
index b62107cd050..7a8b817ee2b 100644
--- a/arch/tricore/src/common/tricore_main.c
+++ b/arch/tricore/src/common/tricore_main.c
@@ -49,11 +49,23 @@ static void core_main(void)
   IfxCpu_emitEvent(&g_sync_event);
   IfxCpu_waitEvent(&g_sync_event, 1);
 
+#ifdef CONFIG_TRICORE_BL
   if (IfxCpu_getCoreIndex() == 0)
     {
       tricore_earlyserialinit();
       nx_start();
     }
+#endif
+
+#ifdef CONFIG_TRICORE_AP
+  if (IfxCpu_getCoreIndex() == 0)
+    {
+      nx_start();
+    }
+
+    Ifx_Ssw_infiniteLoop();
+    // nx_start();
+#endif
 
   while (1);
 }

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/tricore/src/common/tricore_doirq.c b/arch/tricore/src/common/tricore_doirq.c
index 49607a17df2..ec85c6cd11d 100644
--- a/arch/tricore/src/common/tricore_doirq.c
+++ b/arch/tricore/src/common/tricore_doirq.c
@@ -42,7 +42,12 @@
  * Public Functions
  ****************************************************************************/
 
+#ifndef CONFIG_ARCH_HAVE_MULTICPU
 IFX_INTERRUPT_INTERNAL(tricore_doirq, 0, 255)
+#else
+#define IFX_INT_WRAPPER(COREID) IFX_INTERRUPT_INTERNAL(tricore_doirq, COREID, 255)
+IFX_INT_WRAPPER(CONFIG_CPU_COREID)
+#endif
 {
   struct tcb_s **running_task = &g_running_tasks[this_cpu()];
   struct tcb_s *tcb;
diff --git a/arch/tricore/src/common/tricore_internal.h b/arch/tricore/src/common/tricore_internal.h
index 8581fb5f3a0..b1d64b43b80 100644
--- a/arch/tricore/src/common/tricore_internal.h
+++ b/arch/tricore/src/common/tricore_internal.h
@@ -168,17 +168,17 @@ typedef void (*up_vector_t)(void);
  * end of the heap is CONFIG_RAM_END
  */
 
-extern uintptr_t        __USTACK0_END[];
-extern uintptr_t        __USTACK0[];
-#define g_idle_topstack __USTACK0
+extern uintptr_t        __USTACK_END[];
+extern uintptr_t        __USTACK[];
+#define g_idle_topstack __USTACK
 
 /* Address of the saved user stack pointer */
 
 #if CONFIG_ARCH_INTERRUPTSTACK > 3
... (64 lines truncated)
```
</details>

### 7. patch-organization/unrelated_fixes_in_single_patch (1x) [design]

**Rule:** One patch does one thing — do not bundle unrelated fixes across different subsystems into a single commit, even if each fix is individually correct

**Example:** #6886626 — arm_cpu_psci: fix build error with clang
**Transformation:** Architect stripped 3 unrelated fixes (float cast in audio_fake, malloc_like1 param in fs_heap, NULL-to-0 in imgdata) leaving only the PRId32 format specifier fix in arm_cpu_psci.c

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm/src/armv7-a/arm_cpu_psci.c b/arch/arm/src/armv7-a/arm_cpu_psci.c
index ca5af515381..d9f140b04f1 100644
--- a/arch/arm/src/armv7-a/arm_cpu_psci.c
+++ b/arch/arm/src/armv7-a/arm_cpu_psci.c
@@ -125,7 +125,7 @@ static int psci_detect(void)
 {
   uint32_t ver = psci_get_version();
 
-  sinfo("Detected PSCI v%ld.%ld\n",
+  sinfo("Detected PSCI v%" PRId32 ".%" PRId32 "\n",
         PSCI_VERSION_MAJOR(ver), PSCI_VERSION_MINOR(ver));
 
   if (PSCI_VERSION_MAJOR(ver) == 0 && PSCI_VERSION_MINOR(ver) < 2)
diff --git a/drivers/audio/audio_fake.c b/drivers/audio/audio_fake.c
index cf05262e5e5..515aabee603 100644
--- a/drivers/audio/audio_fake.c
+++ b/drivers/audio/audio_fake.c
@@ -329,7 +329,7 @@ static int audio_fake_sin_data_init(FAR struct audio_lowerhalf_s *dev)
         {
           float t = (float)i / priv->sample_rate;
           float sample = sinf(2 * M_PI_F * freq * t);
-          data[i] = (int32_t)(sample * INT32_MAX);
+          data[i] = (int32_t)(sample * (float)INT32_MAX);
         }
 
         priv->sindata = data;
diff --git a/fs/fs_heap.h b/fs/fs_heap.h
index 050232de0af..774c64a413f 100644
--- a/fs/fs_heap.h
+++ b/fs/fs_heap.h
@@ -48,7 +48,7 @@ size_t    fs_large_malloc_size(FAR void *mem);
 FAR void *fs_large_realloc(FAR void *oldmem, size_t size)
           realloc_like(2);
 FAR void *fs_large_memalign(size_t alignment, size_t size)
-          malloc_like1(3);
... (18 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/armv7-a/arm_cpu_psci.c b/arch/arm/src/armv7-a/arm_cpu_psci.c
index ca5af515381..d9f140b04f1 100644
--- a/arch/arm/src/armv7-a/arm_cpu_psci.c
+++ b/arch/arm/src/armv7-a/arm_cpu_psci.c
@@ -125,7 +125,7 @@ static int psci_detect(void)
 {
   uint32_t ver = psci_get_version();
 
-  sinfo("Detected PSCI v%ld.%ld\n",
+  sinfo("Detected PSCI v%" PRId32 ".%" PRId32 "\n",
         PSCI_VERSION_MAJOR(ver), PSCI_VERSION_MINOR(ver));
 
   if (PSCI_VERSION_MAJOR(ver) == 0 && PSCI_VERSION_MINOR(ver) < 2)

```
</details>

### 8. correctness/overflow_safe_comparison (1x) [correctness]

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

### 9. design-philosophy/sensible_default_config (1x) [design]

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

### 10. error-handling/capture_return_value (1x) [correctness]

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

### 11. root-cause-fix/remove_unnecessary_conditional (1x) [design]

**Rule:** When a conditional compilation branch adds unnecessary complexity, remove the branching entirely rather than fixing the condition — simplify, don't patch

**Example:** #6188366 — tricore: doirq remove depend of CPU_COREID
**Transformation:** Developer changed the #ifdef condition from CONFIG_ARCH_HAVE_MULTICPU to CONFIG_CPU_COREID; architect removed the entire #ifdef/#else/#endif block, making IFX_INTERRUPT_INTERNAL unconditional with fixed core 0

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/tricore/src/common/tricore_doirq.c b/arch/tricore/src/common/tricore_doirq.c
index f97497decc1..e0887e3042e 100644
--- a/arch/tricore/src/common/tricore_doirq.c
+++ b/arch/tricore/src/common/tricore_doirq.c
@@ -43,7 +43,7 @@
  * Public Functions
  ****************************************************************************/
 
-#ifndef CONFIG_ARCH_HAVE_MULTICPU
+#ifndef CONFIG_CPU_COREID
 IFX_INTERRUPT_INTERNAL(tricore_doirq, 0, 255)
 #else
 #define IFX_INT_WRAPPER(COREID) IFX_INTERRUPT_INTERNAL(tricore_doirq, COREID, 255)

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/tricore/src/common/tricore_doirq.c b/arch/tricore/src/common/tricore_doirq.c
index f97497decc1..3943bac6901 100644
--- a/arch/tricore/src/common/tricore_doirq.c
+++ b/arch/tricore/src/common/tricore_doirq.c
@@ -43,12 +43,7 @@
  * Public Functions
  ****************************************************************************/
 
-#ifndef CONFIG_ARCH_HAVE_MULTICPU
 IFX_INTERRUPT_INTERNAL(tricore_doirq, 0, 255)
-#else
-#define IFX_INT_WRAPPER(COREID) IFX_INTERRUPT_INTERNAL(tricore_doirq, COREID, 255)
-IFX_INT_WRAPPER(CONFIG_CPU_COREID)
-#endif
 {
   uintptr_t istackbase = up_get_intstackbase(up_cpu_index());
   struct tcb_s **running_task = &g_running_task;

```
</details>

### 12. root-cause-fix/minimal_fix_over_broad_change (1x) [correctness]

**Rule:** Apply the minimum correct fix: clearing one register (LR) to terminate backtraces is sufficient — zeroing the entire saved register context (memset) is overkill, risks losing state needed by signal delivery, and mixes an unrelated behavioral change into a backtrace-cleanup patch.

**Example:** #6221675 — arm: fix unwind to invalid addresses in signal
**Transformation:** Reverted memcpy-to-memset change in armv8-m that zeroed the entire exception context frame; kept only the targeted REG_LR=0 fix consistent with all other architectures. Also fixed REG_LR alignment in armv8-r to match surrounding code.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm/src/arm/arm_schedulesigaction.c b/arch/arm/src/arm/arm_schedulesigaction.c
index a79b42b9300..85ccc362d2e 100644
--- a/arch/arm/src/arm/arm_schedulesigaction.c
+++ b/arch/arm/src/arm/arm_schedulesigaction.c
@@ -106,6 +106,7 @@ void up_schedule_sigaction(struct tcb_s *tcb)
    * disabled
    */
 
+  tcb->xcp.regs[REG_LR]    = 0; /* Avoid backtracing back. */
   tcb->xcp.regs[REG_PC]    = (uint32_t)arm_sigdeliver;
   tcb->xcp.regs[REG_CPSR]  = PSR_MODE_SYS | PSR_I_BIT | PSR_F_BIT;
 #ifdef CONFIG_ARM_THUMB
diff --git a/arch/arm/src/armv6-m/arm_schedulesigaction.c b/arch/arm/src/armv6-m/arm_schedulesigaction.c
index fb03912298d..d3976faeee0 100644
--- a/arch/arm/src/armv6-m/arm_schedulesigaction.c
+++ b/arch/arm/src/armv6-m/arm_schedulesigaction.c
@@ -132,6 +132,8 @@ void up_schedule_sigaction(struct tcb_s *tcb)
       tcb->xcp.regs[REG_LR]         = EXC_RETURN_THREAD;
       tcb->xcp.regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;
       tcb->xcp.regs[REG_CONTROL]    = getcontrol() & ~CONTROL_NPRIV;
+#else
+      tcb->xcp.regs[REG_LR]         = 0; /* Avoid backtracing back. */
 #endif
     }
 }
diff --git a/arch/arm/src/armv7-a/arm_schedulesigaction.c b/arch/arm/src/armv7-a/arm_schedulesigaction.c
index 33b7fcee5b2..b2a5a1d6825 100644
--- a/arch/arm/src/armv7-a/arm_schedulesigaction.c
+++ b/arch/arm/src/armv7-a/arm_schedulesigaction.c
@@ -108,6 +108,7 @@ void up_schedule_sigaction(struct tcb_s *tcb)
    * disabled
    */
 
+  tcb->xcp.regs[REG_LR]    = 0; /* Avoid backtracing back. */
   tcb->xcp.regs[REG_PC]    = (uint32_t)arm_sigdeliver;
... (62 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/arm/arm_schedulesigaction.c b/arch/arm/src/arm/arm_schedulesigaction.c
index a79b42b9300..85ccc362d2e 100644
--- a/arch/arm/src/arm/arm_schedulesigaction.c
+++ b/arch/arm/src/arm/arm_schedulesigaction.c
@@ -106,6 +106,7 @@ void up_schedule_sigaction(struct tcb_s *tcb)
    * disabled
    */
 
+  tcb->xcp.regs[REG_LR]    = 0; /* Avoid backtracing back. */
   tcb->xcp.regs[REG_PC]    = (uint32_t)arm_sigdeliver;
   tcb->xcp.regs[REG_CPSR]  = PSR_MODE_SYS | PSR_I_BIT | PSR_F_BIT;
 #ifdef CONFIG_ARM_THUMB
diff --git a/arch/arm/src/armv6-m/arm_schedulesigaction.c b/arch/arm/src/armv6-m/arm_schedulesigaction.c
index fb03912298d..d3976faeee0 100644
--- a/arch/arm/src/armv6-m/arm_schedulesigaction.c
+++ b/arch/arm/src/armv6-m/arm_schedulesigaction.c
@@ -132,6 +132,8 @@ void up_schedule_sigaction(struct tcb_s *tcb)
       tcb->xcp.regs[REG_LR]         = EXC_RETURN_THREAD;
       tcb->xcp.regs[REG_EXC_RETURN] = EXC_RETURN_THREAD;
       tcb->xcp.regs[REG_CONTROL]    = getcontrol() & ~CONTROL_NPRIV;
+#else
+      tcb->xcp.regs[REG_LR]         = 0; /* Avoid backtracing back. */
 #endif
     }
 }
diff --git a/arch/arm/src/armv7-a/arm_schedulesigaction.c b/arch/arm/src/armv7-a/arm_schedulesigaction.c
index 33b7fcee5b2..b2a5a1d6825 100644
--- a/arch/arm/src/armv7-a/arm_schedulesigaction.c
+++ b/arch/arm/src/armv7-a/arm_schedulesigaction.c
@@ -108,6 +108,7 @@ void up_schedule_sigaction(struct tcb_s *tcb)
    * disabled
    */
 
+  tcb->xcp.regs[REG_LR]    = 0; /* Avoid backtracing back. */
   tcb->xcp.regs[REG_PC]    = (uint32_t)arm_sigdeliver;
... (53 lines truncated)
```
</details>

### 13. coding-style/include_alphabetical_order (1x) [style]

**Rule:** Standard library includes must be sorted in alphabetical order within their group

**Example:** #6203143 — fix the compilation error caused by 'DEBUGASSERT'
**Transformation:** In lib_gdbstub.c, moved `#include <assert.h>` from after stdio.h to the top of the include block (before ctype.h), maintaining alphabetical order of standard headers

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm/src/csk6/csk6_irq.c b/arch/arm/src/csk6/csk6_irq.c
index dba50b91405..cd190bbf778 100644
--- a/arch/arm/src/csk6/csk6_irq.c
+++ b/arch/arm/src/csk6/csk6_irq.c
@@ -24,6 +24,7 @@
 
 #include <nuttx/config.h>
 #include <nuttx/arch.h>
+#include <assert.h>
 #include <debug.h>
 #include <arch/csk6/irq.h>
 
diff --git a/arch/arm/src/mx8mp/mx8mp_ipc.c b/arch/arm/src/mx8mp/mx8mp_ipc.c
index f66c2b0c220..f80af1cdb83 100644
--- a/arch/arm/src/mx8mp/mx8mp_ipc.c
+++ b/arch/arm/src/mx8mp/mx8mp_ipc.c
@@ -24,6 +24,7 @@
 
 #include "mx8mp_ipc.h"
 
+#include <assert.h>
 #include <debug.h>
 #include <inttypes.h>
 #include <nuttx/arch.h>
diff --git a/arch/risc-v/src/common/riscv_addrenv_pgmap.c b/arch/risc-v/src/common/riscv_addrenv_pgmap.c
index 0fda7c5a47e..a9563fd0424 100644
--- a/arch/risc-v/src/common/riscv_addrenv_pgmap.c
+++ b/arch/risc-v/src/common/riscv_addrenv_pgmap.c
@@ -24,6 +24,7 @@
 
 #include <nuttx/config.h>
 
+#include <assert.h>
 #include <nuttx/arch.h>
 #include <nuttx/addrenv.h>
... (63 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/csk6/csk6_irq.c b/arch/arm/src/csk6/csk6_irq.c
index 97525f89826..0ff44f2ef90 100644
--- a/arch/arm/src/csk6/csk6_irq.c
+++ b/arch/arm/src/csk6/csk6_irq.c
@@ -24,6 +24,7 @@
 
 #include <nuttx/config.h>
 #include <nuttx/arch.h>
+#include <assert.h>
 #include <debug.h>
 #include <arch/csk6/irq.h>
 
diff --git a/arch/arm/src/mx8mp/mx8mp_ipc.c b/arch/arm/src/mx8mp/mx8mp_ipc.c
index f66c2b0c220..f80af1cdb83 100644
--- a/arch/arm/src/mx8mp/mx8mp_ipc.c
+++ b/arch/arm/src/mx8mp/mx8mp_ipc.c
@@ -24,6 +24,7 @@
 
 #include "mx8mp_ipc.h"
 
+#include <assert.h>
 #include <debug.h>
 #include <inttypes.h>
 #include <nuttx/arch.h>
diff --git a/arch/risc-v/src/common/riscv_addrenv_pgmap.c b/arch/risc-v/src/common/riscv_addrenv_pgmap.c
index 0fda7c5a47e..a9563fd0424 100644
--- a/arch/risc-v/src/common/riscv_addrenv_pgmap.c
+++ b/arch/risc-v/src/common/riscv_addrenv_pgmap.c
@@ -24,6 +24,7 @@
 
 #include <nuttx/config.h>
 
+#include <assert.h>
 #include <nuttx/arch.h>
 #include <nuttx/addrenv.h>
... (63 lines truncated)
```
</details>

### 14. design-philosophy/signal_ignore_vs_block (1x) [correctness]

**Rule:** Use signal(SIG_IGN) to ignore unwanted signals rather than blocking them via sigmask — blocking defers delivery and pollutes shared signal sets, while SIG_IGN expresses the correct intent (discard) and is process-wide without side effects on other signal management

**Example:** #6035411 — sim: default ignore SIGPIPE
**Transformation:** Changed SIGPIPE handling from pthread_sigmask(SIG_BLOCK) via g_sigset to signal(SIGPIPE, SIG_IGN)

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/src/sim/posix/sim_hostirq.c b/arch/sim/src/sim/posix/sim_hostirq.c
index bb36f49539b..cdf282cb635 100644
--- a/arch/sim/src/sim/posix/sim_hostirq.c
+++ b/arch/sim/src/sim/posix/sim_hostirq.c
@@ -148,6 +148,11 @@ void up_irqinitialize(void)
 
   sigemptyset(&g_sigset);
 
+  /* Default mask SIGPIPE */
+
+  sigaddset(&g_sigset, SIGPIPE);
+  pthread_sigmask(SIG_BLOCK, &g_sigset, NULL);
+
 #ifdef CONFIG_SMP
   /* Register the pause handler */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/src/sim/posix/sim_hostirq.c b/arch/sim/src/sim/posix/sim_hostirq.c
index bb36f49539b..2d2868bef85 100644
--- a/arch/sim/src/sim/posix/sim_hostirq.c
+++ b/arch/sim/src/sim/posix/sim_hostirq.c
@@ -148,6 +148,10 @@ void up_irqinitialize(void)
 
   sigemptyset(&g_sigset);
 
+  /* Default ignore SIGPIPE */
+
+  signal(SIGPIPE, SIG_IGN);
+
 #ifdef CONFIG_SMP
   /* Register the pause handler */
 

```
</details>

### 15. root-cause-fix/signal_block_vs_ignore (1x) [correctness]

**Rule:** Use signal(SIG_IGN) to discard unwanted signals process-wide; do not block them via sigmask, which only defers delivery per-thread and contaminates shared signal sets used for other purposes

**Example:** #6035450 — sim: default ignore SIGPIPE
**Transformation:** Replaced pthread_sigmask(SIG_BLOCK) with signal(SIGPIPE, SIG_IGN) to properly ignore SIGPIPE instead of merely blocking it, and avoided polluting the shared g_sigset

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/src/sim/posix/sim_hostirq.c b/arch/sim/src/sim/posix/sim_hostirq.c
index bb36f49539b..cdf282cb635 100644
--- a/arch/sim/src/sim/posix/sim_hostirq.c
+++ b/arch/sim/src/sim/posix/sim_hostirq.c
@@ -148,6 +148,11 @@ void up_irqinitialize(void)
 
   sigemptyset(&g_sigset);
 
+  /* Default mask SIGPIPE */
+
+  sigaddset(&g_sigset, SIGPIPE);
+  pthread_sigmask(SIG_BLOCK, &g_sigset, NULL);
+
 #ifdef CONFIG_SMP
   /* Register the pause handler */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/src/sim/posix/sim_hostirq.c b/arch/sim/src/sim/posix/sim_hostirq.c
index bb36f49539b..2d2868bef85 100644
--- a/arch/sim/src/sim/posix/sim_hostirq.c
+++ b/arch/sim/src/sim/posix/sim_hostirq.c
@@ -148,6 +148,10 @@ void up_irqinitialize(void)
 
   sigemptyset(&g_sigset);
 
+  /* Default ignore SIGPIPE */
+
+  signal(SIGPIPE, SIG_IGN);
+
 #ifdef CONFIG_SMP
   /* Register the pause handler */
 

```
</details>
