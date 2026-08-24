/****************************************************************************
 * fs/lwext4/generated/ext4_config.h
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

/* This file is pulled in by the lwext4 library's own configuration hook:
 *
 *     ext4_config.h:  #if !CONFIG_USE_DEFAULT_CFG
 *                     #include "generated/ext4_config.h"
 *                     #endif
 *
 * The build passes CONFIG_USE_DEFAULT_CFG=0 so the library includes this
 * file (resolved from the fs/lwext4 -I path) before applying its own
 * #ifndef-guarded defaults.  It maps the NuttX Kconfig options onto the
 * lwext4 CONFIG_* macros in one place, replacing the previous
 * force-include of lwext4conf.h.
 */

#ifndef __FS_LWEXT4_GENERATED_EXT4_CONFIG_H
#define __FS_LWEXT4_GENERATED_EXT4_CONFIG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/compiler.h>
#include <stdint.h>
#include <stddef.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Standard Library Configuration
 ****************************************************************************/

/* Use NuttX <fcntl.h> for O_* flags (not lwext4's own Linux-style values) */

#define CONFIG_HAVE_OWN_OFLAGS              0

/* Standard includes - lwext4 needs these */

#define CONFIG_HAVE_OWN_ERRNO               0
#define CONFIG_HAVE_OWN_ASSERT              0

/* Use the NuttX allocator instead of lwext4's own malloc.  The library
 * redirects ext4_malloc/calloc/realloc/free to the ext4_user_* hooks below
 * (see ext4_types.h); they are implemented in lwext4_vfs.c on top of the
 * NuttX file-system heap.
 */

#define CONFIG_USE_USER_MALLOC              1

/****************************************************************************
 * Debug Configuration
 ****************************************************************************/

/* Map the lwext4 debug verbosity (0-3) to the NuttX file-system debug
 * levels.  DEBUG_FS_INFO implies the most verbose output, DEBUG_FS_ERROR
 * the least; when none is enabled debug printing is compiled out.
 */

#if defined(CONFIG_DEBUG_FS_INFO) || defined(CONFIG_DEBUG_FS_WARN) || \
    defined(CONFIG_DEBUG_FS_ERROR)
#  define CONFIG_DEBUG_PRINTF               1
#else
#  define CONFIG_DEBUG_PRINTF               0
#endif

/****************************************************************************
 * Filesystem Feature Configuration
 ****************************************************************************/

/* CONFIG_EXTENT_ENABLE and CONFIG_META_CSUM_ENABLE are intentionally NOT
 * defined here.  ext4_config.h defines them unconditionally (not #ifndef
 * guarded), derived from the selected feature set, so redefining them would
 * trigger -Wmacro-redefined.  Leave them to the library.
 */

/* Block cache configuration.  The lwext4 library takes a block *count* here
 * (ext4_bcache_init_dynamic(bc, cnt, itemsize)), so map it to
 * CONFIG_LWEXT4_BCACHE_BLOCKS.  This cache is only valid for metadata; file
 * data bypasses the cache and is read/written directly to the user buffer.
 */

#define CONFIG_BLOCK_DEV_CACHE_SIZE         CONFIG_LWEXT4_BCACHE_BLOCKS

/* Journal configuration */

#ifdef CONFIG_LWEXT4_ENABLE_JOURNAL
#  define CONFIG_JOURNALING_ENABLE          1
#else
#  define CONFIG_JOURNALING_ENABLE          0
#endif

/****************************************************************************
 * Performance Optimization
 ****************************************************************************/

/* Default to unaligned access support */

#define CONFIG_UNALIGNED_ACCESS             1

/****************************************************************************
 * Limits
 ****************************************************************************/

/* Mount point and block device table sizing.
 * CONFIG_EXT4_MOUNTPOINTS_COUNT / CONFIG_EXT4_BLOCKDEVS_COUNT are
 * #ifndef-guarded in the library (upstream default 2); defining them
 * here lets the NuttX Kconfig value take effect without patching the
 * vendored library source in-tree.
 */

#define CONFIG_EXT4_MOUNTPOINTS_COUNT       CONFIG_LWEXT4_MAX_MOUNTPOINTS
#define CONFIG_EXT4_BLOCKDEVS_COUNT         CONFIG_LWEXT4_MAX_MOUNTPOINTS

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/* Allocator hooks the library redirects ext4_malloc/calloc/realloc/free to
 * when CONFIG_USE_USER_MALLOC is set; implemented in lwext4_vfs.c on top of
 * the NuttX file-system heap.
 */

FAR void *ext4_user_malloc(size_t size);
FAR void *ext4_user_calloc(size_t nmemb, size_t size);
FAR void *ext4_user_realloc(FAR void *ptr, size_t size);
void      ext4_user_free(FAR void *ptr);

#endif /* __FS_LWEXT4_GENERATED_EXT4_CONFIG_H */
