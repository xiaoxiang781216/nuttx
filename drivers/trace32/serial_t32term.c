/****************************************************************************
 * drivers/trace32/serial_t32term.c
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

#include <assert.h>
#include <sys/types.h>
#include <syslog.h>

#include <nuttx/kmalloc.h>
#include <nuttx/serial/serial.h>
#include <nuttx/wdog.h>

#include "t32term/t32term.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

struct serial_t32term_s
{
  struct uart_dev_s uart;
  struct wdog_s wdog;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  serial_t32term_setup(FAR struct uart_dev_s *dev);
static void serial_t32term_shutdown(FAR struct uart_dev_s *dev);
static int  serial_t32term_attach(FAR struct uart_dev_s *dev);
static void serial_t32term_detach(FAR struct uart_dev_s *dev);
static int  serial_t32term_ioctl(FAR struct file *filep, int cmd,
                                 unsigned long arg);
static int  serial_t32term_receive(FAR struct uart_dev_s *dev,
                                   FAR unsigned int *status);
static void serial_t32term_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool serial_t32term_rxavailable(FAR struct uart_dev_s *dev);
static void serial_t32term_dmasend(FAR struct uart_dev_s *dev);
static void serial_t32term_dmareceive(FAR struct uart_dev_s *dev);
static void serial_t32term_dmarxfree(FAR struct uart_dev_s *dev);
static void serial_t32term_dmatxavail(FAR struct uart_dev_s *dev);
static void serial_t32term_send(FAR struct uart_dev_s *dev, int ch);
static void serial_t32term_txint(FAR struct uart_dev_s *dev, bool enable);
static bool serial_t32term_txready(FAR struct uart_dev_s *dev);
static bool serial_t32term_txempty(FAR struct uart_dev_s *dev);

static void serial_t32term_timeout(wdparm_t arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_serial_t32term_ops =
{
  serial_t32term_setup,
  serial_t32term_shutdown,
  serial_t32term_attach,
  serial_t32term_detach,
  serial_t32term_ioctl,
  serial_t32term_receive,
  serial_t32term_rxint,
  serial_t32term_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  NULL,
#endif
  serial_t32term_dmasend,
  serial_t32term_dmareceive,
  serial_t32term_dmarxfree,
  serial_t32term_dmatxavail,
  serial_t32term_send,
  serial_t32term_txint,
  serial_t32term_txready,
  serial_t32term_txempty,
};

static char
g_serial_t32term_xmit_buffer[CONFIG_TRACE32_TERM_MEMORY_BLOCKED_SIZE];

static char
g_serial_t32term_recv_buffer[CONFIG_TRACE32_TERM_MEMORY_BLOCKED_SIZE];

static struct serial_t32term_s g_serial_t32term =
{
  .uart =
  {
#ifdef CONFIG_SERIAL_T32TERM_CONSOLE
    .isconsole = true,
#endif
    .recv =
    {
      .buffer = g_serial_t32term_recv_buffer,
      .size = CONFIG_TRACE32_TERM_MEMORY_BLOCKED_SIZE,
    },
    .xmit =
    {
      .buffer = g_serial_t32term_xmit_buffer,
      .size = CONFIG_TRACE32_TERM_MEMORY_BLOCKED_SIZE,
    },
    .ops = &g_serial_t32term_ops,
    .priv = &g_serial_t32term,
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: serial_t32term_setup
 ****************************************************************************/

static int serial_t32term_setup(FAR struct uart_dev_s *dev)
{
  return OK;
}

/****************************************************************************
 * Name: serial_t32term_shutdown
 ****************************************************************************/

static void serial_t32term_shutdown(FAR struct uart_dev_s *dev)
{
}

/****************************************************************************
 * Name: serial_t32term_attach
 ****************************************************************************/

static int serial_t32term_attach(FAR struct uart_dev_s *dev)
{
  FAR struct serial_t32term_s *term = dev->priv;
  wd_start(&term->wdog, USEC2TICK(CONFIG_SERIAL_T32TERM_POLLING_INTERVAL),
           serial_t32term_timeout, (wdparm_t)dev);
  return OK;
}

/****************************************************************************
 * Name: serial_t32term_detach
 ****************************************************************************/

static void serial_t32term_detach(FAR struct uart_dev_s *dev)
{
  FAR struct serial_t32term_s *term = dev->priv;
  wd_cancel(&term->wdog);
}

