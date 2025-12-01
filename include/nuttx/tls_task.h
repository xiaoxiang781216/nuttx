/****************************************************************************
 * include/nuttx/tls_task.h
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

#ifndef __INCLUDE_NUTTX_TLS_TASK_H
#define __INCLUDE_NUTTX_TLS_TASK_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <net/ethernet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pwd.h>
#include <grp.h>
#include <search.h>
#include <shadow.h>
#include <stdbool.h>
#include <stdint.h>

#include <arch/arch.h>
#include <arch/types.h>

#include <nuttx/compiler.h>
#include <nuttx/cache.h>
#include <nuttx/atexit.h>
#include <nuttx/fs/fs.h>
#include <nuttx/lib/lib.h>
#include <nuttx/sched.h>
#include <nuttx/tls.h>

#ifdef CONFIG_PTHREAD_ATFORK
#  include <nuttx/list.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* TLS Definitions **********************************************************/

#define task_info_init_buffer(buffer, size) \
  do                                        \
    {                                       \
      if (buffer == NULL)                   \
        {                                   \
          buffer = lib_zalloc(size);        \
        }                                   \
    }                                       \
  while (0)

#define task_info_uninit_buffer(buffer) \
  do                                    \
    {                                   \
      if (buffer != NULL)               \
        {                               \
          lib_free(buffer);             \
          buffer = NULL;                \
        }                               \
    }                                   \
  while (0)

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/* type tls_dtor_t **********************************************************/

/* Smallest addressable type that can hold the entire configured number of
 * TLS data indexes.
 */

typedef CODE void (*tls_dtor_t)(FAR void *);

/* This structure encapsulates all variables associated with getopt(). */

struct getopt_s
{
  /* Part of the implementation of the public getopt() interface */

  FAR char *go_optarg;       /* Optional argument following option */
  int       go_opterr;       /* Print error message */
  int       go_optind;       /* Index into argv */
  int       go_optopt;       /* unrecognized option character */

  /* Internal getopt() state */

  FAR char *go_optptr;       /* Current parsing location */
  bool      go_binitialized; /* true:  getopt() has been initialized */
};

#ifdef CONFIG_PTHREAD_ATFORK
/* This structure defines the pthread_atfork_s, which is used to manage
 * the funcs that operates on pthread_atfork() method
 */

struct pthread_atfork_s
{
  CODE void (*prepare)(void);
  CODE void (*child)(void);
  CODE void (*parent)(void);

  struct list_node node;
};
#endif

struct task_info_s
{
  struct getopt_s       ta_getopt; /* Globals used by getopt() */
  mode_t                ta_umask;  /* File mode creation mask */
  rmutex_t              ta_lock;
  pid_t                 ta_pid; /* Process ID */
  FAR char             *ta_strtokptr;
  char                  ta_asctime[26];
  struct hsearch_data   ta_htab;
  char                  ta_ttyname[TTY_NAME_MAX];
  struct tm             ta_gmtime;
  int                   ta_passwd_index;
  struct passwd         ta_passwd;
  struct spwd           ta_spwd;
  FAR char             *ta_passwd_buffer;
  struct group          ta_group;
  FAR char             *ta_group_buffer;
  FAR char             *ta_pass;
  unsigned short int    ta_seed48[7];
  unsigned long         ta_randint1;
#if (CONFIG_LIBC_RAND_ORDER > 1)
  unsigned long         ta_randint2;
#  if (CONFIG_LIBC_RAND_ORDER > 2)
  unsigned long         ta_randint3;
#  endif
#endif
#ifndef CONFIG_DISABLE_PTHREAD
  int                   ta_pthread_concurrency_level;
#endif
#ifdef CONFIG_SYSLOG
  uint8_t               ta_syslog_mask;
#endif
#if CONFIG_TLS_TASK_NELEM > 0
  uintptr_t             ta_telem[CONFIG_TLS_TASK_NELEM]; /* Task local storage elements */
#endif
#if defined(CONFIG_TLS_NELEM) && CONFIG_TLS_NELEM > 0
  tls_dtor_t            ta_tlsdtor[CONFIG_TLS_NELEM]; /* List of TLS destructors      */
#endif
#ifdef CONFIG_LIBC_LOCALE
  char                  ta_domain[NAME_MAX]; /* Current domain for gettext */
  FAR struct mofile_s  *ta_mofile;
#endif
#if CONFIG_LIBC_MAX_EXITFUNS > 0
  struct atexit_list_s  ta_exit; /* Exit functions */
#endif
#ifdef CONFIG_FILE_STREAM
  struct streamlist     ta_streamlist; /* Holds C buffered I/O info */
#endif

#ifdef CONFIG_PTHREAD_ATFORK
  struct list_node      ta_atfork; /* Holds the pthread_atfork_s list */
#endif
#ifdef CONFIG_MM_TASK_HEAP
  FAR struct mm_heap_s *ta_heap;
#endif
#if defined(CONFIG_NET_IPv4) || defined(CONFIG_LIBC_IPv4_ADDRCONV)
  char                  ta_ntoa_buf[INET_ADDRSTRLEN];
#endif
#ifdef CONFIG_LIBC_NETDB
  struct hostent        ta_hostent;
  FAR char             *ta_hostbuffer;
  int                   ta_h_errno;
  struct servent        ta_servent;
  struct protoent       ta_protoent;
#endif
#ifdef CONFIG_NET
  char                  ta_ether_ntoa_buf[20];
  struct ether_addr     ta_ether_aton_addr;
#endif
#ifdef CONFIG_CRYPTO
  FAR char             *ta_passwd_buf;
#endif
#ifndef CONFIG_DISABLE_ENVIRON
  /* Environment variables **************************************************/

