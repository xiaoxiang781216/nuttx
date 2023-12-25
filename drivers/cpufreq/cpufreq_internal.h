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

#include <nuttx/cpufreq.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CPUFREQ_RELATION_L  0   /* lowest frequency at or above target */
#define CPUFREQ_RELATION_H  1   /* highest frequency below or at target */
#define CPUFREQ_RELATION_C  2   /* closest frequency to target */

/****************************************************************************
 * Public Types
 ****************************************************************************/

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
