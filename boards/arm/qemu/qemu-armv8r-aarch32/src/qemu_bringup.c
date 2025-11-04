/****************************************************************************
 * boards/arm/qemu/qemu-armv8r-aarch32/src/qemu_bringup.c
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

#include <nuttx/config.h>

#include <sys/types.h>
#include <syslog.h>
#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/nuttx.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fdt.h>
#include <nuttx/rptun/rptun_bmp.h>
#include <nuttx/serial/uart_rpmsg.h>

#ifdef CONFIG_LIBC_FDT
#  include <libfdt.h>
#endif

#include "chip.h"
#include "gic.h"
#include "qemu-armv8r.h"

#ifdef CONFIG_IOEXPANDER_PL061
#  include <nuttx/ioexpander/ioexpander.h>
#  include <nuttx/ioexpander/pl061.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef QEMU_SPI_IRQ_BASE
#  define QEMU_SPI_IRQ_BASE     32
#endif

#ifdef CONFIG_IOEXPANDER_PL061
#  define QEMU_PWR_PL061_BASE   0x90b0000
#  define QEMU_RESET_PIN        1
#endif

#ifdef CONFIG_RPTUN_BMP
#  define QEMU_RSC_TABLE_SIZE     0x10000
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct restart_gpio_s            /* Data needed to init pl061 IOE */
{
  uintptr_t base;
  uint32_t pin;
};

#ifdef CONFIG_RPTUN_BMP
struct qemu_rptun_rsc
{
  struct rptun_rsc_s rsc;
  uint8_t padding[QEMU_RSC_TABLE_SIZE - sizeof(struct rptun_rsc_s)];
};
static struct qemu_rptun_rsc g_rptun_rsc[3];
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)

/****************************************************************************
 * Name: register_devices_from_fdt
 ****************************************************************************/

static void register_devices_from_fdt(void)
{
  const void *fdt = fdt_get();
  int ret;

  if (fdt == NULL)
    {
      return;
    }

#ifdef CONFIG_DRIVERS_VIRTIO_MMIO
  ret = fdt_virtio_mmio_devices_register(fdt, QEMU_SPI_IRQ_BASE);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_virtio_mmio_devices_register failed, ret=%d\n",
             ret);
    }
#endif

#ifdef CONFIG_PCI
  ret = fdt_pci_ecam_register(fdt);
  if (ret < 0)
    {
      syslog(LOG_ERR, "fdt_pci_ecam_register failed, ret=%d\n", ret);
    }
#endif

  UNUSED(ret);
}

/****************************************************************************
 * Name: fdt_restart_gpio
 *
 * Description:
 *   This function finds the gpio power device from device tree, for
 *   use with arm virt board.
 *
 * Input Parameters:
 *   fdt   - Device tree handle
 *
 * Output Parameters:
 *   restart   - GPIO power device info
 *
 * Returned Value:
 *   Return 0 if success, nageative if failed
 *
 ****************************************************************************/

static int fdt_restart_gpio(FAR const void *fdt,
                            struct restart_gpio_s *restart)
{
  uintptr_t addr;
  uint32_t  pin;
  const uint32_t *prop;
  int offset = -1;

  if (restart)
    {
      offset = fdt_node_offset_by_compatible(fdt, offset, "gpio-restart");
      if (offset == -FDT_ERR_NOTFOUND)
        {
          return -ENODEV;
        }

      prop = fdt_getprop(fdt, offset, "gpios", NULL);
      if (!prop)
        {
          return -ENODEV;
        }

      addr = fdt32_to_cpu(prop[0]);
      pin = fdt32_to_cpu(prop[1]);
      sinfo("phandle=%" PRIxPTR " pin=%" PRIu32 "\n", addr, pin);

      offset = fdt_node_offset_by_phandle(fdt, addr);
      if (offset == -FDT_ERR_NOTFOUND)
        {
          return -ENODEV;
        }

      addr = fdt_get_reg_base(fdt, offset, 0);
      sinfo("base=%" PRIxPTR "\n", addr);

      restart->base = addr;
      restart->pin = pin;
    }

  return OK;
}

#endif

#ifdef CONFIG_RPTUN_BMP

