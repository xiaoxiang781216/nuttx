/****************************************************************************
 * include/nuttx/lru_cache.h
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

#ifndef __INCLUDE_NUTTX_LRU_CACHE_H
#define __INCLUDE_NUTTX_LRU_CACHE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/hashtable.h>
#include <nuttx/list.h>
#include <nuttx/mutex.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct lru_cache_s;

typedef CODE void (*lru_cache_ops_cb_t)(FAR void *value);

/* Callback for LRU cache operations.
 * Return value:
 *   > 0  : continue iteration
 *   == 0 : stop iteration normally
 *   < 0  : error, stop iteration
 */

typedef int (*lru_cache_cb_t)(FAR struct lru_cache_s *cache,
                              FAR const char *key, FAR void *value,
                              FAR void *args);

/* Operations for reference counting */

struct lru_cache_ops_s
{
  lru_cache_ops_cb_t get_cb;            /* Callback to increase reference
                                         * count when entry is retrieved
                                         * from cache
                                         */
  lru_cache_ops_cb_t put_cb;            /* Callback to decrease reference
                                         * count when entry is removed
                                         * from cache, and release it
                                         * if reference count reaches 0
                                         */
};

struct lru_cache_s
{
  struct list_node lru_list;             /* LRU list */
  struct list_node free_list;            /* Free list for removed entries */
  size_t num_entries;                    /* Current number of entries in
                                          * the cache, including removed ones
                                          */
  size_t max_entries;                    /* Maximum number of entries in
                                          * the cache
                                          */
  mutex_t lock;                          /* Mutex lock for LRU list */
  size_t hash_size;                      /* Size of the hash table */
  FAR hash_head_t *hash_table;           /* Pointer to dynamically allocated
                                          * hash table
                                          */
  FAR const struct lru_cache_ops_s *ops; /* Pointer to cache operations */
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int lru_cache_init(FAR struct lru_cache_s *cache, size_t max_entries,
                   size_t hash_size, FAR const struct lru_cache_ops_s *ops);
void lru_cache_uninit(FAR struct lru_cache_s *cache);
FAR void *lru_cache_lookup(FAR struct lru_cache_s *cache,
                           FAR const char *key);
int lru_cache_insert(FAR struct lru_cache_s *cache, FAR const char *key,
                     FAR void *value);
int lru_cache_remove(FAR struct lru_cache_s *cache, FAR const char *key);
int lru_cache_remove_if(FAR struct lru_cache_s *cache,
                        lru_cache_cb_t match_cb, FAR void *arg);
int lru_cache_foreach(FAR struct lru_cache_s *cache, lru_cache_cb_t cb,
                      FAR void *arg);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_LRU_CACHE_H */
