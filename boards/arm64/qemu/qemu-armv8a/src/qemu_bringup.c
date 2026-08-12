/****************************************************************************
 * boards/arm64/qemu/qemu-armv8a/src/qemu_bringup.c
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

#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <syslog.h>

#include <nuttx/arch.h>
#include <nuttx/nuttx.h>
#include <nuttx/fs/fs.h>
#include <nuttx/fdt.h>
#include <nuttx/rpmsg/rpmsg_port.h>

#ifdef CONFIG_RPMSG_UART
#  include <nuttx/serial/uart_rpmsg.h>
#endif

#ifdef CONFIG_RPTUN_BMP
#  include <debug.h>
#  include <nuttx/rptun/rptun_bmp.h>
#  include "arm64_gic.h"
#endif

#include "qemu-armv8a.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#ifndef QEMU_SPI_IRQ_BASE
#define QEMU_SPI_IRQ_BASE     32
#endif

#ifdef CONFIG_RPTUN_BMP
#define QEMU_RSC_TABLE_SIZE   0x10000
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

#ifdef CONFIG_RPTUN_BMP
struct qemu_rptun_rsc
{
  struct rptun_rsc_s rsc;
  uint8_t padding[QEMU_RSC_TABLE_SIZE - sizeof(struct rptun_rsc_s)];
};

/* One resource table per core0<->coreN link.  Lives in the shared image
 * .bss so both the master (core0) and the remote core see it at the same
 * physical address.
 */

static struct qemu_rptun_rsc g_rptun_rsc[CONFIG_NCPUS - 1];
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

  if (fdt == NULL)
    {
      return;
    }

#ifdef CONFIG_DRIVERS_VIRTIO_MMIO
  fdt_virtio_mmio_devices_register(fdt, QEMU_SPI_IRQ_BASE);
#endif

#ifdef CONFIG_PCI
  fdt_pci_ecam_register(fdt);
#endif

#ifdef CONFIG_IOEXPANDER_PL061
  fdt_pl061_devices_register(fdt, QEMU_SPI_IRQ_BASE);
#endif

#ifdef CONFIG_CAN_CTUCANFD
  fdt_ctucanfd_devices_register(fdt, QEMU_SPI_IRQ_BASE);
#endif

#if defined(CONFIG_TIMER) && defined(CONFIG_TIMER_SP804)
  fdt_sp804_devices_register(fdt, QEMU_SPI_IRQ_BASE);
#endif

#ifdef CONFIG_WATCHDOG_CMSDK
  fdt_cmsdk_wdt_devices_register(fdt, QEMU_SPI_IRQ_BASE);
#endif
}

#endif

/****************************************************************************
 * Name: rpmsg_port_uart_init
 ****************************************************************************/

#ifdef CONFIG_RPMSG_PORT_UART
static int rpmsg_port_uart_init(void)
{
  const char *remotecpu;
  const char *localcpu;
  int ret;

  if (strcmp(CONFIG_LIBC_HOSTNAME, "server") == 0)
    {
      localcpu = "server2";
      remotecpu = "proxy2";
    }
  else if (strcmp(CONFIG_LIBC_HOSTNAME, "proxy") == 0)
    {
      localcpu = "proxy2";
      remotecpu = "server2";
    }
  else
    {
      syslog(LOG_ERR, "ERROR: hostname must be server or proxy, now: %s\n",
             CONFIG_LIBC_HOSTNAME);
      return -EINVAL;
    }

  const struct rpmsg_port_config_s cfg =
    {
      .remotecpu = remotecpu,
      .txnum = 8,
      .rxnum = 8,
      .txlen = 2048,
      .rxlen = 2048,
    };

  ret = rpmsg_port_uart_initialize(&cfg, "/dev/ttyV0", localcpu);
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: Failed to initialize rpmsg port uart: %d\n", ret);
    }

  return ret;
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
 *
 * Description:
 *   Establish the RPTUN/BMP link between core0 (master) and core1 (remote)
 *   using GIC SGIs for the doorbell and a shared-memory resource table for
 *   the vrings.
 *
 ****************************************************************************/

