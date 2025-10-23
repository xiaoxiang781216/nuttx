/****************************************************************************
 * include/nuttx/timers/capture.h
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

#ifndef __INCLUDE_NUTTX_TIMERS_CAPTURE_H
#define __INCLUDE_NUTTX_TIMERS_CAPTURE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/signal.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* IOCTL Commands ***********************************************************/

#define CAPIOC_DUTYCYCLE  _CAPIOC(1)
#define CAPIOC_FREQUENCE  _CAPIOC(2)
#define CAPIOC_EDGES      _CAPIOC(3)
#define CAPIOC_ALL        _CAPIOC(4)
#define CAPIOC_REGISTER   _CAPIOC(5)
#define CAPIOC_UNREGISTER _CAPIOC(6)

/****************************************************************************
 * Public Types
 ****************************************************************************/

struct cap_all_s
{
  uint32_t freq;
  uint32_t edges;
  uint8_t  duty;
};

struct cap_lowerhalf_s;
#ifdef CONFIG_CAPTURE_NOTIFY

/* Capture type */

enum cap_type_e
{
  CAP_TYPE_RISING,
  CAP_TYPE_FALLING,
  CAP_TYPE_BOTH,
};

/* Capture notify information, used to cmd:CAPIOC_REGISTER */

struct cap_notify_s
{
  struct sigevent event; /* The signal to be sent */
  int chan;              /* Capture channel */
  enum cap_type_e type;  /* Capture edge type */
  FAR void *ptr;         /* User data pointer */
};

/* Capture edge interrupt notification callback */

typedef CODE void (*capture_notify_t)(FAR struct cap_lowerhalf_s *lower,
                                      FAR void *priv);
#endif

/* This structure provides the "lower-half" driver operations available to
 * the "upper-half" driver.
 */

struct cap_ops_s
{
  /* Required methods *******************************************************/

  /* Start the capture, resetting the configure of timers */

  CODE int (*start)(FAR struct cap_lowerhalf_s *lower);

  /* Stop the capture */

  CODE int (*stop)(FAR struct cap_lowerhalf_s *lower);

  /* Get the result pwm capture duty value */

  CODE int (*getduty)(FAR struct cap_lowerhalf_s *lower,
                      FAR uint8_t *duty);

  /* Get the result pwm capture frequence value */

  CODE int (*getfreq)(FAR struct cap_lowerhalf_s *lower,
                      FAR uint32_t *freq);

  /* Get the result pwm capture edges value */

  CODE int (*getedges)(FAR struct cap_lowerhalf_s *lower,
                       FAR uint32_t *edges);

#ifdef CONFIG_CAPTURE_NOTIFY
  /* Bind the capture edge interrupt notification callback */

  CODE int (*bind)(FAR struct cap_lowerhalf_s *lower,
                   enum cap_type_e type, capture_notify_t cb,
                   FAR void *priv);

  /* Un-Bind the capture edge interrupt notification callback */

  CODE int (*unbind)(FAR struct cap_lowerhalf_s *lower);
#endif
};

/* This structure provides the publicly visible representation of the
 * "lower-half" driver state structure.  "lower half" drivers will have an
 * internal structure definition that will be cast-compatible with this
 * structure definitions.
 */

struct cap_lowerhalf_s
{
  FAR const struct cap_ops_s *ops;
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: cap_register
 *
 * Description:
 *   Register the pulse capture lower half device as 'devpath'
 *
 * Input Parameters:
 *   devpath - The full path to the driver to register. E.g., "/dev/cap0"
 *   lower - An instance of the lower half interface
 *
 * Returned Value:
 *   Zero (OK) on success; a negated errno value on failure.  The following
 *   possible error values may be returned (most are returned by
 *   register_driver()):
 *
 *   EINVAL - 'path' is invalid for this operation
 *   EEXIST - An inode already exists at 'path'
 *   ENOMEM - Failed to allocate in-memory resources for the operation
 *
 ****************************************************************************/

int cap_register(FAR const char *devpath,
                 FAR struct cap_lowerhalf_s *lower);

int cap_register_multiple(FAR const char *devpath,
                          FAR struct cap_lowerhalf_s **lower, int n);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __INCLUDE_NUTTX_TIMERS_CAPTURE_H */
