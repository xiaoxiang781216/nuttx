/****************************************************************************
 * libs/libc/misc/lib_lrucache.c
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

#include <assert.h>
#include <errno.h>

#include <nuttx/lru_cache.h>
#include <nuttx/lib/lib.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct lru_cache_entry_s
{
  struct list_node lru_node;  /* LRU list node */
  hash_node_t      hash_node; /* Hash table node */
  FAR void        *value;     /* Pointer to the value */
  char             key[];     /* Key */
};

/****************************************************************************
 * Private Functions Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lru_cache_hash
 *
 * Description:
 *   Compute the hash value for a given key.
 *
 * Input Parameters:
 *   key - The key
 *
 * Returned Value:
 *   The computed hash value.
 *
 ****************************************************************************/

static inline_function uint32_t lru_cache_hash(FAR const char *key)
{
  uint32_t hash = 5381;
  size_t i;

  for (i = 0; key[i]; i++)
    {
      /* hash * 33 + c */

      hash = ((hash << 5) + hash) + key[i];
    }

  return hash;
}

/****************************************************************************
 * Name: lru_cache_evict
 *
 * Description:
 *   Evict the least recently used cache entry.
 *
 * Input Parameters:
 *   cache - Pointer to cache structure
 *
 * Returned Value:
 *   Pointer to the evicted cache entry; NULL if the cache is empty.
 *
 ****************************************************************************/

static FAR struct lru_cache_entry_s *
lru_cache_evict(FAR struct lru_cache_s *cache)
{
  FAR struct lru_cache_entry_s *entry = NULL;
  FAR struct list_node *lru_node;
  uint32_t hash;

  DEBUGASSERT(cache != NULL);

  /* Get the least recently used entry (tail of the LRU list) */

  lru_node = list_remove_tail(&cache->lru_list);
  if (lru_node != NULL)
    {
      /* Remove from hash table */

      entry = container_of(lru_node, struct lru_cache_entry_s, lru_node);
      hash = lru_cache_hash(entry->key);
      hashtable_delete_n(cache->hash_table, &entry->hash_node, hash,
                         cache->hash_size);
      cache->num_entries--;
    }

  return entry;
}

/****************************************************************************
 * Name: lru_cache_lookup_entry
 *
 * Description:
 *   Lookup a cache entry by key. Note that we don't update its position in
 *   this function.
 *
 * Input Parameters:
 *   cache  - Pointer to cache structure
 *   key    - The key
 *
 * Returned Value:
 *   Pointer to the cache entry if found; NULL if not found.
 *
 ****************************************************************************/

