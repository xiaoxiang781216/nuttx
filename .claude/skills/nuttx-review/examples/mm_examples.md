# Memory Management (mm/) Review Rules

Patterns mined from 90 review deltas in `mm/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| coding-style | 22 | 24% |
| root-cause-fix | 17 | 19% |
| design-philosophy | 17 | 19% |
| code-reuse | 12 | 13% |
| correctness | 8 | 9% |
| patch-organization | 5 | 6% |
| error-handling | 4 | 4% |
| memory-management | 2 | 2% |
| variable-scope | 2 | 2% |
| posix-compliance | 1 | 1% |

## Severity Distribution

correctness: 33 | design: 41 | style: 16 | performance: 0

## Top Patterns

### 1. root-cause-fix/fix_library_not_caller (2x) [correctness]

**Rule:** When a library function produces incorrect results, fix the library at its root rather than patching around it in every caller — this ensures all callers get correct behavior.

**Example:** #905661 — mm/circbuf: update buffer head according to real length of writing
**Transformation:** Moved the fix from the sensor driver caller into circbuf_overwrite itself, correctly tracking skipped bytes in the overwrite count and advancing circ->head so the circular buffer accurately reports overwritten data when input exceeds buffer size.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/sensors/sensor.c b/drivers/sensors/sensor.c
index 813b8c0a4e0..59562f69ad1 100644
--- a/drivers/sensors/sensor.c
+++ b/drivers/sensors/sensor.c
@@ -978,6 +978,11 @@ static ssize_t sensor_push_event(FAR void *priv, FAR const void *data,
         }
     }
 
+  if (bytes > circbuf_size(&upper->buffer))
+    {
+      envcount = circbuf_size(&upper->buffer) / upper->state.esize;
+    }
+
   circbuf_overwrite(&upper->buffer, data, bytes);
   sensor_generate_timing(upper, envcount);
   list_for_every_entry(&upper->userlist, user, struct sensor_user_s, node)

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/circbuf/circbuf.c b/mm/circbuf/circbuf.c
index 4010b2b1d4e..7de8b46bf83 100644
--- a/mm/circbuf/circbuf.c
+++ b/mm/circbuf/circbuf.c
@@ -501,6 +501,7 @@ ssize_t circbuf_overwrite(FAR struct circbuf_s *circ,
                           FAR const void *src, size_t bytes)
 {
   size_t overwrite = 0;
+  size_t skip = 0;
   size_t space;
   size_t off;
 
@@ -514,16 +515,18 @@ ssize_t circbuf_overwrite(FAR struct circbuf_s *circ,
 
   if (bytes > circ->size)
     {
-      src = (FAR const void *)((FAR char *)src + bytes - circ->size);
+      skip = bytes - circ->size;
+      src = (FAR const void *)((FAR char *)src + skip);
       bytes = circ->size;
     }
 
   space = circbuf_space(circ);
   if (bytes > space)
     {
-      overwrite = bytes - space;
+      overwrite = bytes - space + skip;
     }
 
+  circ->head += skip;
   off = circ->head % circ->size;
   space = circ->size - off;
   if (bytes < space)

```
</details>

### 2. root-cause-fix/hack_replaced_with_proper_api_extension (1x) [correctness]

**Rule:** Do not paper over a broken abstraction with a magic-constant hack; fix the abstraction itself (here: use offsetof for correct layout calculation, thread the value through an existing unused field, and fix all dependent macros consistently)

**Example:** #7190477 — mm/iob: Solve some issues under multi-level IOB
**Transformation:** Developer used IOB_ALIGN_SIZE(0) hack to sidestep circular sizeof dependency in IOB_BUFSIZE; architect instead fixed IOB_ALIGN_SIZE to use offsetof(struct iob_s, io_data), stored actual blksize in mempool priv field for O(1) lookup, fixed IOB_FREESPACE to use IOB_BUFSIZE(p) instead of hardcoded CONFIG_IOB_BUFSIZE, and extended mempool macros with priv parameter

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/mm/iob.h b/include/nuttx/mm/iob.h
index 67624ec3e6..96684d228d 100644
--- a/include/nuttx/mm/iob.h
+++ b/include/nuttx/mm/iob.h
@@ -120,7 +120,7 @@
 #define IOB_FLAG_TEST(iob, flag) (IOB_FLAG_GET(iob) & (flag))
 
 #if CONFIG_IOB_NPOOLS > 1
-#  define IOB_BUFSIZE(p) ((p)->io_pool->blocksize - IOB_ALIGN_SIZE(sizeof(struct iob_s)))
+#  define IOB_BUFSIZE(p) ((p)->io_pool->blocksize - IOB_ALIGN_SIZE(0))
 #else
 #  define IOB_BUFSIZE(p) CONFIG_IOB_BUFSIZE
 #endif

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/mm/iob.h b/include/nuttx/mm/iob.h
index 67624ec3e6..b4b55a78c2 100644
--- a/include/nuttx/mm/iob.h
+++ b/include/nuttx/mm/iob.h
@@ -28,8 +28,10 @@
  ****************************************************************************/
 
 #include <nuttx/config.h>
+#include <nuttx/compiler.h>
 
 #include <stdint.h>
+#include <stddef.h>
 #include <stdbool.h>
 #include <sys/param.h>
 #include <nuttx/mm/mm.h>
@@ -82,13 +84,13 @@
 /* Default config of alignment and head padding size */
 
 #define IOB_ALIGNMENT           MAX(CONFIG_IOB_ALIGNMENT, MM_ALIGN)
