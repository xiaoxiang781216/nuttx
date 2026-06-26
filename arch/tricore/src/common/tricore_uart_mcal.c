/****************************************************************************
 * arch/tricore/src/common/tricore_uart_mcal.c
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
 * Architecture:
 *
 * MCAL UART uses an asynchronous buffer-based API:
 *   - Uart_Transmit(channel, buf, size) -> async, notifies via callback
 *   - Uart_Receive(channel, buf, size)  -> async, notifies via callback
 *   - Uart_IsrReceive(HwUnit)           -> ISR for RX FIFO threshold
 *   - Uart_IsrTransmit(HwUnit)          -> ISR for TX FIFO empty
 *   - Uart_IsrExtendedError(HwUnit)     -> ISR for errors + TX complete
 *
 * NuttX serial framework expects lower-half ops:
 *   - dmasend / dmatxavail              -> block TX via DMA
 *   - dmareceive / dmarxfree            -> block RX via DMA
 *   - sendbuf(dev, buf, len)            -> queue a block for TX
 *   - recvbuf(dev, buf, len)            -> drain a block from RX
 *   - send(dev, ch)                     -> send one character
 *   - txint/rxint(dev, en)              -> enable/disable interrupts
 *   - txready/txempty(dev)              -> TX FIFO status
 *   - rxavailable(dev)                  -> RX has a char
 *
 * Bridge strategy:
 *   TX: mcal_uart_sendbuf()/mcal_uart_send() calls Uart_Transmit()
 *       directly with a single byte from NuttX xmit ring buffer.
 *       In DMA mode, mcal_dma_send() passes the batch buffer pointer
 *       to Uart_Transmit(). The TX notification callback signals
 *       completion and triggers the next transfer.
 *   RX: Use Uart_StartStreaming() to continuously receive into a
 *       landing buffer (rx_buf). The streaming notification
 *       callback makes the data available via recvbuf ops, and
 *       uart_recvchars()/uart_recvchars_dma() copies it directly
 *       into the NuttX recv ring buffer.
 *
 *   For early console: up_putc() uses polling via direct ASCLIN
 *       register writes (implemented in this file).
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/serial/serial.h>
#include <nuttx/spinlock.h>
#include <sys/param.h>

#include "mcal.h"
#include "tricore_internal.h"
#include "tricore_uart_mcal.h"

#include "IfxAsclin_reg.h"

/* DMA support for UART DMA mode */

#if (UART_USE_DMA == STD_ON)
#  include "Dma.h"
#endif

/* Conditional inclusion of development/runtime error tracer */

#if UART_DEV_ERROR_DETECT == STD_ON || \
    UART_RUNTIME_ERROR_DETECT == STD_ON
#  include "Det.h"
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* ASCLIN hardware RX FIFO depth (bytes). Used as the streaming landing
 * buffer size. All TC3xx/TC4xx ASCLIN modules have 16-entry FIFO.
 */

#define MCAL_UART_FIFO_SIZE       16

/* Adaptation layer Service IDs (offset from MCAL to avoid collision) */

