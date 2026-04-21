# Device Drivers (drivers/) Review Rules

Patterns mined from 631 review deltas in `drivers/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| coding-style | 154 | 24% |
| design-philosophy | 153 | 24% |
| root-cause-fix | 65 | 10% |
| patch-organization | 63 | 10% |
| code-reuse | 42 | 7% |
| correctness | 36 | 6% |
| concurrency | 35 | 6% |
| error-handling | 30 | 5% |
| variable-scope | 22 | 3% |
| thread-design | 10 | 2% |
| memory-management | 8 | 1% |
| posix-compliance | 8 | 1% |
| performance | 3 | 0% |
| testing | 1 | 0% |
| other | 1 | 0% |

## Severity Distribution

correctness: 228 | design: 257 | style: 143 | performance: 3

## Top Patterns

### 1. coding-style/magic_number_to_named_constant (5x) [style]

**Rule:** Replace magic numbers with named constants (e.g. MSEC2TICK(100) → MSEC2TICK(RPTUN_TIMEOUT_MS)), prefer moving function definitions before their first use over adding forward declarations, preserve established function names when refactoring internals, and ensure side-effect calls like wakeup are guarded by the condition that actually requires them.

**Example:** #851831 — rptun: use timer instead of remote rx done IRQ
**Transformation:** Replaced hardcoded timeout value 100 with named constant RPTUN_TIMEOUT_MS, kept original function name rptun_pm_action instead of renaming to rptun_pm_action_auto, moved rptun_wakeup_tx definition above its callers instead of adding a forward declaration, and moved rptun_wakeup_tx call inside the buffer-check conditional for correctness.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/rptun/rptun.c b/drivers/rptun/rptun.c
index cd767ab7be9..d9ba469e659 100644
--- a/drivers/rptun/rptun.c
+++ b/drivers/rptun/rptun.c
@@ -82,8 +82,8 @@ struct rptun_priv_s
 #endif
 #ifdef CONFIG_RPTUN_PM
   struct pm_wakelock_s         wakelock;
-  bool                         stay;
   uint16_t                     headrx;
+  struct wdog_s                wdog;
 #endif
 #ifdef CONFIG_RPTUN_PING
   struct rpmsg_endpoint        ping;
@@ -117,6 +117,7 @@ struct rptun_store_s
  * Private Function Prototypes
  ****************************************************************************/
 
+static void rptun_wakeup_tx(FAR struct rptun_priv_s *priv);
 static FAR struct remoteproc *
 rptun_init(FAR struct remoteproc *rproc,
            FAR const struct remoteproc_ops *ops,
@@ -211,27 +212,38 @@ static rmutex_t g_rptun_lockpriv = NXRMUTEX_INITIALIZER;
 
 #ifdef CONFIG_RPTUN_PM
 
-#define rptun_pm_action_auto(priv, timeout) \
-        pm_wakelock_staytimeout(&priv->wakelock, timeout)
+static void rptun_pm_callback(wdparm_t arg)
+{
+  FAR struct rptun_priv_s *priv = (FAR struct rptun_priv_s *)arg;
+
+  rptun_wakeup_tx(priv);
+
+  if (rptun_buffer_nused(&priv->rvdev, false))
... (105 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/rptun/rptun.c b/drivers/rptun/rptun.c
index cd767ab7be9..05ec1bf26f7 100644
--- a/drivers/rptun/rptun.c
+++ b/drivers/rptun/rptun.c
@@ -82,8 +82,8 @@ struct rptun_priv_s
 #endif
 #ifdef CONFIG_RPTUN_PM
   struct pm_wakelock_s         wakelock;
-  bool                         stay;
   uint16_t                     headrx;
+  struct wdog_s                wdog;
 #endif
 #ifdef CONFIG_RPTUN_PING
   struct rpmsg_endpoint        ping;
@@ -209,29 +209,51 @@ static rmutex_t g_rptun_lockpriv = NXRMUTEX_INITIALIZER;
  * Private Functions
  ****************************************************************************/
 
+static void rptun_wakeup_tx(FAR struct rptun_priv_s *priv)
+{
+  int semcount;
+
+  nxsem_get_value(&priv->semtx, &semcount);
+  while (semcount++ < 1)
+    {
+      nxsem_post(&priv->semtx);
+    }
+}
+
 #ifdef CONFIG_RPTUN_PM
 
-#define rptun_pm_action_auto(priv, timeout) \
-        pm_wakelock_staytimeout(&priv->wakelock, timeout)
+static void rptun_pm_callback(wdparm_t arg)
+{
... (126 lines truncated)
```
</details>

### 2. coding-style/preserve_existing_format (5x) [style]

**Rule:** When updating binary data arrays, change only the bytes that need updating — do not reformat surrounding code style (hex case, indentation, comment syntax); unrelated style churn obscures the real diff and makes review impossible.