-#define IOB_ALIGN_SIZE(blksize) ALIGN_UP(sizeof(struct iob_s) + blksize - 1, \
-                                         IOB_ALIGNMENT)
+#define IOB_ALIGN_SIZE(blksize) ALIGN_UP(offsetof(struct iob_s, io_data) + \
+                                         blksize, IOB_ALIGNMENT)
 
 /* IOB helpers */
 
 #define IOB_DATA(p)      (&(p)->io_data[(p)->io_offset])
-#define IOB_FREESPACE(p) (CONFIG_IOB_BUFSIZE - (p)->io_len - (p)->io_offset)
+#define IOB_FREESPACE(p) (IOB_BUFSIZE(p) - (p)->io_len - (p)->io_offset)
 
 /* Queue helpers */
 
@@ -120,7 +122,7 @@
 #define IOB_FLAG_TEST(iob, flag) (IOB_FLAG_GET(iob) & (flag))
 
... (110 lines truncated)
```
</details>

### 3. root-cause-fix/proper_lock_lifecycle_instead_of_bailout (1x) [correctness]

**Rule:** Fix the locking design at the source rather than adding bailout guards or disabling features for specific configurations; when a function is called from both normal and interrupt context, implement proper context-aware lock management instead of silently skipping work

**Example:** #6948105 — mm/mm_foreach:update check for mm_foreach
**Transformation:** Replaced simple early-return deadlock avoidance and SMP #ifdef workaround with proper lock acquisition logic in mm_foreach (interrupt context detection, SMP-aware lock skip, locked flag for conditional unlock), moved mm_free_delaylist out of mm_foreach to individual callers (mm_checkcorruption, mm_mallinfo, mm_memdump) where context-appropriate guards can be applied, and dropped the irq_dispatch.c SMP exclusion entirely

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/mm/mm_heap/mm_foreach.c b/mm/mm_heap/mm_foreach.c
index 7432b3d7058..6dd88926877 100644
--- a/mm/mm_heap/mm_foreach.c
+++ b/mm/mm_heap/mm_foreach.c
@@ -29,6 +29,7 @@
 #include <assert.h>
 #include <debug.h>
 
+#include <nuttx/arch.h>
 #include <nuttx/mm/kasan.h>
 #include <nuttx/mm/mm.h>
 
@@ -60,6 +61,16 @@ void mm_foreach(FAR struct mm_heap_s *heap, mm_node_handler_t handler,
 #endif
 
   DEBUGASSERT(handler);
+
+  /* In interrupt context, check if the lock is already held.
+   * If held by another context, skip the check to avoid deadlock.
+   */
+
+  if (up_interrupt_context() && nxrmutex_is_locked(&heap->mm_lock))
+    {
+      return;
+    }
+
   mm_free_delaylist(heap);
 
   /* Visit each region */
diff --git a/sched/irq/irq_dispatch.c b/sched/irq/irq_dispatch.c
index e92c75ac5b3..edee05761af 100644
--- a/sched/irq/irq_dispatch.c
+++ b/sched/irq/irq_dispatch.c
@@ -126,7 +126,7 @@ static DEFINE_PER_CPU_BSS(atomic_t, g_irq_level);
 
... (17 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/mm_heap/mm_checkcorruption.c b/mm/mm_heap/mm_checkcorruption.c
index 8e7ea2a4e35..d23529d2930 100644
--- a/mm/mm_heap/mm_checkcorruption.c
+++ b/mm/mm_heap/mm_checkcorruption.c
@@ -78,5 +78,10 @@ static void checkcorruption_handler(FAR struct mm_allocnode_s *node,
 
 void mm_checkcorruption(FAR struct mm_heap_s *heap)
 {
+  if (!up_interrupt_context())
+    {
+      mm_free_delaylist(heap);
+    }
+
   mm_foreach(heap, checkcorruption_handler, NULL);
 }
diff --git a/mm/mm_heap/mm_foreach.c b/mm/mm_heap/mm_foreach.c
index 7432b3d7058..f0d0d4a3550 100644
--- a/mm/mm_heap/mm_foreach.c
+++ b/mm/mm_heap/mm_foreach.c
@@ -29,8 +29,11 @@
 #include <assert.h>
 #include <debug.h>
 
+#include <nuttx/arch.h>
 #include <nuttx/mm/kasan.h>
 #include <nuttx/mm/mm.h>
+#include <nuttx/mutex.h>
+#include <sched.h>
 
 #include "mm_heap/mm.h"
 
@@ -53,6 +56,7 @@ void mm_foreach(FAR struct mm_heap_s *heap, mm_node_handler_t handler,
   FAR struct mm_allocnode_s *prev;
   size_t nodesize;
   bool bypass;
... (89 lines truncated)
```
</details>

### 4. patch-organization/separate_feature_from_board_config (1x) [design]

**Rule:** A patch introducing a new subsystem feature (e.g., pool-only memory manager) must not bundle board-specific defconfig changes or unrelated config tweaks — submit board enablement as a separate follow-up patch

**Example:** #6212730 — MM_POOL_MANAGER feature, support mempool only for mm
**Transformation:** Removed all board defconfig changes (qemu-armv8r-aarch32 nsh/pnsh configs, cmake dequotelist) from the patch, keeping only the core mm/pool_heap feature implementation (new Kconfig option MM_POOL_MANAGER, pool_heap.c, build files). Renamed Kconfig from MM_MANAGER_SAFE to MM_POOL_MANAGER with proper placement.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/boards/arm/qemu/qemu-armv8r-aarch32/configs/nsh/defconfig b/boards/arm/qemu/qemu-armv8r-aarch32/configs/nsh/defconfig
index 4978b6701ed..f5b6de94144 100644
--- a/boards/arm/qemu/qemu-armv8r-aarch32/configs/nsh/defconfig
+++ b/boards/arm/qemu/qemu-armv8r-aarch32/configs/nsh/defconfig
@@ -6,13 +6,16 @@
 # modifications.
 #
 CONFIG_ARCH="arm"
