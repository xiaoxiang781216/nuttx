/****************************************************************************
 * include/nuttx/vhost/vhost.h
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

#ifndef __INCLUDE_NUTTX_VHOST_VHOST_H
#define __INCLUDE_NUTTX_VHOST_VHOST_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdbool.h>
#include <stdint.h>

#ifdef CONFIG_DRIVERS_VHOST

#include <nuttx/compiler.h>
#include <nuttx/fs/ioctl.h>
#include <nuttx/queue.h>
#include <nuttx/spinlock.h>
#include <nuttx/virtio/virtio-config.h>
#include <openamp/open_amp.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* VHost ioctl command definitions */

#define VHOSTIOC_DUMP         _VHOSTIOC(1)

/* Run a ping measurement.  arg is a struct vhost_ping_s, which names the
 * target device, since /dev/vhost is a single global node.
 */

#define VHOSTIOC_PING         _VHOSTIOC(2)

/****************************************************************************
 * Public Type Definitions
 ****************************************************************************/

/* Wrapper the vhost API to virtio API */

#define vhost_create_virtqueues virtio_create_virtqueues
#define vhost_delete_virtqueues virtio_delete_virtqueues
#define vhost_set_status        virtio_set_status
#define vhost_get_status        virtio_get_status
#define vhost_set_features      virtio_set_features
#define vhost_get_features      virtio_get_features
#define vhost_read_config       virtio_read_config
#define vhost_write_config      virtio_write_config

/* Vhost helper functions */

#define vhost_has_feature         virtio_has_feature
#define vhost_read_config_member  virtio_read_config_member
#define vhost_write_config_member virtio_write_config_member

/* Wrapper the struct vhost_device to struct virtio_device */

#define vhost_device            virtio_device

/* Called for each device visited by vhost_foreach_device().  Returning a
 * negative errno stops the walk and is handed back to the caller.
 */

typedef CODE int (*vhost_foreach_t)(FAR struct vhost_device *hdev,
                                    FAR void *arg);

struct vhost_driver
{
  uint32_t           device;   /* device id */
  CODE int         (*probe)(FAR struct vhost_device *hdev);
  CODE void        (*remove)(FAR struct vhost_device *hdev);
  CODE void        (*dump)(FAR struct vhost_device *hdev);
  struct dq_entry_s  node;
};

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

int vhost_register_device(FAR struct vhost_device *hdev);
int vhost_register_driver(FAR struct vhost_driver *hdrv);
int vhost_unregister_driver(FAR struct vhost_driver *hdrv);
int vhost_unregister_device(FAR struct vhost_device *hdev);
FAR void *vhost_get_next_buffer(FAR struct virtqueue *vq, uint16_t idx,
                                FAR uint16_t *next_idx,
                                FAR uint32_t *next_len);
int vhost_get_vq_buffers(FAR struct virtqueue *vq,
                         FAR struct virtqueue_buf *vb, size_t vbsize,
                         FAR size_t *vbcnt);

/****************************************************************************
 * Name: vhost_get_vq_buffers_lock
 ****************************************************************************/

static inline_function int
vhost_get_vq_buffers_lock(FAR struct virtqueue *vq,
                          FAR struct virtqueue_buf *vb, size_t vbsize,
                          FAR size_t *vbcnt, FAR spinlock_t *lock)
{
  irqstate_t flags;
  int ret;

  flags = spin_lock_irqsave_notrace(lock);
  ret = vhost_get_vq_buffers(vq, vb, vbsize, vbcnt);
  spin_unlock_irqrestore_notrace(lock, flags);

  return ret;
}

/****************************************************************************
 * Name: vhost_register_drivers
 ****************************************************************************/

void vhost_register_drivers(void);

/****************************************************************************
 * Name: vhost_foreach_device
 *
 * Description:
 *   Walk every device on the vhost bus whose device id is 'id' and which a
 *   driver has claimed, so hdev->priv is that driver's context.  The bus
 *   lock is held across the walk, which keeps a device from being removed
 *   underneath the handler.
 *
 * Input Parameters:
 *   id      - Device id to match
 *   handler - Called once per matching device
 *   arg     - Opaque argument passed to handler
 *
 * Returned Value:
 *   Zero on success, or the first negative errno a handler returned.
 *
 ****************************************************************************/

int vhost_foreach_device(uint32_t id, vhost_foreach_t handler,
                         FAR void *arg);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* CONFIG_DRIVERS_VHOST */

#endif /* __INCLUDE_NUTTX_VHOST_VHOST_H */