**Example:** #850914 — update smart pa paramters of 20220803
**Transformation:** Reverted wholesale style reformatting (uppercase hex, removed indentation, changed comment syntax) back to original NuttX format, keeping only the substantive firmware tuning byte changes and checksum updates.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/audio/cs35l41_tune_fw_img.c b/drivers/audio/cs35l41_tune_fw_img.c
index ab8375c0aae..f85d169529f 100755
--- a/drivers/audio/cs35l41_tune_fw_img.c
+++ b/drivers/audio/cs35l41_tune_fw_img.c
@@ -30,382 +30,210 @@
 
 const uint8_t g_cs35l41_tune_fw_img[] =
 {
-  0xff, 0x98, 0xb9, 0x54,  /* IMG_MAGIC_NUMBER_1 */
-  0x02, 0x00, 0x00, 0x00,  /* IMG_FORMAT_REV */
-  0x0c, 0x11, 0x00, 0x00,  /* IMG_SIZE */
-  0x00, 0x00, 0x00, 0x00,  /* SYM_TABLE_SIZE */
-  0x00, 0x00, 0x00, 0x00,  /* ALG_LIST_SIZE */
-  0xa4, 0x00, 0x04, 0x00,  /* FW_ID */
-  0x00, 0x37, 0x00, 0x00,  /* FW_VERSION */
-  0x03, 0x00, 0x00, 0x00,  /* DATA_BLOCKS */
-  0x2c, 0x10, 0x00, 0x00,  /* MAX_BLOCK_SIZE */
-  0x00, 0x00, 0x00, 0x00,  /* FW_IMG_VERSION */
+// Header
+0xFF,0x98,0xB9,0x54, // IMG_MAGIC_NUMBER_1
+0x02,0x00,0x00,0x00, // IMG_FORMAT_REV
+0x0C,0x11,0x00,0x00, // IMG_SIZE
+0x00,0x00,0x00,0x00, // SYM_TABLE_SIZE
+0x00,0x00,0x00,0x00, // ALG_LIST_SIZE
+0xA4,0x00,0x04,0x00, // FW_ID
+0x00,0x37,0x00,0x00, // FW_VERSION
+0x03,0x00,0x00,0x00, // DATA_BLOCKS
+0x2C,0x10,0x00,0x00, // MAX_BLOCK_SIZE
+0x00,0x00,0x00,0x00, // FW_IMG_VERSION
+// Symbol Linking Table
+// Algorithm ID List
+// Firmware Data
 
-  0x2c, 0x10, 0x00, 0x00,  /* COEFF_BLOCK_SIZE_0_0 */
-  0x5c, 0x01, 0xc0, 0x02,  /* COEFF_BLOCK_ADDR_0_0 */
... (1162 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/audio/cs35l41_tune_fw_img.c b/drivers/audio/cs35l41_tune_fw_img.c
index ab8375c0aae..f4bdb42d030 100755
--- a/drivers/audio/cs35l41_tune_fw_img.c
+++ b/drivers/audio/cs35l41_tune_fw_img.c
@@ -152,17 +152,17 @@ const uint8_t g_cs35l41_tune_fw_img[] =
   0x20, 0x00, 0x00, 0x09, 0x00, 0x3f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x09, 0x00, 0x00, 0x5a, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x08, 0x00,
   0x58, 0x00, 0x00, 0x3f, 0x00, 0x20, 0x3f, 0x5d, 0x00, 0x00, 0x40, 0x00,
-  0x0d, 0x0e, 0x34, 0x81, 0x00, 0x0a, 0x00, 0xf0, 0x00, 0x00, 0x0a, 0x00,
+  0x0d, 0x03, 0xc8, 0x42, 0x00, 0x0a, 0x00, 0xf0, 0x00, 0x00, 0x0a, 0x00,
   0x0a, 0x00, 0x00, 0x0a, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00,
   0x00, 0x7f, 0xdf, 0x3b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
-  0x41, 0x00, 0x00, 0x20, 0x34, 0x81, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0x0e,
+  0x41, 0x00, 0x00, 0x20, 0xc8, 0x42, 0x00, 0x00, 0x00, 0xf0, 0x0f, 0x03,
   0x0b, 0x00, 0x00, 0x09, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00,
-  0xae, 0x00, 0x00, 0x03, 0x47, 0xae, 0x01, 0x47, 0x15, 0xcc, 0x8c, 0x01,
-  0x02, 0x48, 0xa9, 0xd3, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
-  0x1a, 0x00, 0x00, 0x00, 0x88, 0x5d, 0x54, 0x1d, 0x7f, 0xff, 0xff, 0x0e,
+  0xae, 0x00, 0x00, 0x03, 0x47, 0xae, 0x01, 0x47, 0x2f, 0xf5, 0x34, 0x01,
+  0x02, 0x4d, 0x05, 0xef, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
+  0x72, 0x00, 0x00, 0x00, 0x7d, 0x91, 0x4f, 0xe8, 0x7f, 0xff, 0xff, 0x3e,
   0x0a, 0x7f, 0xff, 0xff, 0x73, 0x52, 0x00, 0x00, 0x76, 0x73, 0x52, 0x76,
   0xdf, 0x4f, 0x02, 0xdf, 0xf0, 0x10, 0x4f, 0x02, 0x00, 0x00, 0x04, 0x00,
-  0x05, 0x00, 0x00, 0x03, 0x0e, 0x8d, 0x00, 0x00, 0x12, 0x85, 0x72, 0x2a,
+  0x05, 0x00, 0x00, 0x03, 0x15, 0xd3, 0x00, 0x00, 0x12, 0x85, 0x72, 0x3f,
   0x02, 0x7f, 0xff, 0xff, 0x00, 0x42, 0x00, 0x00, 0x00, 0x00, 0x0b, 0x00,
   0x01, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
   0x44, 0x00, 0x00, 0x07, 0x00, 0x20, 0x00, 0xa8, 0x00, 0x00, 0x08, 0x00,
@@ -407,5 +407,5 @@ const uint8_t g_cs35l41_tune_fw_img[] =
   0x00, 0x08, 0x00, 0x00, 0x00, 0x7f, 0xff, 0xff,
 
   0xa6, 0xe2, 0x6b, 0x93,  /* IMG_MAGIC_NUMBER_2 */
-  0x7a, 0x9e, 0xc9, 0xa4,  /* IMG_CHECKSUM */
+  0x10, 0xdf, 0xcf, 0xda,  /* IMG_CHECKSUM */
 };
... (116 lines truncated)
```
</details>

### 3. root-cause-fix/fix_root_cause_not_symptom (3x) [correctness]

**Rule:** When a race condition causes corruption or null access, fix the concurrency root cause (add proper locking at the data-owning layer) instead of adding defensive null checks at the call site.

**Example:** #5586801 — drivers/binder: Fix binder memcpy crash.
**Transformation:** Replaced a superficial null-check band-aid on target_proc with proper mutex locking inside binder_alloc_do_buffer_copy, and removed the caller-side proc lock that was at the wrong granularity.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/binder/binder_trans.c b/drivers/binder/binder_trans.c
index 888e6ae66b8..b0ef7150c03 100644
--- a/drivers/binder/binder_trans.c
+++ b/drivers/binder/binder_trans.c
@@ -1497,9 +1497,9 @@ void binder_transaction(FAR struct binder_proc *proc,
   /* Done processing objects, copy the rest of the buffer */
 
   binder_inner_proc_lock(target_proc);
-  if (binder_alloc_copy_to_buffer(&target_proc->alloc,
+  if ((binder_alloc_copy_to_buffer(&target_proc->alloc,
               t->buffer, user_offset, (void *)(user_buffer + user_offset),
-              tr->data_size - user_offset))
+              tr->data_size - user_offset)) || !target_proc)
     {
       binder_debug(BINDER_DEBUG_ERROR,
                    "[%s][%d:%d]:"

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/binder/binder_alloc.c b/drivers/binder/binder_alloc.c
index b7e63876f79..892e6063fc8 100644
--- a/drivers/binder/binder_alloc.c
+++ b/drivers/binder/binder_alloc.c
@@ -514,10 +514,13 @@ static int binder_alloc_do_buffer_copy(
   FAR struct binder_buffer *buffer, binder_size_t buffer_offset,
   FAR void *ptr, size_t bytes)
 {
+  nxmutex_lock(&alloc->alloc_lock);
+
   /* All copies must be 32-bit aligned and 32-bit size */
 
   if (!check_buffer(alloc, buffer, buffer_offset, bytes))
     {
+      nxmutex_unlock(&alloc->alloc_lock);
       return -EINVAL;
     }
 
@@ -551,6 +554,8 @@ static int binder_alloc_do_buffer_copy(
       buffer_offset += size;
     }
 
+  nxmutex_unlock(&alloc->alloc_lock);
+
   return 0;
 }
 
diff --git a/drivers/binder/binder_trans.c b/drivers/binder/binder_trans.c
index 888e6ae66b8..92adf338b04 100644
--- a/drivers/binder/binder_trans.c
+++ b/drivers/binder/binder_trans.c
@@ -1496,7 +1496,6 @@ void binder_transaction(FAR struct binder_proc *proc,
 
   /* Done processing objects, copy the rest of the buffer */
 
... (18 lines truncated)
```
</details>

### 4. patch-organization/unrelated_change_in_patch (3x) [style]

**Rule:** Each patch must do exactly one thing — do not sneak in unrelated variable declarations or other changes alongside the primary fix.

**Example:** #5251295 — rtc_time:set zero rtc_time struct
**Transformation:** Reviewer removed the unrelated `irqstate_t flags` variable declaration that was added in `rpmsg_rtc_server_ns_bind`, keeping the patch focused solely on zero-initializing `rtctime` structs.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/timers/arch_rtc.c b/drivers/timers/arch_rtc.c
index ba496555a58..46f7a1ec479 100644
--- a/drivers/timers/arch_rtc.c
+++ b/drivers/timers/arch_rtc.c
@@ -125,7 +125,10 @@ int weak_function up_rtc_gettime(FAR struct timespec *tp)
 
   if (g_rtc_lower != NULL)
     {
-      struct rtc_time rtctime;
+      struct rtc_time rtctime =
+        {
+          0
+        };
 
       ret = g_rtc_lower->ops->rdtime(g_rtc_lower, &rtctime);
       if (ret == 0)
diff --git a/drivers/timers/rpmsg_rtc.c b/drivers/timers/rpmsg_rtc.c
index 31cff5f16d4..54a5e4ca935 100644
--- a/drivers/timers/rpmsg_rtc.c
+++ b/drivers/timers/rpmsg_rtc.c
@@ -646,7 +646,10 @@ static int rpmsg_rtc_server_ept_cb(FAR struct rpmsg_endpoint *ept,
     case RPMSG_RTC_GET:
       {
         FAR struct rpmsg_rtc_get_s *msg = data;
-        struct rtc_time rtctime;
+        struct rtc_time rtctime =
+          {
+            0
+          };
 
         header->result = rpmsg_rtc_server_rdtime(priv, &rtctime);
 
@@ -658,7 +661,11 @@ static int rpmsg_rtc_server_ept_cb(FAR struct rpmsg_endpoint *ept,
     case RPMSG_RTC_SET:
       {
... (25 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/timers/arch_rtc.c b/drivers/timers/arch_rtc.c
index ba496555a58..46f7a1ec479 100644
--- a/drivers/timers/arch_rtc.c
+++ b/drivers/timers/arch_rtc.c
@@ -125,7 +125,10 @@ int weak_function up_rtc_gettime(FAR struct timespec *tp)
 
   if (g_rtc_lower != NULL)
     {
-      struct rtc_time rtctime;
+      struct rtc_time rtctime =
+        {
+          0
+        };
 
       ret = g_rtc_lower->ops->rdtime(g_rtc_lower, &rtctime);
       if (ret == 0)
diff --git a/drivers/timers/rpmsg_rtc.c b/drivers/timers/rpmsg_rtc.c
index 31cff5f16d4..1d97a786ee0 100644
--- a/drivers/timers/rpmsg_rtc.c
+++ b/drivers/timers/rpmsg_rtc.c
@@ -646,7 +646,10 @@ static int rpmsg_rtc_server_ept_cb(FAR struct rpmsg_endpoint *ept,
     case RPMSG_RTC_GET:
       {
         FAR struct rpmsg_rtc_get_s *msg = data;
-        struct rtc_time rtctime;
+        struct rtc_time rtctime =
+          {
+            0
+          };
 
         header->result = rpmsg_rtc_server_rdtime(priv, &rtctime);
 
@@ -658,7 +661,11 @@ static int rpmsg_rtc_server_ept_cb(FAR struct rpmsg_endpoint *ept,
     case RPMSG_RTC_SET:
       {
... (23 lines truncated)
```
</details>

### 5. coding-style/unnecessary_blank_line (3x) [style]

**Rule:** Do not leave blank lines between a lock release and the immediately following return statement; they form a single logical unit.

**Example:** #4590019 — rptun: use seperated lock to fix deadlock in some condition
**Transformation:** Removed blank line between nxrmutex_unlock and return statement in rptun_ioctl_foreach, placing them adjacent.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/rptun/rptun.c b/drivers/rptun/rptun.c
index 1925d8e8fcb..c046be4bf02 100644
--- a/drivers/rptun/rptun.c
+++ b/drivers/rptun/rptun.c
@@ -195,6 +195,7 @@ static METAL_DECLARE_LIST(g_rptun_cb);
 static METAL_DECLARE_LIST(g_rptun_priv);
 
 static rmutex_t g_rptun_lockcb = NXRMUTEX_INITIALIZER;
+static rmutex_t g_rptun_lockpriv = NXRMUTEX_INITIALIZER;
 
 /****************************************************************************
  * Private Functions
@@ -1086,6 +1087,7 @@ static int rptun_ioctl_foreach(FAR const char *cpuname, int cmd,
   FAR struct metal_list *node;
   int ret = OK;
 
+  nxrmutex_lock(&g_rptun_lockpriv);
   metal_list_for_each(&g_rptun_priv, node)
     {
       FAR struct rptun_priv_s *priv;
@@ -1099,6 +1101,7 @@ static int rptun_ioctl_foreach(FAR const char *cpuname, int cmd,
               break;
         }
     }
+  nxrmutex_unlock(&g_rptun_lockpriv);
 
   return ret;
 }
@@ -1190,7 +1193,7 @@ int rpmsg_register_callback(FAR void *priv_,
   cb->ns_match       = ns_match;
   cb->ns_bind        = ns_bind;
 
-  nxrmutex_lock(&g_rptun_lockcb);
+  nxrmutex_lock(&g_rptun_lockpriv);
 
... (45 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/rptun/rptun.c b/drivers/rptun/rptun.c
index 1925d8e8fcb..1634d8cc9db 100644
--- a/drivers/rptun/rptun.c
+++ b/drivers/rptun/rptun.c
@@ -195,6 +195,7 @@ static METAL_DECLARE_LIST(g_rptun_cb);
 static METAL_DECLARE_LIST(g_rptun_priv);
 
 static rmutex_t g_rptun_lockcb = NXRMUTEX_INITIALIZER;
+static rmutex_t g_rptun_lockpriv = NXRMUTEX_INITIALIZER;
 
 /****************************************************************************
  * Private Functions
@@ -1086,6 +1087,7 @@ static int rptun_ioctl_foreach(FAR const char *cpuname, int cmd,
   FAR struct metal_list *node;
   int ret = OK;
 
+  nxrmutex_lock(&g_rptun_lockpriv);
   metal_list_for_each(&g_rptun_priv, node)
     {
       FAR struct rptun_priv_s *priv;
@@ -1100,6 +1102,7 @@ static int rptun_ioctl_foreach(FAR const char *cpuname, int cmd,
         }
     }
 
+  nxrmutex_unlock(&g_rptun_lockpriv);
   return ret;
 }
 
@@ -1190,7 +1193,7 @@ int rpmsg_register_callback(FAR void *priv_,
   cb->ns_match       = ns_match;
   cb->ns_bind        = ns_bind;
 
-  nxrmutex_lock(&g_rptun_lockcb);
+  nxrmutex_lock(&g_rptun_lockpriv);
 
... (45 lines truncated)
```
</details>

### 6. root-cause-fix/fix_at_correct_layer (3x) [correctness]

**Rule:** When the TX buffer needs to be drained, fix the wait logic at the character-level put function (uart_putxmitchar) where the buffer-full condition is already handled, not at the higher-level write function; use a loop to ensure complete drainage rather than a single wait.

**Example:** #3235053 — serial:need wait xmit sem until flush all buffer
**Transformation:** Moved TX buffer wait from uart_write() (symptom site) to uart_putxmitchar() (root cause site), wrapping the existing wait-for-space logic in a do-while loop that drains the TX buffer completely before proceeding.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/serial/serial.c b/drivers/serial/serial.c
index 2629477a051..3456a33ca5d 100644
--- a/drivers/serial/serial.c
+++ b/drivers/serial/serial.c
@@ -1328,6 +1328,12 @@ static ssize_t uart_write(FAR struct file *filep, FAR const char *buffer,
       uart_dmatxavail(dev);
 #endif
       uart_enabletxint(dev);
+      ret = nxsem_wait(&dev->xmitsem);
+      if (ret < 0)
+        {
+          nxmutex_unlock(&dev->xmit.lock);
+          return ret;
+        }
     }
 
   nxmutex_unlock(&dev->xmit.lock);

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/serial/serial.c b/drivers/serial/serial.c
index 2629477a051..d5e05e78ac7 100644
--- a/drivers/serial/serial.c
+++ b/drivers/serial/serial.c
@@ -225,21 +225,26 @@ static int uart_putxmitchar(FAR uart_dev_t *dev, int ch, bool oktoblock)
 #endif
           else
             {
-              /* Wait for some characters to be sent from the buffer with
-               * the TX interrupt enabled.  When the TX interrupt is enabled,
-               * uart_xmitchars() should execute and remove some of the data
-               * from the TX buffer.
-               *
-               * NOTE that interrupts will be re-enabled while we wait for
-               * the semaphore.
-               */
+              do
+                {
+                  /* Wait for some characters to be sent from the buffer with
+                   * the TX interrupt enabled.  When the TX interrupt is
+                   * enabled, uart_xmitchars() should execute and remove some
+                   * of the data from the TX buffer.  We may have to wait several
+                   * times for the TX buffer to be entirely emptied.
+                   *
+                   * NOTE that interrupts will be re-enabled while we wait for
+                   * the semaphore.
+                   */
 
 #ifdef CONFIG_SERIAL_TXDMA
-              uart_dmatxavail(dev);
+                  uart_dmatxavail(dev);
 #endif
-              uart_enabletxint(dev);
-              ret = nxsem_wait(&dev->xmitsem);
-              uart_disabletxint(dev);
... (9 lines truncated)
```
</details>

### 7. patch-organization/unrelated_changes_in_single_patch (3x) [style]

**Rule:** Each patch must do exactly one thing — do not bundle unrelated comment fixes, typo corrections, or documentation updates into a functional change patch; submit them as separate commits.

**Example:** #890319 — change strcpy to strlcpy
**Transformation:** Reviewer removed unrelated comment/documentation fixes (interrupt handler description updates, typo corrections like CCTS_IFLOW→CRTS_IFLOW) from a patch whose purpose was replacing strcpy with strlcpy, keeping only the strcpy→strlcpy security hardening changes.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm/src/gd32f4/gd32f4xx_serial.c b/arch/arm/src/gd32f4/gd32f4xx_serial.c
index fdebe29ed9e..d19248e2acd 100644
--- a/arch/arm/src/gd32f4/gd32f4xx_serial.c
+++ b/arch/arm/src/gd32f4/gd32f4xx_serial.c
@@ -2816,7 +2816,7 @@ void arm_serialinit(void)
 
   /* Register all remaining USARTs */
 
-  strcpy(devname, "/dev/ttySx");
+  strlcpy(devname, "/dev/ttySx", sizeof(devname));
 
   for (i = 0; i < GD32_NUSART; i++)
     {
diff --git a/arch/arm/src/nrf52/nrf52_serial.c b/arch/arm/src/nrf52/nrf52_serial.c
index b667e0fee80..d2ec280d300 100644
--- a/arch/arm/src/nrf52/nrf52_serial.c
+++ b/arch/arm/src/nrf52/nrf52_serial.c
@@ -376,10 +376,10 @@ static void nrf52_detach(struct uart_dev_s *dev)
  * Name: nrf52_interrupt
  *
  * Description:
- *   This is the UART status interrupt handler.  It will be invoked when an
- *   interrupt received on the 'irq'  It should call uart_transmitchars or
- *   uart_receivechar to perform the appropriate data transfers.  The
- *   interrupt handling logic must be able to map the 'irq' number into the
+ *   This is the UART interrupt handler.  It will be invoked when an
+ *   interrupt is received on the 'irq'.  It should call uart_xmitchars or
+ *   uart_recvchars to perform the appropriate data transfers.  The
+ *   interrupt handling logic must be able to map the 'arg' to the
  *   appropriate uart_dev_s structure in order to call these functions.
  *
  ****************************************************************************/
@@ -782,7 +782,7 @@ void arm_serialinit(void)
 
   /* Register all remaining UARTs */
... (1060 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/gd32f4/gd32f4xx_serial.c b/arch/arm/src/gd32f4/gd32f4xx_serial.c
index fdebe29ed9e..d19248e2acd 100644
--- a/arch/arm/src/gd32f4/gd32f4xx_serial.c
+++ b/arch/arm/src/gd32f4/gd32f4xx_serial.c
@@ -2816,7 +2816,7 @@ void arm_serialinit(void)
 
   /* Register all remaining USARTs */
 
-  strcpy(devname, "/dev/ttySx");
+  strlcpy(devname, "/dev/ttySx", sizeof(devname));
 
   for (i = 0; i < GD32_NUSART; i++)
     {
diff --git a/arch/arm/src/nrf52/nrf52_serial.c b/arch/arm/src/nrf52/nrf52_serial.c
index b667e0fee80..88604fc1ff8 100644
--- a/arch/arm/src/nrf52/nrf52_serial.c
+++ b/arch/arm/src/nrf52/nrf52_serial.c
@@ -782,7 +782,7 @@ void arm_serialinit(void)
 
   /* Register all remaining UARTs */
 
-  strcpy(devname, "/dev/ttySx");
+  strlcpy(devname, "/dev/ttySx", sizeof(devname));
 
   for (i = 0; i < NRF52_NUART; i++)
     {
diff --git a/arch/arm/src/nrf53/nrf53_serial.c b/arch/arm/src/nrf53/nrf53_serial.c
index cfa2c4d8ce4..085b1d2abaf 100644
--- a/arch/arm/src/nrf53/nrf53_serial.c
+++ b/arch/arm/src/nrf53/nrf53_serial.c
@@ -782,7 +782,7 @@ void arm_serialinit(void)
 
   /* Register all remaining UARTs */
 
-  strcpy(devname, "/dev/ttySx");
... (1189 lines truncated)
```
</details>

### 8. patch-organization/one_patch_one_thing (3x) [design]

**Rule:** Each patch must do exactly one thing; never bundle a bug fix with a refactor — submit the minimal root-cause fix first, then propose the refactor as a separate, clean patch without commented-out code or style violations.

**Example:** #885665 — sensors/bmi160.c: fix the problem Linux SPI doesn't working properly
**Transformation:** A patch that mixed an SPI mode bug fix (MODE3→MODE0) with a large SPI_EXCHANGE refactor (commented-out code, new buffers, unaligned casts, kmm_malloc without null checks) was stripped down to only the minimal one-line mode fix.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/sensors/bmi160.c b/drivers/sensors/bmi160.c
index cee90ed4061..e315a5c9541 100644
--- a/drivers/sensors/bmi160.c
+++ b/drivers/sensors/bmi160.c
@@ -282,7 +282,7 @@ static inline void bmi160_configspi(FAR struct spi_dev_s *spi)
 {
   /* Configure SPI for the BMI160 */
 
-  SPI_SETMODE(spi, SPIDEV_MODE3);
+  SPI_SETMODE(spi, SPIDEV_MODE0);
   SPI_SETBITS(spi, 8);
   SPI_HWFEATURES(spi, 0);
   SPI_SETFREQUENCY(spi, BMI160_SPI_MAXFREQUENCY);
@@ -325,7 +325,8 @@ static uint8_t bmi160_getreg8(FAR struct bmi160_dev_s *priv, uint8_t regaddr)
 
 #else /* CONFIG_SENSORS_BMI160_SPI */
   /* If SPI bus is shared then lock and configure it */
-
+  uint8_t tx_buff[2] = {regaddr | 0x80, 0xFF};
+  uint8_t rx_buff[2];
   SPI_LOCK(priv->spi, true);
   bmi160_configspi(priv->spi);
 
@@ -335,15 +336,15 @@ static uint8_t bmi160_getreg8(FAR struct bmi160_dev_s *priv, uint8_t regaddr)
 
   /* Send register to read and get the next byte */
 
-  SPI_SEND(priv->spi, regaddr | 0x80);
-  SPI_RECVBLOCK(priv->spi, &regval, 1);
-
+  //SPI_SEND(priv->spi, regaddr | 0x80);
+  //SPI_RECVBLOCK(priv->spi, &regval, 1);
+  SPI_EXCHANGE(priv->spi, tx_buff, rx_buff, 2);
   /* Deselect the BMI160 */
 
... (105 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/sensors/bmi160.c b/drivers/sensors/bmi160.c
index bc938c1d4a7..3ce8c4cc2c3 100644
--- a/drivers/sensors/bmi160.c
+++ b/drivers/sensors/bmi160.c
@@ -278,7 +278,7 @@ static inline void bmi160_configspi(FAR struct spi_dev_s *spi)
 {
   /* Configure SPI for the BMI160 */
 
-  SPI_SETMODE(spi, SPIDEV_MODE3);
+  SPI_SETMODE(spi, SPIDEV_MODE0);
   SPI_SETBITS(spi, 8);
   SPI_HWFEATURES(spi, 0);
   SPI_SETFREQUENCY(spi, BMI160_SPI_MAXFREQUENCY);

```
</details>

### 9. patch-organization/unrelated_changes_mixed (3x) [design]

**Rule:** A patch that migrates include paths should touch only files needing that migration — do not bundle unrelated new file additions, and ensure all files requiring the same fix are included for completeness.

**Example:** #885382 — libc: Move crc8.h, crc16.h and crc32.h from include to include/nuttx
**Transformation:** Removed unrelated new esp32c3_wlan.c file (1919 lines) from include-path-migration patch and added missing files (esp32_emac.c, esp32_wlan.c, drivers/mtd/smart.c) that also needed the same crc header path fix.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/arm/src/stm32/stm32_bbsram.c b/arch/arm/src/stm32/stm32_bbsram.c
index 0d6cc9dfe03..f9897f1fadb 100644
--- a/arch/arm/src/stm32/stm32_bbsram.c
+++ b/arch/arm/src/stm32/stm32_bbsram.c
@@ -55,8 +55,7 @@
 #include <unistd.h>
 #include <time.h>
 #include <nuttx/fs/fs.h>
-
-#include <crc32.h>
+#include <nuttx/crc32.h>
 
 #include "stm32_bbsram.h"
 #include "chip.h"
diff --git a/arch/arm/src/stm32f7/stm32_bbsram.c b/arch/arm/src/stm32f7/stm32_bbsram.c
index 522519741e1..f1955ad0ddc 100644
--- a/arch/arm/src/stm32f7/stm32_bbsram.c
+++ b/arch/arm/src/stm32f7/stm32_bbsram.c
@@ -55,8 +55,7 @@
 #include <unistd.h>
 #include <time.h>
 #include <nuttx/fs/fs.h>
-
-#include <crc32.h>
+#include <nuttx/crc32.h>
 
 #include "stm32_bbsram.h"
 #include "chip.h"
diff --git a/arch/arm/src/stm32f7/stm32_ethernet.c b/arch/arm/src/stm32f7/stm32_ethernet.c
index dedbe6abfb2..673656b1c19 100644
--- a/arch/arm/src/stm32f7/stm32_ethernet.c
+++ b/arch/arm/src/stm32f7/stm32_ethernet.c
@@ -57,7 +57,7 @@
 #include <nuttx/net/mii.h>
 #include <nuttx/net/arp.h>
... (5088 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/arm/src/stm32/stm32_bbsram.c b/arch/arm/src/stm32/stm32_bbsram.c
index 0d6cc9dfe03..f9897f1fadb 100644
--- a/arch/arm/src/stm32/stm32_bbsram.c
+++ b/arch/arm/src/stm32/stm32_bbsram.c
@@ -55,8 +55,7 @@
 #include <unistd.h>
 #include <time.h>
 #include <nuttx/fs/fs.h>
-
-#include <crc32.h>
+#include <nuttx/crc32.h>
 
 #include "stm32_bbsram.h"
 #include "chip.h"
diff --git a/arch/arm/src/stm32f7/stm32_bbsram.c b/arch/arm/src/stm32f7/stm32_bbsram.c
index 522519741e1..f1955ad0ddc 100644
--- a/arch/arm/src/stm32f7/stm32_bbsram.c
+++ b/arch/arm/src/stm32f7/stm32_bbsram.c
@@ -55,8 +55,7 @@
 #include <unistd.h>
 #include <time.h>
 #include <nuttx/fs/fs.h>
-
-#include <crc32.h>
+#include <nuttx/crc32.h>
 
 #include "stm32_bbsram.h"
 #include "chip.h"
diff --git a/arch/arm/src/stm32f7/stm32_ethernet.c b/arch/arm/src/stm32f7/stm32_ethernet.c
index dedbe6abfb2..673656b1c19 100644
--- a/arch/arm/src/stm32f7/stm32_ethernet.c
+++ b/arch/arm/src/stm32f7/stm32_ethernet.c
@@ -57,7 +57,7 @@
 #include <nuttx/net/mii.h>
 #include <nuttx/net/arp.h>
... (446 lines truncated)
```
</details>

### 10. coding-style/no_commented_out_code (3x) [style]

**Rule:** Never comment out code to disable it; delete it entirely along with any declarations, definitions, and variables that become unused as a result.

**Example:** #840734 — wifi: Subscribe only to the events we need
**Transformation:** Commented-out code block was replaced by fully deleting the dead code, its forward declaration, and the now-unused function definition and loop variable.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c b/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c
index 1aaca505e5f..2d3e832fd83 100644
--- a/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c
+++ b/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c
@@ -616,10 +616,10 @@ int bcmf_driver_initialize(FAR struct bcmf_dev_s *priv)
 
   /* FIXME Configure event mask to enable all asynchronous events */
 
-  for (i = 0; i < BCMF_EVENT_COUNT; i++)
-    {
-      bcmf_event_register(priv, bcmf_wl_default_event_handler, i);
-    }
+  // for (i = 0; i < BCMF_EVENT_COUNT; i++)
+  //   {
+  //     bcmf_event_register(priv, bcmf_wl_default_event_handler, i);
+  //   }
 
   /*  Register radio event */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c b/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c
index 15dfc8e1c6d..b9893147221 100644
--- a/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c
+++ b/drivers/wireless/ieee80211/bcm43xxx/bcmf_driver.c
@@ -181,9 +181,6 @@ static int bcmf_driver_download_clm(FAR struct bcmf_dev_s *priv);
 
 /* FIXME only for debug purpose */
 
-static void bcmf_wl_default_event_handler(FAR struct bcmf_dev_s *priv,
-                            struct bcmf_event_s *event, unsigned int len);
-
 static void bcmf_wl_radio_event_handler(FAR struct bcmf_dev_s *priv,
                             struct bcmf_event_s *event, unsigned int len);
 
@@ -587,14 +584,6 @@ errout_in_sdio_active:
 
 int bcmf_driver_initialize(FAR struct bcmf_dev_s *priv)
 {
-  int i;
-
-  /* FIXME Configure event mask to enable all asynchronous events */
-
-  for (i = 0; i < BCMF_EVENT_COUNT; i++)
-    {
-      bcmf_event_register(priv, bcmf_wl_default_event_handler, i);
-    }
 
   /*  Register radio event */
 
@@ -634,15 +623,6 @@ int bcmf_driver_initialize(FAR struct bcmf_dev_s *priv)
   return bcmf_netdev_register(priv);
 }
 
-void bcmf_wl_default_event_handler(FAR struct bcmf_dev_s *priv,
-                                   struct bcmf_event_s *event,
... (11 lines truncated)
```
</details>

### 11. patch-organization/unresolved_merge_conflicts (2x) [correctness]

**Rule:** Never submit patches containing unresolved merge conflict markers; always resolve conflicts and verify the result compiles before posting for review.

**Example:** #4745520 — nuttx/can: in trunk support to Send message cancel mechanism.
**Transformation:** Resolved merge conflict markers (<<<<<<< HEAD / ======= / >>>>>>>) that were left in PS1, replacing them with the intended conditional compilation blocks.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/can/Kconfig b/drivers/can/Kconfig
index 9e321b8218c..4f3bce5dc00 100644
--- a/drivers/can/Kconfig
+++ b/drivers/can/Kconfig
@@ -132,6 +132,15 @@ config CAN_TXPRIORITY
 	---help---
 		Prioritize sending based on canid.
 
+config CAN_TXCANCEL
+	bool "Implement tx cancel ability"
+	default n
+        depends on CAN_TXPRIORITY
+	---help---
+		Enabling this feature adds support for the can cancel ability.
+                this ability can cancel the msg with the largest msgID in the
+                mailbox and return true if success.
+
 choice
 	prompt "TX Ready Work Queue"
 	default CAN_TXREADY_HIPRI
diff --git a/drivers/can/can.c b/drivers/can/can.c
index 2eed29fce7d..fff33f0a120 100644
--- a/drivers/can/can.c
+++ b/drivers/can/can.c
@@ -562,6 +562,25 @@ static int can_xmit(FAR struct can_dev_s *dev)
         }
     }
 
+#ifdef CONFIG_CAN_TXCANCEL
+  if (TX_PENDING(&dev->cd_sender) && can_txneed_cancel(&dev->cd_sender))
+    {
+      if (can_cancel_mbmsg(dev))
+        {
+          msg = can_get_msg(&dev->cd_sender);
+
... (210 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/can/Kconfig b/drivers/can/Kconfig
index 9e321b8218c..4f3bce5dc00 100644
--- a/drivers/can/Kconfig
+++ b/drivers/can/Kconfig
@@ -132,6 +132,15 @@ config CAN_TXPRIORITY
 	---help---
 		Prioritize sending based on canid.
 
+config CAN_TXCANCEL
+	bool "Implement tx cancel ability"
+	default n
+        depends on CAN_TXPRIORITY
+	---help---
+		Enabling this feature adds support for the can cancel ability.
+                this ability can cancel the msg with the largest msgID in the
+                mailbox and return true if success.
+
 choice
 	prompt "TX Ready Work Queue"
 	default CAN_TXREADY_HIPRI
diff --git a/drivers/can/can.c b/drivers/can/can.c
index 2eed29fce7d..49e5d1c50b8 100644
--- a/drivers/can/can.c
+++ b/drivers/can/can.c
@@ -562,6 +562,25 @@ static int can_xmit(FAR struct can_dev_s *dev)
         }
     }
 
+#ifdef CONFIG_CAN_TXCANCEL
+  if (TX_PENDING(&dev->cd_sender) && can_txneed_cancel(&dev->cd_sender))
+    {
+      if (can_cancel_mbmsg(dev))
+        {
+          msg = can_get_msg(&dev->cd_sender);
+
... (204 lines truncated)
```
</details>

### 12. coding-style/line_length_wrapping (2x) [style]

**Rule:** Function calls exceeding the line length limit must be wrapped with continuation-line arguments aligned to the opening parenthesis.

**Example:** #4720523 — note: noteram reads multiple binary data at a time
**Transformation:** Long function call arguments were wrapped to align parameters on continuation line, conforming to NuttX line length limits.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/note/noteram_driver.c b/drivers/note/noteram_driver.c
index 61e1022ab6e..8b802995a06 100644
--- a/drivers/note/noteram_driver.c
+++ b/drivers/note/noteram_driver.c
@@ -465,9 +465,21 @@ static ssize_t noteram_read(FAR struct file *filep, FAR char *buffer,
 
   if (ctx->mode == NOTE_MODE_READ_BINARY)
     {
+      size_t nread = 0;
       flags = spin_lock_irqsave_wo_note(&drv->lock);
-      ret = noteram_get(drv, (FAR uint8_t *)buffer, buflen);
+      while (nread < buflen)
+        {
+          ret = noteram_get(drv, (FAR uint8_t *)buffer + nread, buflen - nread);
+          if (ret <= 0)
+            {
+              break;
+            }
+
+          nread += ret;
+        }
+
       spin_unlock_irqrestore_wo_note(&drv->lock, flags);
+      return nread;
     }
   else
     {

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/note/noteram_driver.c b/drivers/note/noteram_driver.c
index 61e1022ab6e..fb0a137e7c7 100644
--- a/drivers/note/noteram_driver.c
+++ b/drivers/note/noteram_driver.c
@@ -465,9 +465,22 @@ static ssize_t noteram_read(FAR struct file *filep, FAR char *buffer,
 
   if (ctx->mode == NOTE_MODE_READ_BINARY)
     {
+      size_t nread = 0;
       flags = spin_lock_irqsave_wo_note(&drv->lock);
-      ret = noteram_get(drv, (FAR uint8_t *)buffer, buflen);
+      while (nread < buflen)
+        {
+          ret = noteram_get(drv, (FAR uint8_t *)buffer + nread,
+                            buflen - nread);
+          if (ret <= 0)
+            {
+              break;
+            }
+
+          nread += ret;
+        }
+
       spin_unlock_irqrestore_wo_note(&drv->lock, flags);
+      return nread;
     }
   else
     {

```
</details>

### 13. design-philosophy/conditional_compilation_guard (2x) [design]

**Rule:** New fields in shared headers that increase struct size must be guarded by a Kconfig option so resource-constrained builds that don't need the feature pay zero cost; also clean up debug artifacts (stray files) before submitting.

**Example:** #4361191 — nuttx/can.h: support timestamp for can frame.
**Transformation:** Unconditionally added struct timespec field was wrapped in #ifdef CONFIG_CAN_TIMESTAMP with Kconfig option, changed type from timespec to timeval, renamed ch_timespec to ch_ts, and removed accidental 'q' file from the patch.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/can/can.h b/include/nuttx/can/can.h
index 4c64297806f..5cbe9923d7d 100644
--- a/include/nuttx/can/can.h
+++ b/include/nuttx/can/can.h
@@ -602,6 +602,8 @@ begin_packed_struct struct can_hdr_s
   uint8_t      ch_esi    : 1; /* Error State Indicator */
 #endif
   uint8_t      ch_tcf    : 1; /* Tx confirmation flag */
+
+  struct timespec ch_timespec; /* record the timestamp of each frame */
 } end_packed_struct;
 #endif
 
diff --git a/q b/q
new file mode 100644
index 00000000000..f66cd1490d5
--- /dev/null
+++ b/q
@@ -0,0 +1,26 @@
+[1mdiff --git a/include/nuttx/can/can.h b/include/nuttx/can/can.h[m
+[1mindex 4c64297806..5cbe9923d7 100644[m
+[1m--- a/include/nuttx/can/can.h[m
+[1m+++ b/include/nuttx/can/can.h[m
+[36m@@ -602,6 +602,8 @@[m [mbegin_packed_struct struct can_hdr_s[m
+   uint8_t      ch_esi    : 1; /* Error State Indicator */[m
+ #endif[m
+   uint8_t      ch_tcf    : 1; /* Tx confirmation flag */[m
+[32m+[m
+[32m+[m[32m  struct timespec ch_timespec; /* record the timestamp of each frame */[m
+ } end_packed_struct;[m
+ #endif[m
+ [m
+[1mdiff --git a/sched/clock/clock_gettime.c b/sched/clock/clock_gettime.c[m
+[1mindex 823ffed604..8f0cc6d5de 100644[m
+[1m--- a/sched/clock/clock_gettime.c[m
... (11 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/can/Kconfig b/drivers/can/Kconfig
index f53366c5f23..95caafbce5b 100644
--- a/drivers/can/Kconfig
+++ b/drivers/can/Kconfig
@@ -33,6 +33,14 @@ config CAN_ERRORS
 		bit will be set in the CAN message and the following message payload
 		will include a more detailed description of certain errors.
 
+config CAN_TIMESTAMP
+	bool "CAN timestamp reporting"
+	default n
+	---help---
+		Support CAN timestamp reporoting. if this option is selected then
+		CAN timestamp reporting is enabled. in the CAN message the ch_ts
+		member variable will record the timestamp of each frame.
+
 config CAN_FD
 	bool "CAN FD"
 	default n
diff --git a/include/nuttx/can/can.h b/include/nuttx/can/can.h
index 4c64297806f..0dd0ab4c160 100644
--- a/include/nuttx/can/can.h
+++ b/include/nuttx/can/can.h
@@ -602,6 +602,10 @@ begin_packed_struct struct can_hdr_s
   uint8_t      ch_esi    : 1; /* Error State Indicator */
 #endif
   uint8_t      ch_tcf    : 1; /* Tx confirmation flag */
+#ifdef CONFIG_CAN_TIMESTAMP
+                              /* record the timestamp of each frame */
+  struct timeval ch_ts;
+#endif
 } end_packed_struct;
 #endif
 

```
</details>

### 14. root-cause-fix/operation_ordering (2x) [correctness]

**Rule:** Operations that populate shared state must execute before the operations that consume that state; review call ordering when moving code.

**Example:** #4117793 — disp: fix black screen when boot ap
**Transformation:** Moved fb_add_paninfo() call before pandisplay() so pan info is queued before the display is panned, and removed the trailing blank line.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/video/fb.c b/drivers/video/fb.c
index f9a728d8518..261c398ce99 100644
--- a/drivers/video/fb.c
+++ b/drivers/video/fb.c
@@ -941,12 +941,12 @@ static int fb_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
 
           memcpy(&paninfo, pinfo, sizeof(*pinfo));
 
+          ret = fb_add_paninfo(fb, &paninfo, FB_NO_OVERLAY);
           if (fb->vtable->pandisplay != NULL)
             {
               fb->vtable->pandisplay(fb->vtable, pinfo);
             }
 
-          ret = fb_add_paninfo(fb, &paninfo, FB_NO_OVERLAY);
         }
         break;
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/video/fb.c b/drivers/video/fb.c
index f9a728d8518..84c54e50d31 100644
--- a/drivers/video/fb.c
+++ b/drivers/video/fb.c
@@ -941,12 +941,11 @@ static int fb_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
 
           memcpy(&paninfo, pinfo, sizeof(*pinfo));
 
+          ret = fb_add_paninfo(fb, &paninfo, FB_NO_OVERLAY);
           if (fb->vtable->pandisplay != NULL)
             {
               fb->vtable->pandisplay(fb->vtable, pinfo);
             }
-
-          ret = fb_add_paninfo(fb, &paninfo, FB_NO_OVERLAY);
         }
         break;
 

```
</details>

### 15. patch-organization/unrelated_whitespace_change (2x) [style]

**Rule:** A rename/refactor patch must contain only the rename and its mechanical fallout; unrelated whitespace or formatting fixes belong in a separate patch.

**Example:** #3635765 — rptun.c: rename rptun.c to rptun_virtio.c
**Transformation:** Removed an unrelated blank line addition in rptun_ping.c from a patch that solely renames rptun.c/h to rptun_virtio.c/h.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/rptun/CMakeLists.txt b/drivers/rptun/CMakeLists.txt
index e4b81d83f98..58e6b9c450f 100644
--- a/drivers/rptun/CMakeLists.txt
+++ b/drivers/rptun/CMakeLists.txt
@@ -20,7 +20,7 @@
 if(CONFIG_RPTUN)
   set(SRCS)
 
-  list(APPEND SRCS rptun.c rptun_dump.c)
+  list(APPEND SRCS rptun_virtio.c rptun_dump.c)
 
   if(CONFIG_RPTUN_PING)
     list(APPEND SRCS rptun_ping.c)
diff --git a/drivers/rptun/Make.defs b/drivers/rptun/Make.defs
index a3fcfdbb2f0..60629e9cf01 100644
--- a/drivers/rptun/Make.defs
+++ b/drivers/rptun/Make.defs
@@ -22,7 +22,7 @@
 
 ifeq ($(CONFIG_RPTUN),y)
 
-CSRCS += rptun.c rptun_dump.c
+CSRCS += rptun_virtio.c rptun_dump.c
 
 ifeq ($(CONFIG_RPTUN_PING),y)
 CSRCS += rptun_ping.c
diff --git a/drivers/rptun/rptun_dump.c b/drivers/rptun/rptun_dump.c
index 9061569e08a..71e6ddf57d4 100644
--- a/drivers/rptun/rptun_dump.c
+++ b/drivers/rptun/rptun_dump.c
@@ -28,7 +28,7 @@
 
 #include <rpmsg/rpmsg_internal.h>
 
-#include "rptun.h"
... (71 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/drivers/rptun/CMakeLists.txt b/drivers/rptun/CMakeLists.txt
index e4b81d83f98..58e6b9c450f 100644
--- a/drivers/rptun/CMakeLists.txt
+++ b/drivers/rptun/CMakeLists.txt
@@ -20,7 +20,7 @@
 if(CONFIG_RPTUN)
   set(SRCS)
 
-  list(APPEND SRCS rptun.c rptun_dump.c)
+  list(APPEND SRCS rptun_virtio.c rptun_dump.c)
 
   if(CONFIG_RPTUN_PING)
     list(APPEND SRCS rptun_ping.c)
diff --git a/drivers/rptun/Make.defs b/drivers/rptun/Make.defs
index a3fcfdbb2f0..60629e9cf01 100644
--- a/drivers/rptun/Make.defs
+++ b/drivers/rptun/Make.defs
@@ -22,7 +22,7 @@
 
 ifeq ($(CONFIG_RPTUN),y)
 
-CSRCS += rptun.c rptun_dump.c
+CSRCS += rptun_virtio.c rptun_dump.c
 
 ifeq ($(CONFIG_RPTUN_PING),y)
 CSRCS += rptun_ping.c
diff --git a/drivers/rptun/rptun_dump.c b/drivers/rptun/rptun_dump.c
index 9061569e08a..71e6ddf57d4 100644
--- a/drivers/rptun/rptun_dump.c
+++ b/drivers/rptun/rptun_dump.c
@@ -28,7 +28,7 @@
 
 #include <rpmsg/rpmsg_internal.h>
 
-#include "rptun.h"
... (59 lines truncated)
```
</details>