#define MCAL_UART_SID_INITIALIZE  (0x80u)
#define MCAL_UART_SID_RX_NOTIFY   (0x81u)
#define MCAL_UART_SID_TX_NOTIFY   (0x82u)
#define MCAL_UART_SID_ATTACH      (0x83u)
#define MCAL_UART_SID_SEND        (0x84u)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct mcal_uart_priv_s
{
  const struct   mcal_uart_info_s *info;

  /* MCAL streaming landing buffer: MCAL ISR writes FIFO data here,
   * then synchronously calls the notification callback. The buffer
   * is only valid during the callback; MCAL reuses it for the next
   * burst after the callback returns.
   */

  uint8_t        rx_buf[MCAL_UART_FIFO_SIZE];

  /* RX pending data: set by streaming_notify, consumed by recvbuf.
   * Points into rx_buf and is only valid within the
   * streaming_notify -> uart_recvchars -> recvbuf call chain.
   */

  const uint8_t *rx_pending;     /* Current read position */
  uint16_t       rx_pending_len; /* Remaining bytes to consume */

  /* TX state */

  volatile bool  tx_active;     /* TX in progress via MCAL */
  bool           txint_enabled; /* NuttX TX interrupt enabled */

  /* RX state */

  bool           rxint_enabled; /* NuttX RX interrupt enabled */
  bool           rx_started;    /* MCAL receive started */

  spinlock_t     lock;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int     mcal_uart_setup(struct uart_dev_s *dev);
static void    mcal_uart_shutdown(struct uart_dev_s *dev);
static int     mcal_uart_attach(struct uart_dev_s *dev);
static void    mcal_uart_detach(struct uart_dev_s *dev);
static int     mcal_uart_ioctl(struct file *filep, int cmd,
                               unsigned long arg);
static ssize_t mcal_uart_recvbuf(struct uart_dev_s *dev,
                                 void *buf, size_t len);
static void    mcal_uart_rxint(struct uart_dev_s *dev, bool enable);
static bool    mcal_uart_rxavailable(struct uart_dev_s *dev);
static void    mcal_uart_send(struct uart_dev_s *dev, int ch);
static ssize_t mcal_uart_sendbuf(FAR struct uart_dev_s *dev,
                                 FAR const void *buf, size_t len);
static void    mcal_uart_txint(struct uart_dev_s *dev, bool enable);
static bool    mcal_uart_txready(struct uart_dev_s *dev);
static bool    mcal_uart_txempty(struct uart_dev_s *dev);

/* DMA ops (forward declarations) */

#if defined(CONFIG_SERIAL_TXDMA) && (UART_USE_DMA == STD_ON)
static void    mcal_dma_send(struct uart_dev_s *dev);
static void    mcal_dma_txavailable(struct uart_dev_s *dev);
#endif
#if defined(CONFIG_SERIAL_RXDMA) && (UART_USE_DMA == STD_ON)
static void    mcal_dma_receive(struct uart_dev_s *dev);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_mcal_uart_ops =
{
  .setup       = mcal_uart_setup,
  .shutdown    = mcal_uart_shutdown,
  .attach      = mcal_uart_attach,
  .detach      = mcal_uart_detach,
  .ioctl       = mcal_uart_ioctl,
  .rxint       = mcal_uart_rxint,
  .rxavailable = mcal_uart_rxavailable,
#if defined(CONFIG_SERIAL_TXDMA) && (UART_USE_DMA == STD_ON)
  .dmasend     = mcal_dma_send,
  .dmatxavail  = mcal_dma_txavailable,
#endif
#if defined(CONFIG_SERIAL_RXDMA) && (UART_USE_DMA == STD_ON)
  .dmareceive  = mcal_dma_receive,
#endif
  .send        = mcal_uart_send,
  .txint       = mcal_uart_txint,
  .txready     = mcal_uart_txready,
  .txempty     = mcal_uart_txempty,
  .recvbuf     = mcal_uart_recvbuf,
  .sendbuf     = mcal_uart_sendbuf
};

/* Global array to track channel-to-dev mapping for notification callbacks */

static struct uart_dev_s *g_mcal_uart_devs[MCAL_UART_MAX_CHANNELS];

/* Console device for up_putc */

static struct uart_dev_s *g_mcal_uart_console_dev;

/****************************************************************************
 * Private Functions: Notification Helpers
 ****************************************************************************/

/****************************************************************************
 * Private Functions: ISR Handlers
 *
 * Route NuttX IRQ to MCAL ISR functions. The MCAL ISR functions
 * internally handle the hardware and call the notification
 * callbacks configured in the MCAL configuration tool.
 *
 ****************************************************************************/

static int mcal_uart_rx_isr(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = arg;
  struct mcal_uart_priv_s *priv = dev->priv;

  Uart_IsrReceive(priv->info->hw_module);
  return OK;
}

static int mcal_uart_tx_isr(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = arg;
  struct mcal_uart_priv_s *priv = dev->priv;

  Uart_IsrTransmit(priv->info->hw_module);
  return OK;
}

static int mcal_uart_err_isr(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = arg;
  struct mcal_uart_priv_s *priv = dev->priv;

  Uart_IsrExtendedError(priv->info->hw_module);
  return OK;
}

/****************************************************************************
 * Name: mcal_uart_dma_isr
 *
 * Description:
 *   DMA channel completion ISR. Routes to MCAL Dma_ChInterruptHandler()
 *   which internally calls Uart_DmaTransferCallout, triggering the
 *   TxNotification / StreamingRecvNotification callback chain.
 *
 ****************************************************************************/

#if UART_USE_DMA == STD_ON
static int mcal_uart_dma_isr(int irq, void *context, void *arg)
{
  struct uart_dev_s *dev = arg;
  struct mcal_uart_priv_s *priv = dev->priv;

  if (irq == priv->info->dma_tx_irq)
    {
      Dma_ChInterruptHandler(priv->info->dma_tx_channel);
    }
  else
    {
      Dma_ChInterruptHandler(priv->info->dma_rx_channel);
    }

  return OK;
}
#endif

/****************************************************************************
 * Name: mcal_uart_get_dev
 *
 * Description:
 *   Validate channel_id and return the associated uart_dev_s.
 *   Reports Det error and returns NULL on invalid channel.
 *
 ****************************************************************************/

static inline struct uart_dev_s *mcal_uart_get_dev(uint8_t channel_id,
                                                   uint8_t sid)
{
  if (channel_id >= MCAL_UART_MAX_CHANNELS ||
      g_mcal_uart_devs[channel_id] == NULL)
    {
#if UART_DEV_ERROR_DETECT == STD_ON
      Det_ReportError(UART_MODULE_ID, UART_INSTANCE_ID,
                      sid, UART_E_PARAM_CHANNEL);
#endif
      return NULL;
    }

  return g_mcal_uart_devs[channel_id];
}

/****************************************************************************
 * Private Functions: NuttX Serial Ops
 ****************************************************************************/

static int mcal_uart_setup(struct uart_dev_s *dev)
{
  /* MCAL Uart_Init() is called in mcal_uart_initialize().
   * Nothing additional needed here per-channel.
   */

  return OK;
}

static void mcal_uart_shutdown(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;

  if (priv->rx_started)
    {
      Uart_StopStreaming(priv->info->channel_id);
    }

  priv->rx_started = false;
  priv->txint_enabled = false;
  priv->rxint_enabled = false;
}

static int mcal_uart_attach(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  const struct mcal_uart_info_s *config = priv->info;
  int ret;

  /* TX: DMA or interrupt */

#if UART_USE_DMA == STD_ON
  if (config->dma_tx_channel >= 0)
    {
      ret = irq_attach(config->dma_tx_irq, mcal_uart_dma_isr, dev);
      if (ret != OK)
        {
          goto err_tx;
        }

      MCAL_IRQ_ENABLE(config->dma_tx_irq);
    }
  else
#endif
    {
      ret = irq_attach(config->tx_irq, mcal_uart_tx_isr, dev);
      if (ret != OK)
        {
          goto err_tx;
        }

      MCAL_IRQ_ENABLE(config->tx_irq);
    }

  /* RX: DMA or interrupt */

#if UART_USE_DMA == STD_ON
  if (config->dma_rx_channel >= 0)
    {
      ret = irq_attach(config->dma_rx_irq, mcal_uart_dma_isr, dev);
      if (ret != OK)
        {
          goto err_rx;
        }

      MCAL_IRQ_ENABLE(config->dma_rx_irq);
    }
  else
#endif
    {
      ret = irq_attach(config->rx_irq, mcal_uart_rx_isr, dev);
      if (ret != OK)
        {
          goto err_rx;
        }

      MCAL_IRQ_ENABLE(config->rx_irq);
    }

  /* ERR: always CPU interrupt */

  ret = irq_attach(config->err_irq, mcal_uart_err_isr, dev);
  if (ret != OK)
    {
      goto err_err;
    }

  MCAL_IRQ_ENABLE(config->err_irq);

  return OK;

err_err:
#if UART_USE_DMA == STD_ON
  if (config->dma_rx_channel >= 0)
    {
      MCAL_IRQ_DISABLE(config->dma_rx_irq);
      irq_detach(config->dma_rx_irq);
    }
  else
#endif
    {
      MCAL_IRQ_DISABLE(config->rx_irq);
      irq_detach(config->rx_irq);
    }

err_rx:
#if (UART_USE_DMA == STD_ON)
  if (config->dma_tx_channel >= 0)
    {
      MCAL_IRQ_DISABLE(config->dma_tx_irq);
      irq_detach(config->dma_tx_irq);
    }
  else
#endif
    {
      MCAL_IRQ_DISABLE(config->tx_irq);
      irq_detach(config->tx_irq);
    }

err_tx:
#if (UART_RUNTIME_ERROR_DETECT == STD_ON)
  Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                         MCAL_UART_SID_ATTACH,
                         UART_E_PARAM_CHANNEL);
#endif
  return ret;
}

