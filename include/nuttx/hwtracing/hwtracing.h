/****************************************************************************
 * include/nuttx/hwtracing/hwtracing.h
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

#ifndef __INCLUDE_NUTTX_HWTRACING_HWTRACING_H
#define __INCLUDE_NUTTX_HWTRACING_HWTRACING_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <nuttx/clk/clk.h>
#include <nuttx/list.h>
#include <nuttx/power/pm.h>
#include <nuttx/spinlock.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Register operations */

#define hwtracing_put8(val, addr) \
        (*(FAR volatile uint8_t *)(addr) = (val))
#define hwtracing_put16(val, addr) \
        (*(FAR volatile uint16_t *)(addr) = (val))
#define hwtracing_put32(val, addr) \
        (*(FAR volatile uint32_t *)(addr) = (val))
#define hwtracing_put64(val, addr) \
        (*(FAR volatile uint64_t *)(addr) = (val))

#define hwtracing_get32(addr) \
        (*(FAR volatile uint32_t *)(addr))
#define hwtracing_modify32(val, mask, addr) \
        hwtracing_put32((hwtracing_get32(addr) & ~(mask)) | \
        ((val) & (mask)), (addr))

/****************************************************************************
 * Public Types
 ****************************************************************************/

enum hwtracing_dev_type_e
{
  HWTRACING_DEV_TYPE_SOURCE,
  HWTRACING_DEV_TYPE_LINK,
  HWTRACING_DEV_TYPE_SINK,
  HWTRACING_DEV_TYPE_MAX
};

enum hwtracing_dev_subtype_source_e
{
  HWTRACING_DEV_SUBTYPE_SOURCE_PROC,       /* ETM */
  HWTRACING_DEV_SUBTYPE_SOURCE_SOFTWARE,   /* STM */
};

enum hwtracing_dev_subtype_link_e
{
  HWTRACING_DEV_SUBTYPE_LINK_MERG,         /* Funnel */
  HWTRACING_DEV_SUBTYPE_LINK_SPLIT,        /* Replocator */
  HWTRACING_DEV_SUBTYPE_LINK_FIFO,         /* FIFO */
};

enum hwtracing_dev_subtype_sink_e
{
  HWTRACING_DEV_SUBTYPE_SINK_PORT,         /* TPIU */
  HWTRACING_DEV_SUBTYPE_SINK_BUFFER,       /* ETB */
  HWTRACING_DEV_SUBTYPE_SINK_TMC_BUFFER,   /* TMC ETB */
  HWTRACING_DEV_SUBTYPE_SINK_TMC_SYSMEM,   /* TMC ETR */
  HWTRACING_DEV_SUBTYPE_SINK_TMC_ETF,      /* TMC ETF */
  HWTRACING_DEV_SUBTYPE_SINK_TPB,          /* TPB  */
  HWTRACING_DEV_SUBTYPE_SINK_TFIF,         /* TFIF */
  HWTRACING_DEV_SUBTYPE_SINK_RAM,          /* RAM  */
  HWTRACING_DEV_SUBTYPE_SINK_SGBT,         /* SGBT */
  HWTRACING_DEV_SUBTYPE_SINK_ETH,          /* ETH  */
  HWTRACING_DEV_SUBTYPE_SINK_DAP,          /* DAP  */
};

/* This structure is used to unify different subtype of devices. */

union hwtracing_dev_subtype_u
{
  enum hwtracing_dev_subtype_source_e source_subtype;
  enum hwtracing_dev_subtype_link_e link_subtype;
  enum hwtracing_dev_subtype_sink_e sink_subtype;
};

struct hwtracing_dev_s;

struct hwtracing_sink_ops_s
{
  int (*enable)(FAR struct hwtracing_dev_s *htdev);
  void (*disable)(FAR struct hwtracing_dev_s *htdev);
  int (*init)(FAR struct hwtracing_dev_s *htdev);
};

struct hwtracing_link_ops_s
{
  int (*enable)(FAR struct hwtracing_dev_s *htdev, int iport, int oport);
  void (*disable)(FAR struct hwtracing_dev_s *htdev, int iport, int oport);
  int (*init)(FAR struct hwtracing_dev_s *htdev);
};

struct hwtracing_source_ops_s
{
  int (*enable)(FAR struct hwtracing_dev_s *htdev);
  void (*disable)(FAR struct hwtracing_dev_s *htdev);
  int (*init)(FAR struct hwtracing_dev_s *htdev);
};

/* This structure is used to unify different operations of devices. */

struct hwtracing_ops_s
{
  union
    {
      FAR const struct hwtracing_sink_ops_s *sink_ops;
      FAR const struct hwtracing_link_ops_s *link_ops;
      FAR const struct hwtracing_source_ops_s *source_ops;
    };
};

