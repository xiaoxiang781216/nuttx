/****************************************************************************
 * drivers/cpufreq/cpufreq_internal.h
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

#ifndef _DRIVERS_CPUFREQ_CPUFREQ_INTERNAL_H
#define _DRIVERS_CPUFREQ_CPUFREQ_INTERNAL_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <syslog.h>
#include <nuttx/cpufreq.h>

#include "qos.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CPUFREQ_NAME_LEN    16
#define CPUFREQ_RELATION_L  0   /* lowest frequency at or above target */
#define CPUFREQ_RELATION_H  1   /* highest frequency below or at target */
#define CPUFREQ_RELATION_C  2   /* closest frequency to target */

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum cpufreq_table_sorting
{
  CPUFREQ_TABLE_UNSORTED,
  CPUFREQ_TABLE_SORTED_ASCENDING,
  CPUFREQ_TABLE_SORTED_DESCENDING
};

struct cpufreq_governor;

struct cpufreq_policy
{
  FAR struct cpufreq_driver *driver;
  bool suspended;

  unsigned int max_freq;
  unsigned int min_freq;

  unsigned int min;                     /* in kHz */
  unsigned int max;                     /* in kHz */
  unsigned int cur;                     /* in kHz */

  struct cpufreq_governor *governor;

  struct freq_constraints constraints;
  struct freq_qos_request min_freq_req;
  struct freq_qos_request max_freq_req;

  const struct cpufreq_frequency_table *freq_table;
  enum cpufreq_table_sorting freq_table_sorted;

  /**
   * The rules for this semaphore:
   * - Any routine that wants to read from the policy structure will
   *   do a down_read on this semaphore.
   * - Any routine that will write to the policy structure and/or may take
   *   away the policy altogether (eg. CPU hotplug), will hold this lock
   *   in write mode before doing so.
   */

  struct mutex_s lock;

  struct blocking_notifier_head notifier_list;
  struct notifier_block nb_min;
  struct notifier_block nb_max;
  FAR void *governor_data;
};

struct cpufreq_governor
{
  char name[CPUFREQ_NAME_LEN];
  int (*init)(struct cpufreq_policy *policy);
  int (*exit)(struct cpufreq_policy *policy);
  int (*start)(struct cpufreq_policy *policy);
  void (*stop)(struct cpufreq_policy *policy);
  void (*limits)(struct cpufreq_policy *policy);
};

struct cpufreq_verify
{
  unsigned int min;                     /* in kHz */
  unsigned int max;                     /* in kHz */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int cpufreq_table_get_index(FAR struct cpufreq_policy *policy,
                            unsigned int freq);
int cpufreq_table_validate(FAR struct cpufreq_policy *policy);
int cpufreq_table_verify(FAR struct cpufreq_policy *policy,
                         FAR struct cpufreq_verify *cv);
unsigned int cpufreq_table_resolve_freq(FAR struct cpufreq_policy *policy,
                                        unsigned int target_freq,
                                        unsigned int relation,
                                        unsigned int *ridx);
int cpufreq_driver_target(FAR struct cpufreq_policy *policy,
                          unsigned int target_freq,
                          unsigned int relation);
FAR struct cpufreq_governor *cpufreq_default_governor(void);

#endif /* _DRIVERS_CPUFREQ_CPUFREQ_INTERNAL_H */
