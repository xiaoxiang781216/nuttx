/****************************************************************************
 * libs/libbuiltin/compiler-rt/profile.h
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

#include <stdatomic.h>
#include <unistd.h>
#include <nuttx/streams.h>

#define LLVM_INSTR_PROF_MAX_NUM_VAL_PER_SITE 255

#define LLVM_DEBUG_COMPILER_RT_COVERAGE 0

/* Record where the data is in memory. Within each of the types of data,
 * it's stored consecutively.
 */

extern weak_data char __start__llvm_prf_names[];
extern weak_data char __end__llvm_prf_names[];
extern weak_data char __start__llvm_prf_data[];
extern weak_data char __end__llvm_prf_data[];
extern weak_data char __start__llvm_prf_cnts[];
extern weak_data char __end__llvm_prf_cnts[];
extern weak_data char __start__llvm_prf_bits[];
extern weak_data char __end__llvm_prf_bits[];
extern weak_data char __start_llvm_prf_vtab[];
extern weak_data char __end_llvm_prf_vtab[];
extern weak_data char __start__llvm_prf_vns[];
extern weak_data char __end__llvm_prf_vns[];
extern weak_data char __start__llvm_prf_vnds[];
extern weak_data char __end__llvm_prf_vnds[];

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum llvm_prf_value_kind
{
  /* Indirect call target profiling: captures function addresses at
   * indirect call sites. Raw addresses are converted to MD5 hashes of
   * function names during profile deserialization (typically during
   * profile merging). This conversion uses ProfData which maps function
   * addresses to their name hashes.
   */

  IPVK_INDIRECT_CALL_TARGET = 0,

  /* Memory intrinsic size profiling: tracks sizes of memory operations. */

  IPVK_MEM_OP_SIZE = 1,

  /* Virtual table profiling: captures vtable pointers from objects
   * (may include offsets from actual vtable base addresses).
   * Raw profiles store:
   * 1. Address ranges of per-class vtable objects
   * 2. Compressed vtable names
   * The reader converts profiled addresses to MD5 hashes of vtable names.
   */

  IPVK_VTABLE_TARGET = 2,

  /* Boundary markers for value kind enumeration.
   * Must be declared last to ensure proper array indexing.
   */

  IPVK_FIRST = IPVK_INDIRECT_CALL_TARGET,
  IPVK_LAST = IPVK_VTABLE_TARGET,
};

typedef struct aligned_data(8) __llvm_profile_data
{
  const uint64_t name_ref;
  const uint64_t func_hash;
  const intptr_t counter_ptr;
  const intptr_t bitmap_ptr;
  const intptr_t func_ptr;
  atomic_intptr_t values;
  const uint32_t num_counters;
  const uint16_t num_value_sites[IPVK_LAST + 1];
  const uint32_t num_bitmap_bytes;
}__llvm_profile_data;

/* Virtual table metadata: stores name hash for linking profiled addresses
 * to globals, plus base address and size to map offset vtable pointers
 * back to their defining vtable objects.
 */

typedef struct llvm_prf_vtable_data
{
  uint64_t name_hash;
  intptr_t ptr;
  uint32_t size;
}llvm_prf_vtable_data;

typedef struct __llvm_profile_header
{
  uint64_t magic;
  uint64_t version;
  uint64_t binary_ids_size;
  uint64_t data_size;
  uint64_t padding_bytes_before_counters;
  uint64_t counters_size;
  uint64_t padding_bytes_after_counters;
  uint64_t bitmap_size;
  uint64_t padding_bytes_after_bitmap;
  uint64_t names_size;
  uint64_t counters_delta;
  uint64_t bitmap_delta;
  uint64_t names_delta;
  uint64_t num_vtables;
  uint64_t vname_size;
  enum llvm_prf_value_kind value_kind_last;
}__llvm_profile_header;

/* Represents a single profiled value with its occurrence count. */

typedef struct llvm_prof_value_data
{
  uint64_t value;
  uint64_t count;
} llvm_prof_value_data;

typedef struct llvm_value_prof_data
{
  /* Total structure size in bytes
   * must be a multiple of sizeof(uint64_t)).
   */

  uint32_t total_size;

  /* Number of value profiling kinds with data.
   * A kind is included if it has non-zero profiling sites.
   * (Alternatively, could skip kinds with no actual profiled values.)
   */

  uint32_t num_value_kinds;
} llvm_value_prof_data;

typedef struct llvm_profile_data_reader
{
  uint32_t (*init_rt_record)(const __llvm_profile_data *data,
                             uint8_t *site_count_array[]);

  uint32_t (*get_value_prof_record_header_size)(uint32_t num_sites);

  struct llvm_value_prof_record *(*get_first_value_prof_record)(
      struct llvm_value_prof_data *);

  uint32_t (*get_num_value_data_for_site)(uint32_t vk, uint32_t site);

  uint32_t (*get_value_prof_data_size)(void);

  /* Reads n value entries for the specified site into dst.
   * If start_node is non-NULL, reading begins from that node.
   * Returns the next node for subsequent batch reads, or NULL if complete.
   */

  FAR struct llvm_value_prof_node *(*get_value_data)(
    uint32_t value_kind,
    uint32_t site,
    FAR struct llvm_prof_value_data *dst,
    FAR struct llvm_value_prof_node *start_node,
    uint32_t n);
} llvm_profile_data_reader;

typedef struct llvm_value_prof_record
{
  /* Value profiling kind for this record. */

  uint32_t kind;

  /* Number of profiling sites (guaranteed non-zero, or record is omitted). */

  uint32_t num_value_sites;

  /* Array storing the count of profiled values per site.
   * Length is num_value_sites (minimum 1).
   */

  uint8_t site_count_array[1];
} llvm_value_prof_record;

/****************************************************************************
 * Public Function Declarations
 ****************************************************************************/

FAR llvm_profile_data_reader *llvm_get_profile_data_reader(void);
int llvm_profile_write(struct lib_outstream_s *stream,
                       llvm_profile_data_reader *reader,
                       const __llvm_profile_data *data_begin,
                       const __llvm_profile_data *data_end);