struct hwtracing_portdesc_s
{
  /* hwtracing device's name this port connects to. */

  FAR const char *remote;

  /* Port connects to. */

  int port;
};

struct hwtracing_desc_s
{
  FAR const char *name;
#ifdef CONFIG_CLK
  FAR const char *clkname;
#endif
  uintptr_t addr;
  enum hwtracing_dev_type_e type;
  union hwtracing_dev_subtype_u subtype;

  /* Used in ETM device. */

  uint8_t cpu;

  /* Used in funnel devices. */

  int inport_num;

  /* Used in STM device: start address of extend stimulus port, this memory
   * should be reserved for stm use.
   * Size of this prealloced memory equals to (256 x number of ports).
   */

  uintptr_t stimulus_port_addr;

  /* Used in TMC-ETR device. */

  FAR void *buffer;
  uint32_t buffer_size;
  uint32_t burst_size;
  uint32_t caps;

  /* Description of outports of current device. */

  int outport_num;
  struct hwtracing_portdesc_s outports[CONFIG_HWTRACING_MAX_OUTPORT_NUM];
};

/* Use to build the trace path. */

struct hwtracing_connect_s
{
  int srcport;
  int destport;

  /* Used to find the dest device when build the trace path. */

  FAR const char *destname;
  FAR struct hwtracing_dev_s *srcdev;
  FAR struct hwtracing_dev_s *destdev;
};

struct hwtracing_dev_s
{
  FAR const char *name;
#ifdef CONFIG_CLK
  FAR struct clk_s *clk;
#endif
#ifdef CONFIG_PM
  struct pm_callback_s pmcb;
#endif

  /* hwtracing device's enable count. */

  uint8_t refcnt;

  /* Memory-mapped base address of current hwtracing device. */

  uintptr_t addr;
  enum hwtracing_dev_type_e type;
  union hwtracing_dev_subtype_u subtype;
  FAR const struct hwtracing_ops_s *ops;
  spinlock_t lock;

  /* Used to connect all the hwtracing device register to hwtracing bus. */

  struct list_node node;

  /* Used in source hwtracing device as trace path's list head. */

  struct list_node path;

  /* Out port number current hwtracing device have. */

  int outport_num;

  /* Pointer to an array of connections, array size is equal
   * to the outport number.
   */

  FAR struct hwtracing_connect_s *outconns;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: hwtracing_register
 *
 * Description:
 *   Register a hwtracing device to the hwtracing bus.
 *
 * Input Parameters:
 *   htdev  - Pointer to the hwtracing device that needs to be registered.
 *   desc   - Pointer to the attribute description of this hwtracing device.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int hwtracing_register(FAR struct hwtracing_dev_s *htdev,
                       FAR const struct hwtracing_desc_s *desc);

/****************************************************************************
 * Name: hwtracing_unregister
 *
 * Description:
 *   Unregister a hwtracing device from hwtracing bus.
 *
 * Input Parameters:
 *   htdev  - Pointer to the hwtracing device that needs to be unregistered.
 *
 ****************************************************************************/

void hwtracing_unregister(FAR struct hwtracing_dev_s *htdev);

/****************************************************************************
 * Name: hwtracing_enable
 *
 * Description:
 *   Enable trace start from srcdev to destdev.
 *
 * Input Parameters:
 *   srcdev  - Source device that generates trace data.
 *   destdev - Sink device that finally accepts the trace data.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int hwtracing_enable(FAR struct hwtracing_dev_s *srcdev,
                     FAR struct hwtracing_dev_s *destdev);

/****************************************************************************
 * Name: hwtracing_disable
 *
 * Description:
 *   Disable the trace start from srcdev to destdev.
 *
 * Input Parameters:
 *   srcdev  - Source device that generates trace data.
 *
 ****************************************************************************/

void hwtracing_disable(FAR struct hwtracing_dev_s *srcdev);

/****************************************************************************
 * Name: hwtracing_find_dev
 *
 * Description:
 *   Retrieve hwtracing device pointer based on name
 *
 * Input Parameters:
 *   name  - Dev name.
 *
 ****************************************************************************/

FAR struct hwtracing_dev_s *hwtracing_find_dev(FAR const char *name);

/****************************************************************************
 * Name: hwtracing_timeout
 *
 * Description:
 *   Loop until a bitmask of register has changed to a specific value.
 *
 * Input Parameters:
 *   addr    - Base addr of the hwtracing device.
 *   off     - Register offset of the hwtracing device.
 *   bitmask - Bitmask to be checked.
 *   val     - Value to be matched.
 *
 * Returned Value:
 *   Zero on success; a negative value on failure.
 *
 ****************************************************************************/

int hwtracing_timeout(uint32_t val, uint32_t mask, uintptr_t addr);

#endif  /* __INCLUDE_NUTTX_HWTRACING_HWTRACING_H */