+CONFIG_ARCH_ADDRENV=y
 CONFIG_ARCH_BOARD="qemu-armv8r-aarch32"
 CONFIG_ARCH_BOARD_QEMU_ARMV8R_AARCH32=y
 CONFIG_ARCH_CHIP="qemu"
 CONFIG_ARCH_CHIP_QEMU_ARM=y
 CONFIG_ARCH_CHIP_QEMU_CORTEXR52=y
 CONFIG_ARCH_INTERRUPTSTACK=2048
+CONFIG_ARCH_KSTACK_PROTECT=y
 CONFIG_ARCH_LOWVECTORS=y
+CONFIG_ARCH_STACK_PROTECT=y
 CONFIG_ARM_GIC_VERSION=2
 CONFIG_ARM_MPU=y
 CONFIG_ARM_SEMIHOSTING_POWEROFF=y
@@ -25,12 +28,14 @@ CONFIG_DEBUG_FEATURES=y
 CONFIG_DEBUG_FULLOPT=y
 CONFIG_DEBUG_SYMBOLS=y
 CONFIG_DEFAULT_TASK_STACKSIZE=4096
-CONFIG_EXAMPLES_HELLO=y
+CONFIG_DISABLE_ENVIRON=y
+CONFIG_DISABLE_MQUEUE=y
+CONFIG_DISABLE_PTHREAD=y
+CONFIG_DISABLE_SIGNALS=y
+CONFIG_E2E_LIBRARY=y
 CONFIG_FLASH_SIZE=1048576
 CONFIG_FS_PROCFS=y
 CONFIG_FS_ROMFS=y
