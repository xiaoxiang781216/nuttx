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

#include <nuttx/config.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FUSE_ARGS_INIT(argc, argv) { argc, argv, 0 }
#define FUSE_OPT_END               { NULL, 0, 0 }

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* A list of arguments, mirroring the libfuse struct fuse_args */

struct fuse_args
{
  int          argc;        /* Argument count */
  FAR char   **argv;        /* Argument vector, NULL terminated */
  int          allocated;   /* Is argv dynamically allocated? */
};

/* Option descriptor (kept for source compatibility) */

struct fuse_opt
{
  FAR const char *templ;    /* Option template */
  unsigned long   offset;   /* Offset in the data structure */
  int             value;    /* Value to set */
};

#if defined(__cplusplus)
extern "C"
{
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: fuse_opt_add_arg
 *
 * Description:
 *   Append an argument to the argument list, taking ownership of a copy.
 *
 ****************************************************************************/

int fuse_opt_add_arg(FAR struct fuse_args *args, FAR const char *arg);

/****************************************************************************
 * Name: fuse_opt_free_args
 *
 * Description:
 *   Free all arguments owned by the argument list.
 *
 ****************************************************************************/

void fuse_opt_free_args(FAR struct fuse_args *args);

#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_FUSE_OPT_H */
