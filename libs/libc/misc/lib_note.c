/****************************************************************************
 * libs/libc/misc/lib_note.c
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

#include <nuttx/ratelimit.h>
#include <nuttx/sched_note.h>

#ifdef CONFIG_SCHED_INSTRUMENTATION_DUMP

/****************************************************************************
 * Public Data
 ****************************************************************************/

DEFINE_RATELIMIT_STATE(g_note_ratelimit,
  CONFIG_DRIVERS_NOTE_RATELIMIT_INTERVAL,
  CONFIG_DRIVERS_NOTE_RATELIMIT_BURST);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

#endif /* CONFIG_SCHED_INSTRUMENTATION_DUMP */