static void mcal_uart_detach(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  const struct mcal_uart_info_s *config = priv->info;

  /* TX */

#if UART_USE_DMA == STD_ON
  if (config->dma_tx_channel >= 0)
    {
      MCAL_IRQ_DISABLE(config->dma_tx_irq);
      irq_detach(config->dma_tx_irq);
    }
  else
#endif
    {
      MCAL_IRQ_DISABLE(config->tx_irq);
      irq_detach(config->tx_irq);
    }

  /* RX */

#if UART_USE_DMA == STD_ON
  if (config->dma_rx_channel >= 0)
    {
      MCAL_IRQ_DISABLE(config->dma_rx_irq);
      irq_detach(config->dma_rx_irq);
    }
  else
#endif
    {
      MCAL_IRQ_DISABLE(config->rx_irq);
      irq_detach(config->rx_irq);
    }

  /* ERR */

  MCAL_IRQ_DISABLE(config->err_irq);
  irq_detach(config->err_irq);
}

static int mcal_uart_ioctl(struct file *filep, int cmd, unsigned long arg)
{
  return -ENOTTY;
}

static ssize_t mcal_uart_recvbuf(struct uart_dev_s *dev,
                                 void *buf, size_t len)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  size_t copy_len;

  if (priv->rx_pending == NULL || priv->rx_pending_len == 0)
    {
      return 0;
    }

  copy_len = len < priv->rx_pending_len ? len : priv->rx_pending_len;
  memcpy(buf, priv->rx_pending, copy_len);

  priv->rx_pending += copy_len;
  priv->rx_pending_len -= copy_len;

  return copy_len;
}

