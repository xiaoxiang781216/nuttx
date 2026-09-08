/****************************************************************************
 * include/nuttx/mtd/mtd_mcal_mem.h
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

#ifndef __INCLUDE_NUTTX_MTD_MTD_MCAL_MEM_H
#define __INCLUDE_NUTTX_MTD_MTD_MCAL_MEM_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

#include <nuttx/mtd/mtd.h>

/* struct mtd_mcal_mem_dev_s carries binding hooks whose signatures use the
 * real AUTOSAR MemAcc scalar types (MemAcc_AddressType / MemAcc_LengthType),
 * so this header includes the vendor MemAcc.h directly.  It is therefore only
 * includable from a translation unit that already has the vendor MemAcc
 * include paths on its search path -- the generic core (built as an isolated
 * aux library in drivers/mtd/CMakeLists.txt) and the vendor binding.  Both
 * are the only consumers of this header.
 */

#include "MemAcc.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* MTD adapter for an AUTOSAR Memory Access (MemAcc) address area.
 *
 * MemAcc (AUTOSAR_SWS_MemoryAccess R4.7) sits above the vendor Mem driver
 * and exposes a standardised, asynchronous job model: MemAcc_Read/Write/
 * Erase merely *accept* a job for an address area, MemAcc_MainFunction
 * advances the state machine, and MemAcc_GetJobStatus/GetJobResult report
 * progress and the final result.  NuttX MTD expects synchronous block-level
 * operations; this adapter bridges the two.
 *
 * The core (mtd_mcal_mem.c) calls the MemAcc standard API directly using an
 * address area id and a logical (area-relative) byte offset.  Because each
 * MTD device maps 1:1 onto one MemAcc address area whose logical address
 * starts at 0, the logical offset is simply the in-device byte offset
 * (block index * blocksize).  Vendor-specific concerns that MemAcc does not
 * cover are delegated to two small binding-provided hooks (see below).
 */

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* One MemAcc address area is presented as a single MTD device.  Its geometry
 * uses the standard struct mtd_geometry_s (blocksize = write/read page size,
 * erasesize = erase sector size, neraseblocks = sector count).  The non-
 * geometry attributes that mtd_geometry_s does not carry (absolute base
 * address, erased-cell value, and whether the memory supports erase at all)
 * live on struct mtd_mcal_mem_dev_s below.  All values originate from the
 * vendor configuration, filled by the binding layer.
 */

/* Each MTD device maps 1:1 onto exactly one MemAcc address area whose
 * logical address space starts at 0.  The in-device byte offset is
 * therefore the area-logical offset passed straight to
 * MemAcc_Read/Write/Erase -- no segment table or offset translation is
 * needed.  A physically split memory (PFLASH bank A vs bank B, DFLASH vs
 * each UCB) is exposed as several independent MTD devices, one per area,
 * rather than one device spanning several areas.
 */

/* The ioctl command MTDIOC_MCAL_MEM_HWSVC is private to the MCAL Mem MTD
 * adapter, so it is defined here rather than in the generic MTD header.  It
 * uses the top of the _MTDIOC nr range (0xff, the 8-bit _IOC_MASK maximum) to
 * stay clear of the generic MTDIOC_* commands, which grow upward from 0x01.
 * It carries a mtd_mcal_mem_hwsvc_s and is a thin passthrough to the AUTOSAR
 * MemAcc_HwSpecificService: the caller supplies the vendor's own hwServiceId
 * and hwId (it includes the vendor Mem header to build the payload), so the
 * generic core forwards the request to MemAcc verbatim and carries no vendor
 * service enum.
 *
 * The core issues the job on the device's own MemAcc area and drives it to
 * completion through the shared poll loop (MemAcc area id and Mem instance are
 * decoupled, so a service that targets another instance -- e.g. the OTA swap
 * UCB -- still queues on this area and completes there).  The vendor writes
 * any output into the caller's data buffer while the job runs; result carries
 * the MemAcc job-result code for diagnostics.
 *
 * NOTE: data is a caller-space pointer, so MTDIOC_MCAL_MEM_HWSVC is currently
 * supported on FLAT builds only (a PROTECTED/KERNEL build would need address
 * translation of the buffer).
 */

#define MTDIOC_MCAL_MEM_HWSVC _MTDIOC(0xff)

