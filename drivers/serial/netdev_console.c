/****************************************************************************
 * drivers/serial/netdev_console.c
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
#include <debug.h>

#include <net/if.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include <nuttx/signal.h>
#include <nuttx/net/net.h>
#include <nuttx/serial/serial.h>
#include <nuttx/wqueue.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#define CONSOLE_WORK_DELAY USEC2TICK(CONFIG_CONSOLE_NETDEV_DELAY_USEC)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct netdev_consol_s
{
  struct uart_dev_s dev;
  struct socket     psock;
  bool              rxint;
  struct work_s     work;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  netdev_console_setup(FAR struct uart_dev_s *dev);
static void netdev_console_shutdown(FAR struct uart_dev_s *dev);
static int  netdev_console_attach(FAR struct uart_dev_s *dev);
static void netdev_console_detach(FAR struct uart_dev_s *dev);
static void netdev_console_rxint(FAR struct uart_dev_s *dev, bool enable);
#ifdef CONFIG_SERIAL_IFLOWCONTROL
static bool netdev_console_rxflowcontrol(FAR struct uart_dev_s *dev,
                                         unsigned int nbuffered,
                                         bool upper);
#endif
static void netdev_console_dmasend(FAR struct uart_dev_s *dev);
static void netdev_console_dmareceive(FAR struct uart_dev_s *dev);
static void netdev_console_dmarxfree(FAR struct uart_dev_s *dev);
static void netdev_console_dmatxavail(FAR struct uart_dev_s *dev);
static void netdev_console_send(FAR struct uart_dev_s *dev, int ch);
static void netdev_console_txint(FAR struct uart_dev_s *dev, bool enable);
static bool netdev_console_txready(FAR struct uart_dev_s *dev);
static bool netdev_console_txempty(FAR struct uart_dev_s *dev);
static void netdev_console_work(FAR void *arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static char g_netdev_console_rxbuf[CONFIG_CONSOLE_NETDEV_RXBUFSIZE];
static char g_netdev_console_txbuf[CONFIG_CONSOLE_NETDEV_TXBUFSIZE];

static const struct uart_ops_s g_netdev_console_ops =
{
  .setup         = netdev_console_setup,
  .shutdown      = netdev_console_shutdown,
  .attach        = netdev_console_attach,
  .detach        = netdev_console_detach,
  .rxint         = netdev_console_rxint,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  .rxflowcontrol = netdev_console_rxflowcontrol,
#endif
  .dmasend       = netdev_console_dmasend,
  .dmareceive    = netdev_console_dmareceive,
  .dmarxfree     = netdev_console_dmarxfree,
  .dmatxavail    = netdev_console_dmatxavail,
  .send          = netdev_console_send,
  .txint         = netdev_console_txint,
  .txready       = netdev_console_txready,
  .txempty       = netdev_console_txempty,
};

static struct netdev_consol_s g_netdev_console =
{
  .dev =
  {
    .ops = &g_netdev_console_ops,
    .xmit =
    {
      .size   = CONFIG_CONSOLE_NETDEV_TXBUFSIZE,
      .buffer = g_netdev_console_rxbuf,
    },
    .recv =
    {
      .size   = CONFIG_CONSOLE_NETDEV_RXBUFSIZE,
      .buffer = g_netdev_console_txbuf,
    },
    .isconsole = true
  },
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_console_setup
 ****************************************************************************/

static int netdev_console_setup(FAR struct uart_dev_s *dev)
{
  return OK;
}

/****************************************************************************
 * Name: netdev_console_shutdown
 ****************************************************************************/

static void netdev_console_shutdown(FAR struct uart_dev_s *dev)
{
}

/****************************************************************************
 * Name: netdev_console_attach
 ****************************************************************************/

static int netdev_console_attach(FAR struct uart_dev_s *dev)
{
  FAR struct netdev_consol_s *priv = (FAR struct netdev_consol_s *)dev;
  struct sockaddr_in          addr;
  int                         ret;

  /* Initialize UDP socket for CONSOLE TX/RX */

  ret = psock_socket(PF_INET, SOCK_DGRAM, 0, &priv->psock);
  if (ret < 0)
    {
      nerr("ERROR: Failed to create a socket: %d\n", ret);
      return ret;
    }

  /* Bind with source socket */

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(CONFIG_CONSOLE_NETDEV_IPADDR);
  addr.sin_port = htons(CONFIG_CONSOLE_NETDEV_PORT);

  ret = psock_connect(&priv->psock, (FAR struct sockaddr *)&addr,
                      sizeof(addr));
  if (ret < 0)
    {
      psock_close(&priv->psock);
      nerr("ERROR: Failed to connect socket: %d\n", ret);
      return ret;
    }

  return OK;
}