/****************************************************************************
 * Name: rptun_setup_shmem
 ****************************************************************************/

static void rptun_setup_shmem(struct rptun_rsc_s *rsc,
                              const char *host_cpuname,
                              const char *remote_cpuname)
{
  memset(rsc, 0, sizeof(struct rptun_rsc_s));
  rsc->offset[0]                = offsetof(struct rptun_rsc_s,
                                           rpmsg_vdev);
  rsc->rpmsg_vdev.type          = RSC_VDEV;
  rsc->rpmsg_vdev.id            = VIRTIO_ID_RPMSG;
  rsc->rpmsg_vdev.dfeatures     = 1 << VIRTIO_RPMSG_F_NS
                                | 1 << VIRTIO_RPMSG_F_ACK
                                | 1 << VIRTIO_RPMSG_F_BUFSZ
                                | 1 << VIRTIO_RPMSG_F_CPUNAME;
  rsc->rpmsg_vdev.num_of_vrings = 2;
  rsc->rpmsg_vdev.notifyid      = RSC_NOTIFY_ID_ANY;
  rsc->rpmsg_vdev.config_len    = sizeof(struct fw_rsc_config);
  rsc->rpmsg_vdev.reserved[0]   = VIRTIO_DEV_DRIVER;
  rsc->rpmsg_vdev.reserved[1]   = 0;
  rsc->rpmsg_vring0.align       = 8;
  rsc->rpmsg_vring0.num         = 8;
  rsc->rpmsg_vring0.notifyid    = RSC_NOTIFY_ID_ANY;
  rsc->rpmsg_vring0.da          = 0;
  rsc->rpmsg_vring1.align       = 8;
  rsc->rpmsg_vring1.num         = 8;
  rsc->rpmsg_vring1.notifyid    = RSC_NOTIFY_ID_ANY;
  rsc->rpmsg_vring1.da          = 0;
  rsc->config.r2h_buf_size      = 0x200;
  rsc->config.h2r_buf_size      = 0x200;
  strlcpy((char *)rsc->config.host_cpuname, host_cpuname,
          VIRTIO_RPMSG_CPUNAME_SIZE);
  strlcpy((char *)rsc->config.remote_cpuname, remote_cpuname,
          VIRTIO_RPMSG_CPUNAME_SIZE);

  rsc->offset[1]                = offsetof(struct rptun_rsc_s,
                                           carveout);
  rsc->carveout.type            = RSC_CARVEOUT;
  rsc->carveout.da              = (metal_phys_addr_t)rsc +
                                  ALIGN_UP(sizeof(struct rptun_rsc_s), 8);
  rsc->carveout.pa              = FW_RSC_U32_ADDR_ANY;
  rsc->carveout.len             = 0x200 * 8 * 2 + 0x1000;
  memcpy(rsc->carveout.name, "vdev0buffer", 11);

  rsc->rsc_tbl_hdr.ver          = 1;
  rsc->rsc_tbl_hdr.num          = 2;
  UP_DMB();
}

/****************************************************************************
 * Name: qemu_rptun_init
 ****************************************************************************/

