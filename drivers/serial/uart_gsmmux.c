/****************************************************************************
 * drivers/serial/uart_gsmmux.c
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

#include <debug.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/poll.h>

#include <nuttx/atomic.h>
#include <nuttx/crc8.h>
#include <nuttx/init.h>
#include <nuttx/fs/fs.h>
#include <nuttx/kmalloc.h>
#include <nuttx/list.h>
#include <nuttx/serial/gsmmux.h>
#include <nuttx/serial/serial.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define GSMMUX_EA                     0x01

#define GSMMUX_CMD                    0x02
#define GSMMUX_RSP                    0x00

#define GSMMUX_PF                     0x10

#define GSMMUX_FLAG                   0xf9 /* Only supports basic mode                 */

#define GSMMUX_ADV_ESC                0x7d /* Advanced mode escaped symbols            */
#define GSMMUX_ADV_ESC_MASK           0x20 /* Advanced mode escaped mask               */

/* GSM0710 frame type */

#define GSMMUX_SABM                   0x2f /* Set Asynchronous Balanced Mode           */
#define GSMMUX_UA                     0x63 /* Unnumbered Acknowledgement               */
#define GSMMUX_DM                     0x0f /* Disconnected Mode                        */
#define GSMMUX_DISC                   0x43 /* Disconnect                               */
#define GSMMUX_UIH                    0xef /* Unnumbered Information with Header check */
#define GSMMUX_UI                     0x03 /* Unnumbered Information                   */

#define GSMMUX_ADDR(cmd, dlci)        (GSMMUX_EA | (cmd) | (dlci) << 2)
#define GSMMUX_LEN0(len)              (GSMMUX_EA | (len) << 1)

/* Control channel commands */

#define GSMMUX_CMD_PN                 (GSMMUX_EA | 0x80) /* DLC parameter negotiation       */
#define GSMMUX_CMD_PSC                (GSMMUX_EA | 0x40) /* Power Saving Control            */
#define GSMMUX_CMD_CLD                (GSMMUX_EA | 0xc0) /* Multiplexer close down          */
#define GSMMUX_CMD_TEST               (GSMMUX_EA | 0x20) /* Test Command                    */
#define GSMMUX_CMD_FCON               (GSMMUX_EA | 0xa0) /* Flow Control On Command         */
#define GSMMUX_CMD_FCOFF              (GSMMUX_EA | 0x60) /* Flow Control Off Command        */
#define GSMMUX_CMD_MSC                (GSMMUX_EA | 0xe0) /* Modem Status Command            */
#define GSMMUX_CMD_NSC                (GSMMUX_EA | 0x10) /* Non Supported Command Response  */
#define GSMMUX_CMD_RPN                (GSMMUX_EA | 0x90) /* Remote Port Negotiation Command */
#define GSMMUX_CMD_RLS                (GSMMUX_EA | 0x50) /* Remote Line Status Command      */
#define GSMMUX_CMD_SNC                (GSMMUX_EA | 0xd0) /* Service Negotiation Command     */

#define GSMMUX_MIN_MTU                8
#define GSMMUX_MIN_MRU                8

/* State machine */

#define GSMMUX_STATE_INIT             0
#define GSMMUX_STATE_ADDRESS          1
#define GSMMUX_STATE_CONTROL          2
#define GSMMUX_STATE_LEN0             3
#define GSMMUX_STATE_LEN1             4
#define GSMMUX_STATE_DATA             5
#define GSMMUX_STATE_FCS              6
#define GSMMUX_STATE_END              7

/* Debug configuration */

#define GSMMUX_DUMP_ERROR             0x01
#define GSMMUX_DUMP_WARNING           0x02
#define GSMMUX_DUMP_INFO              0x04
#define GSMMUX_DUMP_DATA              0x08
#define GSMMUX_DUMP_PAYLOAD           0x10

#define gsmmux_err(fmt, ...)                          \
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_ERROR)   \
    {                                                 \
      syslog(LOG_ERR, fmt, ##__VA_ARGS__);            \
    }

#define gsmmux_warn(fmt, ...)                         \
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_WARNING) \
    {                                                 \
      syslog(LOG_WARNING, fmt, ##__VA_ARGS__);        \
    }

#define gsmmux_info(fmt, ...)                         \
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_INFO)    \
    {                                                 \
      syslog(LOG_INFO, fmt, ##__VA_ARGS__);           \
    }

#define gsmmux_dump_payload(msg, buf, len)            \
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_PAYLOAD) \
    {                                                 \
      lib_dumpbuffer(msg, (FAR uint8_t *)(buf), len); \
    }

#define gsmmux_dump_data(msg, buf, len)               \
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_DATA)    \
    {                                                 \
      lib_dumpbuffer(msg, (FAR uint8_t *)(buf), len); \
    }

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct gsmmux_channel_s
{
  struct uart_dev_s uart;
  FAR struct gsmmux_s *gsmmux;
  struct list_node node;
};

