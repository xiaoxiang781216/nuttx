/****************************************************************************
 * libs/libbuiltin/compiler-rt/profile_value.c
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

#include <nuttx/compiler.h>

#include <stdint.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "profile.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef struct llvm_value_prof_node
{
  atomic_ulong value;
  atomic_ulong count;
  struct llvm_value_prof_node *next;
} llvm_value_prof_node;

/* Runtime data structure for value profiling.
 *
 * This design follows LLVM's ValueProfRecord pattern, implementing
 * the llvm_value_prof_record_closure interface to enable shared
 * serialization code with LLVM's profiling infrastructure.
 */

typedef struct llvm_value_prof_runtime_record
{
  FAR const __llvm_profile_data *data;
  FAR llvm_value_prof_node **node_kind[IPVK_LAST + 1];
  FAR uint8_t **site_count_array;
} llvm_value_prof_runtime_record;

/* Closure abstraction for value profiling data serialization.
 *
 * This design follows LLVM's ValueProfRecordClosure pattern,
 * providing a unified interface for both runtime data collection
 * and offline analysis tools.
 * The abstraction enables different data sources to share serialization
 * code while maintaining compatibility with LLVM's profiling infrastructure.
 */

typedef struct llvm_value_prof_record_closure
{
  FAR const void *record;
  uint32_t (FAR *get_num_value_kinds)(FAR const void *record);
  uint32_t (FAR *get_num_value_sites)(FAR const void *record,
                                      uint32_t vkind);
  uint32_t (FAR *get_num_value_data)(FAR const void *record, uint32_t vkind);
  uint32_t (FAR *get_num_value_data_for_site)(FAR const void *record,
                                          uint32_t vkind,
                                          uint32_t site);

  /* After extracting the value profile data from the value profile record,
   * this method is used to map the in-memory value to on-disk value. If
   * the method is null, value will be written out untranslated.
   */

  uint64_t (FAR *remap_value_data)(uint32_t, uint64_t value);
  void (FAR *get_value_for_site)(FAR const void *r,
                                 FAR llvm_prof_value_data *dst,
                                 uint32_t k, uint32_t s);
  FAR llvm_value_prof_data *
    (FAR *alloc_value_prof_data)(size_t total_size_in_bytes);
} llvm_value_prof_record_closure;

/****************************************************************************
 * Private Declarations
 ****************************************************************************/

static uint32_t llvm_get_num_value_sites_rt(FAR const void *r, uint32_t vk);
static uint32_t llvm_get_num_value_data_rt(FAR const void *r, uint32_t vk);
static uint32_t llvm_get_num_value_data_for_site_rt(FAR const void *r,
                                                    uint32_t vk,
                                                    uint32_t s);

static uint32_t
llvm_init_value_prof_runtime_record(FAR const __llvm_profile_data *data,
                                    FAR uint8_t *site_count_array[]);

uint32_t llvm_get_value_prof_record_header_size(uint32_t num_value_sites);

FAR llvm_value_prof_record *
llvm_get_first_value_prof_record(FAR llvm_value_prof_data *ptr);

static uint32_t llvm_get_num_value_data_for_site_wrapper(uint32_t vk,
                                                         uint32_t s);

static uint32_t llvm_get_value_prof_data_size_wrapper(void);

static FAR llvm_value_prof_node *
llvm_get_next_n_value_data(uint32_t vk, uint32_t site,
                           FAR llvm_prof_value_data *dst,
                           FAR llvm_value_prof_node *start_node,
                           uint32_t n);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static llvm_value_prof_runtime_record llvm_rt_record;

static llvm_value_prof_record_closure llvm_rt_record_closure =
  {
    &llvm_rt_record,
    NULL,
    llvm_get_num_value_sites_rt,
    llvm_get_num_value_data_rt,
    llvm_get_num_value_data_for_site_rt,
    NULL,
    NULL,
    NULL
  };

static llvm_profile_data_reader llvm_profile_vp_data_reader =
  {
    llvm_init_value_prof_runtime_record,
    llvm_get_value_prof_record_header_size,
    llvm_get_first_value_prof_record,
    llvm_get_num_value_data_for_site_wrapper,
    llvm_get_value_prof_data_size_wrapper,
    llvm_get_next_n_value_data
  };

static int llvm_profile_has_static_counters = 1;
static int llvm_profile_has_non_default_vals_per_site = 0;
static uint32_t llvm_profile_vp_max_num_vals_per_site = 24;

