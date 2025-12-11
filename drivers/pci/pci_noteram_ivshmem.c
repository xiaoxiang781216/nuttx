/****************************************************************************
 * drivers/pci/pci_noteram_ivshmem.c
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

#include <debug.h>

#include <nuttx/nuttx.h>
#include <nuttx/kmalloc.h>
#include <nuttx/pci/pci_ivshmem.h>
#include <nuttx/note/noteram_driver.h>

#define NOTERAM_IVSHMEM_NAME "/dev/note/shm"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct noteram_ivshmem_dev_s
{
  struct ivshmem_driver_s  drv;
  FAR struct note_driver_s *note_drv;
};

#define noteram_ivshmem_from_ivdev(dev) \
  container_of(ivshmem_get_driver(dev), struct noteram_ivshmem_dev_s, drv)

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int noteram_ivshmem_probe(FAR struct ivshmem_device_s *ivdev)
{
  FAR struct noteram_ivshmem_dev_s *noteram;
  FAR void *base;
  size_t size;

  noteram = noteram_ivshmem_from_ivdev(ivdev);
  base = ivshmem_get_shmem(ivdev, &size);

  noteram->note_drv = noteram_initialize_with_buffer(NOTERAM_IVSHMEM_NAME,
                                                     base, size, true,
                                                     true, true);
  if (!noteram->note_drv)
    {
      pcierr("failed initialize noteram\n");
      return -ENOMEM;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static void noteram_ivshmem_remove(FAR struct ivshmem_device_s *ivdev)
{
  FAR struct noteram_ivshmem_dev_s *noteram;

  noteram = noteram_ivshmem_from_ivdev(ivdev);
  noteram_deinitialize(noteram->note_drv);
  kmm_free(noteram);
}

int pci_register_noteram_ivshmem_driver(void)
{
  FAR struct noteram_ivshmem_dev_s *noteram;
  int ret;

  noteram = kmm_zalloc(sizeof(*noteram));
  if (noteram == NULL)
    {
      pcierr("failed alloc noteram\n");
      return -ENOMEM;
    }

  noteram->drv.id = CONFIG_PCI_NOTERAM_IVSHMEM_IDX;
  noteram->drv.probe = noteram_ivshmem_probe;
  noteram->drv.remove = noteram_ivshmem_remove;

  ret = ivshmem_register_driver(&noteram->drv);
  if (ret < 0)
    {
      kmm_free(noteram);
    }

  return ret;
}