static void mcal_uart_rxint(struct uart_dev_s *dev, bool enable)
{
  struct mcal_uart_priv_s *priv = dev->priv;

  priv->rxint_enabled = enable;

  if (enable && !priv->rx_started)
    {
      /* Start continuous streaming reception via MCAL */

      priv->rx_started = true;
      Uart_StartStreaming(priv->info->channel_id,
                          priv->rx_buf,
                          sizeof(priv->rx_buf));
    }
}

static bool mcal_uart_rxavailable(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  return priv->rx_pending_len > 0;
}

ssize_t mcal_uart_sendbuf(FAR struct uart_dev_s *dev,
                          FAR const void *buf, size_t len)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  Uart_ReturnType ret;

  /* Uart_Transmit is asynchronous, The ISR fires on transmission
   * completion.
   */

  priv->tx_active = true;
  ret = Uart_Transmit(priv->info->channel_id, buf, len);

  if (ret != UART_E_OK)
    {
      priv->tx_active = false;
#if UART_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                             MCAL_UART_SID_SEND, UART_E_STATE_BUSY);
#endif
      return -EAGAIN;
    }

  return len;
}

static void mcal_uart_send(struct uart_dev_s *dev, int ch)
{
  uint8_t byte = ch;

  mcal_uart_sendbuf(dev, &byte, 1);
}