struct gsmmux_s
{
  FAR struct uart_dev_s *uart; /* The actual physical UART */
  struct list_node head;       /* Channel list of data to be sent */
  FAR const char *devname;     /* Physical UART Name */
  struct file file;            /* File descriptor for the physical UART */
  struct pollfd fds;           /* Poll file descriptor for the physical UART */
  atomic_t refcnt;             /* Reference count for the physical UART */

  /* Framing Layer */

  FAR uint8_t *rxbuf;          /* Gsmmux data receiving frame buffer */
  size_t offset;               /* Offset of the current frame */
  size_t len;                  /* Length of the current frame */
  size_t recv;                 /* Actual read data size */
  uint8_t dlci;                /* DLCI of the current frame */
  uint8_t ctrl;                /* Control field of the current frame */
  uint8_t fcs;                 /* Fcs of the current frame */
  uint8_t state;               /* State of the current frame */

  /* GSM0710 protocol configuration, Retry and timeout configuration
   * Are currently not supported
   */

  unsigned int encapsulation;  /* Encapsulation mode */
  unsigned int initiator;      /* Initiator of the connection */
  size_t mtu;                  /* Maximum transmission unit */
  size_t mru;                  /* Maximum receive unit */

  size_t nchannel;             /* Number of channels */

  /* Channel list, allocate memory at registration time */

  struct gsmmux_channel_s channels[0];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int gsmmux_tty_setup(FAR struct uart_dev_s *dev);
static void gsmmux_tty_shutdown(FAR struct uart_dev_s *dev);
static int gsmmux_tty_attach(FAR struct uart_dev_s *dev);
static void gsmmux_tty_detach(FAR struct uart_dev_s *dev);
static int gsmmux_tty_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg);
static void gsmmux_tty_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool gsmmux_tty_rxavailable(FAR struct uart_dev_s *dev);
static void gsmmux_tty_dmasend(FAR struct uart_dev_s *dev);
static void gsmmux_tty_dmareceive(FAR struct uart_dev_s *dev);
static void gsmmux_tty_dmarxfree(FAR struct uart_dev_s *dev);
static void gsmmux_tty_dmatxavail(FAR struct uart_dev_s *dev);
static void gsmmux_tty_txint(FAR struct uart_dev_s *dev, bool enable);
static bool gsmmux_tty_txready(FAR struct uart_dev_s *dev);
static bool gsmmux_tty_txempty(FAR struct uart_dev_s *dev);
static ssize_t gsmmux_tty_recvbuf(FAR struct uart_dev_s *dev,
                                  FAR void *buf, size_t len);
static ssize_t gsmmux_tty_sendbuf(FAR struct uart_dev_s *dev,
                                   FAR const void *buf, size_t len);