static FAR struct lru_cache_entry_s *
lru_cache_lookup_entry(FAR const struct lru_cache_s *cache,
                       FAR const char *key)
{
  FAR struct lru_cache_entry_s *entry;
  uint32_t hash;

  DEBUGASSERT(cache != NULL && key != NULL);

  hash = lru_cache_hash(key);

  /* Search the hash table for the entry */

  hashtable_for_every_entry_possible_n(cache->hash_table, entry, hash,
                                       cache->hash_size,
                                       struct lru_cache_entry_s, hash_node)
    {
      if (strcmp(key, entry->key) == 0)
        {
          return entry;
        }
    }

  return NULL;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: lru_cache_init
 *
 * Description:
 *   Initialize the cache structure.
 *
 * Input Parameters:
 *   cache       - Pointer to cache structure
 *   max_entries - Maximum number of cache entries
 *   hash_size   - Size of the hash table
 *   ops         - Pointer to cache reference counting operations
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on any failure.
 *
 ****************************************************************************/

int lru_cache_init(FAR struct lru_cache_s *cache, size_t max_entries,
                   size_t hash_size, FAR const struct lru_cache_ops_s *ops)
{
  int ret;

  DEBUGASSERT(cache != NULL && ops != NULL && ops->get_cb != NULL
              && ops->put_cb != NULL);

  /* Limit hash_size to make sure correct HASH calculations */

  if (hash_size == 0 || hash_size > (1UL << 31))
    {
      return -EINVAL;
    }

  cache->hash_table = lib_malloc(hash_size * sizeof(hash_head_t));
  if (cache->hash_table == NULL)
    {
      return -ENOMEM;
    }

  /* Initialize mutex lock */

  ret = nxmutex_init(&cache->lock);
  if (ret < 0)
    {
      goto errout_with_hashtable;
    }

  cache->num_entries = 0;
  cache->max_entries = max_entries;
  cache->hash_size = hash_size;
  cache->ops = ops;
  hashtable_init_n(cache->hash_table, hash_size);
  list_initialize(&cache->lru_list);
  list_initialize(&cache->free_list);
  return OK;

errout_with_hashtable:
  lib_free(cache->hash_table);
  return ret;
}

/****************************************************************************
 * Name: lru_cache_lookup
 *
 * Description:
 *   Lookup a cache entry by key. This function also updates its position
 *   in the LRU list and increments its reference count.
 *
 * Input Parameters:
 *   cache  - Pointer to cache structure
 *   key    - The key
 *
 * Returned Value:
 *   Pointer to the cache entry value if found; NULL if not found.
 *
 ****************************************************************************/

FAR void *lru_cache_lookup(FAR struct lru_cache_s *cache,
                           FAR const char *key)
{
  FAR struct lru_cache_entry_s *entry;
  FAR void *value = NULL;

  DEBUGASSERT(cache != NULL && key != NULL);

  nxmutex_lock(&cache->lock);
  entry = lru_cache_lookup_entry(cache, key);
  if (entry != NULL)
    {
      value = entry->value;
      list_delete(&entry->lru_node);
      list_add_head(&cache->lru_list, &entry->lru_node);
      cache->ops->get_cb(entry->value);
    }

  nxmutex_unlock(&cache->lock);
  return value;
}

/****************************************************************************
 * Name: lru_cache_insert
 *
 * Description:
 *   Insert a new cache entry.
 *
 * Input Parameters:
 *   cache  -  Pointer to cache structure
 *   key    -  Pointer to the key
 *   value  -  Pointer to the value
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on any failure.
 *
 ****************************************************************************/

int lru_cache_insert(FAR struct lru_cache_s *cache, FAR const char *key,
                     FAR void *value)
{
  FAR struct lru_cache_entry_s *tmp = NULL;
  FAR struct lru_cache_entry_s *entry;
  FAR void *evicted = NULL;
  uint32_t hash;
  int ret = OK;
  int len;

  DEBUGASSERT(cache != NULL && key != NULL && value != NULL);

  len = strlen(key) + 1;
  nxmutex_lock(&cache->lock);
  entry = lru_cache_lookup_entry(cache, key);

  /* If an entry with this key already exists */

  if (entry != NULL)
    {
      /* Entry already exists, insertion fails */

      ret = -EEXIST;
      goto errout_with_lock;
    }

  /* Try to reuse an entry from free_list first */

  if (!list_is_empty(&cache->free_list))
    {
      tmp = container_of(list_remove_head(&cache->free_list),
                         struct lru_cache_entry_s, lru_node);
    }
  else if (cache->num_entries >= cache->max_entries)
    {
      /* Cache is full and no free entries, evict the LRU entry */

      tmp = lru_cache_evict(cache);
      if (tmp != NULL)
        {
          evicted = tmp->value;
        }
    }

  /* Allocate or reallocate entry */

  entry = lib_realloc(tmp, sizeof(*entry) + len);
  if (entry == NULL)
    {
      /* Free the tmp entry if allocation fails */

      lib_free(tmp);
      ret = -ENOMEM;
      goto errout_with_lock;
    }

  memcpy(entry->key, key, len);
  entry->value = value;

  /* Insert into hash table and LRU list */

  cache->ops->get_cb(value);
  hash = lru_cache_hash(entry->key);
  hashtable_add_n(cache->hash_table, &entry->hash_node, hash,
                  cache->hash_size);
  list_add_head(&cache->lru_list, &entry->lru_node);

  /* Increment num_entries if we allocated new entry or evicted one
   * (evict decremented num_entries, so we need to increment it back).
   * We don't change num_entries when reusing an entry from free_list.
   */

  if (tmp == NULL || evicted != NULL)
    {
      cache->num_entries++;
    }

errout_with_lock:
  nxmutex_unlock(&cache->lock);

  /* Decrease reference count of evicted entry if any */

  if (evicted != NULL)
    {
      cache->ops->put_cb(evicted);
    }

  return ret;
}

/****************************************************************************
 * Name: lru_cache_remove
 *
 * Description:
 *   Remove a cache entry.
 *
 * Input Parameters:
 *   cache  - Pointer to cache structure
 *   key    - The key
 *
 * Returned Value:
 *   true if the entry was found and removed; false if not found.
 *
 ****************************************************************************/

int lru_cache_remove(FAR struct lru_cache_s *cache, FAR const char *key)
{
  FAR struct lru_cache_entry_s *entry;
  FAR void *value;

  DEBUGASSERT(cache != NULL && key != NULL);

  nxmutex_lock(&cache->lock);
  entry = lru_cache_lookup_entry(cache, key);
  if (entry == NULL)
    {
      nxmutex_unlock(&cache->lock);
      return -ENOENT;
    }

  value = entry->value;
  list_delete(&entry->lru_node);
  hashtable_delete_n(cache->hash_table, &entry->hash_node,
                     lru_cache_hash(entry->key), cache->hash_size);

  /* Add to free_list, num_entries remains unchanged */

  list_add_tail(&cache->free_list, &entry->lru_node);
  nxmutex_unlock(&cache->lock);
  cache->ops->put_cb(value);
  return OK;
}

/****************************************************************************
 * Name: lru_cache_uninit
 *
 * Description:
 *   Uninitialize the cache structure and free all entries.
 *
 * Input Parameters:
 *   cache - Pointer to cache structure
 *
 ****************************************************************************/

void lru_cache_uninit(FAR struct lru_cache_s *cache)
{
  FAR struct lru_cache_entry_s *entry;

  DEBUGASSERT(cache != NULL);

  /* We just free all entries in lru_list with no lock, since caller should
   * not use the lru_cache when uninit.
   */

  while (!list_is_empty(&cache->lru_list))
    {
      entry = container_of(list_remove_head(&cache->lru_list),
                           struct lru_cache_entry_s, lru_node);
      cache->ops->put_cb(entry->value);
      lib_free(entry);
    }

  /* Free all entries in free_list */

  while (!list_is_empty(&cache->free_list))
    {
      entry = container_of(list_remove_head(&cache->free_list),
                           struct lru_cache_entry_s, lru_node);
      lib_free(entry);
    }

  /* Free the hash table */

  lib_free(cache->hash_table);

  /* Reset the cache structure */

  cache->hash_table = NULL;
  cache->max_entries = 0;
  cache->hash_size = 0;
  cache->num_entries = 0;
  nxmutex_destroy(&cache->lock);
}

/****************************************************************************
 * Name: lru_cache_foreach
 *
 * Description:
 *   Iterate through all cache entries in LRU order and call the callback
 *   for each entry.
 *
 * Input Parameters:
 *   cache - Pointer to cache structure
 *   cb    - Callback to be called for each entry
 *   arg   - User argument passed to callback
 *
 * Returned Value:
 *   > 0  : continue iteration
 *   == 0 : stop iteration normally
 *   < 0  : error, stop iteration
 *
 ****************************************************************************/

int lru_cache_foreach(FAR struct lru_cache_s *cache, lru_cache_cb_t cb,
                      FAR void *arg)
{
  FAR struct lru_cache_entry_s *entry;
  int ret = OK;

  DEBUGASSERT(cache != NULL && cb != NULL);

  nxmutex_lock(&cache->lock);

  list_for_every_entry(&cache->lru_list, entry,
                       struct lru_cache_entry_s, lru_node)
    {
      ret = cb(cache, entry->key, entry->value, arg);
      if (ret <= 0)
        {
          break;
        }
    }

  nxmutex_unlock(&cache->lock);
  return ret;
}

/****************************************************************************
 * Name: lru_cache_remove_if
 *
 * Description:
 *   Iterate through all cache entries, remove entries that match the
 *   condition, and call the put callback for each removed entry.
 *   The matching is done under lock, but the put callback is called
 *   after releasing the lock to avoid holding lock during expensive
 *   operations.
 *
 * Input Parameters:
 *   cache    - Pointer to cache structure
 *   match_cb - Callback to check if entry should be removed
 *   arg      - User argument passed to callbacks
 *
 * Returned Value:
 *   OK on success; a negated errno value on any failure.
 *
 ****************************************************************************/

int lru_cache_remove_if(FAR struct lru_cache_s *cache,
                        lru_cache_cb_t match_cb, FAR void *arg)
{
  FAR struct lru_cache_entry_s *entry;
  FAR struct lru_cache_entry_s *tmp;
  struct list_node removed_list;
  int ret = OK;

  DEBUGASSERT(cache != NULL && match_cb != NULL);

  /* Initialize temporary list to collect removed entries.
   * We should use a temporary list here instead of using free_list,
   * otherwise we have to hold a lock while iterating free_list to release
   * entries, which may be time-consuming.
   */

  list_initialize(&removed_list);
  nxmutex_lock(&cache->lock);

  /* Iterate through all entries and collect matching ones */

  list_for_every_entry_safe(&cache->lru_list, entry, tmp,
                            struct lru_cache_entry_s, lru_node)
    {
      /* Check if this entry matches the removal condition */

      ret = match_cb(cache, entry->key, entry->value, arg);
      if (ret > 0)
        {
          /* Remove from lru_list and hash table, add to temp list */

          cache->num_entries--;
          list_delete(&entry->lru_node);
          hashtable_delete_n(cache->hash_table, &entry->hash_node,
                             lru_cache_hash(entry->key), cache->hash_size);
          list_add_tail(&removed_list, &entry->lru_node);
        }
      else if (ret < 0)
        {
          /* Error occurred, stop iteration */

          break;
        }
    }

  nxmutex_unlock(&cache->lock);

  /* Now call free_cb for each removed entry outside the lock */

  while (!list_is_empty(&removed_list))
    {
      entry = container_of(list_remove_head(&removed_list),
                           struct lru_cache_entry_s, lru_node);
      cache->ops->put_cb(entry->value);
      lib_free(entry);
    }

  return ret;
}
