# Other Subsystems Review Rules

Patterns mined from 246 review deltas in `other/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| coding-style | 88 | 36% |
| design-philosophy | 49 | 20% |
| patch-organization | 33 | 13% |
| root-cause-fix | 26 | 11% |
| code-reuse | 21 | 9% |
| other | 10 | 4% |
| correctness | 9 | 4% |
| error-handling | 4 | 2% |
| testing | 2 | 1% |
| performance | 2 | 1% |
| concurrency | 1 | 0% |
| variable-scope | 1 | 0% |

## Severity Distribution

correctness: 94 | design: 72 | style: 79 | performance: 1

## Top Patterns

### 1. other/no_review_delta (2x) [style]

**Rule:** When a patch is clean and self-contained (adding a missing field to both display and data output consistently), it can be merged as-is without review changes.

**Example:** #5312822 — nxgdb/crash: add stack size to stackoverflow report
**Transformation:** PS1 and FINAL diffs are identical — the patch was accepted without modification during review.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py b/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py
index 97bea4cd3e9..ffee2c0afb9 100644
--- a/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py
+++ b/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py
@@ -62,9 +62,13 @@ class CrashStackOverflow(gdb.Command):
             gdb.write("No stack overflow found\n")
             return
 
-        print(f"Found stack overflow threads\n{'PID':<4} {'name':<10} {'FILLED'}")
+        print(
+            f"Found stack overflow threads\n{'PID':<4} {'NAME':<10} {'STACKSIZE':<10} {'FILLED'}"
+        )
         for tcb, filled in collected:
-            print(f"{tcb['pid']:<4} {utils.get_task_name(tcb):<10} {filled:.2%}")
+            print(
+                f"{tcb['pid']:<4} {utils.get_task_name(tcb):<10} {tcb.adj_stack_size:<10} {filled:.2%}"
+            )
 
     def diagnose(self, *args, **kwargs):
         collected = self.collect(utils.get_tcbs())
@@ -77,6 +81,7 @@ class CrashStackOverflow(gdb.Command):
                 {
                     "pid": tcb["pid"],
                     "name": utils.get_task_name(tcb),
+                    "stacksize": tcb.adj_stack_size,
                     "filled": filled,
                     "backtrace": utils.Backtrace(utils.get_backtrace(int(tcb["pid"]))),
                 }

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py b/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py
index d6d2ad22343..0148d52d6e2 100644
--- a/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py
+++ b/tools/pynuttx/nxgdb/nxcrash/stackoverflow.py
@@ -62,9 +62,13 @@ class CrashStackOverflow(gdb.Command):
             gdb.write("No stack overflow found\n")
             return
 
-        print(f"Found stack overflow threads\n{'PID':<4} {'name':<10} {'FILLED'}")
+        print(
+            f"Found stack overflow threads\n{'PID':<4} {'NAME':<10} {'STACKSIZE':<10} {'FILLED'}"
+        )
         for tcb, filled in collected:
-            print(f"{tcb['pid']:<4} {utils.get_task_name(tcb):<10} {filled:.2%}")
+            print(
+                f"{tcb['pid']:<4} {utils.get_task_name(tcb):<10} {tcb.adj_stack_size:<10} {filled:.2%}"
+            )
 
     def diagnose(self, *args, **kwargs):
         collected = self.collect(utils.get_tcbs())
@@ -77,6 +81,7 @@ class CrashStackOverflow(gdb.Command):
                 {
                     "pid": tcb["pid"],
                     "name": utils.get_task_name(tcb),
+                    "stacksize": tcb.adj_stack_size,
                     "filled": filled,
                     "backtrace": utils.Backtrace(utils.get_backtrace(int(tcb["pid"]))),
                 }

```
</details>

### 2. coding-style/preprocessor_indentation (2x) [style]