static size_t gsmmux_read_frame(FAR struct gsmmux_s *gsmmux,
                                FAR const void *buf, size_t len);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_gsmmux_ops =
{
  gsmmux_tty_setup,
  gsmmux_tty_shutdown,
  gsmmux_tty_attach,
  gsmmux_tty_detach,
  gsmmux_tty_ioctl,
  NULL,
  gsmmux_tty_rxint,
  gsmmux_tty_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  NULL,
#endif
  gsmmux_tty_dmasend,
  gsmmux_tty_dmareceive,
  gsmmux_tty_dmarxfree,
  gsmmux_tty_dmatxavail,
  NULL,
  gsmmux_tty_txint,
  gsmmux_tty_txready,
  gsmmux_tty_txempty,
  NULL,
  gsmmux_tty_recvbuf,
  gsmmux_tty_sendbuf,
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gsmmux_frame_fcs
 ****************************************************************************/

static inline_function uint8_t
gsmmux_frame_fcs(FAR const void *buffer, size_t len)
{
  return crc8rohcpart(buffer, len, 0);
}

/****************************************************************************
 * Name: gsmmux_frame_fcs_add
 ****************************************************************************/

static inline_function uint8_t gsmmux_frame_fcs_add(uint8_t fcs, uint8_t ch)
{
  return crc8rohcpart(&ch, 1, fcs);
}

/****************************************************************************
 * Name: gsmmux_uart_pollin
 *
 * Description:
 *  Physical layer data sending and receiving.
 *
 ****************************************************************************/

static void gsmmux_uart_pollin(FAR struct gsmmux_s *gsmmux)
{
  FAR struct uart_dev_s *dev = gsmmux->uart;
  FAR struct uart_buffer_s *rxbuf = &dev->recv;
  irqstate_t flags;
  size_t tail;
  size_t head;
  size_t recv;
  size_t len;

  flags = uart_spinlock(dev, true);
  tail = rxbuf->tail;
  head = rxbuf->head;
  if (tail <= head)
    {
      len = head - tail;
      gsmmux_dump_data("<---uart", rxbuf->buffer + tail, len);
      recv = gsmmux_read_frame(gsmmux, rxbuf->buffer + tail, len);
      rxbuf->tail += recv;
    }
  else
    {
      len = rxbuf->size - tail;
      gsmmux_dump_data("<---uart1", rxbuf->buffer + tail, len);
      recv = gsmmux_read_frame(gsmmux, rxbuf->buffer + tail, len);
      if (recv >= len)
        {
          gsmmux_dump_data("<---uart2", rxbuf->buffer, head);
          recv = gsmmux_read_frame(gsmmux, rxbuf->buffer, head);
          rxbuf->tail = recv;
        }
      else
        {
          rxbuf->tail += recv;
        }
    }

  uart_dmarxfree(dev);
  uart_spinunlock(dev, true, flags);
}

/****************************************************************************
 * Name: gsmmux_uart_pollout
 ****************************************************************************/

static void gsmmux_uart_pollout(FAR struct gsmmux_s *gsmmux)
{
  struct list_node head = LIST_INITIAL_VALUE(head);
  FAR struct gsmmux_channel_s *channel;
  irqstate_t flags;

  /* Send previously unfinished data */

  flags = uart_spinlock(gsmmux->uart, false);
  list_move(&gsmmux->head, &head);
  uart_spinunlock(gsmmux->uart, false, flags);

  while ((channel = list_remove_head_type(&head,
         struct gsmmux_channel_s, node)))
    {
      flags = uart_spinlock(&channel->uart, true);
      uart_xmitchars_dma(&channel->uart);
      uart_spinunlock(&channel->uart, true, flags);
    }
}

/****************************************************************************
 * Name: gsmmux_uart_poll
 ****************************************************************************/

static void gsmmux_uart_poll(FAR struct pollfd *fds)
{
  FAR struct gsmmux_s *gsmmux = fds->arg;

  if (fds->revents & POLLIN)
    {
      fds->revents &= ~POLLIN;
      gsmmux_uart_pollin(gsmmux);
    }
  else if (fds->revents & POLLOUT)
    {
      fds->revents &= ~POLLOUT;
      gsmmux_uart_pollout(gsmmux);
    }
}

/****************************************************************************
 * Name: gsmmux_uart_open
 ****************************************************************************/

static int gsmmux_uart_open(FAR struct gsmmux_s *gsmmux)
{
  struct termios term;
  int ret;

  gsmmux_info("gsmmux_uart_open: %s, refcnt: %" PRId32 "\n",
              gsmmux->devname, (int32_t)atomic_read(&gsmmux->refcnt));
  if (atomic_add(&gsmmux->refcnt, 1) > 0)
    {
      return OK;
    }

  ret = file_open(&gsmmux->file, gsmmux->devname, O_RDWR | O_NONBLOCK);
  if (ret < 0)
    {
      atomic_sub(&gsmmux->refcnt, 1);
      gsmmux_err("Failed to open %s: %d\n", gsmmux->devname, ret);
      return ret;
    }

  /* Set the serial port to raw mode */

  file_ioctl(&gsmmux->file, TCGETS, &term);
  cfmakeraw(&term);
  file_ioctl(&gsmmux->file, TCSETS, &term);

  /* Register data receiving notification */

  gsmmux->uart       = gsmmux->file.f_inode->i_private;
  gsmmux->fds.arg    = gsmmux;
  gsmmux->fds.events = POLLIN | POLLOUT;
  gsmmux->fds.cb     = gsmmux_uart_poll;
  file_poll(&gsmmux->file, &gsmmux->fds, true);
  return OK;
}

/****************************************************************************
 * Name: gsmmux_uart_close
 ****************************************************************************/

static void gsmmux_uart_close(FAR struct gsmmux_s *gsmmux)
{
  gsmmux_info("gsmmux_uart_close: %s, refcnt: %" PRId32 "\n",
              gsmmux->devname, (int32_t)atomic_read(&gsmmux->refcnt));
  if (atomic_sub(&gsmmux->refcnt, 1) > 1)
    {
      return;
    }

  file_poll(&gsmmux->file, &gsmmux->fds, false);
  file_close(&gsmmux->file);
}

/****************************************************************************
 * Name: gsmmux_uart_rxavailable
 *
 * Description:
 *  Calculate the available space for reading from the RX buffer.
 *
 ****************************************************************************/

static size_t gsmmux_uart_rxavailable(FAR struct uart_buffer_s *rxbuf)
{
  return rxbuf->head >= rxbuf->tail ?
         rxbuf->head - rxbuf->tail :
         rxbuf->size - rxbuf->tail + rxbuf->head;
}

/****************************************************************************
 * Name: gsmmux_uart_txavailable
 *
 * Description:
 *  Calculate the available space for writing to the TX buffer.
 *
 ****************************************************************************/

static size_t gsmmux_uart_txavailable(FAR struct uart_buffer_s *txbuf)
{
  return txbuf->head >= txbuf->tail ?
         txbuf->size - txbuf->head + txbuf->tail - 1 :
         txbuf->tail - txbuf->head - 1;
}

/****************************************************************************
 * Name: gsmmux_uart_read
 *
 * Description:
 *  Update the RX buffer.
 *
 ****************************************************************************/

static ssize_t gsmmux_uart_read(FAR struct gsmmux_s *gsmmux)
{
  FAR struct uart_dev_s *dev = gsmmux->uart;
  FAR struct uart_buffer_s *rxbuf = &dev->recv;
  irqstate_t flags;
  size_t recv = 0;
  ssize_t ret = 0;

  /* Get this next character from the hardware */

  flags = uart_spinlock(dev, true);
  while (uart_rxavailable(dev))
    {
      if (dev->ops->recvbuf)
        {
          FAR char *buf = &rxbuf->buffer[rxbuf->head];
          size_t len;

          if (rxbuf->tail > rxbuf->head)
            {
              len = rxbuf->tail - rxbuf->head - 1;
            }
          else if (rxbuf->tail)
            {
              len = rxbuf->size - rxbuf->head;
            }
          else
            {
              len = rxbuf->size - rxbuf->head - 1;
            }

          ret = uart_recvbuf(dev, buf, len);
          if (ret <= 0)
            {
              break;
            }

          recv += ret;
          rxbuf->head += ret;
          if (rxbuf->head >= rxbuf->size)
            {
              rxbuf->head = 0;
            }
        }
      else
        {
          unsigned int status;
          ret = uart_receive(dev, &status);
          if (ret < 0)
            {
              break;
            }

          recv += 1;
          rxbuf->buffer[rxbuf->head] = ret;
          if (++rxbuf->head >= rxbuf->size)
            {
              rxbuf->head = 0;
            }
        }
    }

  uart_spinunlock(dev, true, flags);
  if (recv > 0)
    {
      gsmmux_uart_pollin(gsmmux);
      ret = recv;
    }

  return ret;
}

static ssize_t gsmmux_uart_write(FAR struct gsmmux_s *gsmmux,
                                 FAR const struct iovec *iov,
                                 size_t iovcnt)
{
  FAR struct uart_buffer_s *txbuf = &gsmmux->uart->xmit;
  irqstate_t flags;
  size_t available;
  size_t written = 0;
  size_t len = 0;
  size_t i;

  for (i = 0; i < iovcnt; i++)
    {
      len += iov[i].iov_len;
      gsmmux_dump_data("uart--->", iov[i].iov_base, iov[i].iov_len);
    }

  /* The integrity of the data frame must be guaranteed here,
   * and each transmission must be atomic.
   */

  flags = uart_spinlock(gsmmux->uart, true);
  if (up_interrupt_context() || sched_idletask() || OSINIT_IS_PANIC())
    {
      size_t j;

      for (i = 0; i < iovcnt; i++)
        {
          if (gsmmux->uart->ops->sendbuf)
            {
              written += uart_sendbuf(gsmmux->uart, iov[i].iov_base,
                                      iov[i].iov_len);
            }
          else
            {
              for (j = 0; j < iov[i].iov_len; j++)
                {
                  int ch = ((FAR const char *)iov[i].iov_base)[j];
                  uart_send(gsmmux->uart, ch);
                  written++;
                }
            }
        }

      uart_spinunlock(gsmmux->uart, true, flags);
      return written;
    }

  available = gsmmux_uart_txavailable(txbuf);

  /* If there is not enough space left in the xmit buffer, we need to
   * wait until there is enough space
   */

  if (available < len)
    {
      uart_spinunlock(gsmmux->uart, true, flags);
      gsmmux_warn("buffer not enough, available: %zu, buflen: %zu\n",
                  available, len);
      return -EAGAIN;
    }

  /* This is the only write port, so it is safe to operate xmit buffer */

  uart_disabletxint(gsmmux->uart);
  written = len;
  for (i = 0; i < iovcnt; i++)
    {
      len = txbuf->size - txbuf->head;
      if (iov[i].iov_len > len)
        {
          memcpy(txbuf->buffer + txbuf->head, iov[i].iov_base, len);
          memcpy(txbuf->buffer, iov[i].iov_base + len, iov[i].iov_len - len);
          txbuf->head = iov[i].iov_len - len;
        }
      else
        {
          memcpy(txbuf->buffer + txbuf->head, iov[i].iov_base,
                 iov[i].iov_len);
          txbuf->head += iov[i].iov_len;
        }
    }

  /* Enable Data transmission */

  uart_dmatxavail(gsmmux->uart);
  uart_enabletxint(gsmmux->uart);
  uart_spinunlock(gsmmux->uart, true, flags);

  return written;
}

static ssize_t gsmmux_send_packet(FAR struct gsmmux_s *gsmmux,
                                  int dlci, int cmd, int type,
                                  FAR const void *buf, size_t len)
{
  struct iovec iov[3];
  uint8_t head[6];
  uint8_t end[2];
  size_t headlen;
  ssize_t ret;

  /* Add Frame Header, including Address, Control, Length */

  head[0] = GSMMUX_FLAG;
  head[1] = GSMMUX_ADDR(cmd, dlci);
  head[2] = type | GSMMUX_PF;

  if (len <= 127)
    {
      head[3] = GSMMUX_LEN0(len);
      headlen = 4;
    }
  else
    {
      head[3] = len << 1;
      head[4] = len >> 7;
      headlen = 5;
    }

  iov[0].iov_base = head;
  iov[0].iov_len = headlen;

  iov[1].iov_base = (FAR uint8_t *)buf;
  iov[1].iov_len = len;

  /* Add Frame End, including FCS and Flag */

  end[0] = gsmmux_frame_fcs(head + 1, headlen - 1);
  end[1] = GSMMUX_FLAG;
  iov[2].iov_base = end;
  iov[2].iov_len = sizeof(end);

  ret = gsmmux_uart_write(gsmmux, iov, 3);
  return ret < 0 ? ret : len;
}

static ssize_t gsmmux_send_data(FAR struct gsmmux_channel_s *channel,
                                FAR const void *buf, size_t len)
{
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;
  size_t written = 0;
  ssize_t ret = 0;

  /* dlci 0 is the control channel, and the actual effective channel
   * starts from 1
   */

  int dlci = channel - gsmmux->channels + 1;

  while (len > 0)
    {
      ret = MIN(gsmmux->mtu, len);
      gsmmux_dump_payload("UIH --->:", buf, ret);
      ret = gsmmux_send_packet(gsmmux, dlci, GSMMUX_CMD, GSMMUX_UIH,
                               buf, ret);
      if (ret < 0)
        {
          irqstate_t flags = uart_spinlock(gsmmux->uart, false);
          if (!list_in_list(&channel->node))
            {
              list_add_tail(&gsmmux->head, &channel->node);
            }

          uart_spinunlock(gsmmux->uart, false, flags);
          break;
        }

      written += ret;
      buf += ret;
      len -= ret;
    }

  return written ? written : ret;
}

static size_t gsmmux_send_command(FAR struct gsmmux_s *gsmmux, int dlci,
                                  int type, FAR const uint8_t *buf,
                                  size_t len)
{
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_PAYLOAD)
    {
      FAR const char *cmd = type == GSMMUX_SABM ? "SABM" :
                            type == GSMMUX_DISC ? "DISC" : "Unknown";
      gsmmux_info("CMD --->: (%s) %02x\n", cmd, type);
    }

  return gsmmux_send_packet(gsmmux, dlci, GSMMUX_CMD, type, buf, len);
}