static FAR llvm_value_prof_node *llvm_profile_current_vnode =
  (FAR llvm_value_prof_node *)&__start__llvm_prf_vnds;
static const FAR llvm_value_prof_node *llvm_profile_end_vnode =
  (FAR llvm_value_prof_node *)&__end__llvm_prf_vnds;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static uint32_t llvm_get_num_value_sites_rt(FAR const void *r, uint32_t vk)
{
  const FAR llvm_value_prof_runtime_record *record =
    (FAR llvm_value_prof_runtime_record *)r;
  return record->data->num_value_sites[vk];
}

static uint32_t llvm_get_num_value_data_rt(FAR const void *r, uint32_t vk)
{
  uint32_t s = 0;
  uint32_t i;
  FAR const llvm_value_prof_runtime_record *record =
      (FAR const llvm_value_prof_runtime_record *)r;

  if (record->site_count_array[vk] == NULL)
    {
      return 0;
    }

  for (i = 0; i < record->data->num_value_sites[vk]; i++)
    {
      s += record->site_count_array[vk][i];
    }

  return s;
}

static uint32_t llvm_get_num_value_data_for_site_rt(FAR const void *r,
                                                    uint32_t vk,
                                                    uint32_t s)
{
  FAR const llvm_value_prof_runtime_record *record =
      (FAR const llvm_value_prof_runtime_record *)r;
  return record->site_count_array[vk][s];
}

static uint32_t
llvm_init_value_prof_runtime_record(FAR const __llvm_profile_data *data,
                                     FAR uint8_t *site_count_array[])
{
  unsigned i;
  unsigned j;
  unsigned s = 0;
  unsigned num_value_kinds = 0;
  FAR llvm_value_prof_node **nodes =
    (FAR llvm_value_prof_node **)data->values;

  llvm_rt_record.data = data;
  llvm_rt_record.site_count_array = site_count_array;

  for (i = 0; i <= IPVK_LAST; i++)
    {
      uint16_t n = data->num_value_sites[i];
      if (!n)
        {
          continue;
        }

      num_value_kinds++;

      llvm_rt_record.node_kind[i] = nodes ? &nodes[s] : NULL;
      for (j = 0; j < n; j++)
        {
          /* Compute value count for each site. */

          uint32_t c = 0;
          FAR llvm_value_prof_node *site =
              nodes ? llvm_rt_record.node_kind[i][j] : NULL;
          while (site)
            {
              c++;
              site = site->next;
            }

          if (c > UCHAR_MAX)
            {
              c = UCHAR_MAX;
            }

          llvm_rt_record.site_count_array[i][j] = c;
        }

      s += n;
    }

  return num_value_kinds;
}

uint32_t llvm_get_value_prof_record_header_size(uint32_t num_value_sites)
{
  uint32_t size = offsetof(llvm_value_prof_record, site_count_array) +
                  sizeof(uint8_t) * num_value_sites;

  /* Round the size to multiple of 8 bytes. */

  size = (size + 7) & ~7;
  return size;
}

FAR llvm_value_prof_record *
llvm_get_first_value_prof_record(FAR llvm_value_prof_data *ptr)
{
  return (FAR llvm_value_prof_record *)((FAR char *)ptr +
                                   sizeof(llvm_value_prof_data));
}

static uint32_t
llvm_get_num_value_data_for_site_wrapper(uint32_t vk, uint32_t s)
{
  return llvm_get_num_value_data_for_site_rt(&llvm_rt_record, vk, s);
}

uint32_t llvm_get_value_prof_record_size(uint32_t num_value_sites,
                                         uint32_t num_value_data)
{
  return llvm_get_value_prof_record_header_size(num_value_sites) +
         sizeof(llvm_prof_value_data) * num_value_data;
}

uint32_t
llvm_get_value_prof_data_size(FAR llvm_value_prof_record_closure *closure)
{
  uint32_t kind;
  uint32_t total_size = sizeof(llvm_value_prof_data);
  FAR const void *record = closure->record;

  for (kind = IPVK_FIRST; kind <= IPVK_LAST; kind++)
    {
      uint32_t num_value_sites = closure->get_num_value_sites(record, kind);
      if (!num_value_sites)
        {
          continue;
        }

      total_size += llvm_get_value_prof_record_size(
          num_value_sites, closure->get_num_value_data(record, kind));
    }

  return total_size;
}

static uint32_t llvm_get_value_prof_data_size_wrapper(void)
{
  return llvm_get_value_prof_data_size(&llvm_rt_record_closure);
}

