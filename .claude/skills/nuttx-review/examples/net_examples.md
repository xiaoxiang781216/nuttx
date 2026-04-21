# Network Stack (net/) Review Rules

Patterns mined from 213 review deltas in `net/`.

## Category Distribution

| Category | Count | % |
|----------|-------|---|
| design-philosophy | 61 | 29% |
| coding-style | 43 | 20% |
| root-cause-fix | 41 | 19% |
| correctness | 15 | 7% |
| code-reuse | 13 | 6% |
| patch-organization | 9 | 4% |
| error-handling | 8 | 4% |
| posix-compliance | 8 | 4% |
| concurrency | 7 | 3% |
| variable-scope | 4 | 2% |
| thread-design | 2 | 1% |
| performance | 2 | 1% |

## Severity Distribution

correctness: 96 | design: 79 | style: 37 | performance: 1

## Top Patterns

### 1. coding-style/unnecessary_cast_removal (2x) [style]

**Rule:** Do not add explicit casts when the source type already matches the destination type; unnecessary casts hide type mismatches and reduce compiler diagnostic effectiveness.

**Example:** #909359 — icmpv6: add route advertise RDNSS field support for send
**Transformation:** Removed unnecessary (FAR uint8_t *) cast from rdnss->servers which is already the correct type, and reordered local variable declarations to place FAR pointers before non-pointer structs per NuttX convention.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/icmpv6/Kconfig b/net/icmpv6/Kconfig
index f83918a5ab6..37d78a2494e 100644
--- a/net/icmpv6/Kconfig
+++ b/net/icmpv6/Kconfig
@@ -202,6 +202,13 @@ config NET_ICMPv6_PREFIX_8
 		Advertisement message.  This is the last of the 8-values.  The
 		default for all eight values is fc00::0.
 
+config NET_ICMPv6_ROUTER_RDNSS
+	bool "Fill the RDNSS into Router Advertisement"
+	default n
+	---help---
+		Fill the RDNSS from the netdb DNS Server into the Router
+		Advertisement.
+
 endif # NET_ICMPv6_ROUTER_MANUAL
 endif # NET_ICMPv6_ROUTER
 
diff --git a/net/icmpv6/icmpv6_radvertise.c b/net/icmpv6/icmpv6_radvertise.c
index 9429b96465f..2ed18fd2047 100644
--- a/net/icmpv6/icmpv6_radvertise.c
+++ b/net/icmpv6/icmpv6_radvertise.c
@@ -28,11 +28,13 @@
 #include <stdint.h>
 #include <string.h>
 #include <debug.h>
+#include <netinet/in.h>
 
 #include <nuttx/net/netconfig.h>
 #include <nuttx/net/netstats.h>
 #include <nuttx/net/netdev.h>
 #include <nuttx/net/icmpv6.h>
+#include <nuttx/net/dns.h>
 
 #include "netdev/netdev.h"