static size_t gsmmux_send_response(FAR struct gsmmux_s *gsmmux, int dlci,
                                   int type, FAR const uint8_t *buffer,
                                   size_t buflen)
{
  if (CONFIG_UART_GSMMUX_DEBUG & GSMMUX_DUMP_PAYLOAD)
    {
      FAR const char *resp = type == GSMMUX_UA ? "UA" :
                            type == GSMMUX_DM ? "DM" : "Unknown";
      gsmmux_info("RSP --->: (%s) %02x\n", resp, type);
    }

  return gsmmux_send_packet(gsmmux, dlci, GSMMUX_RSP, type, buffer, buflen);
}

static size_t gsmmux_handle_data(FAR struct gsmmux_channel_s *channel)
{
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;
  size_t ret;

  irqstate_t flags = uart_spinlock(gsmmux->uart, true);
  gsmmux->recv = 0;
  uart_recvchars_dma(&channel->uart);
  ret = gsmmux->recv;
  uart_spinunlock(gsmmux->uart, true, flags);

  return ret;
}

static void gsmmux_handle_dlci0(FAR struct gsmmux_s *gsmmux,
                                FAR const uint8_t *buf, size_t len)
{
  uint8_t cmd = buf[0] & 0xfd;

  switch (cmd)
    {
    case GSMMUX_CMD_MSC:
      /* Reply with the same MSC command, indicating
       * successful reception
       */

      gsmmux_send_response(gsmmux, 0, GSMMUX_UIH, buf, len);
      break;
    }
}