static void mcal_uart_txint(struct uart_dev_s *dev, bool enable)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  irqstate_t flags;

#if defined(CONFIG_SERIAL_TXDMA) && (UART_USE_DMA == STD_ON)
  if (priv->info->dma_tx_channel >= 0)
    {
      return;
    }
#endif

  flags = spin_lock_irqsave(&priv->lock);
  priv->txint_enabled = enable;

  if (enable && !priv->tx_active)
    {
      /* Kick off TX if there's data waiting */

      uart_xmitchars(dev);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}

static bool mcal_uart_txready(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;

  /* Ready if no MCAL TX is in progress */

  return !priv->tx_active;
}

static bool mcal_uart_txempty(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  Uart_StatusType status;

  /* Empty when no TX active and MCAL channel is idle */

  if (priv->tx_active)
    {
      return false;
    }

  status = Uart_GetStatus(priv->info->channel_id);
  return status == UART_IDLE || status == UART_BUSY_RECEIVE;
}

/****************************************************************************
 * Private Functions: DMA Ops
 *
 * NuttX serial DMA framework uses these to batch-transfer data from the
 * TX ring buffer via Uart_Transmit(), instead of sending 1 byte at a time.
 * For RX, streaming mode already provides efficient batch reception;
 * dmareceive simply starts the streaming engine.
 ****************************************************************************/

#if defined(CONFIG_SERIAL_TXDMA) && (UART_USE_DMA == STD_ON)

/****************************************************************************
 * Name: mcal_dma_send
 *
 * Description:
 *   Called by uart_xmitchars_dma() after setting up dev->dmatx with
 *   buffer pointers and lengths from the TX ring buffer. We call
 *   Uart_Transmit() with the batch data.
 *
 ****************************************************************************/

static void mcal_dma_send(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;
  struct uart_dmaxfer_s *xfer = &dev->dmatx;
  Uart_ReturnType ret;
  size_t len;

  if (priv->info->dma_tx_channel < 0)
    {
      return;
    }

  len = xfer->length;
  if (len == 0)
    {
      return;
    }

  priv->tx_active = true;
  ret = Uart_Transmit(priv->info->channel_id,
                      (const Uart_DataBufferType *)xfer->buffer, len);

  if (ret != UART_E_OK)
    {
      priv->tx_active = false;
      xfer->nbytes = 0;
#if UART_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                             MCAL_UART_SID_SEND, UART_E_STATE_BUSY);
#endif
      uart_xmitchars_done(dev);
      return;
    }

  /* nbytes will be set in tx_notify when MCAL reports completion.
   * For now record the requested length - tx_notify will finalize.
   */

  xfer->nbytes = len;
}

/****************************************************************************
 * Name: mcal_dma_txavailable
 *
 * Description:
 *   Called by the serial framework when new data is available in the TX
 *   ring buffer. If no transfer is in progress, kick off DMA.
 *
 ****************************************************************************/

static void mcal_dma_txavailable(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;

  if (priv->info->dma_tx_channel < 0)
    {
      return;
    }

  if (!priv->tx_active)
    {
      uart_xmitchars_dma(dev);
    }
}

#endif /* CONFIG_SERIAL_TXDMA */

#if defined(CONFIG_SERIAL_RXDMA) && (UART_USE_DMA == STD_ON)

