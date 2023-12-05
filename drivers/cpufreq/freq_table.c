/****************************************************************************
 * drivers/cpufreq/freq_table.c
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

#include "cpufreq_internal.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define clamp_val(val, hi, lo) \
  ((val) >= (hi) ? (hi) : ((val) <= (lo) ? (lo) : (val)))

/* cpufreq_for_each_entry - iterate over a cpufreq_frequency_table
 * pos:   the cpufreq_frequency_table * to use as a loop cursor.
 * table: the cpufreq_frequency_table * to iterate over.
 */

#define cpufreq_for_each_entry(pos, table) \
  for (pos = table; pos->frequency != CPUFREQ_TABLE_END; pos++)

/* cpufreq_for_each_entry_idx
 * iterate over a cpufreq_frequency_table with index
 * pos:   the cpufreq_frequency_table * to use as a loop cursor.
 * table: the cpufreq_frequency_table * to iterate over.
 * idx:   the table entry currently being processed
 */

#define cpufreq_for_each_entry_idx(pos, table, idx) \
  for (pos = table, idx = 0; pos->frequency != CPUFREQ_TABLE_END; pos++, idx++)

/* cpufreq_for_each_valid_entry
 * iterate over a cpufreq_frequency_table
 * excluding CPUFREQ_ENTRY_INVALID frequencies.
 * pos:   the cpufreq_frequency_table * to use as a loop cursor.
 * table: the cpufreq_frequency_table * to iterate over.
 */

#define cpufreq_for_each_valid_entry(pos, table) \
  cpufreq_for_each_entry(pos, table) \
    if (pos->frequency == CPUFREQ_ENTRY_INVALID) \
        continue;                                \
    else

/* cpufreq_for_each_valid_entry_idx
 * iterate with index over a cpufreq
 * frequency_table excluding CPUFREQ_ENTRY_INVALID frequencies.
 * pos:   the cpufreq_frequency_table * to use as a loop cursor.
 * table: the cpufreq_frequency_table * to iterate over.
 * idx:   the table entry currently being processed
 */

#define cpufreq_for_each_valid_entry_idx(pos, table, idx) \
  cpufreq_for_each_entry_idx(pos, table, idx)             \
    if (pos->frequency == CPUFREQ_ENTRY_INVALID)          \
      continue;                                           \
    else

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int cpufreq_table_find_index_al(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq);
static int cpufreq_table_find_index_dl(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq);
static int cpufreq_table_find_index_ah(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq);
static int cpufreq_table_find_index_dh(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq);
static int cpufreq_table_find_index_ac(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq);
static int cpufreq_table_find_index_dc(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq);
static int cpufreq_table_target(FAR struct cpufreq_policy *policy,
                                unsigned int target_freq,
                                unsigned int relation);
static int cpufreq_table_set_sorted(FAR struct cpufreq_policy *policy);
static void cpufreq_verify_within_limits(FAR struct cpufreq_verify *cv,
                                         unsigned int min,
                                         unsigned int max);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int cpufreq_table_find_index_al(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int best = -1;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      if (pos->frequency >= target_freq)
        {
          return idx;
        }

      best = idx;
    }

  return best;
}

/* Find lowest freq at or above target in a table in descending order */

static int cpufreq_table_find_index_dl(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int best = -1;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      unsigned int freq = pos->frequency;

      if (freq == target_freq)
        {
          return idx;
        }

      if (freq > target_freq)
        {
          best = idx;
          continue;
        }

      /* No freq found above target_freq */

      if (best == -1)
        {
          return idx;
        }

      return best;
    }

  return best;
}

/* Find highest freq at or below target in a table in ascending order */

static int cpufreq_table_find_index_ah(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int best = -1;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      unsigned int freq = pos->frequency;

      if (freq == target_freq)
        {
          return idx;
        }

      if (freq < target_freq)
        {
          best = idx;
          continue;
        }

      /* No freq found below target_freq */

      if (best == -1)
        {
          return idx;
        }

      return best;
    }

  return best;
}

/* Find highest freq at or below target in a table in descending order */

static int cpufreq_table_find_index_dh(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int best = -1;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      if (pos->frequency <= target_freq)
        {
          return idx;
        }

      best = idx;
    }

  return best;
}

/* Find closest freq to target in a table in ascending order */

static int cpufreq_table_find_index_ac(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int best = -1;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      unsigned int freq = pos->frequency;

      if (freq == target_freq)
        {
          return idx;
        }

      if (freq < target_freq)
        {
          best = idx;
          continue;
        }

      /* No freq found below target_freq */

      if (best == -1)
        {
          return idx;
        }

      /* Choose the closest freq */

      if (target_freq - table[best].frequency > freq - target_freq)
        {
          return idx;
        }

      return best;
    }

  return best;
}

/* Find closest freq to target in a table in descending order */

static int cpufreq_table_find_index_dc(FAR struct cpufreq_policy *policy,
                                       unsigned int target_freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int best = -1;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      unsigned int freq = pos->frequency;

      if (freq == target_freq)
        {
          return idx;
        }

      if (freq > target_freq)
        {
          best = idx;
          continue;
        }

      /* No freq found above target_freq */

      if (best == -1)
        {
          return idx;
        }

      /* Choose the closest freq */

      if (table[best].frequency - target_freq > target_freq - freq)
        {
          return idx;
        }

      return best;
    }

  return best;
}

