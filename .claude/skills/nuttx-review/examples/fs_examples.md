# Filesystem (fs/) Review Rules

Patterns mined from 158 review deltas in `fs/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| design-philosophy | 37 | 23% |
| coding-style | 25 | 16% |
| root-cause-fix | 23 | 15% |
| error-handling | 17 | 11% |
| code-reuse | 14 | 9% |
| concurrency | 10 | 6% |
| patch-organization | 9 | 6% |
| posix-compliance | 7 | 4% |
| correctness | 7 | 4% |
| memory-management | 5 | 3% |
| variable-scope | 2 | 1% |
| other | 1 | 1% |
| performance | 1 | 1% |

## Severity Distribution

correctness: 76 | design: 57 | style: 24 | performance: 1

## Top Patterns

### 1. coding-style/explicit_null_comparison (2x) [style]

**Rule:** In NuttX code, always use explicit NULL comparisons (`ptr != NULL` / `ptr == NULL`) instead of implicit pointer truthiness checks (`if (ptr)` / `if (!ptr)`).

**Example:** #842913 — fs/fs_fsync: add file sync operation by ioctl
**Transformation:** Changed implicit pointer truthiness check `if (inode)` to explicit `if (inode != NULL)` for clarity and NuttX style compliance.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/vfs/fs_fsync.c b/fs/vfs/fs_fsync.c
index 2eb4cc05ad9..4aadf14a6a2 100644
--- a/fs/vfs/fs_fsync.c
+++ b/fs/vfs/fs_fsync.c
@@ -32,6 +32,7 @@
 #include <nuttx/sched.h>
 #include <nuttx/cancelpt.h>
 #include <nuttx/fs/fs.h>
+#include <nuttx/fs/ioctl.h>
 
 #include "inode/inode.h"
 
@@ -53,7 +54,7 @@
 
 int file_fsync(FAR struct file *filep)
 {
-  struct inode *inode;
+  FAR struct inode *inode;
 
   /* Is this inode a registered mountpoint? Does it support the
    * sync operations may be relevant to device drivers but only
@@ -61,15 +62,21 @@ int file_fsync(FAR struct file *filep)
    */
 
   inode = filep->f_inode;
-  if (!inode || !INODE_IS_MOUNTPT(inode) ||
-      !inode->u.i_mops || !inode->u.i_mops->sync)
+  if (inode)
     {
-      return -EINVAL;
+      if (INODE_IS_MOUNTPT(inode) && inode->u.i_mops->sync)
+        {
+          /* Yes, then tell the mountpoint to sync this file */
+
+          return inode->u.i_mops->sync(filep);
... (15 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/vfs/fs_fsync.c b/fs/vfs/fs_fsync.c
index 2eb4cc05ad9..18b6b2a5d22 100644
--- a/fs/vfs/fs_fsync.c
+++ b/fs/vfs/fs_fsync.c
@@ -32,6 +32,7 @@
 #include <nuttx/sched.h>
 #include <nuttx/cancelpt.h>
 #include <nuttx/fs/fs.h>
+#include <nuttx/fs/ioctl.h>
 
 #include "inode/inode.h"
 
@@ -53,7 +54,7 @@
 
 int file_fsync(FAR struct file *filep)
 {
-  struct inode *inode;
+  FAR struct inode *inode;
 
   /* Is this inode a registered mountpoint? Does it support the
    * sync operations may be relevant to device drivers but only
@@ -61,15 +62,21 @@ int file_fsync(FAR struct file *filep)
    */
 
   inode = filep->f_inode;
-  if (!inode || !INODE_IS_MOUNTPT(inode) ||
-      !inode->u.i_mops || !inode->u.i_mops->sync)
+  if (inode != NULL)
     {
-      return -EINVAL;
+      if (INODE_IS_MOUNTPT(inode) && inode->u.i_mops->sync)
+        {
+          /* Yes, then tell the mountpoint to sync this file */
+
+          return inode->u.i_mops->sync(filep);
... (15 lines truncated)
```
</details>

### 2. root-cause-fix/minimal_correct_fix (2x) [correctness]

**Rule:** Implement the minimal correct fix: in open, only record state without redundant seeks; in write, compare against the authoritative subsystem state (f_tell) not the cached VFS copy (f_pos); and never mix unrelated error-handling refactors into a feature patch.

