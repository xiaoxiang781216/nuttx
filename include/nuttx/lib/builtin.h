/****************************************************************************
 * include/nuttx/lib/builtin.h
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

#ifndef __INCLUDE_NUTTX_LIB_BUILTIN_H
#define __INCLUDE_NUTTX_LIB_BUILTIN_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdbool.h>
#include <sys/types.h>

#ifdef CONFIG_BUILTIN

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct builtin_s
{
  FAR const char *name;     /* Invocation name and as seen under /sbin/ */
  int         priority;     /* Use: SCHED_PRIORITY_DEFAULT */
  int         stacksize;    /* Desired stack size */
  main_t      main;         /* Entry point: main(int argc, char *argv[]) */
#ifdef CONFIG_SCHED_USER_IDENTITY
  uid_t       uid;          /* File owner user identity */
  gid_t       gid;          /* File owner group identity */
  int         mode;         /* File mode added to */
#endif
#ifdef CONFIG_MM_TASK_HEAP
  size_t      heapsize;     /* Task heap size */
#endif
#ifdef CONFIG_LIB_SYSCALL
  bool        kernel;       /* true: kernel app, false: user app */
#endif
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

EXTERN const struct builtin_s g_builtins[];
EXTERN const int g_builtin_count;

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: builtin_isavail
 *
 * Description:
 *   Checks for availability of an application named 'appname' registered
 *   during compile time and, if available, returns the index into the table
 *   of built-in applications.
 *
 * Input Parameters:
 *   filename - Name of the linked-in binary to be started.
 *
 * Returned Value:
 *   This is an internal function, used by by the NuttX binfmt logic and
 *   by the application built-in logic.  It returns a non-negative index to
 *   the application entry in the table of built-in applications on success
 *   or a negated errno value in the event of a failure.
 *
 ****************************************************************************/

int builtin_isavail(FAR const char *appname);

/****************************************************************************
 * Name: builtin_for_index
 *
 * Description:
 *   Returns the builtin_s structure for the selected built-in.
 *   If support for built-in functions is enabled in the NuttX
 *   configuration, then this function must be provided by the application
 *   code.
 *
 * Input Parameters:
 *   index, from 0 and on...
 *
 * Returned Value:
 *   Returns valid pointer pointing to the builtin_s structure if index is
 *   valid.
 *   Otherwise, NULL is returned.
 *
 ****************************************************************************/

FAR const struct builtin_s *builtin_for_index(int index);

/****************************************************************************
 * Name: builtin_getname
 *
 * Description:
 *   Returns pointer to a name of built-in application pointed by the
 *   index.
 *
 * Input Parameters:
 *   index, from 0 and on ...
 *
 * Returned Value:
 *   Returns valid pointer pointing to the app name if index is valid.
 *   Otherwise NULL is returned.
 *
 ****************************************************************************/

static inline_function
FAR const char *builtin_getname(int index)
{
  FAR const struct builtin_s *builtin;

  builtin = builtin_for_index(index);

  if (builtin != NULL)
    {
      return builtin->name;
    }

  return NULL;
}

#ifdef CONFIG_SCHED_USER_IDENTITY

/****************************************************************************
 * Name: builtin_getuid
 *
 * Description:
 *   Returns file uid of the application at 'index' in the table
 *   of built-in applications.
 *
 * Input Parameters:
 *   index - From 0 and on ...
 *
 * Returned Value:
 *   Returns valid uid for app if index is valid.
 *   Otherwise 0 is returned.
 *
 ****************************************************************************/

uid_t builtin_getuid(int index);

/****************************************************************************
 * Name: builtin_getgid
 *
 * Description:
 *   Returns file gid of the application at 'index' in the table
 *   of built-in applications.
 *
 * Input Parameters:
 *   index - From 0 and on ...
 *
 * Returned Value:
 *   Returns valid gid for app if index is valid.
 *   Otherwise 0 is returned.
 *
 ****************************************************************************/

gid_t builtin_getgid(int index);

/****************************************************************************
 * Name: builtin_getmode
 *
 * Description:
 *   Returns file mode of the application at 'index' in the table
 *   of built-in applications.
 *
 * Input Parameters:
 *   index - From 0 and on ...
 *
 * Returned Value:
 *   Returns valid mode for app if index is valid.
 *   Otherwise 0 is returned.
 *
 ****************************************************************************/

int builtin_getmode(int index);

#endif

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* CONFIG_BUILTIN */
#endif /* __INCLUDE_NUTTX_LIB_BUILTIN_H */