  FAR char            **ta_envp;    /* Allocated environment strings        */
  ssize_t               ta_envpc;   /* Maximum entries of environment array */
  ssize_t               ta_envc;    /* Number of environment strings        */
#endif
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#if CONFIG_TLS_TASK_NELEM > 0

/****************************************************************************
 * Name: task_tls_alloc
 *
 * Description:
 *   Allocate a global-unique task local storage data index
 *
 * Input Parameters:
 *   dtor     - The destructor of task local storage data element
 *
 * Returned Value:
 *   A TLS index that is unique.
 *
 ****************************************************************************/

int task_tls_alloc(tls_dtor_t dtor);

/****************************************************************************
 * Name: task_tls_destruct
 *
 * Description:
 *   Destruct all TLS data element associated with allocated key
 *
 * Input Parameters:
 *    None
 *
 * Returned Value:
 *    None
 *
 ****************************************************************************/

void task_tls_destruct(void);

/****************************************************************************
 * Name: task_tls_set_value
 *
 * Description:
 *   Set the task local storage element associated with the 'tlsindex' to
 *   'tlsvalue'
 *
 * Input Parameters:
 *   tlsindex - Index of task local storage data element to set
 *   tlsvalue - The new value of the task local storage data element
 *
 * Returned Value:
 *   Zero is returned on success, a negated errno value is return on
 *   failure:
 *
 *     EINVAL - tlsindex is not in range.
 *
 ****************************************************************************/

int task_tls_set_value(int tlsindex, uintptr_t tlsvalue);

/****************************************************************************
 * Name: task_tls_get_value
 *
 * Description:
 *   Return an the task local storage data value associated with 'tlsindx'
 *
 * Input Parameters:
 *   tlsindex - Index of task local storage data element to return
 *
 * Returned Value:
 *   The value of TLS element associated with 'tlsindex'. Errors are not
 *   reported.  Zero is returned in the event of an error, but zero may also
 *   be valid value and returned when there is no error.  The only possible
 *   error would be if tlsindex < 0 or tlsindex >=CONFIG_TLS_TASK_NELEM.
 *
 ****************************************************************************/

uintptr_t task_tls_get_value(int tlsindex);
#endif

/****************************************************************************
 * Name: task_get_info
 *
 * Description:
 *   Return a reference to the task_info_s structure.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   A reference to the task-specific task_info_s structure is return on
 *   success.  NULL would be returned in the event of any failure.
 *
 ****************************************************************************/

static inline_function FAR struct task_info_s *task_get_info(void)
{
  FAR struct tls_info_s *info = tls_get_info();

  return info ? info->tl_task : NULL;
}

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_TLS_TASK_H */