**Example:** #849223 — fs/fatfs: support O_APPEND mode
**Transformation:** Removed unnecessary f_lseek call in open (just record f_size as f_pos), replaced filep->f_pos with f_tell(&fp->f) for authoritative FatFS pointer check in write, and stripped unrelated goto-label refactoring from the patch.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/fatfs/fatfs_vfs.c b/fs/fatfs/fatfs_vfs.c
index a93462da8f5..2162ad24081 100644
--- a/fs/fatfs/fatfs_vfs.c
+++ b/fs/fatfs/fatfs_vfs.c
@@ -330,8 +330,7 @@ static int fatfs_open(FAR struct file *filep, FAR const char *relpath,
   ret = fatfs_semtake(fs);
   if (ret < 0)
     {
-      kmm_free(fp);
-      return ret;
+      goto errout_with_fp;
     }
 
   fp->path[0] = '0' + fs->pdrv;
@@ -342,16 +341,35 @@ static int fatfs_open(FAR struct file *filep, FAR const char *relpath,
   ret = fatfs_convert_result(f_open(&fp->f, fp->path, oflags));
   if (ret < 0)
     {
-      kmm_free(fp);
-      goto errsem;
+      goto errout_with_sem;
+    }
+
+  /* In append mode, need to set the file pointer to end of the file */
+
+  if ((filep->f_oflags & O_APPEND))
+    {
+      ret = fatfs_convert_result(f_lseek(&fp->f, f_size(&fp->f)));
+      if (ret < 0)
+        {
+          goto errout_with_file;
+        }
+
+      filep->f_pos = f_size(&fp->f);
     }
... (47 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/fatfs/fatfs_vfs.c b/fs/fatfs/fatfs_vfs.c
index a93462da8f5..a75efc9152e 100644
--- a/fs/fatfs/fatfs_vfs.c
+++ b/fs/fatfs/fatfs_vfs.c
@@ -346,6 +346,13 @@ static int fatfs_open(FAR struct file *filep, FAR const char *relpath,
       goto errsem;
     }
 
+  /* In append mode, need to set the file pointer to end of the file */
+
+  if (filep->f_oflags & O_APPEND)
+    {
+      filep->f_pos = f_size(&fp->f);
+    }
+
   fp->refs = 1;
   filep->f_priv = fp;
 
@@ -441,6 +448,19 @@ static ssize_t fatfs_write(FAR struct file *filep, FAR const char *buffer,
       return ret;
     }
 
+  /* In append mode, need to set the file pointer to end of the file */
+
+  if ((filep->f_oflags & O_APPEND) && f_tell(&fp->f) < f_size(&fp->f))
+    {
+      ret = fatfs_convert_result(f_lseek(&fp->f, f_size(&fp->f)));
+      if (ret < 0)
+        {
+          goto errout_with_sem;
+        }
+
+      filep->f_pos = f_size(&fp->f);
+    }
+
... (12 lines truncated)
```
</details>

### 3. design-philosophy/proper_limit_with_kconfig_over_symptom_patch (1x) [design]

**Rule:** When a POSIX interface needs a system limit, define it as a Kconfig option, expose it through the standard header (limits.h), validate inputs at the API boundary, and expose via sysconf — do not patch internal control flow or add ad-hoc flags to work around missing validation

**Example:** #5995244 — sysconf:add support for AIO_LISTIO_MAX
**Transformation:** Removed internal control-flow hack in lio_listio and return_status flag in aio_return; instead added Kconfig-configurable AIO_LISTIO_MAX, wired it through limits.h, added nent bounds-check at API entry, and simplified sysconf to unconditionally return AIO_LISTIO_MAX

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/aio/lio_listio.c b/fs/aio/lio_listio.c
index 7414f4eb988..c5d2e22e5c5 100644
--- a/fs/aio/lio_listio.c
+++ b/fs/aio/lio_listio.c
@@ -416,7 +416,9 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
                     list_delete(&aiocbp->lio_link);
                     aio_unlock();
                   }
-                else
+                  }
+
+                if (status > 0)
                   {
                     /* Increment the count of successfully queue operations */
 
diff --git a/libs/libc/aio/aio_return.c b/libs/libc/aio/aio_return.c
index f3a8f5192a0..6ba9bc451c7 100644
--- a/libs/libc/aio/aio_return.c
+++ b/libs/libc/aio/aio_return.c
@@ -110,6 +110,7 @@ ssize_t aio_return(FAR struct aiocb *aiocbp)
    */
 
   aiocbp->aio_result = -EINVAL;
+  aiocbp->return_status = 1;
   return ret;
 }
 
diff --git a/libs/libc/unistd/lib_sysconf.c b/libs/libc/unistd/lib_sysconf.c
index dc6bfb8cba1..09114a978b9 100644
--- a/libs/libc/unistd/lib_sysconf.c
+++ b/libs/libc/unistd/lib_sysconf.c
@@ -262,6 +262,13 @@ long sysconf(int name)
       case _SC_THREAD_THREADS_MAX:
         return UINT8_MAX;
 
... (11 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/aio/Kconfig b/fs/aio/Kconfig
index 3e519cdc3a0..b393ca9cdd7 100644
--- a/fs/aio/Kconfig
+++ b/fs/aio/Kconfig
@@ -11,6 +11,18 @@ config FS_AIO
 		Enable support for aynchronous I/O.  This selection enables the
 		interfaces declared in include/aio.h.
 
+config FS_AIO_LISTIO_MAX
+	int "Maximum number of AIO operations in listio"
+	default 10
+	---help---
+		This option sets the maximum number of asynchronous I/O (AIO) operations
+		that can be submitted in a single call to lio_listio().
+
+		This value defines the upper limit for the 'nent' parameter in
+		lio_listio(mode, aiocb_list, nent, sevp). Increasing this value allows
+		more operations to be queued.
+		Default: 10
+
 if FS_AIO
 
 config FS_NAIOC
diff --git a/fs/aio/lio_listio.c b/fs/aio/lio_listio.c
index 17693a5fdde..a796205cb48 100644
--- a/fs/aio/lio_listio.c
+++ b/fs/aio/lio_listio.c
@@ -332,7 +332,8 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
   int ret;
   int i;
 
-  if (mode != LIO_WAIT && mode != LIO_NOWAIT)
+  if (nent < 0 || nent > AIO_LISTIO_MAX ||
+      (mode != LIO_WAIT && mode != LIO_NOWAIT))
     {
... (30 lines truncated)
```
</details>

### 4. root-cause-fix/minimal_guard_insertion (1x) [correctness]

**Rule:** When adding a guard condition, insert it at the narrowest scope that achieves the fix — do not restructure surrounding control flow or remove unrelated branches. Minimal diff, maximal preservation of existing logic.

**Example:** #5995437 — lio_listio:when mode == LIO_WAIT, lio_link is null, delete it will crash.
**Transformation:** Instead of restructuring control flow by making the new condition the outer check and removing the else branch, the architect nested the new `mode == LIO_NOWAIT && sig` guard inside the existing `if (status < 0 || ...)` block, preserving the original else branch and overall logic structure

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/aio/lio_listio.c b/fs/aio/lio_listio.c
index 7414f4eb988..ae9fa237444 100644
--- a/fs/aio/lio_listio.c
+++ b/fs/aio/lio_listio.c
@@ -409,14 +409,18 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
                     ret = ERROR;
                   }
 
-                if (status < 0 || aiocbp->aio_result == -EBADF ||
-                    aiocbp->aio_result == -EINVAL)
+                if (mode == LIO_NOWAIT && sig)
                   {
-                    aio_lock();
-                    list_delete(&aiocbp->lio_link);
-                    aio_unlock();
+                   if (status < 0 || aiocbp->aio_result == -EBADF ||
+                       aiocbp->aio_result == -EINVAL)
+                     {
+                        aio_lock();
+                        list_delete(&aiocbp->lio_link);
+                        aio_unlock();
+                    }
                   }
-                else
+
+                if (status > 0)
                   {
                     /* Increment the count of successfully queue operations */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/aio/lio_listio.c b/fs/aio/lio_listio.c
index 0a8555fe8f8..17693a5fdde 100644
--- a/fs/aio/lio_listio.c
+++ b/fs/aio/lio_listio.c
@@ -412,9 +412,12 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
                 if (status < 0 || aiocbp->aio_result == -EBADF ||
                     aiocbp->aio_result == -EINVAL)
                   {
-                    aio_lock();
-                    list_delete(&aiocbp->lio_link);
-                    aio_unlock();
+                    if (mode == LIO_NOWAIT && sig)
+                     {
+                       aio_lock();
+                       list_delete(&aiocbp->lio_link);
+                       aio_unlock();
+                     }
                   }
                 else
                   {

```
</details>

### 5. error-handling/goto_label_preserves_error_path (1x) [correctness]

**Rule:** A goto-out label must land before the common error-handling/cleanup path, not after it; skipping error checks silently swallows failures

**Example:** #6203115 — bugfix:aio dereference a NULL pointer
**Transformation:** Moved `out:` label from before `return OK` to before `if (ret < 0)` error-checking block, so that the goto still passes through the error handling path instead of bypassing it

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/aio/lio_listio.c b/fs/aio/lio_listio.c
index 7414f4eb988..c4d469071fc 100644
--- a/fs/aio/lio_listio.c
+++ b/fs/aio/lio_listio.c
@@ -495,6 +495,25 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
            * removed, so manually signal the client
            */
 
+          /* Find a non-NULL aiocbp */
+
+          if (aiocbp == NULL)
+            {
+              for (i = 0; i < nent; i++)
+                {
+                  if (list[i])
+                    {
+                      aiocbp = list[i];
+                      break;
+                    }
+                }
+
+              if (aiocbp == NULL)
+                {
+                  goto out;
+                }
+            }
+
           status = nxsig_notification(nxsched_getpid(),
                                       &aiocbp->lio_sigevent,
                                       SI_ASYNCIO,
@@ -522,6 +541,7 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
       return ERROR;
     }
 
+out:
... (4 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/aio/lio_listio.c b/fs/aio/lio_listio.c
index 7414f4eb988..0a8555fe8f8 100644
--- a/fs/aio/lio_listio.c
+++ b/fs/aio/lio_listio.c
@@ -495,6 +495,25 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
            * removed, so manually signal the client
            */
 
+          /* Find a non-NULL aiocbp */
+
+          if (aiocbp == NULL)
+            {
+              for (i = 0; i < nent; i++)
+                {
+                  if (list[i])
+                    {
+                      aiocbp = list[i];
+                      break;
+                    }
+                }
+
+              if (aiocbp == NULL)
+                {
+                  goto out;
+                }
+            }
+
           status = nxsig_notification(nxsched_getpid(),
                                       &aiocbp->lio_sigevent,
                                       SI_ASYNCIO,
@@ -516,6 +535,7 @@ int lio_listio(int mode, FAR struct aiocb * const list[], int nent,
    *   Just return now.
    */
 
+out:
... (4 lines truncated)
```
</details>

### 6. coding-style/include_alphabetical_order (1x) [style]

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

### 7. design-philosophy/header_dependency_colocation (1x) [correctness]

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

### 8. coding-style/kconfig_indentation_tabs (1x) [style]

**Rule:** Kconfig files must use tab indentation consistently; do not mix spaces and tabs

**Example:** #6098995 — modify config related to zipfs
**Transformation:** Changed indentation of 'depends on UTILS_LIBARCHIVE' line from spaces to tab character to match surrounding Kconfig entries

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/boards/sim/sim/sim/configs/zipfs/defconfig b/boards/sim/sim/sim/configs/zipfs/defconfig
index 35b7448e3cc..dfd00829224 100644
--- a/boards/sim/sim/sim/configs/zipfs/defconfig
+++ b/boards/sim/sim/sim/configs/zipfs/defconfig
@@ -66,5 +66,6 @@ CONFIG_START_MONTH=6
 CONFIG_START_YEAR=2008
 CONFIG_SYSTEM_DUMPSTACK=y
 CONFIG_SYSTEM_NSH=y
+CONFIG_UTILS_LIBARCHIVE=y
 CONFIG_UTILS_UNZIP=y
 CONFIG_UTILS_ZIP=y
diff --git a/fs/zipfs/Kconfig b/fs/zipfs/Kconfig
index 420a6b46492..2a2a9a379c1 100644
--- a/fs/zipfs/Kconfig
+++ b/fs/zipfs/Kconfig
@@ -5,6 +5,7 @@
 
 config FS_ZIPFS
 	bool "ZIP File System"
+    depends on UTILS_LIBARCHIVE
 	default n
 
 if FS_ZIPFS

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/boards/sim/sim/sim/configs/zipfs/defconfig b/boards/sim/sim/sim/configs/zipfs/defconfig
index 35b7448e3cc..dfd00829224 100644
--- a/boards/sim/sim/sim/configs/zipfs/defconfig
+++ b/boards/sim/sim/sim/configs/zipfs/defconfig
@@ -66,5 +66,6 @@ CONFIG_START_MONTH=6
 CONFIG_START_YEAR=2008
 CONFIG_SYSTEM_DUMPSTACK=y
 CONFIG_SYSTEM_NSH=y
+CONFIG_UTILS_LIBARCHIVE=y
 CONFIG_UTILS_UNZIP=y
 CONFIG_UTILS_ZIP=y
diff --git a/fs/zipfs/Kconfig b/fs/zipfs/Kconfig
index 420a6b46492..185ec36669e 100644
--- a/fs/zipfs/Kconfig
+++ b/fs/zipfs/Kconfig
@@ -5,6 +5,7 @@
 
 config FS_ZIPFS
 	bool "ZIP File System"
+	depends on UTILS_LIBARCHIVE
 	default n
 
 if FS_ZIPFS

```
</details>

### 9. design-philosophy/use_higher_level_api (1x) [design]

**Rule:** Use the highest appropriate abstraction layer — callers should not reach through internal implementation details (fdlist + task lookup) when a purpose-built wrapper (file_dup) already encapsulates that logic

**Example:** #6065680 — fs/inode: using file_allocate and file_dup to avoid racecondition to allocate new fd
**Transformation:** Replaced low-level fdlist_dupfile(nxsched_get_fdlist_from_tcb(this_task()), ...) with higher-level file_dup(filep, minfd, oflags); added missing FAR macro on file_allocate return type

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/inode/fs_files.c b/fs/inode/fs_files.c
index 02081a08d32..8fa3b2f53cd 100644
--- a/fs/inode/fs_files.c
+++ b/fs/inode/fs_files.c
@@ -647,81 +647,6 @@ found:
 #endif
 }
 
-/****************************************************************************
- * Name: fdlist_allocate
- *
- * Description:
- *   Allocate a struct fd instance and associate it with an empty file
- *   instance. The difference between this function and
- *   file_allocate_from_inode is that this function is only used for
- *   placeholder purposes. Later, the caller will initialize the file entity
- *   through the returned filep.
- *
- *   The fd allocated by this function can be released using fdlist_close.
- *
- *   After the function call is completed, it will hold a reference count
- *   for the filep. Therefore, when the filep is no longer in use, it is
- *   necessary to call file_put to release the reference count, in order
- *   to avoid a race condition where the file might be closed during
- *   this process.
- *
- * Returned Value:
- *   Returns the file descriptor == index into the files array on success;
- *   a negated errno value is returned on any failure.
- *
- ****************************************************************************/
-
-int fdlist_allocate(FAR struct fdlist *list, int oflags,
-                    int minfd, FAR struct file **filep)
-{
... (333 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/inode/fs_files.c b/fs/inode/fs_files.c
index 02081a08d32..ab28ba23553 100644
--- a/fs/inode/fs_files.c
+++ b/fs/inode/fs_files.c
@@ -647,81 +647,6 @@ found:
 #endif
 }
 
-/****************************************************************************
- * Name: fdlist_allocate
- *
- * Description:
- *   Allocate a struct fd instance and associate it with an empty file
- *   instance. The difference between this function and
- *   file_allocate_from_inode is that this function is only used for
- *   placeholder purposes. Later, the caller will initialize the file entity
- *   through the returned filep.
- *
- *   The fd allocated by this function can be released using fdlist_close.
- *
- *   After the function call is completed, it will hold a reference count
- *   for the filep. Therefore, when the filep is no longer in use, it is
- *   necessary to call file_put to release the reference count, in order
- *   to avoid a race condition where the file might be closed during
- *   this process.
- *
- * Returned Value:
- *   Returns the file descriptor == index into the files array on success;
- *   a negated errno value is returned on any failure.
- *
- ****************************************************************************/
-
-int fdlist_allocate(FAR struct fdlist *list, int oflags,
-                    int minfd, FAR struct file **filep)
-{
... (320 lines truncated)
```
</details>

### 10. memory-management/stack_alloc_missing_zero_init (1x) [correctness]

**Rule:** When replacing heap allocation (zalloc/calloc) with stack allocation, always explicitly zero-initialize the struct — stack memory is uninitialized and stale fields cause undefined behavior

**Example:** #6062055 — fs/inode: using file_allocate_from_inode to avoid racecondition to allocate new fd
**Transformation:** When converting struct file from heap-allocated (fs_heap_zalloc, which zeros memory) to stack-allocated (struct file mq), architect required adding memset(mq, 0, sizeof(struct file)) to explicitly zero-initialize; also fixed broken parenthesization in file_allocate_from_inode by extracting fdlist to a local variable for correct and readable call syntax

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/inode/fs_files.c b/fs/inode/fs_files.c
index 02081a08d32..d727d4db7e9 100644
--- a/fs/inode/fs_files.c
+++ b/fs/inode/fs_files.c
@@ -648,22 +648,11 @@ found:
 }
 
 /****************************************************************************
- * Name: fdlist_allocate
+ * Name: fdlist_file_allocate_from_inode
  *
  * Description:
- *   Allocate a struct fd instance and associate it with an empty file
- *   instance. The difference between this function and
- *   file_allocate_from_inode is that this function is only used for
- *   placeholder purposes. Later, the caller will initialize the file entity
- *   through the returned filep.
- *
- *   The fd allocated by this function can be released using fdlist_close.
- *
- *   After the function call is completed, it will hold a reference count
- *   for the filep. Therefore, when the filep is no longer in use, it is
- *   necessary to call file_put to release the reference count, in order
- *   to avoid a race condition where the file might be closed during
- *   this process.
+ *   Allocate a struct fd instance and associate it with an file instance
+ *   with specify list, And initialize them with inode, oflags, pos and priv.
  *
  * Returned Value:
  *   Returns the file descriptor == index into the files array on success;
@@ -671,57 +660,37 @@ found:
  *
  ****************************************************************************/
 
-int fdlist_allocate(FAR struct fdlist *list, int oflags,
... (300 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/inode/fs_files.c b/fs/inode/fs_files.c
index 02081a08d32..8886952541d 100644
--- a/fs/inode/fs_files.c
+++ b/fs/inode/fs_files.c
@@ -648,22 +648,11 @@ found:
 }
 
 /****************************************************************************
- * Name: fdlist_allocate
+ * Name: fdlist_file_allocate_from_inode
  *
  * Description:
- *   Allocate a struct fd instance and associate it with an empty file
- *   instance. The difference between this function and
- *   file_allocate_from_inode is that this function is only used for
- *   placeholder purposes. Later, the caller will initialize the file entity
- *   through the returned filep.
- *
- *   The fd allocated by this function can be released using fdlist_close.
- *
- *   After the function call is completed, it will hold a reference count
- *   for the filep. Therefore, when the filep is no longer in use, it is
- *   necessary to call file_put to release the reference count, in order
- *   to avoid a race condition where the file might be closed during
- *   this process.
+ *   Allocate a struct fd instance and associate it with an file instance
+ *   with specify list, And initialize them with inode, oflags, pos and priv.
  *
  * Returned Value:
  *   Returns the file descriptor == index into the files array on success;
@@ -671,57 +660,37 @@ found:
  *
  ****************************************************************************/
 
-int fdlist_allocate(FAR struct fdlist *list, int oflags,
... (319 lines truncated)
```
</details>

### 11. design-philosophy/extract_complex_logic_to_helper_functions (1x) [design]

**Rule:** Complex filesystem operations involving disk layout changes must be decomposed into well-named static helper functions with clear single responsibilities, proper NuttX doxygen headers, and complete error-path cleanup — never inline raw buffer arithmetic and spare-region manipulation in a caller.

**Example:** #5970455 — bugfix:lrofs rename may fail when renamed to a longer name
**Transformation:** Monolithic inline rename logic (in-place header extension with raw buffer math) was refactored into modular static helpers (`lrofs_copy_filecontent`, `lrofs_move_renamedfile`) that allocate new space, copy content sector-by-sector, and update references with proper error handling and NuttX comment style.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/lrofs/fs_lrofsutil.c b/fs/lrofs/fs_lrofsutil.c
index d37cd864e0c..6fb88232985 100644
--- a/fs/lrofs/fs_lrofsutil.c
+++ b/fs/lrofs/fs_lrofsutil.c
@@ -2406,7 +2406,100 @@ int lrofs_rename_file(FAR struct lrofs_mountpt_s *lm,
     {
       return -ENOENT;
     }
-
+  
+  if (newname != NULL)
+  {
+    if (strlen(newname) <= ln_old->ln_namesize)
+      {
+        ln_old->ln_namesize = strlen(newname);
+        memcpy(ln_old->ln_name, newname, strlen(newname) + 1);
+        ret = lrofs_update_filename(lm, ln_old);
+        if (ret < 0)
+          {
+            ferr("ERROR: lrofs_update_filename failed: %d\n", ret);
+            return ret;
+          }
+      }
+    else
+      {
+        FAR void *tmp;
+        int old_namesize;
+        tmp = fs_heap_realloc(ln_old, sizeof(struct lrofs_nodeinfo_s) +
+                                      strlen(newname));
+        if (tmp == NULL)
+          {
+            return -ENOMEM;
+          }
+
+        ln_old = tmp;
... (116 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/lrofs/fs_lrofsutil.c b/fs/lrofs/fs_lrofsutil.c
index 4dcfbaa8d19..c5e0e553c2b 100644
--- a/fs/lrofs/fs_lrofsutil.c
+++ b/fs/lrofs/fs_lrofsutil.c
@@ -1205,6 +1205,194 @@ static int lrofs_update_filenext(FAR struct lrofs_mountpt_s *lm,
   return lrofs_devcachewrite(lm, SEC_NSECTORS(lm, ln->ln_origoffset));
 }
 
+/****************************************************************************
+ * Name: lrofs_copy_filecontent
+ *
+ * Description:
+ *   Copy content of a file in the disk
+ *
+ ****************************************************************************/
+
+static int lrofs_copy_filecontent(FAR struct lrofs_mountpt_s *lm,
+                                  FAR struct lrofs_nodeinfo_s *ln_old,
+                                  uint32_t old_offset, uint32_t new_offset)
+{
+  int16_t ndx;
+  int ret = OK;
+  int bytes = 0;
+
+  while (ln_old->ln_size - bytes >= lm->lm_hwsectorsize)
+    {
+      ndx = lrofs_devcacheload(lm, old_offset + bytes);
+      if (ndx < 0)
+        {
+          ferr("ERROR: lrofs_devcacheload failed: %d\n", ndx);
+          return ndx;
+        }
+
+      ret = lrofs_devcachewrite(lm, SEC_NSECTORS(lm, new_offset + bytes));
+      if (ret < 0)
... (318 lines truncated)
```
</details>

### 12. error-handling/missing_return_value_check (1x) [correctness]

**Rule:** Every function that returns an error code must have its return value checked; silently ignoring disk write errors in filesystem code can cause silent data corruption.

**Example:** #5968114 — bugfix:lrofs the data written to hw after rename is incorrect
**Transformation:** Added return value checks with error logging for all lrofs_update_filenext and lrofs_update_filename calls, and fixed ln_offset to ln_origoffset in devcacheload to use the correct on-disk offset.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/lrofs/fs_lrofsutil.c b/fs/lrofs/fs_lrofsutil.c
index 9d2d481086a..8752160439a 100644
--- a/fs/lrofs/fs_lrofsutil.c
+++ b/fs/lrofs/fs_lrofsutil.c
@@ -1150,6 +1150,58 @@ static int lrofs_followhardlinks(FAR struct lrofs_mountpt_s *lm,
   return -ELOOP;
 }
 
+/****************************************************************************
+ * Name: lrofs_update_filename
+ *
+ * Description:
+ *   Update the file name to lrofs
+ *
+ ****************************************************************************/
+static int lrofs_update_filename(FAR struct lrofs_mountpt_s *lm,
+                                 FAR struct lrofs_nodeinfo_s *ln)
+{
+  int16_t ndx;
+
+  /* Get the node sector index */
+
+  ndx = lrofs_devcacheload(lm, ln->ln_offset);
+  if (ndx < 0)
+    {
+      return ndx;
+    }
+
+  /* Update the file name */
+
+  memcpy(lm->lm_devbuffer + ndx + LROFS_FHDR_NAME, ln->ln_name,
+         ln->ln_namesize + 1);
+  return lrofs_devcachewrite(lm, SEC_NSECTORS(lm, ln->ln_origoffset));
+}
+
... (67 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/fs/lrofs/fs_lrofsutil.c b/fs/lrofs/fs_lrofsutil.c
index 9d2d481086a..4dcfbaa8d19 100644
--- a/fs/lrofs/fs_lrofsutil.c
+++ b/fs/lrofs/fs_lrofsutil.c
@@ -1150,6 +1150,61 @@ static int lrofs_followhardlinks(FAR struct lrofs_mountpt_s *lm,
   return -ELOOP;
 }
 
+/****************************************************************************
+ * Name: lrofs_update_filename
+ *
+ * Description:
+ *   Update the file name to lrofs
+ *
+ ****************************************************************************/
+
+static int lrofs_update_filename(FAR struct lrofs_mountpt_s *lm,
+                                 FAR struct lrofs_nodeinfo_s *ln)
+{
+  int16_t ndx;
+
+  /* Get the node sector index */
+
+  ndx = lrofs_devcacheload(lm, ln->ln_origoffset);
+  if (ndx < 0)
+    {
+      return ndx;
+    }
+
+  /* Update the file name */
+
+  memcpy(lm->lm_devbuffer + ndx + LROFS_FHDR_NAME, ln->ln_name,
+         ln->ln_namesize + 1);
+  return lrofs_devcachewrite(lm, SEC_NSECTORS(lm, ln->ln_origoffset));
+}
... (77 lines truncated)
```
</details>

### 13. code-reuse/extend_existing_abstraction (1x) [design]

**Rule:** When existing build system abstractions (functions, macros) don't support a needed feature, extend the abstraction with new parameters rather than bypassing it with lower-level commands — this preserves maintainability and avoids duplicating logic.

**Example:** #5918671 — [BUGFIX]:Syscall parameter and return value truncation
**Transformation:** Instead of bypassing the existing generate_syscall_files() CMake function with raw add_custom_command (which also left merge conflict markers), the final version extends generate_syscall_files() with a new TYPE_REFERENCES parameter to pass the -r option through the existing abstraction.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/Kconfig b/fs/Kconfig
index 1e72530bd05..2842e7fed76 100644
--- a/fs/Kconfig
+++ b/fs/Kconfig
@@ -16,11 +16,6 @@ config FS_LARGEFILE
 		Support files which's length is larger than 4GB:
 		https://www.opengroup.org/platform/lfs.html
 
-		Note: the protected and kernel mode on 32bit platform can't exceed
-		the 4GB limitation since the auto generated proxy and stub still
-		cut 64bit to 32bit value. Please check tools/mksyscall.c for more
-		information.
-
 config FS_AUTOMOUNTER
 	bool "Auto-mounter"
 	default n
diff --git a/syscall/Makefile b/syscall/Makefile
index a1256018be4..e0354a50516 100644
--- a/syscall/Makefile
+++ b/syscall/Makefile
@@ -29,6 +29,13 @@ include wraps/Make.defs
 MKSYSCALL = "$(TOPDIR)$(DELIM)tools$(DELIM)mksyscall$(HOSTEXEEXT)"
 CSVFILE = "$(TOPDIR)$(DELIM)syscall$(DELIM)syscall.csv"
 
+ifeq ($(CONFIG_FS_LARGEFILE),y)
+  ifeq ($(CONFIG_ARCH_64BIT),)
+    # off_t is 64bit, so off_t need be passed as pointer on 32bit arch
+    OPTIONS := -r off_t
+  endif
+endif
+
 STUB_SRCS += syscall_names.c
 
 ifeq ($(CONFIG_SCHED_INSTRUMENTATION_SYSCALL),y)
@@ -95,8 +102,8 @@ depend: .depend
... (285 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/cmake/nuttx_syscall_utils.cmake b/cmake/nuttx_syscall_utils.cmake
index e55cd407815..fc0be91a6f4 100644
--- a/cmake/nuttx_syscall_utils.cmake
+++ b/cmake/nuttx_syscall_utils.cmake
@@ -72,7 +72,7 @@ function(generate_syscall_files)
   # Parse function arguments
   set(OPTIONS)
   set(ONEVALUEARG OUTPUT_VAR OUTPATH MODE)
-  set(MULTIVALUEARGS ALL_SYSCALLS CSV_FILES)
+  set(MULTIVALUEARGS ALL_SYSCALLS CSV_FILES TYPE_REFERENCES)
   cmake_parse_arguments(ARG "${OPTIONS}" "${ONEVALUEARG}" "${MULTIVALUEARGS}"
                         ${ARGN})
 
@@ -112,7 +112,7 @@ function(generate_syscall_files)
   add_custom_command(
     OUTPUT ${ARG_ALL_SYSCALLS}
     COMMAND ${CMAKE_BINARY_DIR}/bin_host/mksyscall ${MKSYSCALL_MODE}
-            ${ARG_CSV_FILES}
+            ${ARG_CSV_FILES} ${ARG_TYPE_REFERENCES}
     WORKING_DIRECTORY ${ARG_OUTPATH}
     DEPENDS mksyscall
     COMMENT "Generating system call ${ARG_MODE}s")
diff --git a/fs/Kconfig b/fs/Kconfig
index 1e72530bd05..2842e7fed76 100644
--- a/fs/Kconfig
+++ b/fs/Kconfig
@@ -16,11 +16,6 @@ config FS_LARGEFILE
 		Support files which's length is larger than 4GB:
 		https://www.opengroup.org/platform/lfs.html
 
-		Note: the protected and kernel mode on 32bit platform can't exceed
-		the 4GB limitation since the auto generated proxy and stub still
-		cut 64bit to 32bit value. Please check tools/mksyscall.c for more
-		information.
-
... (291 lines truncated)
```
</details>

### 14. root-cause-fix/incomplete_guard_migration (1x) [correctness]

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

### 15. variable-scope/global_to_per_instance_embed (1x) [design]

**Rule:** Embed notifier/callback registration state in the per-instance device struct rather than using file-scope globals; this avoids hidden coupling, simplifies lifetime management, and makes the code safe for multiple instances.

**Example:** #4917657 — timerfd: notify application of system relatime changes
**Transformation:** Replaced global notifier_block and global list_node with a per-device notifier_block embedded in struct timerfd_priv_s, eliminating shared mutable global state.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/vfs/fs_timerfd.c b/fs/vfs/fs_timerfd.c
index 8434005a6a4..0b5fda7e4b1 100644
--- a/fs/vfs/fs_timerfd.c
+++ b/fs/vfs/fs_timerfd.c
@@ -32,8 +32,11 @@
 #include <debug.h>
 
 #include <nuttx/irq.h>
-#include <nuttx/wdog.h>
+#include <nuttx/list.h>
 #include <nuttx/mutex.h>
+#include <nuttx/notifier.h>
+#include <nuttx/wdog.h>
+#include <nuttx/timechanged_notifier.h>
 
 #include <sys/ioctl.h>
 #include <sys/timerfd.h>
@@ -60,14 +63,16 @@ typedef struct timerfd_waiter_sem_s
 
 struct timerfd_priv_s
 {
-  mutex_t                   lock;    /* Enforces device exclusive access */
-  FAR timerfd_waiter_sem_t *rdsems;  /* List of blocking readers */
-  int                       clock;   /* Clock to use as the timing base */
-  int                       delay;   /* If non-zero, used to reset repetitive
-                                      * timers */
-  struct wdog_s             wdog;    /* The watchdog that provides the timing */
-  timerfd_t                 counter; /* timerfd counter */
-  uint8_t                   crefs;   /* References counts on timerfd (max: 255) */
+  mutex_t                   lock;     /* Enforces device exclusive access */
+  FAR timerfd_waiter_sem_t *rdsems;   /* List of blocking readers */
+  int                       clock;    /* Clock to use as the timing base */
+  int                       delay;    /* If non-zero, used to reset repetitive
+                                       * timers */
+  struct wdog_s             wdog;     /* The watchdog that provides the timing */
... (433 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/timers/rpmsg_rtc.c b/drivers/timers/rpmsg_rtc.c
index 22310d6444b..712a11fc400 100644
--- a/drivers/timers/rpmsg_rtc.c
+++ b/drivers/timers/rpmsg_rtc.c
@@ -26,6 +26,7 @@
 
 #include <nuttx/list.h>
 #include <nuttx/clock.h>
+#include <nuttx/clock_changed_notifier.h>
 #include <nuttx/kmalloc.h>
 #include <nuttx/rpmsg/rpmsg.h>
 #include <nuttx/mutex.h>
@@ -298,8 +299,13 @@ static int rpmsg_rtc_ept_cb(FAR struct rpmsg_endpoint *ept, FAR void *data,
           struct rpmsg_rtc_set_s *msg = data;
 
 #ifdef CONFIG_RTC_RPMSG_SYNC_BASETIME
+          struct timespec ts;
+
           g_basetime.tv_sec  = msg->base_sec;
           g_basetime.tv_nsec = msg->base_nsec;
+
+          clock_gettime(CLOCK_REALTIME, &ts);
+          clock_changed_notifier_call_chain(CLOCK_REALTIME, &ts);
 #else
           struct timespec tp;
 
diff --git a/fs/vfs/fs_timerfd.c b/fs/vfs/fs_timerfd.c
index 8434005a6a4..3ca74e7cd93 100644
--- a/fs/vfs/fs_timerfd.c
+++ b/fs/vfs/fs_timerfd.c
@@ -32,8 +32,10 @@
 #include <debug.h>
 
 #include <nuttx/irq.h>
-#include <nuttx/wdog.h>
... (475 lines truncated)
```
</details>
