/****************************************************************************
 * include/nuttx/note/notebuf_driver.h
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

#ifndef __INCLUDE_NUTTX_NOTE_NOTEBUF_DRIVER_H
#define __INCLUDE_NUTTX_NOTE_NOTEBUF_DRIVER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/note/note_driver.h>
#include <nuttx/ring.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Public Types
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTEBUF
struct notebuf_driver_s
{
  struct note_driver_s driver;
  FAR struct note_driver_s *lower;
  spinlock_t lock;
  RING_DECLARE(ring, char, CONFIG_DRIVERS_NOTEBUF_BUFSIZE);
#if CONFIG_DRIVERS_NOTEBUF_FLUSH_DELAY > 0
  bool irqsafe;
  struct work_s work;
#endif
};

/****************************************************************************
 * Public Data
 ****************************************************************************/

extern const struct note_driver_ops_s g_notebuf_ops;
#endif

#endif /* __INCLUDE_NUTTX_NOTE_NOTEBUF_DRIVER_H */
