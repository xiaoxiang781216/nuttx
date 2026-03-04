/****************************************************************************
 * include/nuttx/devfreq/devfreq_rpmsg.h
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

#ifndef __INCLUDE_NUTTX_DEVFREQ_DEVFREQ_RPMSG_H
#define __INCLUDE_NUTTX_DEVFREQ_DEVFREQ_RPMSG_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/devfreq.h>

/****************************************************************************
 * Public Function Prototypes
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
 * Name: devfreq_rpmsg_register
 *
 * Description:
 *   Register a devfreq rpmsg proxy device. This creates a local devfreq
 *   device that forwards all operations to a remote CPU via rpmsg.
 *   The frequency table is obtained from the server automatically
 *   when the rpmsg connection is established.
 *
 *   After the server pushes the frequency table, the devfreq device
 *   is registered and users can use standard devfreq APIs:
 *   - devfreq_find_by_name() to get the device handle
 *   - devfreq_qos_add_request() to add frequency constraints
 *   - devfreq_qos_update_request() to update constraints
 *   - devfreq_qos_remove_request() to remove constraints
 *   - devfreq_get_frequency() to get current frequency
 *
 * Input Parameters:
 *   name       - Local device name (e.g., "cpu")
 *   cpuname    - Remote CPU name (e.g., "ap")
 *   governor   - Governor to use (e.g., devfreq_performance())
 *
 * Returned Value:
 *   Opaque handle on success; NULL on failure
 *
 * Example:
 *   void *handle = devfreq_rpmsg_register("cpu", "ap",
 *                                         devfreq_performance());
 *
 ****************************************************************************/

FAR void *
devfreq_rpmsg_register(FAR const char *name,
                       FAR const char *cpuname,
                       FAR const struct devfreq_governor_s *governor);

/****************************************************************************
 * Name: devfreq_rpmsg_unregister
 *
 * Description:
 *   Unregister a devfreq rpmsg proxy device.
 *
 * Input Parameters:
 *   handle - handle returned by devfreq_rpmsg_register
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure
 *
 ****************************************************************************/

int devfreq_rpmsg_unregister(FAR void *handle);

/****************************************************************************
 * Name: devfreq_rpmsg_server_init
 *
 * Description:
 *   Initialize the devfreq rpmsg server. This allows remote CPUs to
 *   control any registered devfreq device via rpmsg. The target device
 *   is resolved by name from the endpoint name at bind time.
 *
 *   The server uses QoS mechanism to aggregate frequency requests from
 *   multiple remote CPUs. Each remote CPU's request is tracked separately,
 *   and the final frequency is determined by taking the maximum of all
 *   minimum constraints.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Zero on success; a negated errno on failure
 *
 * Example:
 *   devfreq_rpmsg_server_init();
 *
 ****************************************************************************/

int devfreq_rpmsg_server_init(void);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_DEVFREQ_DEVFREQ_RPMSG_H */