static size_t gsmmux_handle_frame(FAR struct gsmmux_s *gsmmux, uint8_t dlci,
                                  uint8_t ctrl, FAR const void *buf,
                                  size_t len)
{
  FAR struct gsmmux_channel_s *channel;

  /* dlci 0 is the control channel, and the actual effective channel
   * starts from 1
   */

  channel = dlci ? &gsmmux->channels[dlci - 1] : NULL;

  switch (ctrl)
    {
    case GSMMUX_UI:
    case GSMMUX_UIH:
    case GSMMUX_UI | GSMMUX_PF:
    case GSMMUX_UIH | GSMMUX_PF:
      if (dlci == 0)
        {
          gsmmux_dump_payload("<--- ctrl cmd:", buf, len);
          gsmmux_handle_dlci0(gsmmux, buf, len);
        }
      else
        {
          gsmmux_dump_payload("<--- UIH:", buf, len);
          return gsmmux_handle_data(channel);
        }
      break;
    case GSMMUX_SABM:
    case GSMMUX_SABM | GSMMUX_PF:
      gsmmux_dump_payload("<--- SABM:", buf, len);
      gsmmux_send_command(gsmmux, dlci, GSMMUX_UA, NULL, 0);
      break;
    case GSMMUX_DISC:
    case GSMMUX_DISC | GSMMUX_PF:
      gsmmux_dump_payload("<--- DISC:", buf, len);
      gsmmux_send_command(gsmmux, dlci, GSMMUX_DM, NULL, 0);
      break;
    case GSMMUX_UA:
    case GSMMUX_UA | GSMMUX_PF:
      gsmmux_dump_payload("<--- UA:", buf, len);
      break;
    case GSMMUX_DM:
    case GSMMUX_DM | GSMMUX_PF:
      gsmmux_dump_payload("<--- DM:", buf, len);
      break;
    default:
      gsmmux_warn("unknown frame type: %02x\n", ctrl);
      break;
    }

  return len;
}

