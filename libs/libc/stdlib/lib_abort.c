/****************************************************************************
 * libs/libc/stdlib/lib_abort.c
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

#include <stdlib.h>
#include <pthread.h>
#include <execinfo.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: Abort
 *
 * Description:
 *   The abort() first unblocks the SIGABRT signal, and then raises that
 *   signal for the calling process. This results in the abnormal
 *   termination of the process unless the SIGABRT signal is caught and
 *   the signal handler does not return.
 *
 *   If the abort() function causes process termination, all open
 *   streams are closed and flushed.
 *
 *   If the SIGABRT signal is ignored, or caught by a handler that
 *   returns, the abort() function will still terminate the process.
 *   It does this by restoring the default disposition for SIGABRT and
 *   then raising the signal for a second time.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *  This function does not return,
 *
 ****************************************************************************/

void abort(void)
{
#ifndef CONFIG_DISABLE_SIGNALS
  struct sigaction sa;
  sigset_t set;

  sigemptyset(&set);
  sigaddset(&set, SIGABRT);
  sigprocmask(SIG_UNBLOCK, &set, NULL);

  raise(SIGABRT);

  sa.sa_handler = SIG_DFL;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGABRT, &sa, NULL);

  raise(SIGABRT);
#endif

#ifdef CONFIG_DEBUG_FEATURES
  dump_stack();
#endif

  _exit(EXIT_FAILURE);
}
