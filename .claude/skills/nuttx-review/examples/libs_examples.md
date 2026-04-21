# Standard Libraries (libs/) Review Rules

Patterns mined from 182 review deltas in `libs/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| coding-style | 56 | 31% |
| design-philosophy | 36 | 20% |
| code-reuse | 23 | 13% |
| root-cause-fix | 16 | 9% |
| patch-organization | 15 | 8% |
| posix-compliance | 12 | 7% |
| correctness | 9 | 5% |
| concurrency | 6 | 3% |
| error-handling | 4 | 2% |
| variable-scope | 3 | 2% |
| performance | 1 | 1% |
| memory-management | 1 | 1% |

## Severity Distribution

correctness: 69 | design: 65 | style: 48 | performance: 0

## Top Patterns

### 1. coding-style/missing_far_pointer_annotation (3x) [style]

**Rule:** In NuttX, all pointer types in function signatures and casts must use the FAR annotation macro for flat/protected/kernel memory model portability.

**Example:** #3277391 — libs/libc/wchar: add the wcschr implementatioin
**Transformation:** Added FAR annotation to return type and all cast expressions of pointer types in wcschr function.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/wchar/Make.defs b/libs/libc/wchar/Make.defs
index 6b752d86961..500e2d6ebfe 100644
--- a/libs/libc/wchar/Make.defs
+++ b/libs/libc/wchar/Make.defs
@@ -27,7 +27,7 @@ CSRCS += lib_wcscoll.c lib_wcstol.c lib_wcstoll.c lib_wcstoul.c
 CSRCS += lib_wcstoull.c lib_wcstold.c lib_wcstof.c lib_wcstod.c
 CSRCS += lib_swprintf.c lib_mbsnrtowcs.c lib_wcsnrtombs.c lib_mbsinit.c
 CSRCS += lib_mbrlen.c lib_mbsrtowcs.c lib_wcsrtombs.c lib_wcscpy.c
-CSRCS += lib_wcscat.c lib_wcslcat.c lib_wcsncat.c lib_wcsrchr.c
+CSRCS += lib_wcscat.c lib_wcslcat.c lib_wcsncat.c lib_wcsrchr.c lib_wcschr.c
 
 # Add the wchar directory to the build
 
diff --git a/libs/libc/wchar/lib_wcschr.c b/libs/libc/wchar/lib_wcschr.c
new file mode 100644
index 00000000000..6c2ad79673c
--- /dev/null
+++ b/libs/libc/wchar/lib_wcschr.c
@@ -0,0 +1,62 @@
+/****************************************************************************
+ * libs/libc/wchar/lib_wcschr.c
+ *
+ * Copyright © 2005-2014 Rich Felker, et al.
+ *
+ * Permission is hereby granted, free of charge, to any person obtaining
+ * a copy of this software and associated documentation files (the
+ * "Software"), to deal in the Software without restriction, including
+ * without limitation the rights to use, copy, modify, merge, publish,
+ * distribute, sublicense, and/or sell copies of the Software, and to
+ * permit persons to whom the Software is furnished to do so, subject to
+ * the following conditions:
+ *
+ * The above copyright notice and this permission notice shall be
+ * included in all copies or substantial portions of the Software.
+ *
... (47 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libc/wchar/Make.defs b/libs/libc/wchar/Make.defs
index 6b752d86961..500e2d6ebfe 100644
--- a/libs/libc/wchar/Make.defs
+++ b/libs/libc/wchar/Make.defs
@@ -27,7 +27,7 @@ CSRCS += lib_wcscoll.c lib_wcstol.c lib_wcstoll.c lib_wcstoul.c
 CSRCS += lib_wcstoull.c lib_wcstold.c lib_wcstof.c lib_wcstod.c
 CSRCS += lib_swprintf.c lib_mbsnrtowcs.c lib_wcsnrtombs.c lib_mbsinit.c
 CSRCS += lib_mbrlen.c lib_mbsrtowcs.c lib_wcsrtombs.c lib_wcscpy.c
-CSRCS += lib_wcscat.c lib_wcslcat.c lib_wcsncat.c lib_wcsrchr.c
+CSRCS += lib_wcscat.c lib_wcslcat.c lib_wcsncat.c lib_wcsrchr.c lib_wcschr.c
 
 # Add the wchar directory to the build
 
diff --git a/libs/libc/wchar/lib_wcschr.c b/libs/libc/wchar/lib_wcschr.c
new file mode 100644
index 00000000000..5bc3dc03a19
--- /dev/null
+++ b/libs/libc/wchar/lib_wcschr.c
@@ -0,0 +1,62 @@
+/****************************************************************************
+ * libs/libc/wchar/lib_wcschr.c
+ *
+ * Copyright © 2005-2014 Rich Felker, et al.
+ *
+ * Permission is hereby granted, free of charge, to any person obtaining
+ * a copy of this software and associated documentation files (the
+ * "Software"), to deal in the Software without restriction, including
+ * without limitation the rights to use, copy, modify, merge, publish,
+ * distribute, sublicense, and/or sell copies of the Software, and to
+ * permit persons to whom the Software is furnished to do so, subject to
+ * the following conditions:
+ *
+ * The above copyright notice and this permission notice shall be
+ * included in all copies or substantial portions of the Software.
+ *
... (47 lines truncated)
```
</details>

### 2. patch-organization/incomplete_conditional_removal (2x) [correctness]

**Rule:** When removing a config guard, remove it consistently from all layers — source files, public headers, C++ wrappers, CSV symbol tables, and Kconfig — otherwise the API declarations and build conditions become inconsistent with the implementations.

**Example:** #833188 — libc/wchar: remove unnecessary config
**Transformation:** PS1 only removed CONFIG_LIBC_WCHAR guards from .c implementation files; FINAL also removed them from public headers (stdlib.h, cxx/cstdlib, cwchar), Kconfig, libc.csv conditions, and wchar/Kconfig source entry to complete the guard removal across the entire build and API surface.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/stdlib/lib_mblen.c b/libs/libc/stdlib/lib_mblen.c
index e5e3e176f16..64fe2c1a33b 100644
--- a/libs/libc/stdlib/lib_mblen.c
+++ b/libs/libc/stdlib/lib_mblen.c
@@ -25,8 +25,6 @@
 #include <stdlib.h>
 #include <wchar.h>
 
