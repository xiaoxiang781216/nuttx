/****************************************************************************
 * drivers/fault/wraps/fault_wrap_irq.c
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

#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>

#include <nuttx/fault_inject.h>

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

int  __real_irq_attach(int irq, xcpt_t isr, FAR void *arg);
int  __real_irq_attach_thread(int irq, xcpt_t isr, xcpt_t isrthread,
                              FAR void *arg, int priority, int stack_size);
int  __real_irq_attach_wqueue(int irq, xcpt_t isr, xcpt_t isrwork,
                              FAR void *arg, int priority);
void __real_up_enable_irq(int irq);
void __real_up_disable_irq(int irq);

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap_irq_attach
 ****************************************************************************/

int __wrap_irq_attach(int irq, xcpt_t isr, FAR void *arg)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(irq, irq_attach, -EIO,
                           __real_irq_attach(irq, isr, arg), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_irq_attach_thread
 ****************************************************************************/

int __wrap_irq_attach_thread(int irq, xcpt_t isr, xcpt_t isrthread,
                             FAR void *arg, int priority, int stack_size)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(irq, irq_attach_thread, -EIO,
                           __real_irq_attach_thread(irq, isr, isrthread,
                                                    arg, priority,
                                                    stack_size), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_irq_attach_wqueue
 ****************************************************************************/

int __wrap_irq_attach_wqueue(int irq, xcpt_t isr, xcpt_t isrwork,
                             FAR void *arg, int priority)
{
  int ret;
  FAULT_INJECT_WRAP_RETVAL(irq, irq_attach_wqueue, -EIO,
                           __real_irq_attach_wqueue(irq, isr, isrwork,
                                                    arg, priority), &ret);
  return ret;
}

/****************************************************************************
 * Name: __wrap_up_enable_irq
 ****************************************************************************/

void __wrap_up_enable_irq(int irq)
{
  FAULT_INJECT_WRAP_RETVAL_VOID(irq, up_enable_irq,
                                __real_up_enable_irq(irq));
}

/****************************************************************************
 * Name: __wrap_up_disable_irq
 ****************************************************************************/

void __wrap_up_disable_irq(int irq)
{
  FAULT_INJECT_WRAP_RETVAL_VOID(irq, up_disable_irq,
                                __real_up_disable_irq(irq));
}