static int qemu_rptun_init(void)
{
  int cpu = this_cpu();
  cpu_set_t cpuset;
  int ret = 0;

  CPU_ZERO(&cpuset);

  if (cpu == 0)
    {
      /* Master: set up one link to every secondary core.  Per link N the
       * doorbell SGI pair descends from the top (link 1 == SGI15/SGI14, as
       * on fvp-v8r) to stay clear of the low SGIs reserved for SMP.
       */

      char remote[16];
      int i;

      for (i = 1; i < CONFIG_NCPUS; i++)
        {
          FAR struct rptun_rsc_s *rsc = &g_rptun_rsc[i - 1].rsc;

          snprintf(remote, sizeof(remote), "core%d", i);
          rptun_setup_shmem(rsc, "core0", remote);

          CPU_ZERO(&cpuset);
          CPU_SET(i, &cpuset);
          ret = rptun_bmp_init(remote, true, rsc,
                               GIC_IRQ_SGI15 - 2 * (i - 1),   /* event   */
                               GIC_IRQ_SGI14 - 2 * (i - 1),   /* trigger */
                               cpuset);
          DEBUGASSERT(ret >= 0);
        }
    }
  else
    {
      /* Remote: mirror core0's link for this core (event/trigger swapped). */

      FAR struct rptun_rsc_s *rsc = &g_rptun_rsc[cpu - 1].rsc;

      CPU_SET(0, &cpuset);
      ret = rptun_bmp_init("core0", false, rsc,
                           GIC_IRQ_SGI14 - 2 * (cpu - 1),     /* event   */
                           GIC_IRQ_SGI15 - 2 * (cpu - 1),     /* trigger */
                           cpuset);
      DEBUGASSERT(ret >= 0);
    }

  return ret;
}
#endif /* CONFIG_RPTUN_BMP */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#ifdef CONFIG_RPMSG_UART

/****************************************************************************
 * Name: rpmsg_serialinit
 *
 * Description:
 *   Initialize the rpmsg-uart tunnels for this board (BMP topology).  core0
 *   owns the physical console and exposes one rpmsg-uart end per secondary
 *   core (/dev/ttyCOREn); each secondary core registers its own /dev/console
 *   over rpmsg back to core0 (devname == true).  Invoked automatically from
 *   drivers_initialize() on every core.  Board policy lives here (not
 *   arch/chip) so it never clashes with a vendor board's own definition.
 *
 ****************************************************************************/

void rpmsg_serialinit(void)
{
  char devname[16];

  if (this_cpu() == 0)
    {
      /* core0 owns the physical console and exposes one rpmsg-uart tunnel
       * (/dev/ttyCOREn) per secondary core.
       */

      char cpuname[16];
      int cpu;

      for (cpu = 1; cpu < CONFIG_NCPUS; cpu++)
        {
          snprintf(cpuname, sizeof(cpuname), "core%d", cpu);
          snprintf(devname, sizeof(devname), "CORE%d", cpu);
          uart_rpmsg_init(cpuname, devname, 256, false);
        }
    }
  else
    {
      /* Each secondary core registers its own /dev/console over rpmsg back
       * to core0.
       */

      snprintf(devname, sizeof(devname), "CORE%d", this_cpu());
      uart_rpmsg_init("core0", devname, 256, true);
    }
}

#endif /* CONFIG_RPMSG_UART */

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
  /* Mount the tmp file system */

  ret = nx_mount(NULL, CONFIG_LIBC_TMPDIR, "tmpfs", 0, NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "ERROR: Failed to mount tmpfs at /tmp: %d\n", ret);
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

#ifdef CONFIG_RPMSG_PORT_UART
  ret = rpmsg_port_uart_init();
  if (ret < 0)
    {
      syslog(LOG_ERR,
             "ERROR: Failed to initialize rpmsg port uart: %d\n", ret);
    }
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