-#ifdef CONFIG_LIBC_WCHAR
-
 /****************************************************************************
  * Public Functions
  ****************************************************************************/
@@ -43,5 +41,3 @@ int mblen(FAR const char *s, size_t n)
 {
   return mbtowc(NULL, s, n);
 }
-
-#endif
diff --git a/libs/libc/stdlib/lib_mbstowcs.c b/libs/libc/stdlib/lib_mbstowcs.c
index 2dd4201beca..41a6c4f670a 100644
--- a/libs/libc/stdlib/lib_mbstowcs.c
+++ b/libs/libc/stdlib/lib_mbstowcs.c
@@ -25,8 +25,6 @@
 #include <stdlib.h>
 #include <wchar.h>
 
-#ifdef CONFIG_LIBC_WCHAR
-
 /****************************************************************************
  * Public Functions
  ****************************************************************************/
@@ -43,5 +41,3 @@ size_t mbstowcs(FAR wchar_t *dst, FAR const char *src, size_t len)
 {
   return mbsrtowcs(dst, &src, len, NULL);
... (729 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/cxx/cstdlib b/include/cxx/cstdlib
index 8443f4f7b0d..51bc3d8d261 100644
--- a/include/cxx/cstdlib
+++ b/include/cxx/cstdlib
@@ -86,10 +86,8 @@ namespace std
 
   // Wide character operations
 
-#ifdef CONFIG_LIBC_WCHAR
   using ::mbtowc;
   using ::wctomb;
-#endif
 
   // Memory Management
 
diff --git a/include/cxx/cwchar b/include/cxx/cwchar
index 8101b6f481e..6df46055f95 100644
--- a/include/cxx/cwchar
+++ b/include/cxx/cwchar
@@ -41,8 +41,6 @@ namespace std
   using ::wint_t;
   using ::wctype_t;
   using ::size_t;
-
-#ifdef CONFIG_LIBC_WCHAR
   using ::btowc;
   using ::fwprintf;
   using ::fwscanf;
@@ -126,7 +124,6 @@ namespace std
   using ::wmemset;
   using ::wprintf;
   using ::wscanf;
-#endif
 };
 
... (1134 lines truncated)
```
</details>

### 3. root-cause-fix/minimal_correct_change (2x) [correctness]

**Rule:** Comment out only the specific rules that need disabling; do not blanket-comment adjacent logic (download, build) that may still be required — apply the minimum correct change to achieve the goal.

**Example:** #816070 — libxx: use libxxabi, uClibxx in local code
**Transformation:** PS1 blanket-commented all Makefile rules (download, extract, distclean, context); FINAL selectively commented only distclean/DELDIR cleanup and context trigger, preserving download and build rules so the source acquisition logic remains functional if needed.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libxx/libcxxabi.defs b/libs/libxx/libcxxabi.defs
index d9e8babdcd9..c703252eebf 100644
--- a/libs/libxx/libcxxabi.defs
+++ b/libs/libxx/libcxxabi.defs
@@ -20,20 +20,20 @@
 
 VERSION=12.0.0
 
-libcxxabi-$(VERSION).src.tar.xz:
-	$(Q) curl -O -L https://github.com/llvm/llvm-project/releases/download/llvmorg-$(VERSION)/libcxxabi-$(VERSION).src.tar.xz
-
-libcxxabi: libcxxabi-$(VERSION).src.tar.xz
-	$(Q) tar -xf libcxxabi-$(VERSION).src.tar.xz
-	$(Q) mv libcxxabi-$(VERSION).src libcxxabi
-	$(Q) patch -p0 < 0001-libc-abi-avoid-the-waring-__EXCEPTIONS-is-not-define.patch
-	$(Q) touch $@
-
-distclean::
-	$(Q) $(DELFILE) libcxxabi-$(VERSION).src.tar.xz
-	$(call DELDIR, libcxxabi)
-
-context:: libcxxabi
+#libcxxabi-$(VERSION).src.tar.xz:
+#	$(Q) curl -O -L https://github.com/llvm/llvm-project/releases/download/llvmorg-$(VERSION)/libcxxabi-$(VERSION).src.tar.xz
+#
+#libcxxabi: libcxxabi-$(VERSION).src.tar.xz
+#	$(Q) tar -xf libcxxabi-$(VERSION).src.tar.xz
+#	$(Q) mv libcxxabi-$(VERSION).src libcxxabi
+#	$(Q) patch -p0 < 0001-libc-abi-avoid-the-waring-__EXCEPTIONS-is-not-define.patch
+#	$(Q) touch $@
+#
+#distclean::
+#	$(Q) $(DELFILE) libcxxabi-$(VERSION).src.tar.xz
+#	$(call DELDIR, libcxxabi)
+#
... (35 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libxx/libcxxabi.defs b/libs/libxx/libcxxabi.defs
index 4e4b5b51a21..cd1b5fd340e 100644
--- a/libs/libxx/libcxxabi.defs
+++ b/libs/libxx/libcxxabi.defs
@@ -29,11 +29,11 @@ libcxxabi: libcxxabi-$(LIBCXXABI_VERSION).src.tar.xz
 	$(Q) patch -p0 < 0001-libc-abi-avoid-the-waring-__EXCEPTIONS-is-not-define.patch
 	$(Q) touch $@
 
-distclean::
-	$(Q) $(DELFILE) libcxxabi-$(LIBCXXABI_VERSION).src.tar.xz
-	$(call DELDIR, libcxxabi)
+#distclean::
+#	$(Q) $(DELFILE) libcxxabi-$(LIBCXXABI_VERSION).src.tar.xz
+#	$(call DELDIR, libcxxabi)
 
-context:: libcxxabi
+#context:: libcxxabi
 
 ifeq ($(CONFIG_LIBCXXABI), y)
 CXXFLAGS += ${shell $(DEFINE) "$(CC)" LIBCXX_BUILDING_LIBCXXABI}
diff --git a/libs/libxx/uClibc++.defs b/libs/libxx/uClibc++.defs
index c347f3c9af9..583190cc593 100644
--- a/libs/libxx/uClibc++.defs
+++ b/libs/libxx/uClibc++.defs
@@ -21,10 +21,10 @@
 UCLIBCXX_VERSION=0.2.5
 
 $(TOPDIR)/include/uClibc++:
-	curl -O -L https://git.busybox.net/uClibc++/snapshot/uClibc++-$(UCLIBCXX_VERSION).tar.bz2
-	$(Q) tar -xf uClibc++-$(UCLIBCXX_VERSION).tar.bz2
-	$(Q) $(DELFILE) uClibc++-$(UCLIBCXX_VERSION).tar.bz2
-	$(Q) mv uClibc++-$(UCLIBCXX_VERSION) uClibc++
+#	curl -O -L https://git.busybox.net/uClibc++/snapshot/uClibc++-$(UCLIBCXX_VERSION).tar.bz2
+#	$(Q) tar -xf uClibc++-$(UCLIBCXX_VERSION).tar.bz2
+#	$(Q) $(DELFILE) uClibc++-$(UCLIBCXX_VERSION).tar.bz2
... (14 lines truncated)
```
</details>

### 4. coding-style/redundant_early_return_to_inline_guard (1x) [style]

**Rule:** When adding a NULL check before an existing condition that uses the same variable, merge it into the existing condition rather than adding a separate early-return block

**Example:** #7166060 — libc/tls: Fix NULL pointer dereference in task_tls_get_value()
**Transformation:** Replaced separate NULL-check early-return block with inline NULL guard in the existing conditional expression

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/tls/task_tls.c b/libs/libc/tls/task_tls.c
index c2a796b3a5..0c3414f135 100644
--- a/libs/libc/tls/task_tls.c
+++ b/libs/libc/tls/task_tls.c
@@ -56,6 +56,11 @@ uintptr_t task_tls_get_value(int tlsindex)
 {
   FAR struct task_info_s *info = task_get_info();
 
+  if (info == NULL)
+    {
+      return 0;
+    }
+
   if (tlsindex >= 0 && tlsindex < CONFIG_TLS_TASK_NELEM)
     {
       return info->ta_telem[tlsindex];

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libc/tls/task_tls.c b/libs/libc/tls/task_tls.c
index c2a796b3a5..1714e1b26e 100644
--- a/libs/libc/tls/task_tls.c
+++ b/libs/libc/tls/task_tls.c
@@ -56,7 +56,7 @@ uintptr_t task_tls_get_value(int tlsindex)
 {
   FAR struct task_info_s *info = task_get_info();
 
-  if (tlsindex >= 0 && tlsindex < CONFIG_TLS_TASK_NELEM)
+  if (info && tlsindex >= 0 && tlsindex < CONFIG_TLS_TASK_NELEM)
     {
       return info->ta_telem[tlsindex];
     }

```
</details>

### 5. design-philosophy/proper_limit_with_kconfig_over_symptom_patch (1x) [design]

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

### 7. coding-style/snprintf_continuation_alignment (1x) [style]

**Rule:** In multi-line function calls, continuation lines must align arguments to the opening parenthesis of the function call, not use arbitrary indentation

**Example:** #6077567 — Implement comprehensive strftime with full format specifier support.
**Transformation:** Fixed indentation of snprintf continuation lines to align arguments with the opening parenthesis, consistently across cases 'C', 'c', 'F', and 'x'

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/time/lib_strftime.c b/libs/libc/time/lib_strftime.c
index ddb64663a36..6d886f272a7 100644
--- a/libs/libc/time/lib_strftime.c
+++ b/libs/libc/time/lib_strftime.c
@@ -214,8 +214,11 @@ static int get_week_year(FAR const struct tm *time)
  *   %A     The full weekday name according to the current locale.
  *   %b     The abbreviated month name according to the current locale.
  *   %B     The full month name according to the current locale.
+ *   %c     The complete date and time representation
+ *          in format "wDay Mon DD HH:MM:SS YYYY"
  *   %C     The century number (year/100) as a 2-digit integer. (SU)
  *   %d     The day of the month as a decimal number (range 01 to 31).
+ *   %D     Date in %m/%d/%y format
  *   %e     Like %d, the day of the month as a decimal number, but a leading
  *          zero is replaced by a space.
  *   %F     The full date format but with no time fields
@@ -257,8 +260,9 @@ static int get_week_year(FAR const struct tm *time)
  *   %X     The locale's appropriate time representation, but without date.
  *   %y     The year as a decimal number without a century (range 00 to 99).
  *   %Y     The year as a decimal number including the century.
- *   %z     The timezone name or abbreviation, or by no bytes if no timezone
- *          information exists.
+ *   %z     Numeric timezone as hour and minute offset from UTC
+ *          "+hhmm" or "-hhmm"
+ *   %Z     Time zone name.
  *   %%     A literal '%' character.
  *
  * Returned Value:
@@ -278,6 +282,12 @@ size_t strftime(FAR char *s, size_t max, FAR const char *format,
   int             value;
   int             len;
 
+#ifdef CONFIG_LIBC_LOCALTIME
+  /* Ensure tzname is up to date at the beginning of strftime */
+
... (165 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libc/time/lib_strftime.c b/libs/libc/time/lib_strftime.c
index ddb64663a36..c2e5f87f136 100644
--- a/libs/libc/time/lib_strftime.c
+++ b/libs/libc/time/lib_strftime.c
@@ -214,8 +214,11 @@ static int get_week_year(FAR const struct tm *time)
  *   %A     The full weekday name according to the current locale.
  *   %b     The abbreviated month name according to the current locale.
  *   %B     The full month name according to the current locale.
+ *   %c     The complete date and time representation
+ *          in format "wDay Mon DD HH:MM:SS YYYY"
  *   %C     The century number (year/100) as a 2-digit integer. (SU)
  *   %d     The day of the month as a decimal number (range 01 to 31).
+ *   %D     Date in %m/%d/%y format
  *   %e     Like %d, the day of the month as a decimal number, but a leading
  *          zero is replaced by a space.
  *   %F     The full date format but with no time fields
@@ -257,8 +260,9 @@ static int get_week_year(FAR const struct tm *time)
  *   %X     The locale's appropriate time representation, but without date.
  *   %y     The year as a decimal number without a century (range 00 to 99).
  *   %Y     The year as a decimal number including the century.
- *   %z     The timezone name or abbreviation, or by no bytes if no timezone
- *          information exists.
+ *   %z     Numeric timezone as hour and minute offset from UTC
+ *          "+hhmm" or "-hhmm"
+ *   %Z     Time zone name.
  *   %%     A literal '%' character.
  *
  * Returned Value:
@@ -278,6 +282,12 @@ size_t strftime(FAR char *s, size_t max, FAR const char *format,
   int             value;
   int             len;
 
+#ifdef CONFIG_LIBC_LOCALTIME
+  /* Ensure tzname is up to date at the beginning of strftime */
+
... (167 lines truncated)
```
</details>

### 8. coding-style/function_name_typo (1x) [correctness]

**Rule:** Always verify function names in declarations match the POSIX standard spelling exactly; typos in headers propagate silently as link errors or wrong symbols.

**Example:** #5872593 — pthread: add pthread_{get|set}concurrency support
**Transformation:** Fixed typo in function declaration: `pthread_gerconcurrency` → `pthread_getconcurrency` in pthread.h header.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/Documentation/reference/user/08_pthread.rst b/Documentation/reference/user/08_pthread.rst
index 67208b5ca59..109279d1869 100644
--- a/Documentation/reference/user/08_pthread.rst
+++ b/Documentation/reference/user/08_pthread.rst
@@ -113,7 +113,6 @@ No support for the following pthread interfaces is provided by NuttX:
 
   -  ``pthread_attr_getscope``. get and set the contentionscope attribute.
   -  ``pthread_attr_setscope``. get and set the contentionscope attribute.
-  -  ``pthread_getconcurrency``. get and set the level of concurrency.
   -  ``pthread_getcpuclockid``. access a thread CPU-time clock.
   -  ``pthread_mutex_getprioceiling``. get and set the priority ceiling of
      a mutex.
@@ -123,7 +122,6 @@ No support for the following pthread interfaces is provided by NuttX:
      attribute of the mutex attributes object.
   -  ``pthread_mutexattr_setprioceiling``. get and set the prioceiling
      attribute of the mutex attributes object.
-  -  ``pthread_setconcurrency``. get and set the level of concurrency.
 
 .. c:function:: int pthread_attr_init(pthread_attr_t *attr);
 
diff --git a/Documentation/standards/posix.rst b/Documentation/standards/posix.rst
index 286563492b6..10241688441 100644
--- a/Documentation/standards/posix.rst
+++ b/Documentation/standards/posix.rst
@@ -34,7 +34,7 @@ Units of Functionality Requirements:
 +------------------------------+----------------+--------------------------+
 | `POSIX_THREADS_EXT`_ [#fn2]_ | Yes            |                          |
 +------------------------------+----------------+--------------------------+
-| `XSI_THREADS_EXT`_           | 2/4            |                          |
+| `XSI_THREADS_EXT`_           | Yes            |                          |
 +------------------------------+----------------+--------------------------+
 
 .. [#fn1] ``fenv.h`` related functions not supported.
@@ -2450,17 +2450,17 @@ XSI_THREADS_EXT
 
... (163 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/Documentation/reference/user/08_pthread.rst b/Documentation/reference/user/08_pthread.rst
index 67208b5ca59..109279d1869 100644
--- a/Documentation/reference/user/08_pthread.rst
+++ b/Documentation/reference/user/08_pthread.rst
@@ -113,7 +113,6 @@ No support for the following pthread interfaces is provided by NuttX:
 
   -  ``pthread_attr_getscope``. get and set the contentionscope attribute.
   -  ``pthread_attr_setscope``. get and set the contentionscope attribute.
-  -  ``pthread_getconcurrency``. get and set the level of concurrency.
   -  ``pthread_getcpuclockid``. access a thread CPU-time clock.
   -  ``pthread_mutex_getprioceiling``. get and set the priority ceiling of
      a mutex.
@@ -123,7 +122,6 @@ No support for the following pthread interfaces is provided by NuttX:
      attribute of the mutex attributes object.
   -  ``pthread_mutexattr_setprioceiling``. get and set the prioceiling
      attribute of the mutex attributes object.
-  -  ``pthread_setconcurrency``. get and set the level of concurrency.
 
 .. c:function:: int pthread_attr_init(pthread_attr_t *attr);
 
diff --git a/Documentation/standards/posix.rst b/Documentation/standards/posix.rst
index 286563492b6..10241688441 100644
--- a/Documentation/standards/posix.rst
+++ b/Documentation/standards/posix.rst
@@ -34,7 +34,7 @@ Units of Functionality Requirements:
 +------------------------------+----------------+--------------------------+
 | `POSIX_THREADS_EXT`_ [#fn2]_ | Yes            |                          |
 +------------------------------+----------------+--------------------------+
-| `XSI_THREADS_EXT`_           | 2/4            |                          |
+| `XSI_THREADS_EXT`_           | Yes            |                          |
 +------------------------------+----------------+--------------------------+
 
 .. [#fn1] ``fenv.h`` related functions not supported.
@@ -2450,17 +2450,17 @@ XSI_THREADS_EXT
 
... (163 lines truncated)
```
</details>

### 9. root-cause-fix/phony_real_target_conflict (1x) [correctness]

**Rule:** Never mark real filesystem targets (especially directories) as .PHONY; .PHONY is only for targets that do not correspond to actual files, otherwise make will unconditionally re-execute the recipe on every invocation.

**Example:** #5431823 — libbuitin: Fixed the issue of not creating bin directory when the file exists
**Transformation:** Reviewer removed $(BIN_OBJDIR) and $(KBIN_OBJDIR) from .PHONY because they are real filesystem directories that should only be created when missing, not rebuilt every time.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libbuiltin/compiler-rt/Make.defs b/libs/libbuiltin/compiler-rt/Make.defs
index c9fedfe74f6..421c1d4d6e8 100644
--- a/libs/libbuiltin/compiler-rt/Make.defs
+++ b/libs/libbuiltin/compiler-rt/Make.defs
@@ -42,6 +42,9 @@ COMPILER_RT_OBJDIR = compiler-rt \
 BIN_OBJDIR = $(addprefix $(BINDIR)$(DELIM),$(COMPILER_RT_OBJDIR))
 KBIN_OBJDIR = $(addprefix $(KBINDIR)$(DELIM),$(COMPILER_RT_OBJDIR))
 
+$(BIN_OBJDIR) $(KBIN_OBJDIR):
+	$(Q) mkdir -p $@
+
 ifeq ($(wildcard compiler-rt/compiler-rt/lib),)
 
 compiler-rt-$(COMPILER_RT_VERSION).src.tar.xz:
@@ -51,7 +54,6 @@ compiler-rt/compiler-rt: compiler-rt-$(COMPILER_RT_VERSION).src.tar.xz
 	$(Q) tar -xf $<
 	$(Q) mv compiler-rt-$(COMPILER_RT_VERSION).src $@
 	$(call DELDIR, $<)
-	$(Q) mkdir $(BIN_OBJDIR) $(KBIN_OBJDIR)
 
 compiler-rt: compiler-rt/compiler-rt
 
@@ -59,8 +61,8 @@ endif
 
 ifeq ($(CONFIG_LIB_COMPILER_RT),y)
 
-.PHONY: compiler-rt
-depend:: compiler-rt
+.PHONY: compiler-rt $(BIN_OBJDIR) $(KBIN_OBJDIR)
+depend:: compiler-rt $(BIN_OBJDIR) $(KBIN_OBJDIR)
 
 distclean::
 	$(call DELDIR, $(BIN_OBJDIR))

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libbuiltin/compiler-rt/Make.defs b/libs/libbuiltin/compiler-rt/Make.defs
index c9fedfe74f6..12465172d7d 100644
--- a/libs/libbuiltin/compiler-rt/Make.defs
+++ b/libs/libbuiltin/compiler-rt/Make.defs
@@ -42,6 +42,9 @@ COMPILER_RT_OBJDIR = compiler-rt \
 BIN_OBJDIR = $(addprefix $(BINDIR)$(DELIM),$(COMPILER_RT_OBJDIR))
 KBIN_OBJDIR = $(addprefix $(KBINDIR)$(DELIM),$(COMPILER_RT_OBJDIR))
 
+$(BIN_OBJDIR) $(KBIN_OBJDIR):
+	$(Q) mkdir -p $@
+
 ifeq ($(wildcard compiler-rt/compiler-rt/lib),)
 
 compiler-rt-$(COMPILER_RT_VERSION).src.tar.xz:
@@ -51,7 +54,6 @@ compiler-rt/compiler-rt: compiler-rt-$(COMPILER_RT_VERSION).src.tar.xz
 	$(Q) tar -xf $<
 	$(Q) mv compiler-rt-$(COMPILER_RT_VERSION).src $@
 	$(call DELDIR, $<)
-	$(Q) mkdir $(BIN_OBJDIR) $(KBIN_OBJDIR)
 
 compiler-rt: compiler-rt/compiler-rt
 
@@ -60,7 +62,7 @@ endif
 ifeq ($(CONFIG_LIB_COMPILER_RT),y)
 
 .PHONY: compiler-rt
-depend:: compiler-rt
+depend:: compiler-rt $(BIN_OBJDIR) $(KBIN_OBJDIR)
 
 distclean::
 	$(call DELDIR, $(BIN_OBJDIR))

```
</details>

### 10. coding-style/missing_newline_at_eof (1x) [style]

**Rule:** All text files must end with a newline character; missing trailing newlines violate POSIX and can cause toolchain warnings or concatenation issues.

**Example:** #5186510 — libc/errno: add option to custom errno implementation
**Transformation:** Added missing newline at end of Kconfig file to comply with POSIX text file requirements

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/Kconfig b/libs/libc/Kconfig
index b5d5e027173..e36c25fb000 100644
--- a/libs/libc/Kconfig
+++ b/libs/libc/Kconfig
@@ -34,3 +34,4 @@ source "libs/libc/regex/Kconfig"
 source "libs/libc/gnssutils/Kconfig"
 source "libs/libc/fdt/Kconfig"
 source "libs/libc/queue/Kconfig"
+source "libs/libc/errno/Kconfig"
diff --git a/libs/libc/errno/Kconfig b/libs/libc/errno/Kconfig
new file mode 100644
index 00000000000..5716d1bdb5f
--- /dev/null
+++ b/libs/libc/errno/Kconfig
@@ -0,0 +1,15 @@
+#
+# For a description of the syntax of this configuration file,
+# see the file kconfig-language.txt in the NuttX tools repository.
+#
+
+menu "lib_errno custom options"
+
+config LIBC_ERRNO_CUSTOM
+	bool "use custom implemantation of errno"
+	default n
+	---help---
+		If the default implemantation of errno cannot match requirements,
+		Possible remove the default implementation and use a custom one.
+
+endmenu #lib_errno custom options
\ No newline at end of file
diff --git a/libs/libc/errno/lib_errno.c b/libs/libc/errno/lib_errno.c
index 30144fc8450..34cd6183cde 100644
--- a/libs/libc/errno/lib_errno.c
+++ b/libs/libc/errno/lib_errno.c
... (14 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libc/Kconfig b/libs/libc/Kconfig
index b5d5e027173..e36c25fb000 100644
--- a/libs/libc/Kconfig
+++ b/libs/libc/Kconfig
@@ -34,3 +34,4 @@ source "libs/libc/regex/Kconfig"
 source "libs/libc/gnssutils/Kconfig"
 source "libs/libc/fdt/Kconfig"
 source "libs/libc/queue/Kconfig"
+source "libs/libc/errno/Kconfig"
diff --git a/libs/libc/errno/Kconfig b/libs/libc/errno/Kconfig
new file mode 100644
index 00000000000..9960ae45c03
--- /dev/null
+++ b/libs/libc/errno/Kconfig
@@ -0,0 +1,15 @@
+#
+# For a description of the syntax of this configuration file,
+# see the file kconfig-language.txt in the NuttX tools repository.
+#
+
+menu "lib_errno custom options"
+
+config LIBC_ERRNO_CUSTOM
+	bool "use custom implemantation of errno"
+	default n
+	---help---
+		If the default implemantation of errno cannot match requirements,
+		Possible remove the default implementation and use a custom one.
+
+endmenu #lib_errno custom options
diff --git a/libs/libc/errno/lib_errno.c b/libs/libc/errno/lib_errno.c
index 30144fc8450..34cd6183cde 100644
--- a/libs/libc/errno/lib_errno.c
+++ b/libs/libc/errno/lib_errno.c
@@ -26,6 +26,7 @@
... (13 lines truncated)
```
</details>

### 11. design-philosophy/syscall_number_allocation (1x) [design]

**Rule:** New syscalls that must work in all build modes (including CONFIG_BUILD_FLAT) should be allocated in the unconditional syscall number range with proper reserved count updates, rather than appended after conditional syscalls and patched with chip-specific Kconfig exceptions.

**Example:** #5012856 — arm xtensa:add up_assert api
**Transformation:** Moved SYS_assert_handler from appended slot 8 (behind #ifndef CONFIG_BUILD_FLAT) to slot 4 (unconditional), bumped all subsequent syscall numbers, updated CONFIG_SYS_RESERVED, removed chip-specific Kconfig hack, and replaced running_regs() with up_current_regs().

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/Kconfig b/arch/Kconfig
index 3dd95b5873c..b022d743bc5 100644
--- a/arch/Kconfig
+++ b/arch/Kconfig
@@ -16,7 +16,7 @@ config ARCH_ARM
 	select ARCH_HAVE_CUSTOMOPT
 	select ARCH_HAVE_STDARG_H
 	select ARCH_HAVE_SETJMP if !ARCH_TOOLCHAIN_IAR
-	select ARCH_HAVE_SYSCALL
+	select ARCH_HAVE_SYSCALL if !ARCH_CHIP_TLSR82
 	select ARCH_HAVE_SYSCALL_HOOKS
 	select ARCH_HAVE_RDWR_MEM_CPU_RUN
 	select ARCH_HAVE_TCBINFO
diff --git a/arch/arm/include/syscall.h b/arch/arm/include/syscall.h
index 60d7b1b6f71..9ad63180ced 100644
--- a/arch/arm/include/syscall.h
+++ b/arch/arm/include/syscall.h
@@ -123,6 +123,13 @@
 #define SYS_signal_handler_return (7)
 #endif /* !CONFIG_BUILD_FLAT */
 
+/* SYS call 8:
+ *
+ * void _assert(const char *filename, int linenum, const char *msg);
+ */
+
+#define SYS_assert_handler        (8)
+
 /****************************************************************************
  * Public Types
  ****************************************************************************/
diff --git a/arch/arm/src/armv6-m/arm_svcall.c b/arch/arm/src/armv6-m/arm_svcall.c
index 55aa6ea07f1..e1375a7e858 100644
--- a/arch/arm/src/armv6-m/arm_svcall.c
+++ b/arch/arm/src/armv6-m/arm_svcall.c
... (1331 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/include/syscall.h b/arch/arm/include/syscall.h
index 60d7b1b6f71..d4698159dc3 100644
--- a/arch/arm/include/syscall.h
+++ b/arch/arm/include/syscall.h
@@ -58,9 +58,9 @@
  */
 
 #ifndef CONFIG_BUILD_FLAT
-#  define CONFIG_SYS_RESERVED 8
+#  define CONFIG_SYS_RESERVED 9
 #else
-#  define CONFIG_SYS_RESERVED 4
+#  define CONFIG_SYS_RESERVED 5
 #endif
 
 /* Cortex-M system calls ****************************************************/
@@ -88,39 +88,46 @@
 #define SYS_syscall_return        (3)
 #endif /* CONFIG_LIB_SYSCALL */
 
-#ifndef CONFIG_BUILD_FLAT
 /* SYS call 4:
+ *
+ * void uo_assert(const char *filename, int linenum, const char *msg);
+ */
+
+#define SYS_assert_handler        (4)
+
+#ifndef CONFIG_BUILD_FLAT
+/* SYS call 5:
  *
  * void up_task_start(main_t taskentry, int argc, char *argv[])
  *        noreturn_function;
  */
 
... (438 lines truncated)
```
</details>

### 12. coding-style/format_specifier_correctness (1x) [correctness]

**Rule:** When fixing format specifier type-safety (e.g., adding 'z' length modifier for size_t), preserve the original numeric base (x for hex, u for decimal) — fix the type, not the semantics.

**Example:** #4898800 — libs/libc/gdbstub/lib_gdbstub.c: fix warning
**Transformation:** Changed sizeof() format specifier from %zu (decimal) back to %zx (hex) to preserve the original hex output format expected by the GDB protocol.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/gdbstub/lib_gdbstub.c b/libs/libc/gdbstub/lib_gdbstub.c
index ace13079380..f86cb119ecb 100644
--- a/libs/libc/gdbstub/lib_gdbstub.c
+++ b/libs/libc/gdbstub/lib_gdbstub.c
@@ -1376,11 +1376,11 @@ static int gdb_query(FAR struct gdb_state_s *state)
     {
 #ifdef CONFIG_ARCH_HAVE_DEBUG
       state->pkt_len = sprintf(state->pkt_buf,
-                               "hwbreak+;PacketSize=%x",
+                               "hwbreak+;PacketSize=%zu",
                                sizeof(state->pkt_buf));
 #else
       state->pkt_len = sprintf(state->pkt_buf,
-                               "PacketSize=%x",
+                               "PacketSize=%zu",
                                sizeof(state->pkt_buf));
 #endif
       gdb_send_packet(state);

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libc/gdbstub/lib_gdbstub.c b/libs/libc/gdbstub/lib_gdbstub.c
index ed83a9148ee..2da89228e3f 100644
--- a/libs/libc/gdbstub/lib_gdbstub.c
+++ b/libs/libc/gdbstub/lib_gdbstub.c
@@ -1376,11 +1376,11 @@ static int gdb_query(FAR struct gdb_state_s *state)
     {
 #ifdef CONFIG_ARCH_HAVE_DEBUG
       state->pkt_len = sprintf(state->pkt_buf,
-                               "hwbreak+;PacketSize=%x",
+                               "hwbreak+;PacketSize=%zx",
                                sizeof(state->pkt_buf));
 #else
       state->pkt_len = sprintf(state->pkt_buf,
-                               "PacketSize=%x",
+                               "PacketSize=%zx",
                                sizeof(state->pkt_buf));
 #endif
       gdb_send_packet(state);

```
</details>

### 13. correctness/uninitialized_union_fields (1x) [correctness]

**Rule:** Always zero-initialize unions and structs before partial field assignment to prevent undefined behavior from uninitialized padding or inactive members.

**Example:** #4747335 — netdb: fix may add duplicate DNS servers
**Transformation:** Added memset to zero-initialize union before partial IPv4/IPv6 address conversion, ensuring no stale data leaks through unused fields.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libc/netdb/lib_dnsforeach.c b/libs/libc/netdb/lib_dnsforeach.c
index 75d4e58cf27..f9c4ea06b18 100644
--- a/libs/libc/netdb/lib_dnsforeach.c
+++ b/libs/libc/netdb/lib_dnsforeach.c
@@ -176,6 +176,7 @@ int dns_foreach_nameserver(dns_callback_t callback, FAR void *arg)
             }
 #endif /* CONFIG_NETDB_RESOLVCONF_NONSTDPORT */
 
+          memset(&u, 0, sizeof(u));
 #ifdef CONFIG_NET_IPv4
           /* Try to convert the IPv4 address */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libc/netdb/lib_dnsforeach.c b/libs/libc/netdb/lib_dnsforeach.c
index fb7e40b29b1..8aa3f8ea6de 100644
--- a/libs/libc/netdb/lib_dnsforeach.c
+++ b/libs/libc/netdb/lib_dnsforeach.c
@@ -176,6 +176,7 @@ int dns_foreach_nameserver(dns_callback_t callback, FAR void *arg)
             }
 #endif /* CONFIG_NETDB_RESOLVCONF_NONSTDPORT */
 
+          memset(&u, 0, sizeof(u));
 #ifdef CONFIG_NET_IPv4
           /* Try to convert the IPv4 address */
 

```
</details>

### 14. coding-style/typo_fix_in_variable_name (1x) [correctness]

**Rule:** Variable and function names must be spelled correctly; typos in build system variables cause silent failures when the misspelled name is referenced elsewhere.

**Example:** #4622875 — cmake(sync):resolve cmake sync conflicts in NuttX upgrade
**Transformation:** Reviewer caught a typo: `PREPROCES` was corrected to `PREPROCESS` in the final merged version of nuttx_toolchain.cmake.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/cmake/nuttx_generate_sim_ld.cmake b/cmake/nuttx_generate_sim_ld.cmake
index 2d974942d40..c59301dd6ce 100644
--- a/cmake/nuttx_generate_sim_ld.cmake
+++ b/cmake/nuttx_generate_sim_ld.cmake
@@ -60,5 +60,6 @@ add_custom_command(
     $<$<BOOL:${CONFIG_SIM_M32}>:-m32> -Wl,-verbose 2> /dev/null > nuttx-orig.ld
     || true
   COMMAND sh process_sim_ld_script.sh nuttx-orig.ld nuttx.ld
+  COMMAND sed -i '/\\.data *:/i " ${CONFIG_SIM_CUSTOM_DATA_SECTION} " ' nuttx.ld
   COMMENT "Generating sim linker script nuttx.ld"
   WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
diff --git a/cmake/nuttx_multiple_link.cmake b/cmake/nuttx_multiple_link.cmake
index f8aa11235d3..fef695f52dc 100644
--- a/cmake/nuttx_multiple_link.cmake
+++ b/cmake/nuttx_multiple_link.cmake
@@ -1,6 +1,8 @@
 # ##############################################################################
 # cmake/nuttx_multiple_link.cmake
 #
+# SPDX-License-Identifier: Apache-2.0
+#
 # Licensed to the Apache Software Foundation (ASF) under one or more contributor
 # license agreements.  See the NOTICE file distributed with this work for
 # additional information regarding copyright ownership.  The ASF licenses this
diff --git a/cmake/nuttx_source_file_properties.cmake b/cmake/nuttx_source_file_properties.cmake
index 25a7f6493db..64dc84d1bed 100644
--- a/cmake/nuttx_source_file_properties.cmake
+++ b/cmake/nuttx_source_file_properties.cmake
@@ -1,6 +1,8 @@
 # ##############################################################################
 # cmake/nuttx_source_file_properties.cmake
 #
+# SPDX-License-Identifier: Apache-2.0
+#
 # Licensed to the Apache Software Foundation (ASF) under one or more contributor
... (61 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/cmake/nuttx_generate_sim_ld.cmake b/cmake/nuttx_generate_sim_ld.cmake
index 2d974942d40..c59301dd6ce 100644
--- a/cmake/nuttx_generate_sim_ld.cmake
+++ b/cmake/nuttx_generate_sim_ld.cmake
@@ -60,5 +60,6 @@ add_custom_command(
     $<$<BOOL:${CONFIG_SIM_M32}>:-m32> -Wl,-verbose 2> /dev/null > nuttx-orig.ld
     || true
   COMMAND sh process_sim_ld_script.sh nuttx-orig.ld nuttx.ld
+  COMMAND sed -i '/\\.data *:/i " ${CONFIG_SIM_CUSTOM_DATA_SECTION} " ' nuttx.ld
   COMMENT "Generating sim linker script nuttx.ld"
   WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
diff --git a/cmake/nuttx_multiple_link.cmake b/cmake/nuttx_multiple_link.cmake
index f8aa11235d3..fef695f52dc 100644
--- a/cmake/nuttx_multiple_link.cmake
+++ b/cmake/nuttx_multiple_link.cmake
@@ -1,6 +1,8 @@
 # ##############################################################################
 # cmake/nuttx_multiple_link.cmake
 #
+# SPDX-License-Identifier: Apache-2.0
+#
 # Licensed to the Apache Software Foundation (ASF) under one or more contributor
 # license agreements.  See the NOTICE file distributed with this work for
 # additional information regarding copyright ownership.  The ASF licenses this
diff --git a/cmake/nuttx_source_file_properties.cmake b/cmake/nuttx_source_file_properties.cmake
index 25a7f6493db..64dc84d1bed 100644
--- a/cmake/nuttx_source_file_properties.cmake
+++ b/cmake/nuttx_source_file_properties.cmake
@@ -1,6 +1,8 @@
 # ##############################################################################
 # cmake/nuttx_source_file_properties.cmake
 #
+# SPDX-License-Identifier: Apache-2.0
+#
 # Licensed to the Apache Software Foundation (ASF) under one or more contributor
... (61 lines truncated)
```
</details>

### 15. correctness/wrong_config_variable_name (1x) [correctness]

**Rule:** Use the canonical Kconfig variable name (CONFIG_ARCH_TOOLCHAIN_CLANG, not CONFIG_ARM_TOOLCHAIN_CLANG) and correct CMake keyword casing (NOT, not `not`) to ensure conditional compilation guards work across all architectures and build systems.

**Example:** #4571371 — nuttx/libcxx: Fix clang compilation cannot recognize compilation options error.
**Transformation:** Changed `CONFIG_ARM_TOOLCHAIN_CLANG` to `CONFIG_ARCH_TOOLCHAIN_CLANG` and fixed CMake case `not` to `NOT` for correct Kconfig variable reference and CMake syntax

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/libs/libxx/libcxx.cmake b/libs/libxx/libcxx.cmake
index 7aa31172e5f..f387802c2c9 100644
--- a/libs/libxx/libcxx.cmake
+++ b/libs/libxx/libcxx.cmake
@@ -99,12 +99,16 @@ if(NOT CONFIG_CXX_LOCALIZATION)
   list(REMOVE_ITEM SRCS ${SRCSTMP})
 endif()
 
+set(FLAGS -Wno-attributes -Wno-deprecated-declarations -Wno-shadow
+          -Wno-sign-compare)
+
+if(not CONFIG_ARM_TOOLCHAIN_CLANG)
+  list(APPEND FLAGS -Wno-maybe-uninitialized -Wno-alloc-size-larger-than)
+endif()
+
 nuttx_add_system_library(libcxx)
 target_sources(libcxx PRIVATE ${SRCS})
-target_compile_options(
-  libcxx
-  PRIVATE -Wno-maybe-uninitialized -Wno-alloc-size-larger-than -Wno-attributes
-          -Wno-deprecated-declarations -Wno-shadow -Wno-sign-compare)
+target_compile_options(libcxx PRIVATE ${FLAGS})
 
 target_include_directories(libcxx BEFORE
                            PRIVATE ${CMAKE_CURRENT_LIST_DIR}/libcxx/src)
diff --git a/libs/libxx/libcxx.defs b/libs/libxx/libcxx.defs
index 77bbb21681b..89f08631c2d 100644
--- a/libs/libxx/libcxx.defs
+++ b/libs/libxx/libcxx.defs
@@ -53,8 +53,12 @@ ifeq ($(CONFIG_LIBSUPCXX), y)
 CXXFLAGS += ${DEFINE_PREFIX}__GLIBCXX__
 endif
 
-CXXFLAGS += -Wno-shadow -Wno-maybe-uninitialized -Wno-alloc-size-larger-than
-CXXFLAGS += -Wno-sign-compare -Wno-attributes -Wno-deprecated-declarations
... (10 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/libs/libxx/libcxx.cmake b/libs/libxx/libcxx.cmake
index 7aa31172e5f..95f9a2ac88a 100644
--- a/libs/libxx/libcxx.cmake
+++ b/libs/libxx/libcxx.cmake
@@ -99,12 +99,16 @@ if(NOT CONFIG_CXX_LOCALIZATION)
   list(REMOVE_ITEM SRCS ${SRCSTMP})
 endif()
 
+set(FLAGS -Wno-attributes -Wno-deprecated-declarations -Wno-shadow
+          -Wno-sign-compare)
+
+if(NOT CONFIG_ARCH_TOOLCHAIN_CLANG)
+  list(APPEND FLAGS -Wno-maybe-uninitialized -Wno-alloc-size-larger-than)
+endif()
+
 nuttx_add_system_library(libcxx)
 target_sources(libcxx PRIVATE ${SRCS})
-target_compile_options(
-  libcxx
-  PRIVATE -Wno-maybe-uninitialized -Wno-alloc-size-larger-than -Wno-attributes
-          -Wno-deprecated-declarations -Wno-shadow -Wno-sign-compare)
+target_compile_options(libcxx PRIVATE ${FLAGS})
 
 target_include_directories(libcxx BEFORE
                            PRIVATE ${CMAKE_CURRENT_LIST_DIR}/libcxx/src)
diff --git a/libs/libxx/libcxx.defs b/libs/libxx/libcxx.defs
index 77bbb21681b..4cafb09e630 100644
--- a/libs/libxx/libcxx.defs
+++ b/libs/libxx/libcxx.defs
@@ -53,8 +53,12 @@ ifeq ($(CONFIG_LIBSUPCXX), y)
 CXXFLAGS += ${DEFINE_PREFIX}__GLIBCXX__
 endif
 
-CXXFLAGS += -Wno-shadow -Wno-maybe-uninitialized -Wno-alloc-size-larger-than
-CXXFLAGS += -Wno-sign-compare -Wno-attributes -Wno-deprecated-declarations
... (10 lines truncated)
```
</details>
