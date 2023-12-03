/****************************************************************************
 * drivers/cpufreq/qos.h
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

#ifndef _DRIVERS_CPUFREQ_QOS_H
#define _DRIVERS_CPUFREQ_QOS_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>

#include <nuttx/plist.h>
#include <nuttx/notifier.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define FREQ_QOS_MIN_DEFAULT_VALUE              0
#define FREQ_QOS_MAX_DEFAULT_VALUE              INT32_MAX

#define PM_QOS_DEFAULT_VALUE                    (-1)
#define PM_QOS_FLAG_NO_POWER_OFF                (1 << 0)

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum pm_qos_type
{
  PM_QOS_UNITIALIZED,
  PM_QOS_MAX,         /* Return the largest value */
  PM_QOS_MIN,         /* Return the smallest value */
};

struct pm_qos_constraints
{
  struct plist_head list;
  int32_t target_value;
  int32_t default_value;
  int32_t no_constraint_value;
  enum pm_qos_type type;
  struct blocking_notifier_head *notifiers;
};

struct pm_qos_request
{
  struct plist_node node;
  struct pm_qos_constraints *qos;
};

struct pm_qos_flags_request
{
  struct list_node node;
  int32_t flags;
};

struct pm_qos_flags
{
  struct list_node list;
  int32_t effective_flags;
};

enum freq_qos_req_type
{
  FREQ_QOS_MIN = 1,
  FREQ_QOS_MAX,
};

struct freq_constraints
{
  struct pm_qos_constraints min_freq;
  struct blocking_notifier_head min_freq_notifiers;
  struct pm_qos_constraints max_freq;
  struct blocking_notifier_head max_freq_notifiers;
};

struct freq_qos_request
{
  enum freq_qos_req_type type;
  struct plist_node pnode;
  struct freq_constraints *qos;
};

/* Action requested to pm_qos_update_target */

enum pm_qos_req_action
{
  PM_QOS_ADD_REQ,               /* Add a new request */
  PM_QOS_UPDATE_REQ,            /* Update an existing request */
  PM_QOS_UPDATE_REQ_NON_NOTIFY, /* Update an existing request without notify */
  PM_QOS_REMOVE_REQ             /* Remove an existing request */
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int32_t pm_qos_read_value(FAR struct pm_qos_constraints *c);

int pm_qos_update_target(FAR struct pm_qos_constraints *c,
                         FAR struct plist_node *node,
                         enum pm_qos_req_action action, int32_t value);
int pm_qos_update_flags(FAR struct pm_qos_flags *pqf,
                        FAR struct pm_qos_flags_request *req,
                        enum pm_qos_req_action action, int32_t val);

void freq_constraints_init(FAR struct freq_constraints *qos);
int32_t freq_qos_read_value(FAR struct freq_constraints *qos,
                            enum freq_qos_req_type type);

int freq_qos_add_request(FAR struct freq_constraints *qos,
                         FAR struct freq_qos_request *req,
                         enum freq_qos_req_type type, int32_t value);
int freq_qos_update_request(FAR struct freq_qos_request *req,
                            int32_t new_value);
int freq_qos_remove_request(FAR struct freq_qos_request *req);

int freq_qos_apply(FAR struct freq_qos_request *req,
                   enum pm_qos_req_action action, int32_t value);

int freq_qos_add_notifier(FAR struct freq_constraints *qos,
                          enum freq_qos_req_type type,
                          struct notifier_block *notifier);
int freq_qos_remove_notifier(FAR struct freq_constraints *qos,
                             enum freq_qos_req_type type,
                             FAR struct notifier_block *notifier);

#endif /* _DRIVERS_CPUFREQ_QOS_H */