static FAR llvm_value_prof_node *
llvm_get_next_n_value_data(uint32_t vk, uint32_t site,
                           FAR llvm_prof_value_data *dst,
                           FAR llvm_value_prof_node *start_node,
                           uint32_t n)
{
  unsigned i;
  FAR llvm_value_prof_node *vnode =
    start_node ? start_node : llvm_rt_record.node_kind[vk][site];

  for (i = 0; i < n; i++)
    {
      dst[i].value = vnode->value;
      dst[i].count = vnode->count;
      vnode = vnode->next;
    }

  return vnode;
}

/****************************************************************************
 * Name: allocate_value_profile_counters
 *
 * Description:
 *   Allocate an array that holds the pointers to the linked lists of
 *   value profile counter nodes. The number of elements in the array
 *   is the total number of value profile sites instrumented.
 *
 * Note:
 *   By default, LLVM inserts sections into the ELF binary for static
 *   allocation, so this function will not be called in typical scenarios.
 *
 ****************************************************************************/

static int
llvm_allocate_value_profile_counters(FAR __llvm_profile_data *data)
{
  uint32_t vki;
  uint64_t num_vsites = 0;
  intptr_t expected = 0;
  FAR llvm_value_prof_node **mem = NULL;
  FAR atomic_intptr_t *ptr = &data->values;

  /* This function will never be called when value site array is
   * allocated statically at compile time.
   */

  llvm_profile_has_static_counters = 0;

  /* When dynamic allocation is enabled, allow tracking the max number
   * of values allowed.
   */

  if (!llvm_profile_has_non_default_vals_per_site)
    {
      llvm_profile_vp_max_num_vals_per_site =
        LLVM_INSTR_PROF_MAX_NUM_VAL_PER_SITE;
    }

  for (vki = IPVK_FIRST; vki <= IPVK_LAST; ++vki)
    {
      num_vsites += data->num_value_sites[vki];
    }

  mem = (FAR llvm_value_prof_node **)calloc(num_vsites,
                                        sizeof(FAR llvm_value_prof_node *));
  if (!mem)
    {
      return -1;
    }

  if (!atomic_compare_exchange_strong(ptr, &expected, (intptr_t)mem))
    {
      free(mem);
      return -1;
    }

  return 0;
}

/****************************************************************************
 * Name: llvm_allocate_one_node
 *
 * Description:
 *   Allocate a single value profile node. Depending on the allocation mode,
 *   the node is either dynamically allocated from the heap or obtained from
 *   a statically allocated section in the ELF binary.
 *
 * Note:
 *   In most cases, this function uses static allocation from LLVM-inserted
 *   ELF sections rather than dynamic heap allocation.
 *
 ****************************************************************************/

static FAR llvm_value_prof_node *llvm_allocate_one_node(void)
{
  FAR llvm_value_prof_node *node;

  if (!llvm_profile_has_static_counters)
    {
      FAR llvm_value_prof_node * rs =
        calloc(1, sizeof(llvm_value_prof_node));
      return rs;
    }

  /* Early check to avoid value wrapping around.  */

  if (llvm_profile_current_vnode + 1 > llvm_profile_end_vnode)
    {
      syslog(LOG_WARNING,
             "LLVM Value profiling: Static counter pool exhausted. "
             "Use -mllvm -vp-counters-per-site=<n> "
             "to increase allocation.\n");
      return 0;
    }

  node = (FAR llvm_value_prof_node *)atomic_fetch_add(
          (atomic_intptr_t *)&llvm_profile_current_vnode,
          sizeof(llvm_value_prof_node));

  /* Due to section padding, end_vnode point to a byte which is one pass
   * an incomplete vnode, so we need to skip the last incomplete node.
   */

  if (node + 1 > llvm_profile_end_vnode)
    {
      return 0;
    }

  return node;
}

/****************************************************************************
 * Name: llvm_get_memop_size_bucket
 *
 * Description:
 *   Map an observed memory operation size to a representative range value
 *   to reduce the number of distinct values that need to be tracked.
 *
 *   Mapping rules:
 *   - Values 1-8: Tracked individually (1, 2, 3, ..., 8)
 *   - Values >= 513: Capped at 513
 *   - Powers of two: Preserved as-is (16, 32, 64, 128, 256, etc.)
 *   - Other values: Mapped to (previous power of two + 1)
 *     For example: 9-16 -> 9, 17-32 -> 17, 33-64 -> 33, 65-128 -> 65
 *
 *   Examples: 5 -> 5, 22 -> 17, 99 -> 65, 256 -> 256, 1001 -> 513
 *
 ****************************************************************************/