static size_t gsmmux_read_frame(FAR struct gsmmux_s *gsmmux,
                                FAR const void *buf, size_t len)
{
  size_t i;

  if (gsmmux->len && gsmmux->state == GSMMUX_STATE_END)
    {
      size_t recv = gsmmux_handle_frame(gsmmux, gsmmux->dlci,
                                        gsmmux->ctrl,
                                        gsmmux->rxbuf,
                                        gsmmux->len);
      gsmmux->len -= recv;
      gsmmux->offset -= recv;
      if (gsmmux->len != 0)
        {
          memmove(gsmmux->rxbuf, gsmmux->rxbuf + recv, gsmmux->len);
          return 0;
        }
      else
        {
          gsmmux->state = GSMMUX_STATE_INIT;
        }
    }

  /* GSM0710 frame format:
   *
   * | Flag | Address | Control | Length | Data | FCS | Flag |
   *
   */

  for (i = 0; i < len; i++)
    {
      uint8_t ch = ((FAR const char *)buf)[i];
      uint8_t dlci;

      switch (gsmmux->state)
        {
        case GSMMUX_STATE_INIT:
          if (ch == GSMMUX_FLAG)
            {
              gsmmux->state = GSMMUX_STATE_ADDRESS;
              gsmmux->fcs = 0;
              gsmmux->offset = 0;
              gsmmux->len = 0;
            }
          break;
        case GSMMUX_STATE_ADDRESS:
          dlci = ch >> 2;
          if (ch == GSMMUX_FLAG)
            {
              gsmmux_warn("addr error, skip %02x\n", ch);
            }
          else if (dlci > gsmmux->nchannel)
            {
              gsmmux_warn("addr error: %d, skip %02x\n", dlci, ch);
              gsmmux->state = GSMMUX_STATE_INIT;
            }
          else
            {
              gsmmux->dlci = dlci;
              gsmmux->fcs = gsmmux_frame_fcs_add(gsmmux->fcs, ch);
              gsmmux->state = GSMMUX_STATE_CONTROL;
            }
          break;
        case GSMMUX_STATE_CONTROL:
          gsmmux->fcs = gsmmux_frame_fcs_add(gsmmux->fcs, ch);
          gsmmux->ctrl = ch;
          gsmmux->state = GSMMUX_STATE_LEN0;
          break;
        case GSMMUX_STATE_LEN0:
          gsmmux->len = ch >> 1;
          if (gsmmux->len > gsmmux->mru)
            {
              gsmmux_warn("len0 error, skip %02x\n", ch);
              gsmmux->state = GSMMUX_STATE_INIT;
            }

          gsmmux->fcs = gsmmux_frame_fcs_add(gsmmux->fcs, ch);
          if (ch & GSMMUX_EA)
            {
              gsmmux->state = gsmmux->len ? GSMMUX_STATE_DATA :
                              GSMMUX_STATE_FCS;
            }
          else
            {
              gsmmux->state = GSMMUX_STATE_LEN1;
            }
          break;
        case GSMMUX_STATE_LEN1:
          gsmmux->fcs = gsmmux_frame_fcs_add(gsmmux->fcs, ch);
          gsmmux->len = gsmmux->len | (ch << 7);
          if (gsmmux->len > gsmmux->mru)
            {
              gsmmux_warn("len1 error, skip %02x\n", ch);
              gsmmux->state = GSMMUX_STATE_INIT;
            }
          else
            {
              gsmmux->state = GSMMUX_STATE_DATA;
            }
          break;
        case GSMMUX_STATE_DATA:
          gsmmux->rxbuf[gsmmux->offset++] = ch;
          if (gsmmux->offset == gsmmux->len)
            {
              gsmmux->state = GSMMUX_STATE_FCS;
            }
          break;
        case GSMMUX_STATE_FCS:
          if (gsmmux->fcs == ch)
            {
              gsmmux->state = GSMMUX_STATE_END;
            }
          else
            {
              gsmmux_warn("fcs error, skip %02x, expected %02x\n",
                          ch, gsmmux->fcs);
              gsmmux->state = GSMMUX_STATE_INIT;
            }
          break;
        case GSMMUX_STATE_END:
          if (ch == GSMMUX_FLAG)
            {
              DEBUGASSERT(gsmmux->offset == gsmmux->len);
              size_t recv = gsmmux_handle_frame(gsmmux, gsmmux->dlci,
                                                gsmmux->ctrl,
                                                gsmmux->rxbuf,
                                                gsmmux->len);
              gsmmux->len -= recv;
              gsmmux->offset -= recv;
              if (gsmmux->len != 0)
                {
                  memmove(gsmmux->rxbuf, gsmmux->rxbuf + recv, gsmmux->len);
                  return i;
                }
              else
                {
                  gsmmux->state = GSMMUX_STATE_INIT;
                }
            }
          else
            {
              gsmmux_warn("end error, skip %02x\n", ch);
              gsmmux->state = GSMMUX_STATE_INIT;
            }
          break;
        default:
          gsmmux_warn("unknown %d, skip %02x\n", gsmmux->state, ch);
          gsmmux->state = GSMMUX_STATE_INIT;
          break;
        }
    }

  return len;
}

