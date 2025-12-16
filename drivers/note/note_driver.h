/****************************************************************************
 * drivers/note/note_driver.h
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

#ifndef __NUTTX_DRIVERS_NOTE_NOTE_DRIVER_H
#define __NUTTX_DRIVERS_NOTE_NOTE_DRIVER_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

#include <nuttx/fs/ioctl.h>
#include <nuttx/note/note_driver.h>
#include <nuttx/sched.h>
#include <nuttx/sched_note.h>
#include <nuttx/streams.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define NOTERPMSG_EPT_NAME           "rpmsg-note"

#define NOTE_BUFFER_SIZE 256
static_assert(NOTE_BUFFER_SIZE >= sizeof(struct note_event_s),
              "Buffer size too small");

/****************************************************************************
 * Public Types
 ****************************************************************************/

/****************************************************************************
 * Public Data
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTELOG
extern struct note_driver_s g_notelog_driver;
#endif

#ifdef CONFIG_DRIVERS_NOTERAM
extern struct noteram_driver_s g_noteram_driver;
#endif

#ifdef CONFIG_DRIVERS_NOTERPMSG
extern struct noterpmsg_driver_s g_noterpmsg_driver;
#endif

#ifdef CONFIG_DRIVERS_NOTELOWEROUT
extern struct notestream_driver_s g_notestream_lowerout;
#endif

#ifdef CONFIG_DRIVERS_NOTET32FDX
extern struct notefdx_s g_notefdx;
#endif

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: note_common
 *
 * Description:
 *   Fill in some of the common fields in the note structure.
 *
 * Input Parameters:
 *   tcb    - The TCB containing the information
 *   note   - The common note structure to use
 *   length - The total lengthof the note structure
 *   type   - The type of the note
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_common(FAR struct tcb_s *tcb, FAR struct note_common_s *note,
                 uint8_t length, uint8_t type);

/****************************************************************************
 * Name: note_get_taskname
 *
 * Description:
 *   Get the task name string of the specified PID
 *
 * Input Parameters:
 *   PID - Task ID
 *   buf - A writable buffer to hold the task name
 *   len - The length of the buffer
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

void note_get_taskname(pid_t pid, FAR char *buf, size_t len);

/****************************************************************************
 * Name: notectl_register
 *
 * Description:
 *   Register a driver at /dev/notectl that can be used by an application to
 *   control the note filter.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTECTL
int notectl_register(void);
#endif

/****************************************************************************
 * Name: noteram_register
 *
 * Description:
 *   Register RAM note driver at /dev/note/ram that can be used by an
 *   application to read note data from the circular note buffer.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Zero on succress. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERAM
int noteram_register(void);
#endif

/****************************************************************************
 * Name: notesnap_register
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTESNAP
int notesnap_register(void);

/****************************************************************************
 * Name: notesnap_dump_with_stream
 ****************************************************************************/

void notesnap_dump_with_stream(FAR struct lib_outstream_s *stream);
#endif

/****************************************************************************
 * Name: noterpmsg_server_init
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERPMSG_SERVER
int noterpmsg_server_init(void);
#endif

/****************************************************************************
 * Name: noterpmsg_init
 ****************************************************************************/

#ifdef CONFIG_DRIVERS_NOTERPMSG
int noterpmsg_init(void);
#endif

#endif /* __NUTTX_DRIVERS_NOTE_NOTE_DRIVER_H */