... (116 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/icmpv6/Kconfig b/net/icmpv6/Kconfig
index eeb493c8b77..147ebfcfc86 100644
--- a/net/icmpv6/Kconfig
+++ b/net/icmpv6/Kconfig
@@ -202,6 +202,13 @@ config NET_ICMPv6_PREFIX_8
 		Advertisement message.  This is the last of the 8-values.  The
 		default for all eight values is fc00::0.
 
+config NET_ICMPv6_ROUTER_RDNSS
+	bool "Fill the RDNSS into Router Advertisement"
+	default n
+	---help---
+		Fill the RDNSS from the netdb DNS Server into the Router
+		Advertisement.
+
 endif # NET_ICMPv6_ROUTER_MANUAL
 endif # NET_ICMPv6_ROUTER
 
diff --git a/net/icmpv6/icmpv6_radvertise.c b/net/icmpv6/icmpv6_radvertise.c
index 9429b96465f..c9a882a9739 100644
--- a/net/icmpv6/icmpv6_radvertise.c
+++ b/net/icmpv6/icmpv6_radvertise.c
@@ -28,11 +28,13 @@
 #include <stdint.h>
 #include <string.h>
 #include <debug.h>
+#include <netinet/in.h>
 
 #include <nuttx/net/netconfig.h>
 #include <nuttx/net/netstats.h>
 #include <nuttx/net/netdev.h>
 #include <nuttx/net/icmpv6.h>
+#include <nuttx/net/dns.h>
 
 #include "netdev/netdev.h"
... (123 lines truncated)
```
</details>

### 2. root-cause-fix/minimal_correct_fix (2x) [correctness]

**Rule:** When fixing loop termination logic, prefer adding explicit break conditions over inverting return value semantics — double negation obscures intent and may miss edge cases where the loop should exit for a different reason (e.g., no data to send).

**Example:** #885576 — net/devif: fix devif_poll loop logic
**Transformation:** Reviewer removed the negation of devif_poll_connections return value and instead added an explicit else-break when d_len is 0, fixing the loop exit logic without inverting the callback semantics.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/devif/devif_poll.c b/net/devif/devif_poll.c
index 89567a187ad..96017c0da73 100644
--- a/net/devif/devif_poll.c
+++ b/net/devif/devif_poll.c
@@ -929,9 +929,9 @@ int devif_poll(FAR struct net_driver_s *dev, devif_poll_callback_t callback)
           break;
         }
 
-      /* Perform all connections poll */
+      /* Perform all connections poll, it returns 0 when all done. */
 
-      bstop = devif_poll_connections(dev, devif_poll_callback);
+      bstop = !devif_poll_connections(dev, devif_poll_callback);
       if (dev->d_len > 0)
         {
           /* Copy iob to flat buffer */
@@ -965,7 +965,7 @@ int devif_poll(FAR struct net_driver_s *dev, devif_poll_callback_t callback)
             }
         }
     }
-  while (bstop);
+  while (!bstop);
 
   /* Device polling completed, release iob */
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/devif/devif_poll.c b/net/devif/devif_poll.c
index 89567a187ad..4b77fbe2187 100644
--- a/net/devif/devif_poll.c
+++ b/net/devif/devif_poll.c
@@ -964,8 +964,14 @@ int devif_poll(FAR struct net_driver_s *dev, devif_poll_callback_t callback)
                 }
             }
         }
+      else
+        {
+          /* Not stopped by devif_poll_callback, just stop and return bstop */
+
+          break;
+        }
     }
-  while (bstop);
+  while (!bstop);
 
   /* Device polling completed, release iob */
 

```
</details>

### 3. root-cause-fix/resource_lifecycle_consolidation (1x) [correctness]

**Rule:** Co-locate resource allocation and cleanup in the same function; when a resource (FIFO) is created as part of an operation (accept), its teardown must be in the same error-handling scope, not scattered across caller and callee

**Example:** #7045949 — net/local: fix the return address is incorrect when accept
**Transformation:** Moved local_create_fifos from local_stream_connect into local_alloc_accept so FIFO creation and cleanup are co-located with the accept connection lifecycle; added errout_with_fifos label for proper FIFO release on error; reordered local_alloc_accept before local_open_client_tx to ensure connection is established before opening client FIFOs; fixed local_getaddr to use conn->lc_peer and server->lc_path instead of client path

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/local/local.h b/net/local/local.h
index 26f15615f6e..0a23273a51f 100644
--- a/net/local/local.h
+++ b/net/local/local.h
@@ -604,7 +604,8 @@ int local_release_halfduplex(FAR struct local_conn_s *conn);
  *
  ****************************************************************************/
 
-int local_open_client_rx(FAR struct local_conn_s *client, bool nonblock);
+int local_open_client_rx(FAR struct local_conn_s *client,
+                         FAR struct local_conn_s *server, bool nonblock);
 
 /****************************************************************************
  * Name: local_open_client_tx
@@ -614,7 +615,8 @@ int local_open_client_rx(FAR struct local_conn_s *client, bool nonblock);
  *
  ****************************************************************************/
 
-int local_open_client_tx(FAR struct local_conn_s *client, bool nonblock);
+int local_open_client_tx(FAR struct local_conn_s *client,
+                         FAR struct local_conn_s *server, bool nonblock);
 
 /****************************************************************************
  * Name: local_open_server_rx
diff --git a/net/local/local_accept.c b/net/local/local_accept.c
index b802cf5754f..6710956ec2b 100644
--- a/net/local/local_accept.c
+++ b/net/local/local_accept.c
@@ -149,9 +149,9 @@ int local_accept(FAR struct socket *psock, FAR struct sockaddr *addr,
 
           /* Return the address family */
 
-          if (addr != NULL)
+          if (addr != NULL && conn->lc_peer != NULL)
             {
... (261 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/local/local.h b/net/local/local.h
index 26f15615f6e..0a23273a51f 100644
--- a/net/local/local.h
+++ b/net/local/local.h
@@ -604,7 +604,8 @@ int local_release_halfduplex(FAR struct local_conn_s *conn);
  *
  ****************************************************************************/
 
-int local_open_client_rx(FAR struct local_conn_s *client, bool nonblock);
+int local_open_client_rx(FAR struct local_conn_s *client,
+                         FAR struct local_conn_s *server, bool nonblock);
 
 /****************************************************************************
  * Name: local_open_client_tx
@@ -614,7 +615,8 @@ int local_open_client_rx(FAR struct local_conn_s *client, bool nonblock);
  *
  ****************************************************************************/
 
-int local_open_client_tx(FAR struct local_conn_s *client, bool nonblock);
+int local_open_client_tx(FAR struct local_conn_s *client,
+                         FAR struct local_conn_s *server, bool nonblock);
 
 /****************************************************************************
  * Name: local_open_server_rx
diff --git a/net/local/local_accept.c b/net/local/local_accept.c
index b802cf5754f..6710956ec2b 100644
--- a/net/local/local_accept.c
+++ b/net/local/local_accept.c
@@ -149,9 +149,9 @@ int local_accept(FAR struct socket *psock, FAR struct sockaddr *addr,
 
           /* Return the address family */
 
-          if (addr != NULL)
+          if (addr != NULL && conn->lc_peer != NULL)
             {
... (293 lines truncated)
```
</details>

### 4. coding-style/switch_default_preserve (1x) [correctness]

**Rule:** In switch statements, always keep the default case explicitly (even if it only breaks) rather than removing it — this prevents compiler warnings about unhandled cases and makes the intent clear; move shared post-switch logic outside the switch body.

**Example:** #6248180 — net/ipfilter: Add break for swich-case to increase robustness
**Transformation:** Reviewer required keeping the default case (with break) instead of removing it, moving the return true after the switch to cover both default and all fall-through paths uniformly.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/ipfilter/ipfilter.c b/net/ipfilter/ipfilter.c
index 8e6ef01c078..a506d6bcaa3 100644
--- a/net/ipfilter/ipfilter.c
+++ b/net/ipfilter/ipfilter.c
@@ -168,6 +168,7 @@ static bool ipfilter_match_proto(FAR const struct ipfilter_entry_s *entry,
             return SPORT_MATCH(entry, NTOHS(udp->srcport)) &&
                    DPORT_MATCH(entry, NTOHS(udp->destport));
           }
+        break;
 
       case IP_PROTO_ICMP:
         if (entry->match_icmp)
@@ -175,6 +176,7 @@ static bool ipfilter_match_proto(FAR const struct ipfilter_entry_s *entry,
             FAR const struct icmp_hdr_s *icmp = l4hdr;
             return ICMP_MATCH(entry, icmp);
           }
+        break;
 
       case IP_PROTO_ICMP6:
         if (entry->match_icmp)
@@ -182,10 +184,10 @@ static bool ipfilter_match_proto(FAR const struct ipfilter_entry_s *entry,
             FAR const struct icmpv6_hdr_s *icmpv6 = l4hdr;
             return ICMP_MATCH(entry, icmpv6);
           }
-
-      default:
-        return true;
+        break;
     }
+
+  return true;
 }
 
 /****************************************************************************

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/ipfilter/ipfilter.c b/net/ipfilter/ipfilter.c
index 8e6ef01c078..9ce0e36d084 100644
--- a/net/ipfilter/ipfilter.c
+++ b/net/ipfilter/ipfilter.c
@@ -168,6 +168,7 @@ static bool ipfilter_match_proto(FAR const struct ipfilter_entry_s *entry,
             return SPORT_MATCH(entry, NTOHS(udp->srcport)) &&
                    DPORT_MATCH(entry, NTOHS(udp->destport));
           }
+        break;
 
       case IP_PROTO_ICMP:
         if (entry->match_icmp)
@@ -175,6 +176,7 @@ static bool ipfilter_match_proto(FAR const struct ipfilter_entry_s *entry,
             FAR const struct icmp_hdr_s *icmp = l4hdr;
             return ICMP_MATCH(entry, icmp);
           }
+        break;
 
       case IP_PROTO_ICMP6:
         if (entry->match_icmp)
@@ -182,10 +184,13 @@ static bool ipfilter_match_proto(FAR const struct ipfilter_entry_s *entry,
             FAR const struct icmpv6_hdr_s *icmpv6 = l4hdr;
             return ICMP_MATCH(entry, icmpv6);
           }
+        break;
 
       default:
-        return true;
+        break;
     }
+
+  return true;
 }
 
 /****************************************************************************
... (1 lines truncated)
```
</details>

### 5. root-cause-fix/missing_fallthrough_guard (1x) [correctness]

**Rule:** When fixing switch fallthrough bugs, audit all cases in the same switch for the same class of missing break/goto, not just the one that triggered the bug report.

**Example:** #6203777 — net/tcp:Fixed the issue where the client and server sent FIN packets at the same time
**Transformation:** Developer added goto drop after TCP_TIME_WAIT to prevent fallthrough into TCP_CLOSE_WAIT. Architect required the same goto drop after TCP_CLOSE_WAIT to also prevent fallthrough into the default case, applying the fix consistently to all switch cases that need it.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/tcp/tcp_input.c b/net/tcp/tcp_input.c
index 3d36848fea7..052a8dd3c3e 100644
--- a/net/tcp/tcp_input.c
+++ b/net/tcp/tcp_input.c
@@ -1805,6 +1805,8 @@ skip_rtt:
             ninfo("TCP state: TCP_TIME_WAIT\n");
           }
 
+        goto drop;
+
       case TCP_CLOSE_WAIT:
 #ifdef CONFIG_NET_TCP_KEEPALIVE
         /* If the established socket receives an ACK or any kind of data

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/tcp/tcp_input.c b/net/tcp/tcp_input.c
index 3d36848fea7..f1adf00ee08 100644
--- a/net/tcp/tcp_input.c
+++ b/net/tcp/tcp_input.c
@@ -1805,6 +1805,8 @@ skip_rtt:
             ninfo("TCP state: TCP_TIME_WAIT\n");
           }
 
+        goto drop;
+
       case TCP_CLOSE_WAIT:
 #ifdef CONFIG_NET_TCP_KEEPALIVE
         /* If the established socket receives an ACK or any kind of data
@@ -1835,6 +1837,8 @@ skip_rtt:
             return;
           }
 
+        goto drop;
+
       default:
         break;
     }

```
</details>

### 6. root-cause-fix/refcount_explicit_reset (1x) [correctness]

**Rule:** When releasing a resource that must reach zero references, set the refcount to 0 explicitly rather than decrementing, to avoid assumptions about the current count and ensure deterministic cleanup.

**Example:** #5852606 — net/rpmsg: fix the issue of 'use after free' at rpmsgs_dec_ref
**Transformation:** Changed `conn->crefs--` (decrement) to `conn->crefs = 0` (explicit reset) when releasing a rejected connection after backlog full.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/rpmsg/rpmsg_sockif.c b/net/rpmsg/rpmsg_sockif.c
index de335a64289..af284c8dd0f 100644
--- a/net/rpmsg/rpmsg_sockif.c
+++ b/net/rpmsg/rpmsg_sockif.c
@@ -361,7 +361,7 @@ static int rpmsg_socket_ept_cb(FAR struct rpmsg_endpoint *ept,
                   nerr("Reject the connection, listening backlog is full\n");
                   nxmutex_unlock(&conn->server->recvlock);
                   rpmsg_destroy_ept(&conn->ept);
-                  rpmsg_socket_free(conn);
+                  conn->crefs--;
                   return 0;
                 }
             }

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/rpmsg/rpmsg_sockif.c b/net/rpmsg/rpmsg_sockif.c
index de335a64289..5ee0fc702c7 100644
--- a/net/rpmsg/rpmsg_sockif.c
+++ b/net/rpmsg/rpmsg_sockif.c
@@ -361,7 +361,7 @@ static int rpmsg_socket_ept_cb(FAR struct rpmsg_endpoint *ept,
                   nerr("Reject the connection, listening backlog is full\n");
                   nxmutex_unlock(&conn->server->recvlock);
                   rpmsg_destroy_ept(&conn->ept);
-                  rpmsg_socket_free(conn);
+                  conn->crefs = 0;
                   return 0;
                 }
             }

```
</details>

### 7. design-philosophy/unconditional_format_string_with_typed_defaults (1x) [design]

**Rule:** Diagnostic/procfs format strings should be unconditional for consistent output layout; use typed zero defaults in #else branches for disabled features rather than conditionally omitting format specifiers, which improves readability and prevents format/argument mismatch bugs.

**Example:** #5409499 — net/udp: fix build error when NET_UDP_WRITE_BUFFERS disable
**Transformation:** Changed conditional format string fragments to an unconditional format string, always displaying tx/rx fields with typed zero defaults ((uint32_t)0, (int32_t)0) when features are disabled, instead of hiding fields via #ifdef in the format string.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/procfs/net_udp.c b/net/procfs/net_udp.c
index ebff83e3a89..6bae9e75831 100644
--- a/net/procfs/net_udp.c
+++ b/net/procfs/net_udp.c
@@ -95,13 +95,13 @@ static ssize_t netprocfs_udpstats(FAR struct netprocfs_file_s *priv,
       len += snprintf(buffer + len, buflen - len,
                       "    %2" PRIu8
                       ": %3" PRIx8
-#if CONFIG_NET_SEND_BUFSIZE > 0
+#ifdef CONFIG_NET_UDP_WRITE_BUFFERS
                       " %6" PRIu32
 #endif
                       " %6u",
                       priv->offset++,
                       conn->sconn.s_flags,
-#if CONFIG_NET_SEND_BUFSIZE > 0
+#ifdef CONFIG_NET_UDP_WRITE_BUFFERS
                       udp_wrbuffer_inqueue_size(conn),
 #endif
                       (conn->readahead) ? conn->readahead->io_pktlen : 0);
diff --git a/net/udp/udp.h b/net/udp/udp.h
index d1e2f466109..1bc9dd117a1 100644
--- a/net/udp/udp.h
+++ b/net/udp/udp.h
@@ -594,9 +594,7 @@ void udp_wrbuffer_release(FAR struct udp_wrbuffer_s *wrb);
  *
  ****************************************************************************/
 
-#if CONFIG_NET_SEND_BUFSIZE > 0
 uint32_t udp_wrbuffer_inqueue_size(FAR struct udp_conn_s *conn);
-#endif /* CONFIG_NET_SEND_BUFSIZE */
 
 /****************************************************************************
  * Name: udp_wrbuffer_test
diff --git a/net/udp/udp_ioctl.c b/net/udp/udp_ioctl.c
... (52 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/procfs/net_udp.c b/net/procfs/net_udp.c
index ebff83e3a89..6bae9e75831 100644
--- a/net/procfs/net_udp.c
+++ b/net/procfs/net_udp.c
@@ -95,13 +95,13 @@ static ssize_t netprocfs_udpstats(FAR struct netprocfs_file_s *priv,
       len += snprintf(buffer + len, buflen - len,
                       "    %2" PRIu8
                       ": %3" PRIx8
-#if CONFIG_NET_SEND_BUFSIZE > 0
+#ifdef CONFIG_NET_UDP_WRITE_BUFFERS
                       " %6" PRIu32
 #endif
                       " %6u",
                       priv->offset++,
                       conn->sconn.s_flags,
-#if CONFIG_NET_SEND_BUFSIZE > 0
+#ifdef CONFIG_NET_UDP_WRITE_BUFFERS
                       udp_wrbuffer_inqueue_size(conn),
 #endif
                       (conn->readahead) ? conn->readahead->io_pktlen : 0);
diff --git a/net/udp/udp.h b/net/udp/udp.h
index d1e2f466109..1bc9dd117a1 100644
--- a/net/udp/udp.h
+++ b/net/udp/udp.h
@@ -594,9 +594,7 @@ void udp_wrbuffer_release(FAR struct udp_wrbuffer_s *wrb);
  *
  ****************************************************************************/
 
-#if CONFIG_NET_SEND_BUFSIZE > 0
 uint32_t udp_wrbuffer_inqueue_size(FAR struct udp_conn_s *conn);
-#endif /* CONFIG_NET_SEND_BUFSIZE */
 
 /****************************************************************************
  * Name: udp_wrbuffer_test
diff --git a/net/udp/udp_ioctl.c b/net/udp/udp_ioctl.c
... (60 lines truncated)
```
</details>

### 8. design-philosophy/eliminate_redundant_state (1x) [correctness]

**Rule:** Do not maintain a shadow counter for state already queryable from the underlying data structure; prefer querying the source of truth (e.g., iob_get_queue_entry_count) to avoid synchronization bugs and redundant critical sections.

**Example:** #4027833 — CanSocket:Readahead frame count limit in can socket
**Transformation:** Removed redundant readahead_frame_curr/readahead_frame_max counters and critical sections, replaced with iob_get_queue_entry_count() query against a single recv_buffnum field, and unified error paths via goto errout.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/netpacket/can.h b/include/netpacket/can.h
index 236bbbf2a30..bf2d96fef12 100644
--- a/include/netpacket/can.h
+++ b/include/netpacket/can.h
@@ -81,6 +81,8 @@
                                  /* all filters must match to trigger */
 #define CAN_RAW_TX_DEADLINE    (__SO_PROTOCOL + 6)
                                  /* Abort frame when deadline passed */
+#define CAN_READAHEAD_FRAME_COUNT    (__SO_PROTOCOL + 7)
+                                 /* Abort frame when the data on readahead exceeds the upper limit */
 
 /* CAN filter support (Hardware level filtering) ****************************/
 
diff --git a/net/can/Kconfig b/net/can/Kconfig
index b24895fd143..db5c69bd000 100644
--- a/net/can/Kconfig
+++ b/net/can/Kconfig
@@ -145,5 +145,15 @@ config NET_CAN_NOTIFIER
 		purpose notifier, but was developed specifically to support poll()
 		logic where the poll must wait for these events.
 
+config CAN_SOCKET_READAHEAD_FRAME_MAX
+		int "Can conn readahead lenth"
+		default 1
+		---help---
+			Can conn readahead lenth.
+	
+			If Can Socket Stack recive can frame and pending on the can_conns->readahead,
+			but the application layer did not read the frame. This will cause a memory leak, 
+			so it is necessary to limit readahead.
+	
 endif # NET_CAN
 endmenu # CAN Socket Support
diff --git a/net/can/can.h b/net/can/can.h
index 1c0a78c0060..50edefc7c72 100644
... (120 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/netpacket/can.h b/include/netpacket/can.h
index 236bbbf2a30..0915c1660c5 100644
--- a/include/netpacket/can.h
+++ b/include/netpacket/can.h
@@ -81,6 +81,8 @@
                                  /* all filters must match to trigger */
 #define CAN_RAW_TX_DEADLINE    (__SO_PROTOCOL + 6)
                                  /* Abort frame when deadline passed */
+#define CAN_RAW_RECV_BUFFNUM   (__SO_PROTOCOL + 7)
+                                 /* Max count of buffered frames */
 
 /* CAN filter support (Hardware level filtering) ****************************/
 
diff --git a/net/can/Kconfig b/net/can/Kconfig
index b24895fd143..6a46a863b97 100644
--- a/net/can/Kconfig
+++ b/net/can/Kconfig
@@ -145,5 +145,14 @@ config NET_CAN_NOTIFIER
 		purpose notifier, but was developed specifically to support poll()
 		logic where the poll must wait for these events.
 
+config CAN_SOCKET_RECV_BUFFNUM
+	int "Can conn recive buffer number"
+	default 8
+	---help---
+		Can conn recive buffer number
+		If Can Socket Stack recive can frames, but the application layer did
+		not read these frames. This will cause a memory leak.
+		So it is necessary to limit recive buffer number.
+
 endif # NET_CAN
 endmenu # CAN Socket Support
diff --git a/net/can/can.h b/net/can/can.h
index 1c0a78c0060..bf29d0d1b56 100644
--- a/net/can/can.h
... (89 lines truncated)
```
</details>

### 9. root-cause-fix/incomplete_flag_migration (1x) [correctness]

**Rule:** When changing a flag semantic (e.g. IFF_IS_UP to IFF_IS_RUNNING), grep the entire codebase and migrate all check sites consistently; a partial migration leaves inconsistent behavior between subsystems.

**Example:** #4895059 — netdev:when network device is running status,the network protocol stack is allowed to send packet to the network device and to receive from the network device
**Transformation:** Reviewer expanded IFF_IS_UP→IFF_IS_RUNNING migration from upperhalf driver only to also cover net/netdev layer (netdev_default, netdev_findbyaddr) where raw IFF_UP flag checks guarded device selection and routing.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/drivers/net/netdev_upperhalf.c b/drivers/net/netdev_upperhalf.c
index cf5f65223ef..9fa98f003af 100644
--- a/drivers/net/netdev_upperhalf.c
+++ b/drivers/net/netdev_upperhalf.c
@@ -372,9 +372,9 @@ static void netdev_upper_txavail_work(FAR struct netdev_upperhalf_s *upper)
 {
   FAR struct net_driver_s *dev = &upper->lower->netdev;
 
-  /* Ignore the notification if the interface is not yet up */
+  /* Ignore the notification if the interface is not yet running */
 
-  if (IFF_IS_UP(dev->d_flags))
+  if (IFF_IS_RUNNING(dev->d_flags))
     {
       DEBUGASSERT(dev->d_buf == NULL); /* Make sure: IOB only. */
       while (netdev_upper_can_tx(upper) &&
@@ -608,9 +608,9 @@ static void netdev_upper_rxpoll_work(FAR struct netdev_upperhalf_s *upper)
 
   while ((pkt = lower->ops->receive(lower)) != NULL)
     {
-      if (!IFF_IS_UP(dev->d_flags))
+      if (!IFF_IS_RUNNING(dev->d_flags))
         {
-          /* Interface down, drop frame */
+          /* Interface not running, drop frame */
 
           NETDEV_RXDROPPED(dev);
           netpkt_free(lower, pkt, NETPKT_RX);

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/netdev/netdev_default.c b/net/netdev/netdev_default.c
index 187734d9209..da5030be3a9 100644
--- a/net/netdev/netdev_default.c
+++ b/net/netdev/netdev_default.c
@@ -68,7 +68,7 @@ FAR struct net_driver_s *netdev_default(void)
     {
       /* Is the interface in the "up" state? */
 
-      if ((dev->d_flags & IFF_UP) != 0)
+      if (IFF_IS_RUNNING(dev->d_flags) != 0)
         {
           /* Return a reference to the first device that we find in the UP
            * state (but not the loopback device unless it is the only
diff --git a/net/netdev/netdev_findbyaddr.c b/net/netdev/netdev_findbyaddr.c
index 88143b721a9..9867b08c53c 100644
--- a/net/netdev/netdev_findbyaddr.c
+++ b/net/netdev/netdev_findbyaddr.c
@@ -85,7 +85,7 @@ netdev_prefixlen_findby_lipv4addr(in_addr_t lipaddr, FAR int8_t *prefixlen)
     {
       /* Is the interface in the "up" state? */
 
-      if ((dev->d_flags & IFF_UP) != 0 &&
+      if (IFF_IS_RUNNING(dev->d_flags) != 0 &&
           !net_ipv4addr_cmp(dev->d_ipaddr, INADDR_ANY))
         {
 #ifndef CONFIG_ROUTE_LONGEST_MATCH
@@ -196,7 +196,7 @@ netdev_prefixlen_findby_lipv6addr(const net_ipv6addr_t lipaddr,
     {
       /* Is the interface in the "up" state? */
 
-      if ((dev->d_flags & IFF_UP) != 0 && NETDEV_HAS_V6ADDR(dev))
+      if (IFF_IS_RUNNING(dev->d_flags) != 0 && NETDEV_HAS_V6ADDR(dev))
         {
 #ifndef CONFIG_ROUTE_LONGEST_MATCH
           /* Yes.. check for an address match (under the netmask) */
... (1 lines truncated)
```
</details>

### 10. root-cause-fix/replace_debug_log_with_cancellation_point (1x) [correctness]

**Rule:** Do not commit debug logging as a fix; identify the actual deficiency (here, missing cancellation point compliance) and implement the correct POSIX mechanism with proper resource cleanup guarantees.

**Example:** #4029968 — udp:add tls cleanup protection to protect waitsem in udp_txdrain
**Transformation:** Removed ad-hoc syslog debug tracing of pid/tcb and replaced with proper POSIX thread cancellation point support (enter/leave_cancellation_point, tls_cleanup_push/pop) and adjusted udp_notifier_teardown signature to FAR void* for use as a TLS cleanup handler.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/udp/udp_txdrain.c b/net/udp/udp_txdrain.c
index 4bd0da23e24..481e578cc71 100644
--- a/net/udp/udp_txdrain.c
+++ b/net/udp/udp_txdrain.c
@@ -28,9 +28,11 @@
 #include <sched.h>
 #include <assert.h>
 #include <errno.h>
+#include <debug.h>
 
 #include <nuttx/net/net.h>
 #include <nuttx/semaphore.h>
+#include <nuttx/sched.h>
 
 #include "utils/utils.h"
 #include "udp/udp.h"
@@ -100,6 +102,13 @@ int udp_txdrain(FAR struct socket *psock, unsigned int timeout)
 
   nxsem_init(&waitsem, 0, 0);
 
+  uint32_t pid = nxsched_getpid();
+  FAR struct tcb_s *tcb = nxsched_get_tcb(pid);
+  if(tcb != NULL)
+    {
+      syslog(0,"func %s pid %ld tcb->name %s \n",__func__, pid, tcb->name);
+    }
+
   /* The following needs to be done with the network stable */
 
   net_lock();

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/udp/udp.h b/net/udp/udp.h
index 920b5bb5fad..fd7b180d879 100644
--- a/net/udp/udp.h
+++ b/net/udp/udp.h
@@ -885,7 +885,7 @@ int udp_writebuffer_notifier_setup(worker_t worker,
  ****************************************************************************/
 
 #ifdef CONFIG_NET_UDP_NOTIFIER
-void udp_notifier_teardown(int key);
+void udp_notifier_teardown(FAR void *key);
 #endif
 
 /****************************************************************************
diff --git a/net/udp/udp_notifier.c b/net/udp/udp_notifier.c
index b1726ab5127..77322d35731 100644
--- a/net/udp/udp_notifier.c
+++ b/net/udp/udp_notifier.c
@@ -168,11 +168,11 @@ int udp_writebuffer_notifier_setup(worker_t worker,
  *
  ****************************************************************************/
 
-void udp_notifier_teardown(int key)
+void udp_notifier_teardown(FAR void *key)
 {
   /* This is just a simple wrapper around work_notifier_teardown(). */
 
-  work_notifier_teardown(key);
+  work_notifier_teardown(*(FAR int *)key);
 }
 
 /****************************************************************************
diff --git a/net/udp/udp_txdrain.c b/net/udp/udp_txdrain.c
index 4bd0da23e24..3d95f1925b0 100644
--- a/net/udp/udp_txdrain.c
+++ b/net/udp/udp_txdrain.c
... (47 lines truncated)
```
</details>

### 11. patch-organization/missing_symmetric_change (1x) [correctness]

**Rule:** When modifying one protocol-variant file (e.g. ipt_* for IPv4), always check and apply the same change to its counterpart (e.g. ip6t_* for IPv6) to maintain consistency and avoid missing-header build failures.

**Example:** #4430058 — fix compile warning with ipt_sockopt.c and ip6t_sockopt.c: implicit declaration of function 'popcount'
**Transformation:** Reviewer identified that adding #include <strings.h> to ipt_sockopt.c also required the same include in the symmetric ip6t_sockopt.c file (IPv6 counterpart).

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/net/netfilter/ipt_sockopt.c b/net/netfilter/ipt_sockopt.c
index d1e6e0223d4..97e2eb62289 100644
--- a/net/netfilter/ipt_sockopt.c
+++ b/net/netfilter/ipt_sockopt.c
@@ -26,6 +26,7 @@
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>
+#include <strings.h>
 
 #include <sys/param.h>
 

```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/net/netfilter/ip6t_sockopt.c b/net/netfilter/ip6t_sockopt.c
index c4804ba5d47..c474285b4e1 100644
--- a/net/netfilter/ip6t_sockopt.c
+++ b/net/netfilter/ip6t_sockopt.c
@@ -26,6 +26,7 @@
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>
+#include <strings.h>
 
 #include <sys/param.h>
 
diff --git a/net/netfilter/ipt_sockopt.c b/net/netfilter/ipt_sockopt.c
index d1e6e0223d4..97e2eb62289 100644
--- a/net/netfilter/ipt_sockopt.c
+++ b/net/netfilter/ipt_sockopt.c
@@ -26,6 +26,7 @@
 #include <stddef.h>
 #include <stdint.h>
 #include <string.h>
+#include <strings.h>
 
 #include <sys/param.h>
 

```
</details>

### 12. coding-style/switch_case_indentation (1x) [style]

**Rule:** In NuttX switch/case blocks, the body of each case should be indented by exactly 2 spaces from the case label, not 4; maintain consistent indentation across all cases including newly added ones.

**Example:** #4194036 — net: Support to send the GSO UDP packets.
**Transformation:** Fixed inconsistent indentation in switch/case bodies from 10 spaces to 8 spaces (2-space indent from case label) to match NuttX coding standard.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/include/nuttx/net/offload.h b/include/nuttx/net/offload.h
index e5bbe773e49..8efc10e7d4e 100644
--- a/include/nuttx/net/offload.h
+++ b/include/nuttx/net/offload.h
@@ -65,6 +65,8 @@ enum GSO_TYPE_E
 {
     PKT_GSO_TCPV4 = 1 << 0,
     PKT_GSO_TCPV6 = 1 << 1,
+    PKT_GSO_UDPV4 = 1 << 2,
+    PKT_GSO_UDPV6 = 1 << 3,
 };
 
 struct gso_cb
@@ -226,6 +228,24 @@ FAR struct iob_s *devif_send_gso_pkt(FAR struct iob_s *pkt,
                        unsigned int sndlen, unsigned int offset,
                        unsigned int hdrlen, uint16_t gso_size);
 
+/****************************************************************************
+ * Name: devif_prepare_gso_segments
+ *
+ * Description:
+ *   Allocate the iob for the GSO packet, based on the size and count.
+ *
+ * Input Parameters:
+ *   size  - the sum of L3 header length, L4 header length, and gso_size.
+ *   count  - the features are supported by the target dev of the packet.
+ *
+ * Returned Value:
+ *   The NULL indicates that the allocation is failure.
+ *   The segs is the head of the multiple iob.
+ *
+ ****************************************************************************/
+
+FAR struct iob_s *devif_prepare_gso_segments(uint16_t size, uint32_t count);
+
... (648 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/include/nuttx/net/offload.h b/include/nuttx/net/offload.h
index 082aa1c84c8..f5df0ec6b5e 100644
--- a/include/nuttx/net/offload.h
+++ b/include/nuttx/net/offload.h
@@ -47,6 +47,8 @@ enum gso_type_e
 {
     PKT_GSO_TCPV4 = 1 << 0,
     PKT_GSO_TCPV6 = 1 << 1,
+    PKT_GSO_UDPV4 = 1 << 2,
+    PKT_GSO_UDPV6 = 1 << 3,
 };
 
 struct gso_cb_s
@@ -197,6 +199,24 @@ FAR struct iob_s *devif_send_gso_pkt(FAR struct iob_s *pkt,
                        unsigned int sndlen, unsigned int offset,
                        unsigned int hdrlen, uint16_t gso_size);
 
+/****************************************************************************
+ * Name: devif_prepare_gso_segments
+ *
+ * Description:
+ *   Allocate the iob for the GSO packet, based on the size and count.
+ *
+ * Input Parameters:
+ *   size  - the sum of L3 header length, L4 header length, and gso_size.
+ *   count  - the features are supported by the target dev of the packet.
+ *
+ * Returned Value:
+ *   The NULL indicates that the allocation is failure.
+ *   The segs is the head of the multiple iob.
+ *
+ ****************************************************************************/
+
+FAR struct iob_s *devif_prepare_gso_segments(uint16_t size, uint32_t count);
+
... (651 lines truncated)
```
</details>

### 13. code-reuse/ifdef_else_code_duplication (1x) [design]

**Rule:** When both branches of an #ifdef/#else contain identical code, factor the common code out of the conditional so it appears only once — wrap only the differing logic inside the preprocessor guard.

**Example:** #4187491 — net: Support to send the GSO TCP packets.
**Transformation:** Removed duplicated `transmit()` call from both #ifdef/#else branches by restructuring so the GSO-specific logic is wrapped in its own #ifdef block and the common transmit call appears once outside it.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/src/sim/sim_netdriver.c b/arch/sim/src/sim/sim_netdriver.c
index 830b5499fe3..a44216c94f2 100644
--- a/arch/sim/src/sim/sim_netdriver.c
+++ b/arch/sim/src/sim/sim_netdriver.c
@@ -65,6 +65,7 @@
 #include <nuttx/compiler.h>
 #include <nuttx/kmalloc.h>
 #include <nuttx/wqueue.h>
+#include <nuttx/net/offload.h>
 #include <nuttx/net/net.h>
 #include <nuttx/net/netdev_lowerhalf.h>
 #include <nuttx/net/pkt.h>
@@ -134,6 +135,26 @@ static int netdriver_send(struct netdev_lowerhalf_s *dev, netpkt_t *pkt)
 {
   unsigned int len  = netpkt_getdatalen(dev, pkt);
 
+  #ifdef CONFIG_NET_GSO
+  struct net_driver_s *netdev = &(dev->netdev);
+
+  if (PKT_GSOINFO(netdev)->gso_size != 0 &&
+      PKT_GSOINFO(netdev)->gso_segs != 0)
+    {
+      /* Loop to send  the gso list */
+
+      netpkt_t *segs = pkt;
+      while (segs)
+        {
+          sim_netdev_send(DEVIDX(dev), netpkt_getdata(dev, segs),
+                          netpkt_getdatalen(dev, segs));
+          segs = segs->io_flink;
+        }
+
+      memset(PKT_GSOINFO(netdev), 0, sizeof(struct gso_cb));
+    }
+  else
... (1888 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/src/sim/sim_netdriver.c b/arch/sim/src/sim/sim_netdriver.c
index 842e174c8c8..8560150933f 100644
--- a/arch/sim/src/sim/sim_netdriver.c
+++ b/arch/sim/src/sim/sim_netdriver.c
@@ -65,6 +65,7 @@
 #include <nuttx/compiler.h>
 #include <nuttx/kmalloc.h>
 #include <nuttx/wqueue.h>
+#include <nuttx/net/offload.h>
 #include <nuttx/net/net.h>
 #include <nuttx/net/netdev_lowerhalf.h>
 #include <nuttx/net/pkt.h>
@@ -134,6 +135,25 @@ static int netdriver_send(struct netdev_lowerhalf_s *dev, netpkt_t *pkt)
 {
   unsigned int len  = netpkt_getdatalen(dev, pkt);
 
+#ifdef CONFIG_NET_GSO
+  struct net_driver_s *netdev = &(dev->netdev);
+
+  if (PKT_GSOINFO(netdev)->gso_segs != 0)
+    {
+      /* Loop to send  the gso list */
+
+      netpkt_t *segs = pkt;
+      while (segs)
+        {
+          sim_netdev_send(DEVIDX(dev), netpkt_getdata(dev, segs),
+                          netpkt_getdatalen(dev, segs));
+          segs = segs->io_flink;
+        }
+
+      memset(PKT_GSOINFO(netdev), 0, sizeof(struct gso_cb_s));
+    }
+  else
+#endif
... (1688 lines truncated)
```
</details>

### 14. design-philosophy/state_ownership_placement (1x) [design]

**Rule:** Place protocol offload state at the correct abstraction level — GRO aggregation context belongs to the network device (net_driver_s), not to individual IO buffers (iob_s), because offload is a per-device capability not per-packet metadata; avoid wrapper functions that hide trivial flag-setting behind an API.

**Example:** #4162136 — net: Support to receive the LRO packets.
**Transformation:** Moved GRO state (gro_info, include) from per-packet iob_s to per-device net_driver_s, removed unnecessary wrapper function netdev_enable_lro() in favor of direct field assignment, and eliminated unused NETIF_F_GRO_HW feature bit.

<details><summary>Before (developer's submission)</summary>

```diff
diff --git a/arch/sim/src/sim/sim_netdriver.c b/arch/sim/src/sim/sim_netdriver.c
index a24f5c9da41..1182a1885d9 100644
--- a/arch/sim/src/sim/sim_netdriver.c
+++ b/arch/sim/src/sim/sim_netdriver.c
@@ -267,6 +267,11 @@ int sim_netdriver_init(void)
         }
 #endif
 
+#ifdef CONFIG_NET_SEG_OFFLOAD
+#ifdef CONFIG_NET_GRO
+      netdev_enable_lro(&dev->netdev);
+#endif
+#endif
       /* Register the device with the OS so that socket IOCTLs can be
        * performed
        */
diff --git a/include/nuttx/mm/iob.h b/include/nuttx/mm/iob.h
index a69be9803e8..88cb6eb9357 100644
--- a/include/nuttx/mm/iob.h
+++ b/include/nuttx/mm/iob.h
@@ -34,6 +34,10 @@
 #  include <nuttx/wqueue.h>
 #endif
 
+#ifdef CONFIG_NET_SEG_OFFLOAD
+#  include <nuttx/net/offload.h>
+#endif
+
 #ifdef CONFIG_MM_IOB
 
 /****************************************************************************
@@ -125,6 +129,10 @@ struct iob_s
 #endif
   unsigned int io_pktlen; /* Total length of the packet */
 
... (374 lines truncated)
```
</details>

<details><summary>After (architect's approved version)</summary>

```diff
diff --git a/arch/sim/src/sim/sim_netdriver.c b/arch/sim/src/sim/sim_netdriver.c
index a24f5c9da41..842e174c8c8 100644
--- a/arch/sim/src/sim/sim_netdriver.c
+++ b/arch/sim/src/sim/sim_netdriver.c
@@ -267,6 +267,13 @@ int sim_netdriver_init(void)
         }
 #endif
 
+#ifdef CONFIG_NET_GRO
+      /* Enable the LRO function for the netdev. */
+
+      dev->netdev.d_features |= NETIF_F_LRO;
+      dev->netdev.d_gro_max_size = GRO_MAX_SIZE;
+      dev->netdev.d_gro_max_segs = GRO_MAX_SEGS;
+#endif
       /* Register the device with the OS so that socket IOCTLs can be
        * performed
        */
diff --git a/include/nuttx/net/netdev.h b/include/nuttx/net/netdev.h
index 74dfcea9a3b..d9236e149d2 100644
--- a/include/nuttx/net/netdev.h
+++ b/include/nuttx/net/netdev.h
@@ -65,6 +65,10 @@
 #include <nuttx/net/ip.h>
 #include <nuttx/nuttx.h>
 
+#ifdef CONFIG_NET_SEG_OFFLOAD
+#include <nuttx/net/offload.h>
+#endif
+
 #ifdef CONFIG_NET_IGMP
 #  include <nuttx/net/igmp.h>
 #endif
@@ -181,6 +185,15 @@
      (netdev_ipv6_lookup(dev, addr, true) != NULL)
... (293 lines truncated)
```
</details>

### 15. design-philosophy/localize_driver_changes_avoid_core_header_pollution (1x) [design]

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