static int gsmmux_tty_setup(FAR struct uart_dev_s *dev)
{
  return OK;
}

static void gsmmux_tty_shutdown(FAR struct uart_dev_s *dev)
{
}

static int gsmmux_tty_attach(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;

  return gsmmux_uart_open(gsmmux);
}

static void gsmmux_tty_detach(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;

  gsmmux_uart_close(gsmmux);
}

static int gsmmux_tty_ioctl(FAR struct file *filep, int cmd,
                            unsigned long arg)
{
  /* Data cannot be forwarded to a physical serial port;
   * otherwise, the physical serial port configuration will
   * be modified when the pseudo-device is set up with ioctl.
   * For example, if you register a /dev/console pseudo-node and
   * execute tcsetattr(infd, TCSANOW, &cfg);
   * the physical serial port will be changed to console.
   */

  return -ENOTTY;
}

static ssize_t gsmmux_tty_recvbuf(FAR struct uart_dev_s *dev,
                                  FAR void *buf, size_t len)
{
  FAR struct uart_buffer_s *rxbuf = &dev->recv;
  FAR uint8_t *dest = (FAR uint8_t *)buf;
  irqstate_t flags;
  size_t copy;

  if (len == 0 || !gsmmux_tty_rxavailable(dev))
    {
      return 0;
    }

  flags = uart_spinlock(dev, true);
  len = MIN(len, gsmmux_uart_rxavailable(rxbuf));
  copy = MIN(len, rxbuf->size - rxbuf->tail);

  memcpy(dest, rxbuf->buffer + rxbuf->tail, copy);
  if (copy < len)
    {
      memcpy(dest + copy, rxbuf->buffer, len - copy);
    }

  rxbuf->tail += len;
  if (rxbuf->tail >= rxbuf->size)
    {
      rxbuf->tail -= rxbuf->size;
    }

  uart_spinunlock(dev, true, flags);
  return len;
}

static ssize_t gsmmux_tty_sendbuf(FAR struct uart_dev_s *dev,
                                  FAR const void *buf, size_t len)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  return gsmmux_send_data(channel, buf, len);
}

static void gsmmux_tty_rxint(FAR struct uart_dev_s *dev, bool enable)
{
}

static bool gsmmux_tty_rxavailable(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;
  irqstate_t flags;
  size_t ret;

  /* Check if physical layer has data before reading */

  gsmmux_uart_read(gsmmux);

  flags = uart_spinlock(dev, true);
  ret = gsmmux_uart_rxavailable(&dev->recv) > 0;
  uart_spinunlock(dev, true, flags);
  return ret;
}

static void gsmmux_tty_dmasend(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  ssize_t ret;

  ret = gsmmux_send_data(channel, dev->dmatx.buffer,
                         dev->dmatx.length);
  dev->dmatx.nbytes = ret > 0 ? ret : 0;
  uart_xmitchars_done(dev);
}

static void gsmmux_tty_dmareceive(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;
  FAR struct uart_dmaxfer_s *rxbuf = &channel->uart.dmarx;
  FAR const uint8_t *buf = gsmmux->rxbuf;
  size_t len = gsmmux->len;

  if (len <= rxbuf->length)
    {
      memcpy(rxbuf->buffer, buf, len);
    }
  else
    {
      len = MIN(len, rxbuf->length + rxbuf->nlength);
      memcpy(rxbuf->buffer, buf, rxbuf->length);
      memcpy(rxbuf->nbuffer, buf + rxbuf->length, len - rxbuf->length);
    }

  rxbuf->nbytes = len;
  gsmmux->recv = len;
  uart_recvchars_done(&channel->uart);
}

static void gsmmux_tty_dmarxfree(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;

  /* Check if physical layer has data before reading */

  gsmmux_uart_read(gsmmux);
}

static void gsmmux_tty_dmatxavail(FAR struct uart_dev_s *dev)
{
  uart_xmitchars_dma(dev);
}

static void gsmmux_tty_txint(FAR struct uart_dev_s *dev, bool enable)
{
}

static bool gsmmux_tty_txready(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;
  return uart_txready(gsmmux->uart);
}

static bool gsmmux_tty_txempty(FAR struct uart_dev_s *dev)
{
  FAR struct gsmmux_channel_s *channel = dev->priv;
  FAR struct gsmmux_s *gsmmux = channel->gsmmux;
  return uart_txempty(gsmmux->uart);
}