struct mtd_mcal_mem_hwsvc_s
{
  uint32_t  hw_service_id; /* in:  vendor MemAcc hwServiceId, e.g. an
                            *      MEM_17_NVM_HWSID_* value; forwarded verbatim */
  uint32_t  hw_id_area;    /* in:  MemAcc area id whose Mem instance the service
                            *      runs on; the core resolves it to the concrete
                            *      MemAcc hwId via MemAcc_GetMemoryInfo */
  FAR void *data;          /* in/out: service payload, caller-typed and owned */
  uint32_t  length;        /* in:  payload length in bytes */
  uint32_t  result;        /* out: MemAcc_JobResultType of the completed job
                            *      (diagnostics; 0 == MEMACC_OK) */
};

/* A generic MTD device backed by one MemAcc address area.  The binding
 * layer allocates this statically (one per MTD device), fills the area id,
 * geometry, hook context and hooks, and hands it to
 * mtd_mcal_mem_register(), which validates it and wires up the MTD ops.
 * The mtd member must be first so a (struct mtd_dev_s *) aliases the
 * device.
 *
 * The binding hooks cover the concerns MemAcc does not.  Each pre/after
 * pair brackets the corresponding MemAcc operation and is an escape hatch
 * for memories that need a vendor-specific workaround (a silicon erratum or
 * a buggy access path) around that operation.  A pair is only consulted
 * when its CONFIG_MCAL_MEM_<OP>_HOOKS option is enabled, and even then only
 * if the binding installs a non-NULL function; otherwise the operation runs
 * bare.  All hooks receive the MemAcc area id and the area-logical
 * offset/length of the access (MemAcc_AddressType / MemAcc_LengthType), so
 * the binding can resolve the physical bank (via MemAcc_GetMemoryInfo).
 *
 *   - pre_read/after_read   (CONFIG_MCAL_MEM_READ_HOOKS): bracket each read.
 *   - pre_write/after_write (CONFIG_MCAL_MEM_WRITE_HOOKS): bracket each
 *     write.  On AURIX PRRAM this carries the erratum NVMR_TC.003
 *     workaround: the CPU prefetch buffer of the written bank is disabled
 *     before the write and re-enabled after, and the just-written range is
 *     read back while still disabled, otherwise a later read of a rewritten
 *     address may return stale prefetched data.
 *   - pre_erase/after_erase (CONFIG_MCAL_MEM_ERASE_HOOKS): bracket each
 *     erase.
 *   - hwsvc: OPTIONAL override for MTDIOC_MCAL_MEM_HWSVC.  By default the core
 *     forwards the request straight to MemAcc_HwSpecificService (the standard
 *     path) and no binding code runs.  A binding only installs this hook to
 *     handle a vendor-private service that is NOT expressible through the
 *     standard MemAcc hardware-service API; when non-NULL it fully replaces
 *     the default MemAcc path for that ioctl.  Leave NULL for the common case.
 *
 * priv is an opaque binding context passed back to every hook.
 */

struct mtd_mcal_mem_dev_s
{
  struct mtd_dev_s          mtd;        /* MTD interface (must be first) */
  struct mtd_geometry_s     geo;        /* Standard MTD geometry (blocksize =
                                         * page, erasesize = sector,
                                         * neraseblocks = sector count) */

  uint32_t                  area_id;    /* MemAcc address area id (1:1) */
  uint32_t                  base_addr;  /* Absolute base address (XIP/info,
                                         * UCB address resolution) */
  uint8_t                   erased_value; /* Value of erased cells (eFlash 0) */
  FAR void                 *priv;       /* Opaque binding hook context */

  /* Binding hooks (see the comment above).  Each pre/after pair is gated
   * by its CONFIG_MCAL_MEM_<OP>_HOOKS option in the core; a binding that
   * does not provide a hook leaves it NULL.  A pair is compiled in only
   * when its option is enabled, so a build with no hooks costs no space.
   */

#ifdef CONFIG_MCAL_MEM_READ_HOOKS
  CODE void (*pre_read)(FAR void *priv, uint32_t area_id,
                        MemAcc_AddressType off, MemAcc_LengthType len);
  CODE void (*after_read)(FAR void *priv, uint32_t area_id,
                          MemAcc_AddressType off, MemAcc_LengthType len);
#endif
#ifdef CONFIG_MCAL_MEM_WRITE_HOOKS
  CODE void (*pre_write)(FAR void *priv, uint32_t area_id,
                         MemAcc_AddressType off, MemAcc_LengthType len);
  CODE void (*after_write)(FAR void *priv, uint32_t area_id,
                           MemAcc_AddressType off, MemAcc_LengthType len);
#endif
#ifdef CONFIG_MCAL_MEM_ERASE_HOOKS
  CODE void (*pre_erase)(FAR void *priv, uint32_t area_id,
                         MemAcc_AddressType off, MemAcc_LengthType len);
  CODE void (*after_erase)(FAR void *priv, uint32_t area_id,
                           MemAcc_AddressType off, MemAcc_LengthType len);
#endif