/****************************************************************************
 * Name: netdev_console_detach
 ****************************************************************************/

static void netdev_console_detach(FAR struct uart_dev_s *dev)
{
  FAR struct netdev_consol_s *priv = (FAR struct netdev_consol_s *)dev;

  psock_close(&priv->psock);
}

/****************************************************************************
 * Name: netdev_console_rxint
 ****************************************************************************/

static void netdev_console_rxint(FAR struct uart_dev_s *dev, bool enable)
{
  FAR struct netdev_consol_s *priv = (FAR struct netdev_consol_s *)dev;

  priv->rxint = enable;
  if (enable)
    {
      work_queue(LPWORK, &priv->work, netdev_console_work, dev, 0);
    }
  else
    {
      work_cancel(LPWORK, &priv->work);
    }
}

#ifdef CONFIG_SERIAL_IFLOWCONTROL
/****************************************************************************
 * Name: netdev_console_rxflowcontrol
 ****************************************************************************/

static bool netdev_console_rxflowcontrol(FAR struct uart_dev_s *dev,
                                         unsigned int nbuffered, bool upper)
{
  return false;
}
#endif

/****************************************************************************
 * Name: netdev_console_dmasend
 ****************************************************************************/

static void netdev_console_dmasend(FAR struct uart_dev_s *dev)
{
  FAR struct netdev_consol_s *priv  = (FAR struct netdev_consol_s *)dev;
  int                         retry = 3;
  ssize_t                     ret;

  do
    {
      ret = psock_send(&priv->psock, dev->dmatx.buffer, dev->dmatx.length,
                       0);

      /* Wait for ARP */

      if (ret < 0)
        {
          nxsig_sleep(1);
        }
    }
  while (ret < 0 && retry--);

  dev->dmatx.nbytes = ret > 0 ? ret : 0;
  uart_xmitchars_done(dev);
}

/****************************************************************************
 * Name: netdev_console_dmareceive
 ****************************************************************************/

static void netdev_console_dmareceive(FAR struct uart_dev_s *dev)
{
  FAR struct netdev_consol_s *priv = (FAR struct netdev_consol_s *)dev;
  ssize_t                     ret;

  ret = psock_recv(&priv->psock, dev->dmarx.buffer, dev->dmarx.length,
                   MSG_DONTWAIT);
  dev->dmarx.nbytes = ret > 0 ? ret : 0;
  uart_recvchars_done(dev);
}

/****************************************************************************
 * Name: netdev_console_dmarxfree
 ****************************************************************************/

static void netdev_console_dmarxfree(FAR struct uart_dev_s *dev)
{
  uart_recvchars_dma(dev);
}

/****************************************************************************
 * Name: netdev_console_dmatxavail
 ****************************************************************************/

static void netdev_console_dmatxavail(FAR struct uart_dev_s *dev)
{
  uart_xmitchars_dma(dev);
}

/****************************************************************************
 * Name: netdev_console_send
 ****************************************************************************/

static void netdev_console_send(FAR struct uart_dev_s *dev, int ch)
{
  FAR struct netdev_consol_s *priv = (FAR struct netdev_consol_s *)dev;
  char                        tmp  = ch;

  psock_send(&priv->psock, &tmp, 1, 0);
}

/****************************************************************************
 * Name: netdev_console_txint
 ****************************************************************************/

static void netdev_console_txint(FAR struct uart_dev_s *dev, bool enable)
{
}

/****************************************************************************
 * Name: netdev_console_txready
 ****************************************************************************/

static bool netdev_console_txready(FAR struct uart_dev_s *dev)
{
  return true;
}

/****************************************************************************
 * Name: netdev_console_txempty
 ****************************************************************************/

static bool netdev_console_txempty(FAR struct uart_dev_s *dev)
{
  return true;
}

/****************************************************************************
 * Name: netdev_console_work
 ****************************************************************************/

static void netdev_console_work(FAR void *arg)
{
  FAR struct netdev_consol_s *priv = arg;

  if (priv->rxint)
    {
      uart_dmarxfree(&priv->dev);
    }

  work_queue_next(LPWORK, &priv->work, netdev_console_work, arg,
                  CONSOLE_WORK_DELAY);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_console_init
 *
 * Description:
 *   Register a NETDEV console channel
 *
 * Input Parameters:
 *   dev - The lower half device driver structure to be registered.
 *
 * Returned Value:
 *   Zero on success. A negated errno value is returned on a failure.
 *
 ****************************************************************************/

int netdev_console_init(void)
{
  /* Register as console */

  return uart_register("/dev/console", &g_netdev_console.dev);
}