static int qemu_rptun_init(void)
{
  struct rptun_rsc_s *rsc = &g_rptun_rsc[0].rsc;
  cpu_set_t cpuset;
  int ret = 0;

  CPU_ZERO(&cpuset);

  switch (this_cpu())
    {
      case 0:
        rptun_setup_shmem(rsc, "core0", "core1");
        CPU_SET(1, &cpuset);
        ret = rptun_bmp_init("core1", true, rsc, GIC_IRQ_SGI1,
                            GIC_IRQ_SGI2, cpuset);
        DEBUGASSERT(ret >= 0);

        rsc = &g_rptun_rsc[1].rsc;
        rptun_setup_shmem(rsc, "core0", "core2");
        CPU_ZERO(&cpuset);
        CPU_SET(2, &cpuset);
        ret = rptun_bmp_init("core2", true, rsc, GIC_IRQ_SGI3,
                            GIC_IRQ_SGI4, cpuset);
        DEBUGASSERT(ret >= 0);

        rsc = &g_rptun_rsc[2].rsc;
        rptun_setup_shmem(rsc, "core0", "core3");
        CPU_ZERO(&cpuset);
        CPU_SET(3, &cpuset);
        ret = rptun_bmp_init("core3", true, rsc, GIC_IRQ_SGI5,
                            GIC_IRQ_SGI6, cpuset);
        DEBUGASSERT(ret >= 0);
        break;
      case 1:
        rsc = &g_rptun_rsc[0].rsc;
        CPU_SET(0, &cpuset);
        ret = rptun_bmp_init("core0", false, rsc, GIC_IRQ_SGI2,
                            GIC_IRQ_SGI1, cpuset);
        DEBUGASSERT(ret >= 0);
        break;
      case 2:
        rsc = &g_rptun_rsc[1].rsc;
        CPU_SET(0, &cpuset);
        ret = rptun_bmp_init("core0", false, rsc, GIC_IRQ_SGI4,
                            GIC_IRQ_SGI3, cpuset);
        DEBUGASSERT(ret >= 0);
        break;
      case 3:
        rsc = &g_rptun_rsc[2].rsc;
        CPU_SET(0, &cpuset);
        ret = rptun_bmp_init("core0", false, rsc, GIC_IRQ_SGI6,
                            GIC_IRQ_SGI5, cpuset);
        DEBUGASSERT(ret >= 0);
        break;
      default:
        break;
    }

  return ret;
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_serialinit
 *
 * Description:
 *    Initialize the RPMsg UART driver
 *
 ****************************************************************************/

#ifdef CONFIG_RPMSG_UART
void rpmsg_serialinit(void)
{
  switch (this_cpu())
    {
      case 0:
        uart_rpmsg_init("core1", "CORE1", 256, false);
        uart_rpmsg_init("core2", "CORE2", 256, false);
        uart_rpmsg_init("core3", "CORE3", 256, false);
        break;
      case 1:
        uart_rpmsg_init("core0", "CORE1", 256, true);
        break;
      case 2:
        uart_rpmsg_init("core0", "CORE2", 256, true);
        break;
      case 3:
        uart_rpmsg_init("core0", "CORE3", 256, true);
        break;
      default:
        break;
    }
}
#endif

/****************************************************************************
 * Name: qemu_bringup
 *
 * Description:
 *   Bring up board features
 *
 ****************************************************************************/

int qemu_bringup(void)
{
  int ret;

#ifdef CONFIG_FS_TMPFS
  /* Mount the tmpfs file system */

  ret = nx_mount(NULL, CONFIG_LIBC_TMPDIR, "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount tmpfs at %s: %d\n",
             CONFIG_LIBC_TMPDIR, ret);
    }
#endif

#ifdef CONFIG_FS_PROCFS
  /* Mount the procfs file system */

  ret = nx_mount(NULL, "/proc", "procfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount procfs at /proc: %d\n", ret);
    }
#endif

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)
  register_devices_from_fdt();
#endif

#ifdef CONFIG_RPTUN_BMP
  ret = qemu_rptun_init();
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to init rptun: %d\n", ret);
    }
#endif

  UNUSED(ret);
  return OK;
}

/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reboot board
 *
 ****************************************************************************/

#ifdef CONFIG_BOARDCTL_RESET
int board_reset(int status)
{
#ifdef CONFIG_IOEXPANDER_PL061
  struct ioexpander_dev_s *ioe;
  struct restart_gpio_s restart =
  {
    QEMU_PWR_PL061_BASE,
    QEMU_RESET_PIN
  };

#if defined(CONFIG_LIBC_FDT) && defined(CONFIG_DEVICE_TREE)
  const void *fdt = fdt_get();

  if (fdt && fdt_restart_gpio(fdt, &restart) != OK)
    {
      swarn("ERROR: no pl061, use hardcoded settings\n");
    }
#endif

  ioe = pl061_ioe_initialize(restart.base, 0);
  if (ioe)
    {
      IOEXP_SETDIRECTION(ioe, restart.pin, IOEXPANDER_DIRECTION_OUT);
      IOEXP_WRITEPIN(ioe, restart.pin, 1);
    }
  else
    {
      syslog(LOG_ERR, "ERROR: Failed pl061 init: %x\n", restart.base);
    }
#endif

  UNUSED(status);
  return 0;
}
#endif