  /* Hardware-busy query.  The core may only sleep (yield the CPU) while a
   * write/erase job is in flight if this hook reports the underlying memory
   * is genuinely executing a command on silicon; otherwise it busy-polls.
   * Returns true if the hardware backing the area is busy, false if idle.
   * A binding that cannot determine hardware busy state leaves this NULL,
   * which forces the core to busy-poll (never sleep).
   */

  CODE bool (*is_busy)(FAR void *priv, uint32_t area_id);

  CODE int  (*hwsvc)(FAR void *priv,
                     FAR struct mtd_mcal_mem_hwsvc_s *req);
};

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
 * Name: mtd_mcal_mem_register
 *
 * Description:
 *   Initialise a statically-allocated generic MTD device backed by a MemAcc
 *   address area and wire up its MTD operations.  The caller (binding layer)
 *   provides a device whose area_id / geo / priv / hooks fields are already
 *   populated; this routine validates them and installs the MTD op vector.
 *   The device translates synchronous block operations into asynchronous
 *   MemAcc jobs.  No dynamic memory is used; the device must outlive every
 *   access to the returned MTD.
 *
 * Input Parameters:
 *   dev - Statically-allocated device.  Required: non-zero geometry
 *         (sector_size / page_size / nsectors).
 *
 * Returned Value:
 *   &dev->mtd on success; NULL if the device is invalid.
 *
 ****************************************************************************/

FAR struct mtd_dev_s *
mtd_mcal_mem_register(FAR struct mtd_mcal_mem_dev_s *dev);

/****************************************************************************
 * Name: mtd_mcal_mem_geo_from_area
 *
 * Description:
 *   Fill an MTD device's geometry (dev->geo, dev->base_addr,
 *   dev->erased_value) from a MemAcc address area, using ONLY the standard
 *   MemAcc API (MemAcc_GetMemoryInfo) -- no vendor configuration types.  The
 *   area's sub-address areas (SAAs) are walked by logical address, exactly
 *   nsaa times, aggregating a multi-SAA area (e.g. an AURIX PFLASH bank of
 *   six PFLASHxx sub-areas) as well as a single-SAA area (a UCB, DFLASH0):
 *
 *     blocksize    = SAA write page size   (required uniform across the area)
 *     erasesize    = SAA erase sector size (required uniform across the area)
 *     neraseblocks = total area size / erasesize   (aggregated over all SAAs)
 *     base_addr    = lowest SAA physical start address
 *
 *   The geometry model string is fixed to "MCAL-MEM".  no_erase collapses the
 *   geometry for a memory with no erase operation (e.g. RRAM): erasesize is
 *   set to blocksize and neraseblocks scaled up by the same ratio, keeping
 *   erasesize * neraseblocks == total capacity while erasesize == blocksize
 *   marks "no erase" to the upper layers.
 *
 * Input Parameters:
 *   area_id  - MemAcc address area id (1:1 with the MTD device).
 *   nsaa     - Number of sub-address areas in the area (from the binding).
 *              Bounds the SAA walk; zero yields false.
 *   dev      - Device whose geo / base_addr / erased_value are filled in.
 *   no_erase - true for a no-erase memory (RRAM): collapse erasesize to the
 *              write page size.
 *
 * Returned Value:
 *   true on success with the geometry populated; false if the area id is
 *   unknown, nsaa is zero, or its SAA geometry is empty / inconsistent.
 *
 ****************************************************************************/

bool mtd_mcal_mem_geo_from_area(uint32_t area_id, size_t nsaa,
                                FAR struct mtd_mcal_mem_dev_s *dev,
                                bool no_erase);

/****************************************************************************
 * Name: mtd_mcal_mem_unregister
 *
 * Description:
 *   Detach a device previously initialised by mtd_mcal_mem_register().  The
 *   device is statically allocated by the binding, so this only clears the
 *   MTD op vector; no memory is freed.
 *
 ****************************************************************************/

void mtd_mcal_mem_unregister(FAR struct mtd_dev_s *dev);

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_NUTTX_MTD_MTD_MCAL_MEM_H */
