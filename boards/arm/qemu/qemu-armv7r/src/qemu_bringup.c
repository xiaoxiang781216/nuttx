/****************************************************************************
 * boards/arm/qemu/qemu-armv7r/src/qemu_bringup.c
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

#include <nuttx/fs/fs.h>
#include <nuttx/fdt.h>
#include <nuttx/virtio/virtio-mmio.h>

#ifdef CONFIG_LIBC_FDT
#  include <libfdt.h>
#endif

#include "chip.h"
#include "qemu-armv7r.h"

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

#define QEMU_VIRTIO_MMIO_BASE    0x0a000000
#define QEMU_VIRTIO_MMIO_REGSIZE 0x200
#define QEMU_VIRTIO_MMIO_IRQ     (QEMU_SPI_IRQ_BASE + 16)
#define QEMU_VIRTIO_MMIO_NUM     32

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct restart_gpio_s            /* Data needed to init pl061 IOE */
{
  uintptr_t base;
  uint32_t pin;
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_VIRTIO_MMIO
static void qemu_virtio_register_mmio_devices(void)
{
  uintptr_t virtio = (uintptr_t)QEMU_VIRTIO_MMIO_BASE;
  size_t size = QEMU_VIRTIO_MMIO_REGSIZE;
  int irq = QEMU_VIRTIO_MMIO_IRQ;
  int i;

  for (i = 0; i < QEMU_VIRTIO_MMIO_NUM; i++)
    {
      virtio_register_mmio_device((void *)(virtio + size * i), irq + i);
    }
}
#endif

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

/****************************************************************************
 * Public Functions
 ****************************************************************************/

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
#elif defined(CONFIG_DRIVERS_VIRTIO_MMIO)
  qemu_virtio_register_mmio_devices();
#endif

  UNUSED(ret);
  return OK;
}

#ifdef CONFIG_BOARDCTL_RESET
/****************************************************************************
 * Name: board_reset
 *
 * Description:
 *   Reboot board
 *
 ****************************************************************************/

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