static int cpufreq_table_target(FAR struct cpufreq_policy *policy,
                                unsigned int target_freq,
                                unsigned int relation)
{
  target_freq = clamp_val(target_freq, policy->max, policy->min);

  switch (relation)
    {
    case CPUFREQ_RELATION_L:
      if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
        {
          return cpufreq_table_find_index_al(policy, target_freq);
        }
      else
        {
          return cpufreq_table_find_index_dl(policy, target_freq);
        }

    case CPUFREQ_RELATION_H:
      if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
        {
          return cpufreq_table_find_index_ah(policy, target_freq);
        }
      else
        {
          return cpufreq_table_find_index_dh(policy, target_freq);
        }

    case CPUFREQ_RELATION_C:
      if (policy->freq_table_sorted == CPUFREQ_TABLE_SORTED_ASCENDING)
        {
          return cpufreq_table_find_index_ac(policy, target_freq);
        }
      else
        {
          return cpufreq_table_find_index_dc(policy, target_freq);
        }

    default:
      return -EINVAL;
    }
}

static int cpufreq_table_set_sorted(FAR struct cpufreq_policy *policy)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *prev = NULL;
  FAR const struct cpufreq_frequency_table *pos;
  int ascending = 0;

  cpufreq_for_each_valid_entry(pos, table)
    {
      if (!prev)
        {
          prev = pos;
          continue;
        }

      if (pos->frequency == prev->frequency)
        {
          pwrwarn("Duplicate freq-table entries: %u\n", pos->frequency);
          return -EINVAL;
        }

      /* Frequency increased from prev to pos */

      if (pos->frequency > prev->frequency)
        {
          /* But frequency was decreasing earlier */

          if (ascending < 0)
            {
              pwrerr("Freq table is unsorted\n");
              return -EINVAL;
            }

          ascending++;
        }
      else
        {
          /* Frequency decreased from prev to pos,
           * but frequency was increasing earlier
           */

          if (ascending > 0)
            {
              pwrerr("Freq table is unsorted\n");
              return -EINVAL;
            }

          ascending--;
        }

      prev = pos;
    }

  if (ascending > 0)
    {
      policy->freq_table_sorted = CPUFREQ_TABLE_SORTED_ASCENDING;
    }
  else
    {
      policy->freq_table_sorted = CPUFREQ_TABLE_SORTED_DESCENDING;
    }

  pwrinfo("Freq table is sorted in %s order\n",
           ascending > 0 ? "ascending" : "descending");

  return 0;
}

static void cpufreq_verify_within_limits(FAR struct cpufreq_verify *cv,
                                         unsigned int min,
                                         unsigned int max)
{
  if (cv->min < min)
    {
      cv->min = min;
    }

  if (cv->max < min)
    {
      cv->max = min;
    }

  if (cv->min > max)
    {
      cv->min = max;
    }

  if (cv->max > max)
    {
      cv->max = max;
    }

  if (cv->min > cv->max)
    {
      cv->min = cv->max;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int cpufreq_table_get_index(FAR struct cpufreq_policy *policy,
                            unsigned int freq)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  int idx;

  cpufreq_for_each_valid_entry_idx(pos, table, idx)
    {
      if (pos->frequency == freq)
        {
          return idx;
        }
    }

  return -EINVAL;
}

int cpufreq_table_validate(FAR struct cpufreq_policy *policy)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  unsigned int min_freq = UINT_MAX;
  unsigned int max_freq = 0;

  cpufreq_for_each_valid_entry(pos, table)
    {
      unsigned int freq = pos->frequency;

      if (freq < min_freq)
        {
          min_freq = freq;
        }

      if (freq > max_freq)
        {
          max_freq = freq;
        }
    }

  policy->min = policy->min_freq = min_freq;
  policy->max = policy->max_freq = max_freq;

  if (policy->min == UINT_MAX)
    {
      return -EINVAL;
    }

  return cpufreq_table_set_sorted(policy);
}

int cpufreq_table_verify(FAR struct cpufreq_policy *policy,
                         FAR struct cpufreq_verify *cv)
{
  FAR const struct cpufreq_frequency_table *table = policy->freq_table;
  FAR const struct cpufreq_frequency_table *pos;
  unsigned int next_larger = UINT_MAX;
  bool found = false;

  cpufreq_verify_within_limits(cv, policy->min_freq, policy->max_freq);

  cpufreq_for_each_valid_entry(pos, table)
    {
      unsigned int freq = pos->frequency;

      if (freq >= cv->min && freq <= cv->max)
        {
          found = true;
          break;
        }

      if (next_larger > freq && freq > cv->max)
        {
          next_larger = freq;
        }
    }

  if (!found)
    {
      cv->max = next_larger;
      cpufreq_verify_within_limits(cv, policy->min_freq, policy->max_freq);
    }

  return 0;
}

unsigned int cpufreq_table_resolve_freq(FAR struct cpufreq_policy *policy,
                                        unsigned int target_freq,
                                        unsigned int relation,
                                        unsigned int *ridx)
{
  unsigned int idx;

  idx = cpufreq_table_target(policy, target_freq, relation);
  if (ridx)
    {
      *ridx = idx;
    }

  return policy->freq_table[idx].frequency;
}
