/****************************************************************************
 * include/nuttx/rpmsg/rpmsg_note.h
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

#ifndef __DRIVERS_RPMSG_RPMSG_NOTE_H
#define __DRIVERS_RPMSG_RPMSG_NOTE_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <openamp/rpmsg.h>
#include <openamp/rpmsg_internal.h>

#ifdef CONFIG_RPMSG_NOTE

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

int rpmsg_note_initialize(void);
void rpmsg_note_receive(FAR const struct rpmsg_hdr *hdr,
                        FAR const struct rpmsg_endpoint *ept);
void rpmsg_note_send(FAR struct rpmsg_device *rdev,
                     uint32_t src, uint32_t dst,
                     const void *data, int len);
void rpmsg_note_binary(FAR const char *name,
                       FAR const void *buf, size_t len);
void rpmsg_note_printf(FAR const char *name, FAR const char *format, ...);
void rpmsg_note_vprintf(FAR const char *name,
                        FAR const char *format, va_list ap);
#else
#  define rpmsg_note_initialize()
#  define rpmsg_note_receive(hdr, ept)
#  define rpmsg_note_send(rdev, src, dst, data, len)
#  define rpmsg_note_binary(name, buf, len) \
     do \
       { \
         UNUSED(name); \
         UNUSED(buf); \
         UNUSED(len); \
       } \
     while (0)
#  define rpmsg_note_printf(name, format, ...) \
     do \
       { \
         UNUSED(name); \
         UNUSED(format); \
       } \
     while (0)
#  define rpmsg_note_vprintf(name, format, ap) \
     do \
       { \
         UNUSED(name); \
         UNUSED(format); \
         UNUSED(ap); \
       } \
     while (0)
#endif /* CONFIG_RPMSG_NOTE */

#endif /* __DRIVERS_RPMSG_RPMSG_NOTE_H */