/****************************************************************************
 * Name: mcal_dma_receive
 *
 * Description:
 *   Start DMA reception. For MCAL UART, streaming mode already provides
 *   continuous batch reception, so this simply ensures streaming is started.
 *
 ****************************************************************************/

static void mcal_dma_receive(struct uart_dev_s *dev)
{
  struct mcal_uart_priv_s *priv = dev->priv;

  if (priv->info->dma_rx_channel < 0)
    {
      return;
    }

  if (!priv->rx_started)
    {
      priv->rx_started = true;
      Uart_StartStreaming(priv->info->channel_id,
                          priv->rx_buf,
                          sizeof(priv->rx_buf));
    }
}

#endif /* CONFIG_SERIAL_RXDMA */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mcal_uart_streaming_notify
 *
 * Description:
 *   Streaming RX notification handler. Called from board-level
 *   notification callback wrappers when MCAL streaming RX completes.
 *   These generic handlers are called from board-level EB notification
 *   callback wrappers (e.g., Uart_Ch0_StreamingRecvNotification in tc4d9.c).
 *   Board files implement the EB-named callbacks because different boards
 *   may have different channel counts.
 *
 ****************************************************************************/

void mcal_uart_streaming_notify(uint8_t channel_id,
                                Uart_ErrorIdType error, uint16_t size)
{
  struct uart_dev_s *dev;
  struct mcal_uart_priv_s *priv;

  dev = mcal_uart_get_dev(channel_id, MCAL_UART_SID_RX_NOTIFY);
  if (dev == NULL)
    {
      return;
    }

  priv = (struct mcal_uart_priv_s *)dev->priv;

  if (error != UART_E_NO_ERR)
    {
#if UART_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                             MCAL_UART_SID_RX_NOTIFY, error);
#endif
      return;
    }

#if defined(CONFIG_SERIAL_RXDMA) && (UART_USE_DMA == STD_ON)
  if (priv->info->dma_rx_channel >= 0)
    {
      /* DMA path: copy data into NuttX recv ring buffer via
       * uart_recvchars_dma/uart_recvchars_done interface.
       * Loop because one streaming burst may exceed available
       * ring buffer space in a single dmarx segment.
       */

      uint16_t offset = 0;

      while (offset < size)
        {
          struct uart_dmaxfer_s *xfer = &dev->dmarx;
          size_t copy_len;

          /* Prepare dmarx target pointers */

          uart_recvchars_dma(dev);
          if (xfer->length == 0)
            {
              /* Ring buffer full */

              break;
            }

          copy_len = size - offset;
          if (copy_len > xfer->length)
            {
              copy_len = xfer->length;
            }

          memcpy(xfer->buffer, &priv->rx_buf[offset], copy_len);
          xfer->nbytes = copy_len;
          offset += copy_len;

          uart_recvchars_done(dev);
        }

      if (offset < size)
        {
#if UART_RUNTIME_ERROR_DETECT == STD_ON
          Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                                 MCAL_UART_SID_RX_NOTIFY, UART_E_RXFIFO_OVERFLOW_ERROR);
#endif
        }
    }
  else
#endif
    {
      /* Non-DMA path: use recvbuf ops via uart_recvchars */

      priv->rx_pending = priv->rx_buf;
      priv->rx_pending_len = size;

      if (priv->rxint_enabled)
        {
          uart_recvchars(dev);

          if (priv->rx_pending_len > 0)
            {
#if UART_RUNTIME_ERROR_DETECT == STD_ON
              Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                                    MCAL_UART_SID_RX_NOTIFY, UART_E_RXFIFO_OVERFLOW_ERROR);
#endif
            }
        }

      priv->rx_pending = NULL;
      priv->rx_pending_len = 0;
    }
}