/****************************************************************************
 * Name: serial_t32term_ioctl
 ****************************************************************************/

static int serial_t32term_ioctl(FAR struct file *filep, int cmd,
                                unsigned long arg)
{
  return -ENOTTY;
}

/****************************************************************************
 * Name: serial_t32term_receive
 ****************************************************************************/

static int serial_t32term_receive(FAR struct uart_dev_s *dev,
                                  FAR unsigned int *status)
{
  uint8_t ch;

  T32_Term_GetBlock(&ch, 1);
  return ch;
}

/****************************************************************************
 * Name: serial_t32term_rxint
 ****************************************************************************/

static void serial_t32term_rxint(FAR struct uart_dev_s *dev, bool enable)
{
}

/****************************************************************************
 * Name: serial_t32term_rxavailable
 ****************************************************************************/

static bool serial_t32term_rxavailable(FAR struct uart_dev_s *dev)
{
  return T32_Term_IsReadable();
}

/****************************************************************************
 * Name: serial_t32term_dmasend
 ****************************************************************************/

static void serial_t32term_dmasend(FAR struct uart_dev_s *dev)
{
  FAR struct uart_dmaxfer_s *xfer = &dev->dmatx;

  T32_Term_PutBlock((const unsigned char *)xfer->buffer, xfer->length);
  xfer->nbytes = xfer->length;

  if (xfer->nlength)
    {
      T32_Term_PutBlock((const unsigned char *)xfer->nbuffer, xfer->nlength);
      xfer->nbytes += xfer->nlength;
    }

  uart_xmitchars_done(dev);
}

/****************************************************************************
 * Name: serial_t32term_dmareceive
 ****************************************************************************/

static void serial_t32term_dmareceive(FAR struct uart_dev_s *dev)
{
  FAR struct uart_dmaxfer_s *xfer = &dev->dmarx;
  size_t len;

  len = T32_Term_GetBlockNoWait((unsigned char *)xfer->buffer, xfer->length);
  xfer->nbytes = len;

  uart_recvchars_done(dev);
}

/****************************************************************************
 * Name: serial_t32term_dmarxfree
 ****************************************************************************/

static void serial_t32term_dmarxfree(FAR struct uart_dev_s *dev)
{
  /* When the DMA buffer is empty, check whether there is data to read */

  if (serial_t32term_rxavailable(dev))
    {
      uart_recvchars_dma(dev);
    }
}

/****************************************************************************
 * Name: serial_t32term_dmatxavail
 ****************************************************************************/

static void serial_t32term_dmatxavail(FAR struct uart_dev_s *dev)
{
  if (serial_t32term_txready(dev))
    {
      uart_xmitchars_dma(dev);
    }
}

/****************************************************************************
 * Name: serial_t32term_send
 ****************************************************************************/

static void serial_t32term_send(FAR struct uart_dev_s *dev, int ch)
{
  uint8_t c = ch;

  T32_Term_PutBlock(&c, 1);
}

/****************************************************************************
 * Name: serial_t32term_txint
 ****************************************************************************/

static void serial_t32term_txint(FAR struct uart_dev_s *dev, bool enable)
{
}

/****************************************************************************
 * Name: serial_t32term_txready
 ****************************************************************************/

static bool serial_t32term_txready(FAR struct uart_dev_s *dev)
{
  return T32_Term_IsWritable();
}

/****************************************************************************
 * Name: serial_t32term_txempty
 ****************************************************************************/

static bool serial_t32term_txempty(FAR struct uart_dev_s *dev)
{
  return T32_Term_IsWritable();
}

/****************************************************************************
 * Name: serial_t32term_timeout
 ****************************************************************************/

static void serial_t32term_timeout(wdparm_t arg)
{
  FAR struct serial_t32term_s *term = (FAR struct serial_t32term_s *)arg;

  serial_t32term_dmarxfree(&term->uart);
  serial_t32term_dmatxavail(&term->uart);
  wd_start(&term->wdog, USEC2TICK(CONFIG_SERIAL_T32TERM_POLLING_INTERVAL),
           serial_t32term_timeout, arg);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * serial_t32term_initialize
 ****************************************************************************/

void serial_t32term_initialize(void)
{
  uart_register("/dev/ttyT32TERM", &g_serial_t32term.uart);

#ifdef CONFIG_SERIAL_T32TERM_CONSOLE
  uart_register("/dev/console", &g_serial_t32term.uart);
#endif
}