-CONFIG_HAVE_CXX=y
... (2834 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/Kconfig b/mm/Kconfig
index 0f9c8988b5a..6990b901884 100644
--- a/mm/Kconfig
+++ b/mm/Kconfig
@@ -17,6 +17,11 @@ config MM_TLSF_MANAGER
 	---help---
 		TLSF memory manager strategy.
 
+config MM_POOL_MANAGER
+	bool "Pool only memory manager"
+	---help---
+		NuttX memory manager strategy for mempool only.
+
 config MM_CUSTOMIZE_MANAGER
 	bool "Customized heap manager"
 	---help---
diff --git a/mm/Makefile b/mm/Makefile
index 53eb8bd72d2..59846110068 100644
--- a/mm/Makefile
+++ b/mm/Makefile
@@ -36,6 +36,7 @@ include ubsan/Make.defs
 include tlsf/Make.defs
 include map/Make.defs
 include kmap/Make.defs
+include pool_heap/Make.defs
 
 BINDIR ?= bin
 
diff --git a/mm/pool_heap/CMakeLists.txt b/mm/pool_heap/CMakeLists.txt
new file mode 100644
index 00000000000..ff94b246114
--- /dev/null
+++ b/mm/pool_heap/CMakeLists.txt
@@ -0,0 +1,30 @@
+# ##############################################################################
... (863 lines truncated)
```
</details>

### 5. root-cause-fix/fix_macro_not_callsite (1x) [correctness]

**Rule:** When a disabled-feature stub macro causes unused-variable warnings by expanding to its argument, fix the macro definition to not reference the parameter rather than suppressing warnings at every call site.

**Example:** #5326110 — mm/kasan: fix wanring expression result unused
**Transformation:** Instead of wrapping kasan_bypass(flag) with UNUSED() at the call site, fixed the kasan_bypass stub macro to not reference its argument (returning false instead of state), and added a separate UNUSED(flag) to suppress the now-truly-unused variable warning.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/mm/mm_heap/mm_malloc_size.c b/mm/mm_heap/mm_malloc_size.c
index 895f11ee2bd..a2de542b1ee 100644
--- a/mm/mm_heap/mm_malloc_size.c
+++ b/mm/mm_heap/mm_malloc_size.c
@@ -74,6 +74,6 @@ size_t mm_malloc_size(FAR struct mm_heap_s *heap, FAR void *mem)
 
   size = MM_SIZEOF_NODE(node) - MM_ALLOCNODE_OVERHEAD;
 
-  kasan_bypass(flag);
+  UNUSED(kasan_bypass(flag));
   return size;
 }

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/mm/kasan.h b/include/nuttx/mm/kasan.h
index 4d686b68004..cdc54e9a036 100644
--- a/include/nuttx/mm/kasan.h
+++ b/include/nuttx/mm/kasan.h
@@ -48,7 +48,7 @@
 #  define kasan_stop()
 #  define kasan_debugpoint(t,a,s) 0
 #  define kasan_init_early()
-#  define kasan_bypass(state) (state)
+#  define kasan_bypass(state) false
 #else
 
 #  define kasan_init_early() kasan_stop()
diff --git a/mm/mm_heap/mm_malloc_size.c b/mm/mm_heap/mm_malloc_size.c
index 895f11ee2bd..544b0e306e4 100644
--- a/mm/mm_heap/mm_malloc_size.c
+++ b/mm/mm_heap/mm_malloc_size.c
@@ -75,5 +75,6 @@ size_t mm_malloc_size(FAR struct mm_heap_s *heap, FAR void *mem)
   size = MM_SIZEOF_NODE(node) - MM_ALLOCNODE_OVERHEAD;
 
   kasan_bypass(flag);
+  UNUSED(flag);
   return size;
 }

```
</details>

### 6. coding-style/use_header_not_extern_declaration (1x) [design]

**Rule:** Never add standalone extern declarations for functions that already have a canonical header; include that header instead to maintain single-source-of-truth for declarations.

**Example:** #5326041 — mm/record_pid: fix nxsched_verify_pid not declared
**Transformation:** Replaced inline extern function declaration in mm.h with proper #include <nuttx/sched.h> in the .c files that need nxsched_verify_pid.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/mm/mm.h b/include/nuttx/mm/mm.h
index 5c7c336df7b..c6a474d13df 100644
--- a/include/nuttx/mm/mm.h
+++ b/include/nuttx/mm/mm.h
@@ -137,6 +137,7 @@
 #endif
 
 #ifdef CONFIG_MM_RECORD_PID
+extern bool nxsched_verify_pid(pid_t pid);
 #  define MM_DUMP_ALLOC(dump, node) \
     ((node) != NULL && (dump)->pid == PID_MM_ALLOC && \
      (node)->pid != PID_MM_MEMPOOL)

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/mm_heap/mm_mallinfo.c b/mm/mm_heap/mm_mallinfo.c
index 8a06cd6843e..4dbdbd68187 100644
--- a/mm/mm_heap/mm_mallinfo.c
+++ b/mm/mm_heap/mm_mallinfo.c
@@ -29,6 +29,7 @@
 #include <assert.h>
 #include <debug.h>
 
+#include <nuttx/sched.h>
 #include <nuttx/mm/mm.h>
 
 #include "mm_heap/mm.h"
diff --git a/mm/mm_heap/mm_memdump.c b/mm/mm_heap/mm_memdump.c
index d78e6d734fe..fa8ec4159f2 100644
--- a/mm/mm_heap/mm_memdump.c
+++ b/mm/mm_heap/mm_memdump.c
@@ -31,6 +31,7 @@
 #include <debug.h>
 #include <execinfo.h>
 #include <sys/prctl.h>
+#include <nuttx/sched.h>
 #include <nuttx/mm/mm.h>
 
 #include "mm_heap/mm.h"

```
</details>

### 7. code-reuse/consistent_naming_with_existing_api (1x) [design]

**Rule:** When converting internal macros to public functions, preserve the original macro names rather than inventing new names, ensuring API consistency and minimizing churn for existing callers.

**Example:** #5112941 — kasan: fix realloc memcpy tags check error
**Transformation:** Renamed `kasan_get_tagged_addr` to `kasan_set_tag` and `kasan_reset_tag` to `kasan_clear_tag` to maintain consistent naming with the existing macro-based API (`kasan_set_tag`/`kasan_get_tag`) being promoted to functions.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/mm/kasan.h b/include/nuttx/mm/kasan.h
index a349eb9675b..c0a7ba7a1f6 100644
--- a/include/nuttx/mm/kasan.h
+++ b/include/nuttx/mm/kasan.h
@@ -145,6 +145,25 @@ void kasan_unregister(FAR void *addr);
 
 FAR void *kasan_reset_tag(FAR const void *addr);
 
+/****************************************************************************
+ * Name: kasan_get_tag
+ *
+ * Input Parameters:
+ *   addr - The address of the memory to get the tag.
+ *
+ * Returned Value:
+ *   address tag
+ *
+ ****************************************************************************/
+
+uint8_t kasan_get_tag(FAR const void *addr);
+
+/****************************************************************************
+ * Name: kasan_get_tagged_addr
+ ****************************************************************************/
+
+FAR void *kasan_get_tagged_addr(FAR const void *addr, uint8_t tag);
+
 /****************************************************************************
  * Name: kasan_start
  *
diff --git a/mm/kasan/generic.c b/mm/kasan/generic.c
index b5b6d3ee561..0af96991613 100644
--- a/mm/kasan/generic.c
+++ b/mm/kasan/generic.c
@@ -209,6 +209,16 @@ FAR void *kasan_reset_tag(FAR const void *addr)
... (79 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/mm/kasan.h b/include/nuttx/mm/kasan.h
index aad36e45c98..76f19cd482f 100644
--- a/include/nuttx/mm/kasan.h
+++ b/include/nuttx/mm/kasan.h
@@ -41,6 +41,8 @@
 #  define kasan_unpoison(addr, size) addr
 #  define kasan_register(addr, size)
 #  define kasan_unregister(addr)
+#  define kasan_get_tag(addr) 0
+#  define kasan_set_tag(addr, tag) addr
 #  define kasan_clear_tag(addr) addr
 #  define kasan_start()
 #  define kasan_stop()
@@ -132,6 +134,12 @@ void kasan_register(FAR void *addr, FAR size_t *size);
 
 void kasan_unregister(FAR void *addr);
 
+/****************************************************************************
+ * Name: kasan_set_tag
+ ****************************************************************************/
+
+FAR void *kasan_set_tag(FAR const void *addr, uint8_t tag);
+
 /****************************************************************************
  * Name: kasan_clear_tag
  *
@@ -145,6 +153,19 @@ void kasan_unregister(FAR void *addr);
 
 FAR void *kasan_clear_tag(FAR const void *addr);
 
+/****************************************************************************
+ * Name: kasan_get_tag
+ *
+ * Input Parameters:
+ *   addr - The address of the memory to get the tag.
... (98 lines truncated)
```
</details>

### 8. root-cause-fix/incomplete_guard_migration (1x) [correctness]

**Rule:** When migrating a preprocessor guard to a new config symbol, audit ALL occurrences across every code path for consistency — partial migration leaves some paths gated on the wrong condition, causing compile errors or dead code when configs diverge.

**Example:** #4994287 — mm: fix missed seqno change
**Transformation:** PS1 inconsistently changed some seqno-related guards to `CONFIG_MM_BACKTRACE > 0` while others to `CONFIG_MM_BACKTRACE_SEQNO`; FINAL uniformly uses `CONFIG_MM_BACKTRACE_SEQNO` for all seqno-dependent code paths and extends the change to missed cases (leak, mempool, biggest, orphan).

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/procfs/fs_procfsmeminfo.c b/fs/procfs/fs_procfsmeminfo.c
index 513aa28bf09..952071021f9 100644
--- a/fs/procfs/fs_procfsmeminfo.c
+++ b/fs/procfs/fs_procfsmeminfo.c
@@ -433,7 +433,10 @@ static ssize_t memdump_read(FAR struct file *filep, FAR char *buffer,
                   "/on/off"
 #endif
 #if CONFIG_MM_BACKTRACE >= 0
-                 "/leak/pid> <seqmin> <seqmax"
+                 "/leak/pid"
+#endif
+#ifdef CONFIG_MM_BACKTRACE_SEQNO
+                 "> <seqmin> <seqmax"
 #endif
                  ">\n"
                  "used: dump all allocated node\n"
@@ -478,7 +481,7 @@ static ssize_t memdump_write(FAR struct file *filep, FAR const char *buffer,
   struct mm_memdump_s dump =
     {
       PID_MM_ALLOC,
-#if CONFIG_MM_BACKTRACE >= 0
+#ifdef CONFIG_MM_BACKTRACE_SEQNO
       0,
       ULONG_MAX
 #endif
@@ -550,7 +553,7 @@ static ssize_t memdump_write(FAR struct file *filep, FAR const char *buffer,
       case 'u':
         dump.pid = PID_MM_ALLOC;
 
-#if CONFIG_MM_BACKTRACE >= 0
+#if CONFIG_MM_BACKTRACE > 0
         p = (FAR char *)buffer + 4;
         goto dump;
 #endif
@@ -559,7 +562,7 @@ static ssize_t memdump_write(FAR struct file *filep, FAR const char *buffer,
... (127 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/procfs/fs_procfsmeminfo.c b/fs/procfs/fs_procfsmeminfo.c
index 513aa28bf09..19adffa50bd 100644
--- a/fs/procfs/fs_procfsmeminfo.c
+++ b/fs/procfs/fs_procfsmeminfo.c
@@ -433,7 +433,10 @@ static ssize_t memdump_read(FAR struct file *filep, FAR char *buffer,
                   "/on/off"
 #endif
 #if CONFIG_MM_BACKTRACE >= 0
-                 "/leak/pid> <seqmin> <seqmax"
+                 "/leak/pid"
+#endif
+#ifdef CONFIG_MM_BACKTRACE_SEQNO
+                 "> <seqmin> <seqmax"
 #endif
                  ">\n"
                  "used: dump all allocated node\n"
@@ -478,7 +481,7 @@ static ssize_t memdump_write(FAR struct file *filep, FAR const char *buffer,
   struct mm_memdump_s dump =
     {
       PID_MM_ALLOC,
-#if CONFIG_MM_BACKTRACE >= 0
+#ifdef CONFIG_MM_BACKTRACE_SEQNO
       0,
       ULONG_MAX
 #endif
@@ -550,7 +553,7 @@ static ssize_t memdump_write(FAR struct file *filep, FAR const char *buffer,
       case 'u':
         dump.pid = PID_MM_ALLOC;
 
-#if CONFIG_MM_BACKTRACE >= 0
+#ifdef CONFIG_MM_BACKTRACE_SEQNO
         p = (FAR char *)buffer + 4;
         goto dump;
 #endif
@@ -559,7 +562,7 @@ static ssize_t memdump_write(FAR struct file *filep, FAR const char *buffer,
... (169 lines truncated)
```
</details>

### 9. code-reuse/consolidate_symmetric_operations (1x) [design]

**Rule:** When alloc and free paths perform symmetric bookkeeping (magic checks, pid/seqno updates, backtrace recording), consolidate them into one function with a direction parameter rather than duplicating logic at each call site.

**Example:** #4430989 — mm: add backtrace for free block
**Transformation:** Unified alloc and free backtrace tracking into a single function by adding an `alloc` parameter, eliminating duplicated magic/seqno logic scattered across mempool_release and mm_free paths.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/mm/mempool/mempool.c b/mm/mempool/mempool.c
index 10d3de49398..1b5e8957dd7 100644
--- a/mm/mempool/mempool.c
+++ b/mm/mempool/mempool.c
@@ -105,10 +105,20 @@ static inline void mempool_add_backtrace(FAR struct mempool_s *pool,
                                          FAR struct mempool_backtrace_s *buf,
                                          bool alloc)
 {
-  DEBUGASSERT(buf->magic == MEMPOOL_MAGIC_FREE);
-  buf->magic = MEMPOOL_MAGIC_ALLOC;
+  if (alloc)
+    {
+      DEBUGASSERT(buf->magic == MEMPOOL_MAGIC_FREE);
+      buf->magic = MEMPOOL_MAGIC_ALLOC;
+      buf->seqno = g_mm_seqno++;
+    }
+  else
+    {
+      DEBUGASSERT(buf->magic == MEMPOOL_MAGIC_ALLOC);
+      buf->magic = MEMPOOL_MAGIC_FREE;
+    }
+
   buf->pid = _SCHED_GETTID();
-  buf->seqno = g_mm_seqno++;
+
 #  if CONFIG_MM_BACKTRACE > 0
   if (pool->procfs.backtrace)
     {
@@ -429,15 +439,6 @@ void mempool_release(FAR struct mempool_s *pool, FAR void *blk)
 {
   irqstate_t flags = spin_lock_irqsave(&pool->lock);
   size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);
-#if CONFIG_MM_BACKTRACE >= 0
-  FAR struct mempool_backtrace_s *buf =
-    (FAR struct mempool_backtrace_s *)((FAR char *)blk + pool->blocksize);
... (232 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/mempool/mempool.c b/mm/mempool/mempool.c
index 10d3de49398..1b5e8957dd7 100644
--- a/mm/mempool/mempool.c
+++ b/mm/mempool/mempool.c
@@ -105,10 +105,20 @@ static inline void mempool_add_backtrace(FAR struct mempool_s *pool,
                                          FAR struct mempool_backtrace_s *buf,
                                          bool alloc)
 {
-  DEBUGASSERT(buf->magic == MEMPOOL_MAGIC_FREE);
-  buf->magic = MEMPOOL_MAGIC_ALLOC;
+  if (alloc)
+    {
+      DEBUGASSERT(buf->magic == MEMPOOL_MAGIC_FREE);
+      buf->magic = MEMPOOL_MAGIC_ALLOC;
+      buf->seqno = g_mm_seqno++;
+    }
+  else
+    {
+      DEBUGASSERT(buf->magic == MEMPOOL_MAGIC_ALLOC);
+      buf->magic = MEMPOOL_MAGIC_FREE;
+    }
+
   buf->pid = _SCHED_GETTID();
-  buf->seqno = g_mm_seqno++;
+
 #  if CONFIG_MM_BACKTRACE > 0
   if (pool->procfs.backtrace)
     {
@@ -429,15 +439,6 @@ void mempool_release(FAR struct mempool_s *pool, FAR void *blk)
 {
   irqstate_t flags = spin_lock_irqsave(&pool->lock);
   size_t blocksize = MEMPOOL_REALBLOCKSIZE(pool);
-#if CONFIG_MM_BACKTRACE >= 0
-  FAR struct mempool_backtrace_s *buf =
-    (FAR struct mempool_backtrace_s *)((FAR char *)blk + pool->blocksize);
... (236 lines truncated)
```
</details>

### 10. design-philosophy/localize_driver_changes_avoid_core_header_pollution (1x) [design]

**Rule:** Do not embed driver-specific conditional logic into widely-included core headers; localize feature-specific changes behind compile-time guards in the relevant driver and use helper functions instead of complex macro ternaries.

**Example:** #877163 — net/l2/l3/l4: add support of iob offload
**Transformation:** Moved IOB buffer logic out of core netdev.h macros (ETHBUF/IPBUF ternaries, inline functions, struct additions) into the specific sim_netdriver.c behind a compile-time SIM_NETDEV_IOB_OFFLOAD guard, using proper helper functions (netdev_iob_prepare, netdev_iob_release, netdriver_send).

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/src/sim/posix/up_tapdev.c b/arch/sim/src/sim/posix/up_tapdev.c
index ce518006da0..b5dfce2bec7 100644
--- a/arch/sim/src/sim/posix/up_tapdev.c
+++ b/arch/sim/src/sim/posix/up_tapdev.c
@@ -168,11 +168,11 @@ static void set_macaddr(int devidx)
 
   srand(time(NULL) + devidx);
   mac[0] = 0x42;
-  mac[1] = rand() % 256;
-  mac[2] = rand() % 256;
-  mac[3] = rand() % 256;
-  mac[4] = rand() % 256;
-  mac[5] = rand() % 256;
+  mac[1] = 0x43;
+  mac[2] = 0x44;
+  mac[3] = 0x45;
+  mac[4] = 0x46;
+  mac[5] = 0x47;
   mac[6] = 0;
 
   netdriver_setmacaddr(devidx, mac);
diff --git a/include/nuttx/net/netdev.h b/include/nuttx/net/netdev.h
index 2c044ee6386..eab41b2665c 100644
--- a/include/nuttx/net/netdev.h
+++ b/include/nuttx/net/netdev.h
@@ -59,8 +59,11 @@
 #include <net/ethernet.h>
 #include <arpa/inet.h>
 
+#include <nuttx/mm/iob.h>
 #include <nuttx/net/netconfig.h>
+#include <nuttx/net/net.h>
 #include <nuttx/net/ip.h>
+#include <nuttx/nuttx.h>
 
... (1816 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/src/sim/sim_netdriver.c b/arch/sim/src/sim/sim_netdriver.c
index d98c6fa99b7..999459a63d3 100644
--- a/arch/sim/src/sim/sim_netdriver.c
+++ b/arch/sim/src/sim/sim_netdriver.c
@@ -72,6 +72,11 @@
 
 #include "sim_internal.h"
 
+#if CONFIG_IOB_BUFSIZE >= (MAX_NETDEV_PKTSIZE + \
+    CONFIG_NET_GUARDSIZE + CONFIG_NET_LL_GUARDSIZE)
+#  define SIM_NETDEV_IOB_OFFLOAD
+#endif
+
 /****************************************************************************
  * Private Function Prototypes
  ****************************************************************************/
@@ -95,12 +100,23 @@ static struct net_driver_s g_sim_dev[CONFIG_SIM_NETDEV_NUMBER];
  * Private Functions
  ****************************************************************************/
 
-static void netdriver_reply(struct net_driver_s *dev)
+static void netdriver_send(struct net_driver_s *dev)
 {
   int devidx = (intptr_t)dev->d_private;
+#ifdef SIM_NETDEV_IOB_OFFLOAD
+  uint8_t *buf = &dev->d_iob->io_data[CONFIG_NET_LL_GUARDSIZE -
+                                      NET_LL_HDRLEN(dev)];
+#else
+  uint8_t *buf = dev->d_buf;
+#endif
 
   UNUSED(devidx);
 
+  sim_netdev_send(devidx, buf, dev->d_len);
+}
... (4290 lines truncated)
```
</details>

### 11. coding-style/multiline_alignment (1x) [style]

**Rule:** When a multi-line function call includes a cast on the return value, keep the cast and function name on the same line and align continuation arguments to the opening parenthesis, not to the cast.

**Example:** #3885537 — mm/heap: fix heap crash when use KASAN SW_TAG
**Transformation:** Reviewer fixed multi-line indentation of kasan_unpoison call in mm_memalign.c, collapsing the cast and function onto one line and aligning the continuation argument with the opening parenthesis.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/mm/mm_heap/mm_free.c b/mm/mm_heap/mm_free.c
index f75124eb6b3..29266fe602e 100644
--- a/mm/mm_heap/mm_free.c
+++ b/mm/mm_heap/mm_free.c
@@ -112,7 +112,8 @@ void mm_free(FAR struct mm_heap_s *heap, FAR void *mem)
 
   /* Map the memory chunk into a free node */
 
-  node = (FAR struct mm_freenode_s *)((FAR char *)mem - MM_SIZEOF_ALLOCNODE);
+  node = (FAR struct mm_freenode_s *)
+         ((FAR char *)kasan_reset_tag(mem) - MM_SIZEOF_ALLOCNODE);
   nodesize = MM_SIZEOF_NODE(node);
 
   /* Sanity check against double-frees */
diff --git a/mm/mm_heap/mm_memalign.c b/mm/mm_heap/mm_memalign.c
index 3eabd2dc8fe..376bdd7773b 100644
--- a/mm/mm_heap/mm_memalign.c
+++ b/mm/mm_heap/mm_memalign.c
@@ -135,6 +135,8 @@ FAR void *mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
   kasan_poison((FAR void *)rawchunk,
                mm_malloc_size(heap, (FAR void *)rawchunk));
 
+  rawchunk = (uintptr_t)kasan_reset_tag((FAR void *)rawchunk);
+
   /* We need to hold the MM mutex while we muck with the chunks and
    * nodelist.
    */
diff --git a/mm/mm_heap/mm_realloc.c b/mm/mm_heap/mm_realloc.c
index 24eaa61c116..92d10d1304a 100644
--- a/mm/mm_heap/mm_realloc.c
+++ b/mm/mm_heap/mm_realloc.c
@@ -125,7 +125,7 @@ FAR void *mm_realloc(FAR struct mm_heap_s *heap, FAR void *oldmem,
   /* Map the memory chunk into an allocated node structure */
 
   oldnode = (FAR struct mm_allocnode_s *)
... (6 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/mm_heap/mm_free.c b/mm/mm_heap/mm_free.c
index f75124eb6b3..29266fe602e 100644
--- a/mm/mm_heap/mm_free.c
+++ b/mm/mm_heap/mm_free.c
@@ -112,7 +112,8 @@ void mm_free(FAR struct mm_heap_s *heap, FAR void *mem)
 
   /* Map the memory chunk into a free node */
 
-  node = (FAR struct mm_freenode_s *)((FAR char *)mem - MM_SIZEOF_ALLOCNODE);
+  node = (FAR struct mm_freenode_s *)
+         ((FAR char *)kasan_reset_tag(mem) - MM_SIZEOF_ALLOCNODE);
   nodesize = MM_SIZEOF_NODE(node);
 
   /* Sanity check against double-frees */
diff --git a/mm/mm_heap/mm_memalign.c b/mm/mm_heap/mm_memalign.c
index 3eabd2dc8fe..1fbe570b649 100644
--- a/mm/mm_heap/mm_memalign.c
+++ b/mm/mm_heap/mm_memalign.c
@@ -135,6 +135,8 @@ FAR void *mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
   kasan_poison((FAR void *)rawchunk,
                mm_malloc_size(heap, (FAR void *)rawchunk));
 
+  rawchunk = (uintptr_t)kasan_reset_tag((FAR void *)rawchunk);
+
   /* We need to hold the MM mutex while we muck with the chunks and
    * nodelist.
    */
@@ -263,9 +265,8 @@ FAR void *mm_memalign(FAR struct mm_heap_s *heap, size_t alignment,
 
   MM_ADD_BACKTRACE(heap, node);
 
-  alignedchunk = (uintptr_t)
-                 kasan_unpoison((FAR void *)alignedchunk,
-                                mm_malloc_size(heap, (FAR void *)alignedchunk));
+  alignedchunk = (uintptr_t)kasan_unpoison((FAR void *)alignedchunk,
... (18 lines truncated)
```
</details>

### 12. design-philosophy/feature_addition_with_documentation (1x) [design]

**Rule:** When adding a new sanitizer or debug mode variant, the patch must cover the full stack end-to-end: documentation, Kconfig options, toolchain flags, arch-specific enablement (e.g. MMU/TBI), runtime hooks, and shadow memory setup — not split across disconnected follow-ups.

**Example:** #3859094 — kasan: Implementation of Kasan based on software tags.
**Transformation:** Added Software Tag-Based KASAN (SW_TAGS) mode for arm64 alongside existing Generic KASAN, with documentation, build system selection, MMU TBI support, and shadow memory infrastructure — patch accepted without review-driven changes between PS1 and FINAL.

<details><summary>Before (developer's submission)</summary>

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
... (721 lines truncated)
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
... (718 lines truncated)
```
</details>

### 13. design-philosophy/private_header_to_public_include (1x) [design]

**Rule:** When a subsystem header is needed by multiple sibling subsystems (e.g. mm_heap, mempool both need kasan), promote it from a private module header to a public include/nuttx/ header to avoid cross-module relative path includes.

**Example:** #3859093 — mm: Move kasan.h from mm/kasan to include/nuttx/mm
**Transformation:** Moved mm/kasan/kasan.h to include/nuttx/mm/kasan.h and replaced all private relative includes with public angle-bracket includes

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/mm/kasan/kasan.h b/include/nuttx/mm/kasan.h
similarity index 96%
rename from mm/kasan/kasan.h
rename to include/nuttx/mm/kasan.h
index 02ca030a03b..aae56066400 100644
--- a/mm/kasan/kasan.h
+++ b/include/nuttx/mm/kasan.h
@@ -1,5 +1,5 @@
 /****************************************************************************
- * mm/kasan/kasan.h
+ * include/nuttx/mm/kasan.h
  *
  * Licensed to the Apache Software Foundation (ASF) under one or more
  * contributor license agreements.  See the NOTICE file distributed with
@@ -18,8 +18,8 @@
  *
  ****************************************************************************/
 
-#ifndef __MM_KASAN_KASAN_H
-#define __MM_KASAN_KASAN_H
+#ifndef __INCLUDE_NUTTX_MM_KASAN_H
+#define __INCLUDE_NUTTX_MM_KASAN_H
 
 /****************************************************************************
  * Included Files
@@ -148,4 +148,4 @@ void kasan_init_early(void);
 
 #endif /* CONFIG_MM_KASAN */
 
-#endif /* __MM_KASAN_KASAN_H */
+#endif /* __INCLUDE_NUTTX_MM_KASAN_H */
diff --git a/mm/kasan/generic.c b/mm/kasan/generic.c
index 794aa68f76e..f1fc5046a9e 100644
--- a/mm/kasan/generic.c
+++ b/mm/kasan/generic.c
... (147 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/mm/kasan/kasan.h b/include/nuttx/mm/kasan.h
similarity index 96%
rename from mm/kasan/kasan.h
rename to include/nuttx/mm/kasan.h
index 02ca030a03b..aae56066400 100644
--- a/mm/kasan/kasan.h
+++ b/include/nuttx/mm/kasan.h
@@ -1,5 +1,5 @@
 /****************************************************************************
- * mm/kasan/kasan.h
+ * include/nuttx/mm/kasan.h
  *
  * Licensed to the Apache Software Foundation (ASF) under one or more
  * contributor license agreements.  See the NOTICE file distributed with
@@ -18,8 +18,8 @@
  *
  ****************************************************************************/
 
-#ifndef __MM_KASAN_KASAN_H
-#define __MM_KASAN_KASAN_H
+#ifndef __INCLUDE_NUTTX_MM_KASAN_H
+#define __INCLUDE_NUTTX_MM_KASAN_H
 
 /****************************************************************************
  * Included Files
@@ -148,4 +148,4 @@ void kasan_init_early(void);
 
 #endif /* CONFIG_MM_KASAN */
 
-#endif /* __MM_KASAN_KASAN_H */
+#endif /* __INCLUDE_NUTTX_MM_KASAN_H */
diff --git a/mm/kasan/generic.c b/mm/kasan/generic.c
index 794aa68f76e..f1fc5046a9e 100644
--- a/mm/kasan/generic.c
+++ b/mm/kasan/generic.c
... (149 lines truncated)
```
</details>

### 14. patch-organization/incomplete_feature_patch (1x) [design]

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

### 15. patch-organization/missing_documentation_for_subsystem_refactor (1x) [design]

**Rule:** When refactoring or introducing a new subsystem mode (e.g., splitting a monolithic module into pluggable backends), the patch set must include documentation covering architecture, usage, and implementation details — code-only refactors are incomplete.

**Example:** #3719852 — mm: Support the different kasan implementation
**Transformation:** Reviewer required adding comprehensive documentation (Documentation/guides/kasan.rst) explaining KASAN architecture, usage, implementation details, and CMakeLists.txt build support alongside the code refactoring that split kasan.c into generic/common/report/hook modules.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm64/src/Toolchain.defs b/arch/arm64/src/Toolchain.defs
index d61a562b983..12107bc9456 100644
--- a/arch/arm64/src/Toolchain.defs
+++ b/arch/arm64/src/Toolchain.defs
@@ -81,7 +81,9 @@ ifeq ($(CONFIG_MM_UBSAN_TRAP_ON_ERROR),y)
 endif
 
 ifeq ($(CONFIG_MM_KASAN_ALL),y)
-  ARCHOPTIMIZATION += -fsanitize=kernel-address
+  ifeq ($(CONFIG_KASAN_GENERIC),y)
+    ARCHOPTIMIZATION += -fsanitize=kernel-address
+  endif
 endif
 
 ifeq ($(CONFIG_MM_KASAN_DISABLE_READS_CHECK),y)
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
 
diff --git a/mm/Kconfig b/mm/Kconfig
index 9853da5a77e..0f0e6802c5f 100644
--- a/mm/Kconfig
+++ b/mm/Kconfig
@@ -253,6 +253,18 @@ config MM_KASAN
 		bugs in native code. After turn on this option, Please
 		add -fsanitize=kernel-address to CFLAGS/CXXFLAGS too.
 
... (803 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/Documentation/guides/kasan.rst b/Documentation/guides/kasan.rst
new file mode 100644
index 00000000000..3f3b1aeb047
--- /dev/null
+++ b/Documentation/guides/kasan.rst
@@ -0,0 +1,105 @@
+====================================
+The Kernel Address Sanitizer (KASAN)
+====================================
+
+Overview
+--------
+
+Kernel Address Sanitizer (KASAN) is a dynamic memory safety error detector
+designed to find out-of-bounds and use-after-free bugs.
+
+The current version of NuttX has one modes:
+
+1. Generic KASAN
+
+Generic KASAN, enabled with CONFIG_MM_KASAN, is the mode intended for
+debugging, similar to linux user level ASan. This mode is supported on many CPU
+architectures, but it has significant performance and memory overheads.
+The current NuttX Generic KASAN can support memory out of bounds detection
+allocated by the default NuttX heap allocator,which depends on CONFIG_MM_DEFAULT_MANAGER
+or CONFIG_MM_TLSF_MANAGER, and detection of out of bounds with global variables.
+
+Support
+-------
+
+Architectures
+~~~~~~~~~~~~~
+
+Generic KASAN is supported on x86_64, arm, arm64, riscv, xtensa and so on.
+
... (713 lines truncated)
```
</details>