/****************************************************************************
 * Name: mcal_uart_tx_notify
 *
 * Description:
 *   TX notification handler. Called from board-level notification
 *   callback wrappers when MCAL TX completes.
 *   These generic handlers are called from board-level EB notification
 *   callback wrappers (e.g., Uart_Ch0_TxNotification in tc4d9.c).
 *   Board files implement the EB-named callbacks because different boards
 *   may have different channel counts.
 *
 ****************************************************************************/

void mcal_uart_tx_notify(uint8_t channel_id, Uart_ErrorIdType error)
{
  struct uart_dev_s *dev;
  struct mcal_uart_priv_s *priv;

  dev = mcal_uart_get_dev(channel_id, MCAL_UART_SID_TX_NOTIFY);
  if (dev == NULL)
    {
      return;
    }

  priv = dev->priv;

  if (error != UART_E_NO_ERR)
    {
#if UART_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                             MCAL_UART_SID_TX_NOTIFY, (uint8)error);
#endif
    }

  priv->tx_active = false;

  /* Let NuttX send more characters */

#if defined(CONFIG_SERIAL_TXDMA) && (UART_USE_DMA == STD_ON)
  if (priv->info->dma_tx_channel >= 0)
    {
      uart_xmitchars_done(dev);
      uart_xmitchars_dma(dev);
    }
  else
#endif
    {
      if (priv->txint_enabled)
        {
          uart_xmitchars(dev);
        }
    }
}

/****************************************************************************
 * Name: mcal_uart_initialize
 *
 * Description:
 *   Initialize all MCAL UART channels for this core partition.
 *
 ****************************************************************************/

