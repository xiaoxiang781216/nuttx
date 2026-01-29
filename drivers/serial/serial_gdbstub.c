/****************************************************************************
 * drivers/serial/serial_gdbstub.c
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

#include <nuttx/serial/serial.h>
#include <nuttx/kmalloc.h>
#include <nuttx/gdbstub.h>

#include <fcntl.h>
#include <poll.h>
#include <string.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct uart_gdbstub_s
{
  FAR struct uart_dev_s *dev;
  FAR struct gdb_state_s *state;
  struct file file;
  struct pollfd fds;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct uart_gdbstub_s *g_uart_gdbstub;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#ifdef CONFIG_SERIAL_GDBSTUB_AUTO_ATTACH
static void uart_gdbstub_poll(FAR struct pollfd *fds)
{
  FAR struct uart_gdbstub_s *uart_gdbstub = fds->arg;

  if (fds->revents & POLLIN)
    {
      fds->revents &= ~POLLIN;
      uart_disablerxint(uart_gdbstub->dev);
      gdb_process(g_uart_gdbstub->state, GDB_STOPREASON_CTRLC, NULL);
      uart_enablerxint(uart_gdbstub->dev);
    }
}

static void uart_gdbstub_attach(FAR struct uart_gdbstub_s *uart_gdbstub)
{
  int ret;

  ret = file_open(&uart_gdbstub->file, CONFIG_SERIAL_GDBSTUB_PATH,
                  O_RDWR | O_NONBLOCK);
  if (ret >= 0)
    {
      uart_gdbstub->fds.arg = uart_gdbstub;
      uart_gdbstub->fds.events = POLLIN;
      uart_gdbstub->fds.cb = uart_gdbstub_poll;
      file_poll(&uart_gdbstub->file, &uart_gdbstub->fds, true);
    }
}
#endif

/****************************************************************************
 * Name: uart_gdbstub_receive
 *
 * Description:
 *   This is gdbstub receive char function.
 *
 ****************************************************************************/

static ssize_t uart_gdbstub_receive(FAR void *priv, FAR void *buf,
                                    size_t len)
{
  FAR struct uart_gdbstub_s *uart_gdbstub = priv;
  FAR uart_dev_t *dev = uart_gdbstub->dev;
  FAR struct uart_buffer_s *rxbuf = &dev->recv;
  FAR char *ptr = buf;
  unsigned int state;
  size_t tail = rxbuf->tail;
  size_t head = rxbuf->head;
  size_t avail = head >= tail ? head - tail : rxbuf->size - tail + head;
  size_t i = 0;

  while (i < len && avail-- > 0)
    {
      ptr[i++] = rxbuf->buffer[tail++];
      if (tail >= rxbuf->size)
        {
          tail = 0;
        }
    }

  rxbuf->tail = tail;

  while (i < len && dev->ops->rxavailable(dev))
    {
      if (dev->ops->recvbuf)
        {
          i += dev->ops->recvbuf(dev, ptr + i, len - i);
        }
      else
        {
          ptr[i++] = dev->ops->receive(dev, &state);
        }
    }

  return i;
}

/****************************************************************************
 * Name: uart_gdbstub_send
 *
 * Description:
 *   This is gdbstub send char function.
 *
 ****************************************************************************/

static ssize_t uart_gdbstub_send(FAR void *priv, FAR const void *buf,
                                 size_t len)
{
  FAR struct uart_gdbstub_s *uart_gdbstub = priv;
  FAR uart_dev_t *dev = uart_gdbstub->dev;
  FAR const char *ptr = buf;
  size_t i = 0;

  while (i < len)
    {
      if (dev->ops->txready(dev))
        {
          if (dev->ops->sendbuf)
            {
              i += dev->ops->sendbuf(dev, ptr + i, len - i);
            }
          else
            {
              dev->ops->send(dev, ptr[i++]);
            }
        }
    }

  while (!dev->ops->txempty(dev));

  return len;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: uart_gdbstub_register
 *
 * Description:
 *   Use the uart device to register gdbstub.
 *   gdbstub run with serial interrupt.
 *
 ****************************************************************************/

int uart_gdbstub_register(FAR uart_dev_t *dev, FAR const char *path)
{
  FAR struct uart_gdbstub_s *uart_gdbstub;
  struct gdb_state_init_s init =
    {
      0
    };

  if (g_uart_gdbstub == NULL)
    {
      uart_gdbstub = kmm_zalloc(sizeof(struct uart_gdbstub_s));
      if (uart_gdbstub == NULL)
        {
          return -ENOMEM;
        }

      g_uart_gdbstub = uart_gdbstub;
    }
  else
    {
      uart_gdbstub = g_uart_gdbstub;
    }

  if (strcmp(path, CONFIG_SERIAL_GDBSTUB_PATH) != 0)
    {
      return 0;
    }

  init.send          = uart_gdbstub_send;
  init.recv          = uart_gdbstub_receive;
  init.packet        = dev->ops->recvbuf != NULL;
  init.pkt_size      = CONFIG_SERIAL_GDBSTUB_PKTSIZE;
  init.priv          = uart_gdbstub;
  init.panic_timeout = CONFIG_SERIAL_GDBSTUB_PANIC_TIMEOUT;

#ifdef CONFIG_SERIAL_GDBSTUB_AUTO_ATTACH
  uart_gdbstub_attach(uart_gdbstub);
#endif

#ifdef CONFIG_SERIAL_GDBSTUB_WAIT_CONNECT
  init.wait = true;
  uart_disablerxint(dev);
#endif

  uart_gdbstub->dev = dev;
  uart_gdbstub->state = gdb_state_init(&init);
  if (uart_gdbstub->state == NULL)
    {
      kmm_free(uart_gdbstub);
      return -ENOMEM;
    }

#ifdef CONFIG_SERIAL_GDBSTUB_WAIT_CONNECT
  uart_enablerxint(dev);
#endif

  return 0;
}
