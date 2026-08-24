/****************************************************************************
 * libs/libc/netdb/lib_dnsdelserver.c
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

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <arpa/inet.h>

#include <nuttx/net/ip.h>
#include <nuttx/net/dns.h>

#include "netdb/lib_dns.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

#ifdef CONFIG_NETDB_RESOLVCONF
/* Context used while walking the resolv.conf file.  All of the nameservers
 * that do NOT match 'target' are collected into 'keep' so that the file can
 * be re-written without the removed entry.
 */

struct dns_delserver_s
{
  union dns_addr_u target;    /* The nameserver to be removed */
  union dns_addr_u keep[CONFIG_NETDB_DNSSERVER_NAMESERVERS];
  uint8_t nkeep;              /* Number of valid entries in keep[] */
  bool found;                 /* True: at least one entry matched target */
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dns_addr_match
 *
 * Description:
 *   Compare two DNS server addresses.  Only the address family, the IP
 *   address and the port number are significant;  padding fields such as
 *   sin_zero are ignored.
 *
 * Returned Value:
 *   True if both addresses refer to the same DNS server.
 *
 ****************************************************************************/

static bool dns_addr_match(FAR const struct sockaddr *addr1,
                           FAR const struct sockaddr *addr2)
{
  if (addr1->sa_family != addr2->sa_family)
    {
      return false;
    }

#ifdef CONFIG_NET_IPv4
  if (addr1->sa_family == AF_INET)
    {
      FAR const struct sockaddr_in *in1 =
        (FAR const struct sockaddr_in *)addr1;
      FAR const struct sockaddr_in *in2 =
        (FAR const struct sockaddr_in *)addr2;

      return in1->sin_port == in2->sin_port &&
             in1->sin_addr.s_addr == in2->sin_addr.s_addr;
    }
#endif

#ifdef CONFIG_NET_IPv6
  if (addr1->sa_family == AF_INET6)
    {
      FAR const struct sockaddr_in6 *in1 =
        (FAR const struct sockaddr_in6 *)addr1;
      FAR const struct sockaddr_in6 *in2 =
        (FAR const struct sockaddr_in6 *)addr2;

      return in1->sin6_port == in2->sin6_port &&
             memcmp(&in1->sin6_addr, &in2->sin6_addr,
                    sizeof(struct in6_addr)) == 0;
    }
#endif

  return false;
}

/****************************************************************************
 * Name: dns_copy_address
 *
 * Description:
 *   Validate the caller provided address and copy it into a normalized
 *   union dns_addr_u.  A port number of zero is replaced with the default
 *   DNS server port so that the result can be compared against the
 *   nameservers held by the resolver.
 *
 * Returned Value:
 *   Zero (OK) on success;  a negated errno value on failure.
 *
 ****************************************************************************/

static int dns_copy_address(FAR union dns_addr_u *dst,
                            FAR const struct sockaddr *src,
                            socklen_t addrlen)
{
  FAR uint16_t *pport;
  size_t copylen;

#ifdef CONFIG_NET_IPv4
  /* Check for an IPv4 address */

  if (src->sa_family == AF_INET)
    {
      copylen = sizeof(struct sockaddr_in);
      pport   = &dst->ipv4.sin_port;
    }
  else
#endif
#ifdef CONFIG_NET_IPv6
  /* Check for an IPv6 address */

  if (src->sa_family == AF_INET6)
    {
      copylen = sizeof(struct sockaddr_in6);
      pport   = &dst->ipv6.sin6_port;
    }
  else
#endif
    {
      nerr("ERROR: Unsupported family: %d\n", src->sa_family);
      return -ENOSYS;
    }

  if (addrlen < copylen)
    {
      nerr("ERROR: Invalid addrlen %ld for family %d\n",
           (long)addrlen, src->sa_family);
      return -EINVAL;
    }

  memset(dst, 0, sizeof(*dst));
  memcpy(&dst->addr, src, copylen);

  /* A port number of zero means to use the default DNS server port number */

  if (*pport == 0)
    {
      *pport = HTONS(DNS_DEFAULT_PORT);
    }

  return OK;
}

#ifdef CONFIG_NETDB_RESOLVCONF

/****************************************************************************
 * Name: dns_collect_nameserver
 *
 * Description:
 *   dns_foreach_nameserver() callback.  Records every nameserver except the
 *   one that is being removed.
 *
 ****************************************************************************/

static int dns_collect_nameserver(FAR void *arg, FAR struct sockaddr *addr,
                                  socklen_t addrlen)
{
  FAR struct dns_delserver_s *ctx = (FAR struct dns_delserver_s *)arg;

  /* Drop every entry that matches the nameserver being removed */

  if (dns_addr_match(&ctx->target.addr, addr))
    {
      ctx->found = true;
      return OK;
    }

  if (ctx->nkeep >= CONFIG_NETDB_DNSSERVER_NAMESERVERS)
    {
      nwarn("WARNING: More than %d nameservers in %s, entry dropped\n",
            CONFIG_NETDB_DNSSERVER_NAMESERVERS,
            CONFIG_NETDB_RESOLVCONF_PATH);
      return OK;
    }

  memcpy(&ctx->keep[ctx->nkeep++].addr, addr, addrlen);
  return OK;
}

/****************************************************************************
 * Name: dns_write_nameserver
 *
 * Description:
 *   Write a single 'nameserver' record into the resolv.conf file.
 *
 * Returned Value:
 *   Zero (OK) on success;  a negated errno value on failure.
 *
 ****************************************************************************/

static int dns_write_nameserver(FAR FILE *stream,
                                FAR const union dns_addr_u *server)
{
  char addrstr[DNS_MAX_ADDRSTR];
  FAR const void *inaddr;
  uint16_t port;
  int ret;

#ifdef CONFIG_NET_IPv4
  if (server->addr.sa_family == AF_INET)
    {
      inaddr = &server->ipv4.sin_addr;
      port   = server->ipv4.sin_port;
    }
  else
#endif
#ifdef CONFIG_NET_IPv6
  if (server->addr.sa_family == AF_INET6)
    {
      inaddr = &server->ipv6.sin6_addr;
      port   = server->ipv6.sin6_port;
    }
  else
#endif
    {
      nerr("ERROR: Unsupported family: %d\n", server->addr.sa_family);
      return -ENOSYS;
    }

  if (inet_ntop(server->addr.sa_family, inaddr, addrstr,
                DNS_MAX_ADDRSTR) == NULL)
    {
      ret = -get_errno();
      nerr("ERROR: inet_ntop failed: %d\n", ret);
      return ret;
    }

#ifdef CONFIG_NETDB_RESOLVCONF_NONSTDPORT
  /* The OpenBSD version supports a [host]:port syntax.  When a non-standard
   * port is specified the host address must be enclosed in square brackets.
   */

  ret = fprintf(stream, "%s [%s]:%u\n",
                NETDB_DNS_KEYWORD, addrstr, NTOHS(port));
#else
  UNUSED(port);
  ret = fprintf(stream, "%s %s\n", NETDB_DNS_KEYWORD, addrstr);
#endif

  if (ret < 0)
    {
      ret = -get_errno();
      nerr("ERROR: fprintf failed: %d\n", ret);
      return ret;
    }

  return OK;
}

#endif /* CONFIG_NETDB_RESOLVCONF */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: dns_del_nameserver
 *
 * Description:
 *   Remove a DNS server from the resolver.  A port number of zero matches
 *   a nameserver that was added with the default DNS server port.
 *
 *   NOTE:  No nameserver notification is generated.  The notification
 *   interface (dns_register_notify()) carries only an address and cannot
 *   express the removal of a nameserver, so notifying here would be
 *   interpreted by the subscribers as the addition of the address.
 *
 * Input Parameters:
 *   addr    - The address of the DNS server to remove.
 *   addrlen - The length of the address structure.
 *
 * Returned Value:
 *   Zero (OK) is returned if the nameserver was removed.  Otherwise a
 *   negated errno value is returned:
 *
 *     -EINVAL  The addrlen is too small for the address family.
 *     -ENOSYS  The address family is not supported.
 *     -ENOENT  The address is not in the nameserver list.
 *
 ****************************************************************************/

#ifdef CONFIG_NETDB_RESOLVCONF
int dns_del_nameserver(FAR const struct sockaddr *addr, socklen_t addrlen)
{
  struct dns_delserver_s ctx;
  FAR FILE *stream;
  int ret;
  int i;

  DEBUGASSERT(addr != NULL);

  memset(&ctx, 0, sizeof(ctx));

  ret = dns_copy_address(&ctx.target, addr, addrlen);
  if (ret < 0)
    {
      return ret;
    }

  dns_lock();

  /* Collect the nameservers that have to be preserved.  This re-uses the
   * resolv.conf parser of dns_foreach_nameserver().
   */

  ret = dns_foreach_nameserver(dns_collect_nameserver, &ctx);
  if (ret < 0)
    {
      dns_unlock();
      return ret;
    }

  if (!ctx.found)
    {
      dns_unlock();
      return -ENOENT;
    }

  /* Re-write the resolv.conf file without the removed nameserver */

  stream = fopen(CONFIG_NETDB_RESOLVCONF_PATH, "w");
  if (stream == NULL)
    {
      ret = -get_errno();
      nerr("ERROR: Failed to open %s: %d\n",
           CONFIG_NETDB_RESOLVCONF_PATH, ret);
      dns_unlock();
      return ret;
    }

  for (i = 0; i < ctx.nkeep; i++)
    {
      ret = dns_write_nameserver(stream, &ctx.keep[i]);
      if (ret < 0)
        {
          fclose(stream);
          dns_unlock();
          return ret;
        }
    }

  fclose(stream);
  dns_unlock();

#if CONFIG_NETDB_DNSCLIENT_ENTRIES > 0
  /* Any cached answer may have been resolved by the removed server */

  dns_clear_answer();
#endif

  return OK;
}

#else /* CONFIG_NETDB_RESOLVCONF */

int dns_del_nameserver(FAR const struct sockaddr *addr, socklen_t addrlen)
{
  union dns_addr_u target;
  bool found = false;
  int ret;
  int i;

  DEBUGASSERT(addr != NULL);

  ret = dns_copy_address(&target, addr, addrlen);
  if (ret < 0)
    {
      return ret;
    }

  dns_lock();

  for (i = 0; i < g_dns_nservers; )
    {
      if (!dns_addr_match(&target.addr, &g_dns_servers[i].addr))
        {
          i++;
          continue;
        }

      /* Remove the entry, shifting the remaining nameservers down so that
       * the list stays contiguous.  Do not advance 'i': the shifted entry
       * still has to be examined.
       */

      g_dns_nservers--;
      if (i < g_dns_nservers)
        {
          memmove(&g_dns_servers[i], &g_dns_servers[i + 1],
                  (g_dns_nservers - i) * sizeof(union dns_addr_u));
        }

      memset(&g_dns_servers[g_dns_nservers], 0, sizeof(union dns_addr_u));
      found = true;
    }

  dns_unlock();

  if (!found)
    {
      return -ENOENT;
    }

#if CONFIG_NETDB_DNSCLIENT_ENTRIES > 0
  /* Any cached answer may have been resolved by the removed server */

  dns_clear_answer();
#endif

  return OK;
}

#endif /* CONFIG_NETDB_RESOLVCONF */
