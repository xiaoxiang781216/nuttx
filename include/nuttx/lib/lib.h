/****************************************************************************
 * include/nuttx/lib/lib.h
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

#ifndef __INCLUDE_NUTTX_LIB_LIB_H
#define __INCLUDE_NUTTX_LIB_LIB_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#if !defined(CONFIG_BUILD_FLAT) && defined(__KERNEL__)
#  include <nuttx/kmalloc.h>
#else
#  include <stdlib.h>
#endif
#ifdef CONFIG_FILE_STREAM
#  include <nuttx/fs/fs.h>
#endif
#include <stdbool.h>
#include <limits.h>
#include <alloca.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* The NuttX C library can be built in two modes: (1) as a standard,
 * C-library that can be used by normal, user-space applications, or
 * (2) as a special, kernel-mode C-library only used within the OS.
 * If NuttX is not being built as separated kernel- and user-space modules,
 * then only the first mode is supported.
 */

#if !defined(CONFIG_BUILD_FLAT) && defined(__KERNEL__)

  /* Domain-specific allocations */

#  define lib_malloc            kmm_malloc
#  define lib_calloc            kmm_calloc
#  define lib_malloc_size       kmm_malloc_size
#  define lib_zalloc            kmm_zalloc
#  define lib_realloc           kmm_realloc
#  define lib_memalign          kmm_memalign
#  define lib_free              kmm_free

  /* User-accessible allocations */

#  define lib_umalloc           kumm_malloc
#  define lib_ucalloc           kumm_calloc
#  define lib_umalloc_size      kumm_malloc_size
#  define lib_uzalloc           kumm_zalloc
#  define lib_urealloc          kumm_realloc
#  define lib_umemalign         kumm_memalign
#  define lib_ufree             kumm_free

#else

  /* Domain-specific allocations */

#  define lib_malloc            malloc
#  define lib_calloc            calloc
#  define lib_malloc_size       malloc_size
#  define lib_zalloc            zalloc
#  define lib_realloc           realloc
#  define lib_memalign          memalign
#  define lib_free              free

  /* User-accessible allocations */

#  define lib_umalloc           malloc
#  define lib_ucalloc           calloc
#  define lib_umalloc_size      malloc_size
#  define lib_uzalloc           zalloc
#  define lib_urealloc          realloc
#  define lib_umemalign         memalign
#  define lib_ufree             free

#endif

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifndef __ASSEMBLY__

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef CONFIG_FILE_STREAM
/* Functions contained in lib_getstreams.c **********************************/

FAR struct streamlist *lib_get_streams(void);
FAR struct file_struct *lib_get_stream(int fd);
#endif /* CONFIG_FILE_STREAM */

/* Functions defined in lib_srand.c *****************************************/

unsigned long nrand(unsigned long limit);

/* Functions defined in lib_tempbuffer.c ************************************/

#ifdef CONFIG_LIBC_TEMPBUFFER
FAR void *lib_get_tempbuffer(size_t nbytes);
void lib_put_tempbuffer(FAR void *buffer);
#else
#  define lib_get_tempbuffer(n) alloca(n)
#  define lib_put_tempbuffer(b)
#endif

#define lib_get_pathbuffer() lib_get_tempbuffer(PATH_MAX)
#define lib_put_pathbuffer(b) lib_put_tempbuffer(b)

/* Functions defined in lib_realpath.c **************************************/

FAR char *lib_realpath(FAR const char *path, FAR char *resolved,
                       bool notfollow);

/* Functions defined in lib_cxx_initialize.c ********************************/

#if defined(CONFIG_HAVE_CXXINITIALIZE) || defined(CONFIG_CXX_EXCEPTION)
void lib_cxx_initialize(void);
#else
#  define lib_cxx_initialize()
#endif

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __ASSEMBLY__ */
#endif /* __INCLUDE_NUTTX_LIB_LIB_H */