static int gsmmux_tty_register(FAR struct gsmmux_s *gsmmux,
                               FAR const char *path,
                               FAR struct gsmmux_channel_s *channel)
{
  FAR char *buffer = kmm_malloc(CONFIG_UART_GSMMUX_TXBUF_SIZE +
                                CONFIG_UART_GSMMUX_RXBUF_SIZE);
  int ret;

  if (buffer == NULL)
    {
      return -ENOMEM;
    }

  channel->gsmmux = gsmmux;
  channel->uart.ops = &g_gsmmux_ops;
  channel->uart.priv = channel;
  channel->uart.xmit.size = CONFIG_UART_GSMMUX_TXBUF_SIZE;
  channel->uart.xmit.buffer = buffer;
  channel->uart.recv.size = CONFIG_UART_GSMMUX_RXBUF_SIZE;
  channel->uart.recv.buffer = buffer + CONFIG_UART_GSMMUX_TXBUF_SIZE;

  if (strcmp(path, "/dev/console") == 0)
    {
      channel->uart.isconsole = true;
      ret = uart_register("/dev/ttyGSM0", &channel->uart);
      if (ret < 0)
        {
          goto errout;
        }
    }

  ret = uart_register(path, &channel->uart);
  if (ret < 0)
    {
      gsmmux_err("Failed to register %s\n", path);
      goto errout;
    }

  return OK;
errout:
  kmm_free(buffer);
  return ret;
}

static void gsmmux_tty_unregister(FAR struct gsmmux_channel_s *channel,
                                  FAR const char *path)
{
  if (strcmp(path, "/dev/console") == 0)
    {
      unregister_driver("/dev/ttyGSM0");
    }

  unregister_driver(path);
  kmm_free(channel->uart.xmit.buffer);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: gsmmux_register
 *
 * Description:
 *   Register a gsmmux device with multiple tty channels.
 *
 * Note:
 *   This API is called by the application, initializes the serial port
 *   to gsmmux mode according to config,
 *   and creates the device specified by devlist.
 *
 ****************************************************************************/

FAR struct gsmmux_s *
gsmmux_register(FAR const char *devname,
                FAR const struct gsm_config *config,
                FAR const char *devlist[], size_t devcount)
{
  FAR struct gsmmux_s *gsmmux;
  size_t i;
  int ret;

  /* Check the validity of the parameters */

  if (devcount == 0 || devname == NULL ||
      config->mru < GSMMUX_MIN_MRU ||
      config->mtu < GSMMUX_MIN_MTU ||
      config->encapsulation != GSMMUX_BASIC_MODE)
    {
      gsmmux_err("Invalid parameter\n");
      return NULL;
    }

  gsmmux = kmm_zalloc(sizeof(struct gsmmux_s) + config->mru +
                      devcount * sizeof(struct gsmmux_channel_s));
  if (gsmmux == NULL)
    {
      gsmmux_err("Failed to allocate memory\n");
      return NULL;
    }

  gsmmux->devname = devname;
  gsmmux->nchannel = devcount;
  gsmmux->mru = config->mru;
  gsmmux->mtu = config->mtu;
  gsmmux->initiator = config->initiator;
  gsmmux->encapsulation = config->encapsulation;
  list_initialize(&gsmmux->head);

  /* Receive frame buffer */

  gsmmux->rxbuf = (FAR uint8_t *)&gsmmux->channels[devcount];

  /* Register pseudo uart devices */

  for (i = 0; i < devcount; i++)
    {
      ret = gsmmux_tty_register(gsmmux, devlist[i], &gsmmux->channels[i]);
      if (ret < 0)
        {
          gsmmux_err("Failed to register %s\n", devlist[i]);
          goto errout;
        }
    }

  return gsmmux;

errout:
  while (i-- > 0)
    {
      gsmmux_tty_unregister(&gsmmux->channels[i], devlist[i]);
    }

  kmm_free(gsmmux);
  return NULL;
}

/****************************************************************************
 * Name: gsmmux_initialize
 *
 * Description:
 *   Initialize the gsmmux device.
 *
 ****************************************************************************/

#ifdef CONFIG_UART_GSMMUX_TTYGSM
int gsmmux_initialize(void)
{
  FAR struct gsmmux_s *gsmmux;
  const struct gsm_config config =
  {
    .encapsulation = GSMMUX_BASIC_MODE,
    .initiator = GSMMUX_SLAVE,
    .mru = CONFIG_UART_GSMMUX_BUFFER_SIZE,
    .mtu = CONFIG_UART_GSMMUX_BUFFER_SIZE,
  };

  FAR const char *devlist[] =
  {
#ifdef CONFIG_UART_GSMMUX_CONSOLE
    "/dev/console",
#endif
#ifdef CONFIG_UART_GSMMUX_TTYGSM1
    "/dev/ttyGSM1",
#endif
#ifdef CONFIG_UART_GSMMUX_TTYGSM2
    "/dev/ttyGSM2",
#endif
#ifdef CONFIG_UART_GSMMUX_TTYGSM3
    "/dev/ttyGSM3",
#endif
  };

  gsmmux = gsmmux_register(CONFIG_UART_GSMMUX_DEVNAME, &config,
                           devlist, nitems(devlist));
  if (gsmmux == NULL)
    {
      gsmmux_err("Failed to register gsmmux\n");
      return -EINVAL;
    }

  return OK;
}
#endif
