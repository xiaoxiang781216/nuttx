/****************************************************************************
 * include/fuse_opt.h
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

#ifndef __INCLUDE_FUSE_OPT_H
#define __INCLUDE_FUSE_OPT_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/compiler.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Key option: on a match the processing function is called with the
 * specified key instead of setting a variable (offset must be -1).
 */

#define FUSE_OPT_KEY(templ, key)   { templ, -1U, key }

/* Last option: an array of struct fuse_opt must end with this marker */

#define FUSE_OPT_END               { NULL, 0, 0 }

/* Key values passed to the processing function by fuse_opt_parse() */

#define FUSE_OPT_KEY_OPT           -1 /* Option matched no template */
#define FUSE_OPT_KEY_NONOPT        -2 /* Argument is not an option */
#define FUSE_OPT_KEY_KEEP          -3 /* Keep argument, do not call proc */
#define FUSE_OPT_KEY_DISCARD       -4 /* Discard argument, do not call proc */

/* Initializer for 'struct fuse_args' */

#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

struct fuse_opt
{
  FAR const char *templ;  /* Option template */
  unsigned long   offset; /* Offset in the data structure */
  int             value;  /* Value to set */
};

struct fuse_args
{
  int          argc;      /* Argument count */
  FAR char   **argv;      /* Argument vector, NULL terminated */
  int          allocated; /* Is argv dynamically allocated? */
};

/* Processing function for fuse_opt_parse().
 *
 * Return -1 on error, 0 to discard the argument, 1 to keep it in the
 * output argument list.
 */

typedef CODE int (*fuse_opt_proc_t)(FAR void *data, FAR const char *arg,
                                    int key, FAR struct fuse_args *outargs);

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

int fuse_opt_parse(FAR struct fuse_args *args, FAR void *data,
                   FAR const struct fuse_opt opts[], fuse_opt_proc_t proc);

int fuse_opt_add_opt(FAR char **opts, FAR const char *opt);
int fuse_opt_add_opt_escaped(FAR char **opts, FAR const char *opt);

int fuse_opt_add_arg(FAR struct fuse_args *args, FAR const char *arg);
int fuse_opt_insert_arg(FAR struct fuse_args *args, int pos,
                        FAR const char *arg);
void fuse_opt_free_args(FAR struct fuse_args *args);

int fuse_opt_match(FAR const struct fuse_opt opts[], FAR const char *opt);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_FUSE_OPT_H */