uint64_t llvm_get_memop_size_bucket(uint64_t value)
{
  if (value <= 8)
    {
      return value;
    }
  else if (value >= 513)
    {
      return 513;
    }
  else if (__builtin_popcountll(value) == 1)
    {
      return value;
    }
  else
    {
      return (UINT64_C(1) << (64 - __builtin_clzll(value) - 1)) + 1;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR llvm_profile_data_reader *llvm_get_profile_data_reader(void)
{
  return &llvm_profile_vp_data_reader;
}

/****************************************************************************
 * Name: __llvm_profile_instrument_target
 *
 * Description:
 *   Counts the number of times a target value is recorded.
 *
 *   Records the target value for the counter_index if not recorded before.
 *   Otherwise, increments the counter associated with the target value.
 *
 ****************************************************************************/

void __llvm_profile_instrument_target(uint64_t target_value, FAR void *data,
                                      uint32_t counter_index)
{
  uint32_t vdata_count = 0;
  uint64_t min_count = UINT64_MAX;
  FAR __llvm_profile_data *pdata = (__llvm_profile_data *)data;
  FAR llvm_value_prof_node *cur_vnode = NULL;
  FAR llvm_value_prof_node *prev_vnode = NULL;
  FAR llvm_value_prof_node *min_count_vnode = NULL;
  FAR struct llvm_value_prof_node **value_counters = NULL;

  if (!pdata)
    {
      return;
    }

  if (predict_false(!pdata->values))
    {
      /* Normally, this function should not be called.
       * Because LLVM inserts sections for value profile counters.
       */

      if (llvm_allocate_value_profile_counters(pdata))
        {
          return;
        }
    }

  value_counters = (FAR struct llvm_value_prof_node **)pdata->values;
  cur_vnode = value_counters[counter_index];

  while (cur_vnode)
    {
      if (target_value == cur_vnode->value)
        {
          cur_vnode->count += 1;
          return;
        }

      if (cur_vnode->count < min_count)
        {
          min_count = cur_vnode->count;
          min_count_vnode = cur_vnode;
        }

      prev_vnode = cur_vnode;
      cur_vnode = cur_vnode->next;
      ++vdata_count;
    }

  if (vdata_count >= llvm_profile_vp_max_num_vals_per_site)
    {
      /* When the tracking limit is reached, decrement the count of the
       * least frequent value and evict it if the count drops to zero.
       * This strategy helps frequently occurring values persist while
       * less common ones are more easily replaced, ensuring dominant
       * values remain tracked even during the initial profiling phase.
       *
       * In rare scenarios where multiple hot values compete for limited
       * slots, some may be lost due to interleaved access patterns.
       * If this occurs, consider increasing vp_max_num_vals_per_site
       * to allow tracking more values per profiling site.
       */

      if (min_count_vnode->count <= 1)
        {
          cur_vnode = min_count_vnode;
          cur_vnode->value = target_value;
          cur_vnode->count = 1;
        }
      else
        {
          min_count_vnode->count -= 1;
        }

      return;
    }

  cur_vnode = llvm_allocate_one_node();
  if (!cur_vnode)
    {
      return;
    }

  cur_vnode->value = target_value;
  cur_vnode->count += 1;

  bool success = 0;
  intptr_t expected = 0;
  if (!value_counters[counter_index])
    {
      success = atomic_compare_exchange_strong(
                  (FAR atomic_intptr_t *)&value_counters[counter_index],
                  &expected,
                  (intptr_t)cur_vnode);
    }
  else if (prev_vnode && !prev_vnode->next)
    {
      success = atomic_compare_exchange_strong(
                  (atomic_intptr_t *)&(prev_vnode->next),
                  &expected,
                  (intptr_t)cur_vnode);
    }

  if (!success && !llvm_profile_has_static_counters)
    {
      free(cur_vnode);
      return;
    }
}

/****************************************************************************
 * Name: __llvm_profile_instrument_memop
 *
 * Description:
 *  Instrument memory operation(memmove, memcpy).
 *
 ****************************************************************************/

void __llvm_profile_instrument_memop(uint64_t target_value, FAR void *data,
                                     uint32_t counter_index)
{
  const uint64_t rep_value = llvm_get_memop_size_bucket(target_value);
  __llvm_profile_instrument_target(rep_value, data, counter_index);
}