**Rule:** Preprocessor directives (#define, #undef, #if, #else, #endif, extern "C") must start at column 0 with no leading whitespace in NuttX header files

**Example:** #5247065 — inotify.h(bugfix): Fix symbol demangle issue when C++ references inotify
**Transformation:** Removed leading space before #undef and other preprocessor directives so they start at column 0 per NuttX coding style

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/sys/inotify.h b/include/sys/inotify.h
index fdc488800f6..df83fe338ff 100644
--- a/include/sys/inotify.h
+++ b/include/sys/inotify.h
@@ -87,6 +87,15 @@ struct inotify_event
  * Public Function Prototypes
  ****************************************************************************/
 
+ #undef EXTERN
+ #if defined(__cplusplus)
+ #define EXTERN extern "C"
+ extern "C"
+ {
+ #else
+ #define EXTERN extern
+ #endif
+
 /****************************************************************************
  * Name: inotify_init
  *
@@ -168,4 +177,9 @@ int inotify_add_watch(int fd, FAR const char *pathname, uint32_t mask);
 
 int inotify_rm_watch(int fd, int wd);
 
+#undef EXTERN
+#ifdef __cplusplus
+}
 #endif
+
+#endif /* __INCLUDE_SYS_INOTIFY_H */

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/sys/inotify.h b/include/sys/inotify.h
index fdc488800f6..0834c07ce14 100644
--- a/include/sys/inotify.h
+++ b/include/sys/inotify.h
@@ -87,6 +87,15 @@ struct inotify_event
  * Public Function Prototypes
  ****************************************************************************/
 
+#undef EXTERN
+#if defined(__cplusplus)
+#define EXTERN extern "C"
+extern "C"
+{
+#else
+#define EXTERN extern
+#endif
+
 /****************************************************************************
  * Name: inotify_init
  *
@@ -168,4 +177,9 @@ int inotify_add_watch(int fd, FAR const char *pathname, uint32_t mask);
 
 int inotify_rm_watch(int fd, int wd);
 
+#undef EXTERN
+#ifdef __cplusplus
+}
 #endif
+
+#endif /* __INCLUDE_SYS_INOTIFY_H */

```
</details>

### 3. patch-organization/unresolved_merge_conflict (2x) [correctness]

**Rule:** Never submit patches containing unresolved merge conflict markers (<<<<<<< / ======= / >>>>>>>); always resolve conflicts and self-review the diff before uploading

**Example:** #4931269 — can: enhance bit timing ioctl to set both nominal and data bit timing
**Transformation:** Resolved merge conflict markers left in PS1 by choosing the CHANGE branch values for CAN_BITTIMING_DATA/NOMINAL macro definitions

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/can/can.h b/include/nuttx/can/can.h
index cbba371d5ee..719de7f824b 100644
--- a/include/nuttx/can/can.h
+++ b/include/nuttx/can/can.h
@@ -526,8 +526,13 @@
 
 /* CAN bit timing support ***************************************************/
 
+<<<<<<< HEAD   (f1b2ea netdev/upper: Fix wrong CPU when RSS is disabled)
 #define CAN_BITTIMING_NOMINAL     0  /* Specifies nominal bittiming */
 #define CAN_BITTIMING_DATA        1  /* Specifies data bittiming */
+=======
+#define CAN_BITTIMING_DATA        0  /* Specifies nominal bittiming */
+#define CAN_BITTIMING_NOMINAL     1  /* Specifies data bittiming */
+>>>>>>> CHANGE (4b36e8 can: enhance bit timing ioctl to set both nominal and data b)
 
 /****************************************************************************
  * Public Types
@@ -877,6 +882,13 @@ struct canioc_bittiming_s
   uint8_t               bt_tseg1;        /* TSEG1 in time quanta */
   uint8_t               bt_tseg2;        /* TSEG2 in time quanta */
   uint8_t               bt_sjw;          /* Synchronization Jump Width in time quanta */
+#ifdef CONFIG_CAN_FD
+  uint8_t               bt_type;         /* Nominal/Data bit timing. This is
+                                          * used to specify which bit timing
+                                          * should be set/obtained. Applies
+                                          * only if CAN FD is configured.
+                                          */
+#endif
 };
 
 /* CANIOC_GET_CONNMODES/CANIOC_SET_CONNMODES:

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/can/can.h b/include/nuttx/can/can.h
index cbba371d5ee..a0aa7e1885f 100644
--- a/include/nuttx/can/can.h
+++ b/include/nuttx/can/can.h
@@ -526,8 +526,8 @@
 
 /* CAN bit timing support ***************************************************/
 
-#define CAN_BITTIMING_NOMINAL     0  /* Specifies nominal bittiming */
-#define CAN_BITTIMING_DATA        1  /* Specifies data bittiming */
+#define CAN_BITTIMING_DATA        0  /* Specifies nominal bittiming */
+#define CAN_BITTIMING_NOMINAL     1  /* Specifies data bittiming */
 
 /****************************************************************************
  * Public Types
@@ -877,6 +877,13 @@ struct canioc_bittiming_s
   uint8_t               bt_tseg1;        /* TSEG1 in time quanta */
   uint8_t               bt_tseg2;        /* TSEG2 in time quanta */
   uint8_t               bt_sjw;          /* Synchronization Jump Width in time quanta */
+#ifdef CONFIG_CAN_FD
+  uint8_t               bt_type;         /* Nominal/Data bit timing. This is
+                                          * used to specify which bit timing
+                                          * should be set/obtained. Applies
+                                          * only if CAN FD is configured.
+                                          */
+#endif
 };
 
 /* CANIOC_GET_CONNMODES/CANIOC_SET_CONNMODES:

```
</details>

### 4. coding-style/macro_naming_convention (2x) [style]

**Rule:** Never use leading-underscore identifiers in public macros (reserved by C/POSIX); prefer descriptive names like `predict_true`/`predict_false` that avoid namespace conflicts with Linux kernel's `likely`/`unlikely`, and keep patches focused on one logical change.

**Example:** #910078 — include/compiler: replace likely to predict_true
**Transformation:** Reviewer rejected `_likely`/`_unlikely` naming (leading underscore is reserved by C standard) and required `predict_true`/`predict_false` instead, also removing unrelated subproject commits from the patch.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/fs/exfat/exfat b/fs/exfat/exfat
new file mode 160000
index 00000000000..26587cda585
--- /dev/null
+++ b/fs/exfat/exfat
@@ -0,0 +1 @@
+Subproject commit 26587cda585e7806f700ba92e5d4af3f86fe232f
diff --git a/include/assert.h b/include/assert.h
index 7ce0e1512d7..e4cfa74ea50 100644
--- a/include/assert.h
+++ b/include/assert.h
@@ -50,11 +50,11 @@
 #define PANIC()          __assert(__FILE__, __LINE__, "panic")
 
 #ifdef CONFIG_DEBUG_ASSERTIONS_EXPRESSION
-#define ASSERT(f)        do { if (unlikely(!(f))) __assert(__FILE__, __LINE__, #f); } while (0)
-#define VERIFY(f)        do { if (unlikely((f) < 0)) __assert(__FILE__, __LINE__, #f); } while (0)
+#define ASSERT(f)        do { if (_unlikely(!(f))) __assert(__FILE__, __LINE__, #f); } while (0)
+#define VERIFY(f)        do { if (_unlikely((f) < 0)) __assert(__FILE__, __LINE__, #f); } while (0)
 #else
-#define ASSERT(f)        do { if (unlikely(!(f))) __assert(__FILE__, __LINE__, NULL); } while (0)
-#define VERIFY(f)        do { if (unlikely((f) < 0)) __assert(__FILE__, __LINE__, NULL); } while (0)
+#define ASSERT(f)        do { if (_unlikely(!(f))) __assert(__FILE__, __LINE__, NULL); } while (0)
+#define VERIFY(f)        do { if (_unlikely((f) < 0)) __assert(__FILE__, __LINE__, NULL); } while (0)
 #endif
 
 #ifdef CONFIG_DEBUG_ASSERTIONS
diff --git a/include/nuttx/compiler.h b/include/nuttx/compiler.h
index 6b39014b455..b7b0b4641a7 100644
--- a/include/nuttx/compiler.h
+++ b/include/nuttx/compiler.h
@@ -156,8 +156,8 @@
 
 /* Branch prediction */
 
... (70 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/assert.h b/include/assert.h
index 7ce0e1512d7..c506102b804 100644
--- a/include/assert.h
+++ b/include/assert.h
@@ -50,11 +50,11 @@
 #define PANIC()          __assert(__FILE__, __LINE__, "panic")
 
 #ifdef CONFIG_DEBUG_ASSERTIONS_EXPRESSION
-#define ASSERT(f)        do { if (unlikely(!(f))) __assert(__FILE__, __LINE__, #f); } while (0)
-#define VERIFY(f)        do { if (unlikely((f) < 0)) __assert(__FILE__, __LINE__, #f); } while (0)
+#define ASSERT(f)        do { if (predict_false(!(f))) __assert(__FILE__, __LINE__, #f); } while (0)
+#define VERIFY(f)        do { if (predict_false((f) < 0)) __assert(__FILE__, __LINE__, #f); } while (0)
 #else
-#define ASSERT(f)        do { if (unlikely(!(f))) __assert(__FILE__, __LINE__, NULL); } while (0)
-#define VERIFY(f)        do { if (unlikely((f) < 0)) __assert(__FILE__, __LINE__, NULL); } while (0)
+#define ASSERT(f)        do { if (predict_false(!(f))) __assert(__FILE__, __LINE__, NULL); } while (0)
+#define VERIFY(f)        do { if (predict_false((f) < 0)) __assert(__FILE__, __LINE__, NULL); } while (0)
 #endif
 
 #ifdef CONFIG_DEBUG_ASSERTIONS
diff --git a/include/nuttx/compiler.h b/include/nuttx/compiler.h
index 6b39014b455..868845e5cac 100644
--- a/include/nuttx/compiler.h
+++ b/include/nuttx/compiler.h
@@ -156,8 +156,8 @@
 
 /* Branch prediction */
 
-#  define likely(x) __builtin_expect((x), 1)
-#  define unlikely(x) __builtin_expect((x), 0)
+#  define predict_true(x) __builtin_expect((x), 1)
+#  define predict_false(x) __builtin_expect((x), 0)
 
 /* Code locate */
 
... (56 lines truncated)
```
</details>

### 5. patch-organization/unrelated_change_in_patch (2x) [style]

**Rule:** Each patch must contain only related changes; unrelated or accidental edits (especially to unrelated files like README.md) must be removed before submission.

**Example:** #883701 — Solved the problem that when multithreading compiles cp
**Transformation:** Removed unrelated README.md modification (blank line replaced with '1') from a patch that restructures openamp Makefile targets

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/README.md b/README.md
index 7404d4f5019..58f8ca0385a 100644
--- a/README.md
+++ b/README.md
@@ -1,5 +1,5 @@
 # APACHE NUTTX (INCUBATING)
-
+1
 * Introduction
   - Incubation Status
 * Community
diff --git a/openamp/open-amp.defs b/openamp/open-amp.defs
index c0012aba56f..adad80e65e6 100644
--- a/openamp/open-amp.defs
+++ b/openamp/open-amp.defs
@@ -50,12 +50,12 @@ open-amp.zip:
 else
 .openamp_headers:
 endif
+
+context::# .openamp_headers
 	$(eval headers := $(wildcard open-amp/lib/include/openamp/*.h))
 	$(shell mkdir -p $(TOPDIR)$(DELIM)include$(DELIM)openamp$(DELIM))
 	$(foreach header,$(headers),$(shell cp -rf $(header) $(TOPDIR)$(DELIM)include$(DELIM)openamp$(DELIM)))
-	touch $@
-
-context:: .openamp_headers
+	touch .openamp_headers
 
 distclean::
 	$(call DELDIR, $(TOPDIR)$(DELIM)include$(DELIM)openamp$(DELIM))

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/openamp/open-amp.defs b/openamp/open-amp.defs
index c0012aba56f..adad80e65e6 100644
--- a/openamp/open-amp.defs
+++ b/openamp/open-amp.defs
@@ -50,12 +50,12 @@ open-amp.zip:
 else
 .openamp_headers:
 endif
+
+context::# .openamp_headers
 	$(eval headers := $(wildcard open-amp/lib/include/openamp/*.h))
 	$(shell mkdir -p $(TOPDIR)$(DELIM)include$(DELIM)openamp$(DELIM))
 	$(foreach header,$(headers),$(shell cp -rf $(header) $(TOPDIR)$(DELIM)include$(DELIM)openamp$(DELIM)))
-	touch $@
-
-context:: .openamp_headers
+	touch .openamp_headers
 
 distclean::
 	$(call DELDIR, $(TOPDIR)$(DELIM)include$(DELIM)openamp$(DELIM))

```
</details>

### 6. coding-style/missing_far_pointer_annotation (2x) [style]

**Rule:** All pointer declarations in NuttX public headers must use the FAR qualifier to support far-pointer architectures.

**Example:** #880422 — mempool:add a private pointer to stroe private data
**Transformation:** Added FAR qualifier to void pointer declaration in struct mempool_s to follow NuttX pointer annotation convention.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/mm/mempool.h b/include/nuttx/mm/mempool.h
index 0ebb22688bb..f834efba19d 100644
--- a/include/nuttx/mm/mempool.h
+++ b/include/nuttx/mm/mempool.h
@@ -68,6 +68,7 @@ struct mempool_s
   size_t     interruptsize; /* The initialize size in interrupt mempool */
   size_t     expandsize;    /* The size of expand block every time for mempool */
   bool       wait;          /* The flag of need to wait when mempool is empty */
+  void      *priv;          /* This pointer is used to store the user's private data */
   mempool_alloc_t alloc;    /* The alloc function for mempool */
   mempool_free_t  free;     /* The free function for mempool */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/mm/mempool.h b/include/nuttx/mm/mempool.h
index 0ebb22688bb..123f21a4d61 100644
--- a/include/nuttx/mm/mempool.h
+++ b/include/nuttx/mm/mempool.h
@@ -68,6 +68,7 @@ struct mempool_s
   size_t     interruptsize; /* The initialize size in interrupt mempool */
   size_t     expandsize;    /* The size of expand block every time for mempool */
   bool       wait;          /* The flag of need to wait when mempool is empty */
+  FAR void  *priv;          /* This pointer is used to store the user's private data */
   mempool_alloc_t alloc;    /* The alloc function for mempool */
   mempool_free_t  free;     /* The free function for mempool */
 

```
</details>

### 7. coding-style/ioctl_number_allocation_gap (2x) [correctness]

**Rule:** When adding new IOCTL numbers, never renumber existing IOCTLs belonging to other subsystems; instead allocate the next unused number after your subsystem's block, accepting gaps to preserve ABI stability.

**Example:** #871700 — fix: sensor: add ioctl cmd for gh3x2x
**Transformation:** Changed new SNIOC_CTRL_PPG_GAIN from 0x0070 to 0x0071 and removed the renumbering of existing SNIOC_DISPLACE_INFO, leaving a gap at 0x0070 instead of shifting other subsystem's ioctl numbers.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/sensors/ioctl.h b/include/nuttx/sensors/ioctl.h
index fd9ebaab893..fd97f30b9dd 100644
--- a/include/nuttx/sensors/ioctl.h
+++ b/include/nuttx/sensors/ioctl.h
@@ -241,15 +241,16 @@
 #define SNIOC_FEAT_MANAGE          _SNIOC(0x006b)  /* Feature manage command */
 #define SNIOC_SET_SCALE_XL         _SNIOC(0x006c)  /* Set accelerator scale command */
 
-/* IOCTL commands unique to the GH3020 */
+/* IOCTL commands unique to the GH3x2x */
 
 #define SNIOC_CTRL_LED_CURRENT     _SNIOC(0x006d)  /* Set LED driver current */
 #define SNIOC_CTRL_OPEN_FACTEST    _SNIOC(0x006e)  /* Enter factory test mode */
 #define SNIOC_CTRL_EXIT_FACTEST    _SNIOC(0x006f)  /* Exit factory test mode */
+#define SNIOC_CTRL_PPG_GAIN        _SNIOC(0x0070)  /* Set PPG TIA gain */
 
 /* IOCTL commands unique to the PAT9126JA */
 
-#define SNIOC_DISPLACE_INFO        _SNIOC(0x0070)  /* Displacement information */
+#define SNIOC_DISPLACE_INFO        _SNIOC(0x0071)  /* Displacement information */
 
 /* Command:      SNIOC_GET_STATE
  * Description:  Get state for all subscribers, include min_interval,

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/sensors/ioctl.h b/include/nuttx/sensors/ioctl.h
index fd9ebaab893..711b23fcb16 100644
--- a/include/nuttx/sensors/ioctl.h
+++ b/include/nuttx/sensors/ioctl.h
@@ -241,11 +241,12 @@
 #define SNIOC_FEAT_MANAGE          _SNIOC(0x006b)  /* Feature manage command */
 #define SNIOC_SET_SCALE_XL         _SNIOC(0x006c)  /* Set accelerator scale command */
 
-/* IOCTL commands unique to the GH3020 */
+/* IOCTL commands unique to the GH3x2x */
 
 #define SNIOC_CTRL_LED_CURRENT     _SNIOC(0x006d)  /* Set LED driver current */
 #define SNIOC_CTRL_OPEN_FACTEST    _SNIOC(0x006e)  /* Enter factory test mode */
 #define SNIOC_CTRL_EXIT_FACTEST    _SNIOC(0x006f)  /* Exit factory test mode */
+#define SNIOC_CTRL_PPG_GAIN        _SNIOC(0x0071)  /* Set PPG TIA gain */
 
 /* IOCTL commands unique to the PAT9126JA */
 

```
</details>

### 8. design-philosophy/struct_field_alignment (2x) [performance]

**Rule:** In sensor data structs, prefer naturally-aligned types (int32_t) over narrow types (int8_t) to avoid implicit padding, ensure consistent struct layout across architectures, and prevent unaligned access penalties.

**Example:** #870590 — fix: sensor: ECG event struct add lead info
**Transformation:** Changed `lead` field type from `int8_t` to `int32_t` to ensure natural alignment and avoid padding issues in the sensor struct.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/sensors/sensor.h b/include/nuttx/sensors/sensor.h
index 93b18b2386f..74ab9fe83ce 100644
--- a/include/nuttx/sensors/sensor.h
+++ b/include/nuttx/sensors/sensor.h
@@ -543,6 +543,7 @@ struct sensor_ecg           /* Type: ECG */
 {
   uint64_t timestamp;       /* Unit is microseconds */
   float ecg;                /* Unit is μV */
+  int8_t lead;              /* Lead detection info. 0-off, 1-on.*/
 };
 
 struct sensor_ppgd          /* Type: PPGD */

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/sensors/sensor.h b/include/nuttx/sensors/sensor.h
index 93b18b2386f..1a20c151a64 100644
--- a/include/nuttx/sensors/sensor.h
+++ b/include/nuttx/sensors/sensor.h
@@ -543,6 +543,7 @@ struct sensor_ecg           /* Type: ECG */
 {
   uint64_t timestamp;       /* Unit is microseconds */
   float ecg;                /* Unit is μV */
+  int32_t lead;             /* Lead detection info. 0-off, 1-on.*/
 };
 
 struct sensor_ppgd          /* Type: PPGD */

```
</details>

### 9. coding-style/trailing_whitespace (2x) [style]

**Rule:** No trailing whitespace before closing parentheses or at end of expressions.

**Example:** #822771 — tools:minidumpserver.py support xtensa
**Transformation:** Removed trailing whitespace before closing parenthesis in the range() call

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/tools/minidumpserver.py b/tools/minidumpserver.py
old mode 100644
new mode 100755
index 9800708ee39..efaa20a5c06
--- a/tools/minidumpserver.py
+++ b/tools/minidumpserver.py
@@ -165,6 +165,28 @@ reg_table = {
         "T6": 31,
         "PC": 32,
     },
+    "xtensa": {
+        "PC": 0,
+        "SAR": 68,
+        "PS": 73,
+        "SCOM": 29,
+        "A0": 21,
+        "A1": 22,
+        "A2": 23,
+        "A3": 24,
+        "A4": 25,
+        "A5": 26,
+        "A6": 27,
+        "A7": 28,
+        "A8": 29,
+        "A9": 30,
+        "A10": 31,
+        "A11": 32,
+        "A12": 33,
+        "A13": 34,
+        "A14": 35,
+        "A15": 36,
+    }
 }
 
 
... (10 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/tools/minidumpserver.py b/tools/minidumpserver.py
old mode 100644
new mode 100755
index 9800708ee39..61ad8a27e13
--- a/tools/minidumpserver.py
+++ b/tools/minidumpserver.py
@@ -165,6 +165,28 @@ reg_table = {
         "T6": 31,
         "PC": 32,
     },
+    "xtensa": {
+        "PC": 0,
+        "SAR": 68,
+        "PS": 73,
+        "SCOM": 29,
+        "A0": 21,
+        "A1": 22,
+        "A2": 23,
+        "A3": 24,
+        "A4": 25,
+        "A5": 26,
+        "A6": 27,
+        "A7": 28,
+        "A8": 29,
+        "A9": 30,
+        "A10": 31,
+        "A11": 32,
+        "A12": 33,
+        "A13": 34,
+        "A14": 35,
+        "A15": 36,
+    }
 }
 
 
... (10 lines truncated)
```
</details>

### 10. code-reuse/trivial_wrapper_to_macro (2x) [design]

**Rule:** When a function is a trivial one-line wrapper around another function with fixed arguments, implement it as a macro in the header rather than creating a separate source file.

**Example:** #832926 — include/stdio:add setlinebuf marco
**Transformation:** Replaced a new .c file with a full function implementation of setlinebuf() with a single-line macro expanding to setvbuf(), eliminating the source file, build system change, and runtime overhead.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/stdio.h b/include/stdio.h
index 0643b8a2903..39fc6ad9e56 100644
--- a/include/stdio.h
+++ b/include/stdio.h
@@ -167,6 +167,7 @@ FAR char *gets_s(FAR char *s, rsize_t n);
 void   rewind(FAR FILE *stream);
 
 void   setbuf(FAR FILE *stream, FAR char *buf);
+void   setlinebuf(FAR FILE *stream);
 int    setvbuf(FAR FILE *stream, FAR char *buffer, int mode, size_t size);
 
 int    ungetc(int c, FAR FILE *stream);
diff --git a/libs/libc/stdio/Make.defs b/libs/libc/stdio/Make.defs
index 7b3e7436688..cc190f478bc 100644
--- a/libs/libc/stdio/Make.defs
+++ b/libs/libc/stdio/Make.defs
@@ -49,7 +49,7 @@ CSRCS += lib_fputs.c lib_ungetc.c lib_fprintf.c
 CSRCS += lib_vfprintf.c lib_stdinstream.c lib_stdoutstream.c lib_stdsistream.c
 CSRCS += lib_stdsostream.c lib_feof.c lib_ferror.c lib_rewind.c lib_clearerr.c
 CSRCS += lib_scanf.c lib_vscanf.c lib_fscanf.c lib_vfscanf.c lib_tmpfile.c
-CSRCS += lib_setbuf.c lib_setvbuf.c lib_libstream.c lib_libfilesem.c
+CSRCS += lib_setbuf.c lib_setlinebuf.c lib_setvbuf.c lib_libstream.c lib_libfilesem.c
 endif
 
 # Add the stdio directory to the build
diff --git a/libs/libc/stdio/lib_setlinebuf.c b/libs/libc/stdio/lib_setlinebuf.c
new file mode 100644
index 00000000000..276ac8379a4
--- /dev/null
+++ b/libs/libc/stdio/lib_setlinebuf.c
@@ -0,0 +1,61 @@
+/****************************************************************************
+ * libs/libc/stdio/lib_setlinebuf.c
+ *
+ * Licensed to the Apache Software Foundation (ASF) under one or more
... (58 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/stdio.h b/include/stdio.h
index 0643b8a2903..6379072ba97 100644
--- a/include/stdio.h
+++ b/include/stdio.h
@@ -96,6 +96,8 @@
 #  define fsetpos64 fsetpos
 #endif
 
+#define setlinebuf(stream)   setvbuf(stream, NULL, _IOLBF, 0)
+
 /****************************************************************************
  * Public Type Definitions
  ****************************************************************************/

```
</details>

### 11. coding-style/defconfig_alphabetical_order (2x) [style]

**Rule:** Defconfig entries must be kept in strict alphabetical order; insert new CONFIG_ options at their sorted position, not adjacent to semantically related options.

**Example:** #773933 — arch/sim: enable nxlooper
**Transformation:** Moved CONFIG_SYSTEM_NXLOOPER before CONFIG_SYSTEM_NXPLAYER to maintain alphabetical ordering of CONFIG entries in defconfig

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/boards/sim/sim/sim/configs/alsa/defconfig b/boards/sim/sim/sim/configs/alsa/defconfig
index 542e43caeaf..5ea7d966b65 100644
--- a/boards/sim/sim/sim/configs/alsa/defconfig
+++ b/boards/sim/sim/sim/configs/alsa/defconfig
@@ -54,6 +54,7 @@ CONFIG_START_MONTH=6
 CONFIG_START_YEAR=2008
 CONFIG_SYSTEM_NSH=y
 CONFIG_SYSTEM_NXPLAYER=y
+CONFIG_SYSTEM_NXLOOPER=y
 CONFIG_SYSTEM_NXRECORDER=y
 CONFIG_USERMAIN_STACKSIZE=4096
 CONFIG_USER_ENTRYPOINT="nsh_main"

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/boards/sim/sim/sim/configs/alsa/defconfig b/boards/sim/sim/sim/configs/alsa/defconfig
index 542e43caeaf..52dff8a3982 100644
--- a/boards/sim/sim/sim/configs/alsa/defconfig
+++ b/boards/sim/sim/sim/configs/alsa/defconfig
@@ -53,6 +53,7 @@ CONFIG_SDCLONE_DISABLE=y
 CONFIG_START_MONTH=6
 CONFIG_START_YEAR=2008
 CONFIG_SYSTEM_NSH=y
+CONFIG_SYSTEM_NXLOOPER=y
 CONFIG_SYSTEM_NXPLAYER=y
 CONFIG_SYSTEM_NXRECORDER=y
 CONFIG_USERMAIN_STACKSIZE=4096

```
</details>

### 12. patch-organization/mixed_concerns_in_single_patch (2x) [design]

**Rule:** A patch that adds board ROMFS/rcS infrastructure must not also bundle unrelated feature enablement (e.g., uORB); each feature should be a separate patch so changes can be reviewed, reverted, and bisected independently.

**Example:** #761200 — bes/a7: add romfs startup rcS
**Transformation:** Removed CONFIG_UORB/CONFIG_UORB_TESTS from defconfig and stripped uorb startup logic from rcS script, leaving only the ROMFS infrastructure setup.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig b/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig
index 2f0626bb0be..7efce989ef0 100644
--- a/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig
+++ b/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig
@@ -89,6 +89,7 @@ CONFIG_FS_PROCFS=y
 CONFIG_FS_PROCFS_EXCLUDE_UPTIME=y
 CONFIG_FS_PROCFS_REGISTER=y
 CONFIG_FS_RAMMAP=y
+CONFIG_FS_ROMFS=y
 CONFIG_GPIO_LOWER_HALF=y
 CONFIG_GRAN=y
 CONFIG_GRAN_INTR=y
@@ -116,10 +117,12 @@ CONFIG_NET_TCP=y
 CONFIG_NET_UDP=y
 CONFIG_NET_USRSOCK=y
 CONFIG_NSH_ARCHINIT=y
+CONFIG_NSH_ARCHROMFS=y
 CONFIG_NSH_BUILTIN_APPS=y
 CONFIG_NSH_FILEIOSIZE=512
 CONFIG_NSH_PROMPT_STRING="A7nsh> "
 CONFIG_NSH_READLINE=y
+CONFIG_NSH_ROMFSETC=y
 CONFIG_OPENAMP=y
 CONFIG_PASS1_BUILDIR="../vendor/bes/framework/services/"
 CONFIG_PIPES=y
@@ -170,3 +173,5 @@ CONFIG_TTY_SIGINT=y
 CONFIG_TTY_SIGSTP=y
 CONFIG_USEC_PER_TICK=1000
 CONFIG_USER_ENTRYPOINT="nsh_main"
+CONFIG_UORB=y
+CONFIG_UORB_TESTS=y
diff --git a/boards/arm/bes/best2001a7_ep/include/nsh_romfsimg.h b/boards/arm/bes/best2001a7_ep/include/nsh_romfsimg.h
new file mode 100644
index 00000000000..2fbc4ac01da
--- /dev/null
... (74 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig b/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig
index 2f0626bb0be..5ff67b41dd5 100644
--- a/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig
+++ b/boards/arm/bes/best2001a7_ep/configs/mirtos/defconfig
@@ -89,6 +89,7 @@ CONFIG_FS_PROCFS=y
 CONFIG_FS_PROCFS_EXCLUDE_UPTIME=y
 CONFIG_FS_PROCFS_REGISTER=y
 CONFIG_FS_RAMMAP=y
+CONFIG_FS_ROMFS=y
 CONFIG_GPIO_LOWER_HALF=y
 CONFIG_GRAN=y
 CONFIG_GRAN_INTR=y
@@ -116,10 +117,12 @@ CONFIG_NET_TCP=y
 CONFIG_NET_UDP=y
 CONFIG_NET_USRSOCK=y
 CONFIG_NSH_ARCHINIT=y
+CONFIG_NSH_ARCHROMFS=y
 CONFIG_NSH_BUILTIN_APPS=y
 CONFIG_NSH_FILEIOSIZE=512
 CONFIG_NSH_PROMPT_STRING="A7nsh> "
 CONFIG_NSH_READLINE=y
+CONFIG_NSH_ROMFSETC=y
 CONFIG_OPENAMP=y
 CONFIG_PASS1_BUILDIR="../vendor/bes/framework/services/"
 CONFIG_PIPES=y
diff --git a/boards/arm/bes/best2001a7_ep/include/nsh_romfsimg.h b/boards/arm/bes/best2001a7_ep/include/nsh_romfsimg.h
new file mode 100644
index 00000000000..2fbc4ac01da
--- /dev/null
+++ b/boards/arm/bes/best2001a7_ep/include/nsh_romfsimg.h
@@ -0,0 +1,25 @@
+/****************************************************************************
+ * boards/arm/bes/best2001a7_ep/include/nsh_romfsimg.h
+ *
+ * Licensed to the Apache Software Foundation (ASF) under one or more
... (61 lines truncated)
```
</details>

### 13. other/missing_dependency_config (2x) [correctness]

**Rule:** When moving feature configs between defconfigs, verify all implicit dependencies are satisfied in the target config — run a dependency check or build to catch missing Kconfig prerequisites.

**Example:** #760368 — bes: enable zblue in mirtos defconfig instead of nsh defconfig
**Transformation:** Added CONFIG_SPINLOCK=y to mirtos defconfig alongside the BT config migration, fixing a missing dependency that CONFIG_SPINLOCK_IRQ requires.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/boards/arm/bes/best2001_ep/configs/mirtos/defconfig b/boards/arm/bes/best2001_ep/configs/mirtos/defconfig
index 7e1ddb06a59..5413dc1f8d2 100755
--- a/boards/arm/bes/best2001_ep/configs/mirtos/defconfig
+++ b/boards/arm/bes/best2001_ep/configs/mirtos/defconfig
@@ -50,6 +50,40 @@ CONFIG_BOARD_EARLY_INITIALIZE=y
 CONFIG_BOARD_LATE_INITIALIZE=y
 CONFIG_BOARD_LOOPSPERMSEC=2988
 CONFIG_BUILD_2PASS=y
+CONFIG_BT=y
+CONFIG_BT_BAS=y
+CONFIG_BT_CENTRAL=y
+CONFIG_BT_DEVICE_NAME="MIBLE_BEST2001"
+CONFIG_BT_DIS=y
+CONFIG_BT_EARLY_INITIALIZE=y
+CONFIG_BT_EATT=y
+CONFIG_BT_GATT_CLIENT=y
+CONFIG_BT_GATT_DYNAMIC_DB=y
+CONFIG_BT_H4=y
+CONFIG_BT_HCI_ECC_STACK_SIZE=2048
+CONFIG_BT_HCI_MESH_EXT=y
+CONFIG_BT_HCI_RAW_H4=y
+CONFIG_BT_HCI_RAW_H4_ENABLE=y
+CONFIG_BT_HCI_TX_STACK_SIZE=2048
+CONFIG_BT_HCI_TX_STACK_SIZE_WITH_PROMPT=y
+CONFIG_BT_HCI_VS=y
+CONFIG_BT_HCI_VS_EXT_DETECT=y
+CONFIG_BT_HOST_CCM=y
+CONFIG_BT_HRS=y
+CONFIG_BT_L2CAP_DYNAMIC_CHANNEL=y
+CONFIG_BT_MAX_CONN=4
+CONFIG_BT_MAX_PAIRED=0
+CONFIG_BT_PERIPHERAL=y
+CONFIG_BT_REMOTE_VERSION=y
+CONFIG_BT_RX_BUF_COUNT=5
+CONFIG_BT_RX_STACK_SIZE=2048
... (56 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/boards/arm/bes/best2001_ep/configs/mirtos/defconfig b/boards/arm/bes/best2001_ep/configs/mirtos/defconfig
index 7e1ddb06a59..ca8de90539d 100755
--- a/boards/arm/bes/best2001_ep/configs/mirtos/defconfig
+++ b/boards/arm/bes/best2001_ep/configs/mirtos/defconfig
@@ -50,6 +50,40 @@ CONFIG_BOARD_EARLY_INITIALIZE=y
 CONFIG_BOARD_LATE_INITIALIZE=y
 CONFIG_BOARD_LOOPSPERMSEC=2988
 CONFIG_BUILD_2PASS=y
+CONFIG_BT=y
+CONFIG_BT_BAS=y
+CONFIG_BT_CENTRAL=y
+CONFIG_BT_DEVICE_NAME="MIBLE_BEST2001"
+CONFIG_BT_DIS=y
+CONFIG_BT_EARLY_INITIALIZE=y
+CONFIG_BT_EATT=y
+CONFIG_BT_GATT_CLIENT=y
+CONFIG_BT_GATT_DYNAMIC_DB=y
+CONFIG_BT_H4=y
+CONFIG_BT_HCI_ECC_STACK_SIZE=2048
+CONFIG_BT_HCI_MESH_EXT=y
+CONFIG_BT_HCI_RAW_H4=y
+CONFIG_BT_HCI_RAW_H4_ENABLE=y
+CONFIG_BT_HCI_TX_STACK_SIZE=2048
+CONFIG_BT_HCI_TX_STACK_SIZE_WITH_PROMPT=y
+CONFIG_BT_HCI_VS=y
+CONFIG_BT_HCI_VS_EXT_DETECT=y
+CONFIG_BT_HOST_CCM=y
+CONFIG_BT_HRS=y
+CONFIG_BT_L2CAP_DYNAMIC_CHANNEL=y
+CONFIG_BT_MAX_CONN=4
+CONFIG_BT_MAX_PAIRED=0
+CONFIG_BT_PERIPHERAL=y
+CONFIG_BT_REMOTE_VERSION=y
+CONFIG_BT_RX_BUF_COUNT=5
+CONFIG_BT_RX_STACK_SIZE=2048
... (64 lines truncated)
```
</details>

### 14. patch-organization/incomplete_cleanup (2x) [correctness]

**Rule:** When removing a feature's invocation, also remove its associated configuration entries in defconfig to avoid orphaned config that misleads future readers and may cause build warnings.

**Example:** #738180 — ameba/miot: remove dhcpd from init script
**Transformation:** Reviewer required removing orphaned defconfig entries (DHCPD_DRIPADDR, DHCPD_IPADDR) that became unused after the dhcpd startup was removed from rcS.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/boards/arm/ameba/amebaZ/src/etc/init.d/rcS b/boards/arm/ameba/amebaZ/src/etc/init.d/rcS
index 3004ae2b9c55..2eb86c851f39 100644
--- a/boards/arm/ameba/amebaZ/src/etc/init.d/rcS
+++ b/boards/arm/ameba/amebaZ/src/etc/init.d/rcS
@@ -67,10 +67,6 @@ uname -a > /dev/syslog
     wapi mode $ifname 3
   #endif
 
-  #ifdef CONFIG_NETUTILS_DHCPD
-    dhcpd $ifname &
-  #endif
-
   #ifdef CONFIG_MIIO_CLIENT
     miio_client &
   #endif

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/boards/arm/ameba/amebaZ/configs/miot/defconfig b/boards/arm/ameba/amebaZ/configs/miot/defconfig
index 0c4e08cff100..cf60b8d0b440 100644
--- a/boards/arm/ameba/amebaZ/configs/miot/defconfig
+++ b/boards/arm/ameba/amebaZ/configs/miot/defconfig
@@ -42,8 +42,6 @@ CONFIG_DEBUG_FEATURES=y
 CONFIG_DRIVERS_IEEE80211=y
 CONFIG_DRIVERS_WIRELESS=y
 CONFIG_EXAMPLES_DHCPD=y
-CONFIG_EXAMPLES_DHCPD_DRIPADDR=0xc0a80101
-CONFIG_EXAMPLES_DHCPD_IPADDR=0xc0a80101
 CONFIG_EXAMPLES_HELLO=y
 CONFIG_FS_LITTLEFS=y
 CONFIG_FS_PROCFS=y
diff --git a/boards/arm/ameba/amebaZ/src/etc/init.d/rcS b/boards/arm/ameba/amebaZ/src/etc/init.d/rcS
index 3004ae2b9c55..2eb86c851f39 100644
--- a/boards/arm/ameba/amebaZ/src/etc/init.d/rcS
+++ b/boards/arm/ameba/amebaZ/src/etc/init.d/rcS
@@ -67,10 +67,6 @@ uname -a > /dev/syslog
     wapi mode $ifname 3
   #endif
 
-  #ifdef CONFIG_NETUTILS_DHCPD
-    dhcpd $ifname &
-  #endif
-
   #ifdef CONFIG_MIIO_CLIENT
     miio_client &
   #endif

```
</details>

### 15. design-philosophy/incomplete_feature_gate (1x) [correctness]

**Rule:** When feature-gating a group of related functions, gate all of them consistently — do not leave some members of the group ungated. Place the split point so the entire logical group moves together.

**Example:** #6557296 — tools/nxgdbmcp: disable automatic registration of nxthread unless explicity enable by user
**Transformation:** PS1 only moved nxgdb_list_thread into register_nxthread_tools but left nxgdb_thread_apply in register_control_flow_tools (ungated). FINAL inserts the function boundary before nxgdb_list_thread so that all three nxthread tools (list, apply, select) are consistently gated behind --enable-nxthread. Also produces a minimal diff by inserting only the def line rather than delete-and-readd the function body.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/tools/pynuttx/nxgdbmcp/gmcp/main.py b/tools/pynuttx/nxgdbmcp/gmcp/main.py
index c1afed51248..d9b739e1cb3 100644
--- a/tools/pynuttx/nxgdbmcp/gmcp/main.py
+++ b/tools/pynuttx/nxgdbmcp/gmcp/main.py
@@ -40,6 +40,12 @@ def main():
     parser = argparse.ArgumentParser(description="GDB mcp server for NuttX")
     parser.add_argument("--stdio", action="store_true", help="enable stdio transport")
     parser.add_argument("--port", type=int, default=20819, help="server port")
+    parser.add_argument(
+        "--enable-nxthread",
+        action="store_true",
+        help="Enable NuttX-specific thread commands (nxthread, info nxthreads). "
+        "Only use when GDB native thread commands are insufficient or unavailable.",
+    )
 
     args = parser.parse_args()
 
@@ -66,6 +72,10 @@ def main():
     tools.register_util_tools(gdb_mcp)
     tools.register_value_tools(gdb_mcp)
 
+    # Only register NuttX-specific thread commands if explicitly enabled
+    if args.enable_nxthread:
+        tools.register_nxthread_tools(gdb_mcp)
+
     try:
         if args.stdio:
             gdb_mcp.run(transport="stdio")
diff --git a/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py b/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py
index 323b3c49259..e82f21fc216 100644
--- a/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py
+++ b/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py
@@ -24,6 +24,7 @@ __all__ = [
     "register_session_tools",
     "register_command_tools",
... (98 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/tools/pynuttx/nxgdbmcp/gmcp/main.py b/tools/pynuttx/nxgdbmcp/gmcp/main.py
index c1afed51248..d9b739e1cb3 100644
--- a/tools/pynuttx/nxgdbmcp/gmcp/main.py
+++ b/tools/pynuttx/nxgdbmcp/gmcp/main.py
@@ -40,6 +40,12 @@ def main():
     parser = argparse.ArgumentParser(description="GDB mcp server for NuttX")
     parser.add_argument("--stdio", action="store_true", help="enable stdio transport")
     parser.add_argument("--port", type=int, default=20819, help="server port")
+    parser.add_argument(
+        "--enable-nxthread",
+        action="store_true",
+        help="Enable NuttX-specific thread commands (nxthread, info nxthreads). "
+        "Only use when GDB native thread commands are insufficient or unavailable.",
+    )
 
     args = parser.parse_args()
 
@@ -66,6 +72,10 @@ def main():
     tools.register_util_tools(gdb_mcp)
     tools.register_value_tools(gdb_mcp)
 
+    # Only register NuttX-specific thread commands if explicitly enabled
+    if args.enable_nxthread:
+        tools.register_nxthread_tools(gdb_mcp)
+
     try:
         if args.stdio:
             gdb_mcp.run(transport="stdio")
diff --git a/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py b/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py
index 323b3c49259..e82f21fc216 100644
--- a/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py
+++ b/tools/pynuttx/nxgdbmcp/gmcp/tools/__init__.py
@@ -24,6 +24,7 @@ __all__ = [
     "register_session_tools",
     "register_command_tools",
... (55 lines truncated)
```
</details>
