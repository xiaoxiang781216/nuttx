/****************************************************************************
 * libs/libbuiltin/compiler-rt/profile_write.c
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
#include <stdio.h>
#include <stdlib.h>
#include <syslog.h>
#include <unistd.h>

#include "profile.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static llvm_prof_value_data llvm_profile_vp_data_array[16];
static uint32_t llvm_profile_vp_data_array_size =
    sizeof(llvm_profile_vp_data_array) / sizeof(*llvm_profile_vp_data_array);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int llvm_profile_write_one_profile(
    FAR struct lib_outstream_s *stream,
    FAR llvm_profile_data_reader *reader,
    FAR const __llvm_profile_data *data)
{
  size_t write_data_size = 0;
  size_t write_header_size = 0;
  size_t write_record_size = 0;
  unsigned num_value_kinds;
  llvm_value_prof_data vp_header;
  FAR uint8_t *site_count_array[IPVK_LAST + 1] =
    {
      0
    };

  for (size_t i = 0; i <= IPVK_LAST; i++)
    {
      if (!data->num_value_sites[i])
        {
          site_count_array[i] = NULL;
        }
      else
        {
          uint32_t size =
              reader->get_value_prof_record_header_size(
                data->num_value_sites[i]) -
                offsetof(llvm_value_prof_record, site_count_array);

          /* Only use alloca for this small byte array to avoid excessive
           * stack growth.
           */

          site_count_array[i] =
            (FAR uint8_t *)alloca(size);
          memset(site_count_array[i], 0, size);
        }
    }

  /* If num_value_kinds returned is 0, there is nothing to write, report
   * success and return. This should match the raw profile reader's
   * behavior.
   */

  if (!(num_value_kinds = reader->init_rt_record(data, site_count_array)))
    {
      return 0;
    }

  /* First write the header structure. */

  vp_header.total_size = reader->get_value_prof_data_size();
  vp_header.num_value_kinds = num_value_kinds;

  write_header_size = stream->puts(stream,
                                   (FAR const uint8_t *)&vp_header,
                                   sizeof(llvm_value_prof_data));
  if (sizeof(llvm_value_prof_data) != write_header_size)
    {
      syslog(LOG_ERR, "Failed to write value profile header\n");
      return -1;
    }

  /* Make sure nothing else needs to be written before value profile
   * records.
   */

  if ((void *)reader->get_first_value_prof_record(&vp_header) !=
      (void *)(&vp_header + 1))
    {
      syslog(LOG_ERR, "Unexpected data before value profile records\n");
      return -1;
    }

  /* Write out the value profile record for each value kind
   * one by one.
   */

  for (size_t i = 0; i <= IPVK_LAST; i++)
    {
      uint32_t j;
      llvm_value_prof_record record_header;

      /* The size of the value prof record header without counting the
       * site count array.
       */

      uint32_t record_header_size =
          offsetof(llvm_value_prof_record, site_count_array);
      uint32_t site_count_array_size;

      if (!data->num_value_sites[i])
        {
          continue;
        }

      /* Write out the record header. */

      record_header.kind = i;
      record_header.num_value_sites = data->num_value_sites[i];

      write_record_size = stream->puts(stream,
                                       (FAR const uint8_t *)&record_header,
                                       record_header_size);
      if (record_header_size != write_record_size)
        {
          syslog(LOG_ERR, "Failed to write value profile record header\n");
          return -1;
        }

      /* Write out the site value count array including padding space. */

      site_count_array_size =
          reader->get_value_prof_record_header_size(
              data->num_value_sites[i]) - record_header_size;
      if (stream->puts(stream, site_count_array[i],
                       site_count_array_size) != site_count_array_size)
        {
          syslog(LOG_ERR,
            "Failed to write value profile site count array\n");
          return -1;
        }

      /* Write out the value profile data for each value site. */

      for (j = 0; j < data->num_value_sites[i]; j++)
        {
          uint32_t n_read;
          uint32_t n_remain;
          FAR struct llvm_value_prof_node *next_start_node = NULL;

          n_remain = reader->get_num_value_data_for_site(i, j);
          if (!n_remain)
            {
              continue;
            }

          /* Read and write out value data in small chunks till it is
           * done.
           */

          do
            {
              n_read = (n_remain > llvm_profile_vp_data_array_size ?
                        llvm_profile_vp_data_array_size : n_remain);
              next_start_node =
                  reader->get_value_data(i, /* value_kind */
                                         j, /* site */
                                         &llvm_profile_vp_data_array[0],
                                         next_start_node, n_read);
              write_data_size =
                  stream->puts(
                    stream,
                    (FAR const uint8_t *)&llvm_profile_vp_data_array[0],
                    n_read * sizeof(llvm_prof_value_data));
              if (write_data_size != n_read * sizeof(llvm_prof_value_data))
                {
                  syslog(LOG_ERR, "Failed to write value profile data\n");
                  return -1;
                }

              n_remain -= n_read;
            }

          while (n_remain != 0);
        }
    }

  return 0;
}

int llvm_profile_write(FAR struct lib_outstream_s *stream,
                       FAR llvm_profile_data_reader *reader,
                       FAR const __llvm_profile_data *data_begin,
                       FAR const __llvm_profile_data *data_end)
{
  for (FAR const __llvm_profile_data *data = data_begin;
       data < data_end;
       data++)
    {
      if (llvm_profile_write_one_profile(stream, reader, data) != 0)
        {
          syslog(LOG_ERR,
            "Failed to write value profile data for %p\n", data);
          return -1;
        }
    }

  /* All done report success. */

  return 0;
}