int mcal_uart_initialize(const Uart_ConfigType *mcal_config,
                         const struct mcal_uart_info_s *infos,
                         size_t num)
{
  struct mcal_uart_priv_s *priv;
  struct uart_dev_s *dev;
  char path[16];
  size_t i;
  int ret;

#if UART_DEV_ERROR_DETECT == STD_ON
  if (mcal_config == NULL || infos == NULL)
    {
      Det_ReportError(UART_MODULE_ID, UART_INSTANCE_ID,
                      MCAL_UART_SID_INITIALIZE, UART_E_PARAM_POINTER);
      return -EINVAL;
    }

  if (num == 0)
    {
      Det_ReportError(UART_MODULE_ID, UART_INSTANCE_ID,
                      MCAL_UART_SID_INITIALIZE, UART_E_INVALID_SIZE);
      return -EINVAL;
    }
#endif

  for (i = 0; i < num; i++)
    {
#if UART_USE_DMA == STD_ON
      if (infos[i].dma_tx_channel >= 0)
        {
          MCAL_IRQ_ENABLE(infos[i].tx_irq);
          tricore_register_irq_srpn(infos[i].dma_tx_irq);
        }
      else
#endif
        {
          tricore_register_irq_srpn(infos[i].tx_irq);
        }

#if UART_USE_DMA == STD_ON
      if (infos[i].dma_rx_channel >= 0)
        {
          MCAL_IRQ_ENABLE(infos[i].rx_irq);
          tricore_register_irq_srpn(infos[i].dma_rx_irq);
        }
      else
#endif
        {
          tricore_register_irq_srpn(infos[i].rx_irq);
        }

      tricore_register_irq_srpn(infos[i].err_irq);
    }

  /* Initialize MCAL UART module for this core's partition */

  if (mcal_config != NULL)
    {
      Uart_Init(mcal_config);
    }

  /* Create and register uart_dev_s for each channel */

  for (i = 0; i < num; i++)
    {
      priv = kmm_zalloc(sizeof(struct mcal_uart_priv_s));
      if (priv == NULL)
        {
          ret = -ENOMEM;
          goto errmem;
        }

      priv->info = &infos[i];
      spin_lock_init(&priv->lock);

      dev = kmm_zalloc(sizeof(struct uart_dev_s));
      if (dev == NULL)
        {
          ret = -ENOMEM;
          goto errdev;
        }

      /* Setup NuttX uart_dev_s */

      dev->ops = &g_mcal_uart_ops;
      dev->priv = priv;

      /* Allocate NuttX serial RX/TX buffers */

      dev->recv.size = infos[i].recv_len;
      dev->recv.buffer = kmm_zalloc(infos[i].recv_len);
      dev->xmit.size = infos[i].xmit_len;
      dev->xmit.buffer = kmm_zalloc(infos[i].xmit_len);

      if (dev->recv.buffer == NULL || dev->xmit.buffer == NULL)
        {
          ret = -ENOMEM;
          goto errbuf;
        }

      /* Track for notification callbacks */

      if (infos[i].channel_id < MCAL_UART_MAX_CHANNELS)
        {
          g_mcal_uart_devs[infos[i].channel_id] = dev;
        }
      else
        {
          /* channel_id out of range: report config error, release the
           * resources allocated for this channel and skip it.
           */

          ret = -EINVAL;
          goto errbuf;
        }

      /* Register console */

      if (infos[i].isconsole)
        {
          dev->isconsole = true;
          g_mcal_uart_console_dev = dev;
          ret = uart_register("/dev/console", dev);
          if (ret < 0)
            {
              g_mcal_uart_console_dev = NULL;
              dev->isconsole = false;
#if UART_RUNTIME_ERROR_DETECT == STD_ON
              Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                                     MCAL_UART_SID_INITIALIZE, UART_E_PARAM_CHANNEL);
#endif
            }
        }

      /* Register as /dev/ttySx */

      snprintf(path, sizeof(path), "/dev/ttyS%d", infos[i].bus);
      ret = uart_register(path, dev);
      if (ret < 0)
        {
          goto errbuf;
        }

      continue;

errbuf:
      if (infos[i].channel_id < MCAL_UART_MAX_CHANNELS)
        {
          g_mcal_uart_devs[infos[i].channel_id] = NULL;
        }

      kmm_free(dev->recv.buffer);
      kmm_free(dev->xmit.buffer);
      kmm_free(dev);

errdev:
      kmm_free(priv);

errmem:
#if UART_RUNTIME_ERROR_DETECT == STD_ON
      Det_ReportRuntimeError(UART_MODULE_ID, UART_INSTANCE_ID,
                             MCAL_UART_SID_INITIALIZE,
                             (ret == -ENOMEM) ?
                             UART_E_PARAM_POINTER : UART_E_PARAM_CHANNEL);
#endif

      /* Continue initializing remaining channels. Already
       * registered channels (e.g. console) keep working.
       */
    }

  return OK;
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Low-level console character output for early boot and debug.
 *   Directly writes to the ASCLIN TX FIFO register to output a single
 *   character on the console channel. Uses raw register access with
 *   polling, no MCAL/iLLD dependency.
 *
 ****************************************************************************/

void up_putc(int ch)
{
  static volatile Ifx_ASCLIN *base;
  struct mcal_uart_priv_s *priv;

  if (g_mcal_uart_console_dev == NULL)
    {
      return;
    }

  priv = g_mcal_uart_console_dev->priv;

  if (base == NULL)
    {
      if (priv->info == NULL)
        {
          return;
        }

      base = (volatile Ifx_ASCLIN *)((uintptr_t)&MODULE_ASCLIN0 +
             (uintptr_t)priv->info->hw_module *
             ((uintptr_t)&MODULE_ASCLIN1 - (uintptr_t)&MODULE_ASCLIN0));
    }

  /* Enable TX FIFO outlet (set ENO bit) */

  base->TXFIFOCON.B.ENO = 1;

  /* Wait until TX FIFO is empty (FILL == 0) */

  while (base->TXFIFOCON.B.FILL != 0)
    {
    }

  /* Clear all flags and write character */

  base->FLAGSCLEAR.U = 0xffffffffu;
  base->TXDATA[0].U = ch;
}
