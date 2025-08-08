/****************************************************************************
 * drivers/serial/uart_cmux.c
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
#include <debug.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <semaphore.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <sys/poll.h>
#include <syslog.h>
#include <unistd.h>

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/types.h>

#include <nuttx/fs/fs.h>
#include <nuttx/init.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <nuttx/wqueue.h>
#include <nuttx/semaphore.h>
#include <nuttx/serial/gsmmux.h>
#include <nuttx/serial/serial.h>
#include <nuttx/spinlock.h>
#include <nuttx/streams.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Cmux supports up to 64 channels */

#define CMUX_CHANNEL_MAX 64

#define CMUX_BASIC_MODE 0
#define CMUX_ADVANCED_MODE 1

#define CMUX_MASTER 1
#define CMUX_SLAVE  0

#define CMUX_EA 0x01
#define CMUX_PF 0x10

#define CMUX_CMD 0x01
#define CMUX_RSP 0x00

/* Basic mode flag for frame start and end */

#define CMUX_BASIC_FLAG 0xf9

/* Advanced mode flag for frame start and end */

#define CMUX_ADV_FALG 0x7e

/* Advanced mode escaped symbols */

#define CMUX_ADV_ESC 0x7d

/* Advanced mode escaped mask */

#define CMUX_ADV_ESC_MASK 0x20

/* Only supports basic mode */

#define CMUX_FLAG CMUX_BASIC_FLAG

/* GSM0710 frame type */

#define CMUX_UI   0x03 /* Unnumbered Information                   */
#define CMUX_DM   0x1f /* Disconnected Mode                        */
#define CMUX_SABM 0x3f /* Set Asynchronous Balanced Mode           */
#define CMUX_DISC 0x53 /* Disconnect                               */
#define CMUX_UA   0x73 /* Unnumbered Acknowledgement               */
#define CMUX_UIH  0xef /* Unnumbered Information with Header check */

/* Control channel commands */

#define CMD_NSC    0x08  /* Non Supported Command Response         */
#define CMD_TEST   0x10  /* Test Command                           */
#define CMD_PSC    0x20  /* Power Saving Control                   */
#define CMD_RLS    0x28  /* Remote Line Status Command             */
#define CMD_FCOFF  0x30  /* Flow Control Off Command               */
#define CMD_PN     0x40  /* DLC parameter negotiation              */
#define CMD_RPN    0x48  /* Remote Port Negotiation Command        */
#define CMD_FCON   0x50  /* Flow Control On Command                */
#define CMD_CLD    0x60  /* Multiplexer close down                 */
#define CMD_SNC    0x68  /* Service Negotiation Command            */
#define CMD_MSC    0x38  /* Modem Status Command                   */

#define CMUX_IS_COMMAND(cmd, addr) (((addr) & ~CMUX_CMD) == (cmd))
#define CMUX_ADDR(cmd, dlci)       (CMUX_EA | ((cmd) << 1) | (dlci) << 2)
#define CMUX_IS_PF(cmd)            (((cmd) & CMUX_PF) == CMUX_PF)

/* The default maximum transmission frame size of cmux */

#define CMUX_MIN_MTU 8
#define CMUX_MAX_MTU (127 + CMUX_MIN_MTU)
#define CMUX_MIN_MRU 8
#define CMUX_MAX_MRU (127 + CMUX_MIN_MRU)

#define CMUX_DEFAULE_MRU 127
#define CMUX_DEFAULE_MTU 127

/* Debug configuration */

#define CMUX_DUMP_LOG   0x01
#define CMUX_DUMP_DATA    0x02
#define CMUX_DUMP_PAYLOAD 0x04

#define cmux_err(fmt, ...) \
    if (CONFIG_UART_CMUX_DEBUG & CMUX_DUMP_LOG)        \
      {                                             \
        syslog(LOG_ERR, fmt, ##__VA_ARGS__);        \
      }

#define cmux_warn(fmt, ...)                         \
    if (CONFIG_UART_CMUX_DEBUG & CMUX_DUMP_LOG)        \
      {                                             \
        syslog(LOG_WARNING, fmt, ##__VA_ARGS__);    \
      }

#  define cmux_dump_payload(msg, buf, len)          \
    if (CONFIG_UART_CMUX_DEBUG & CMUX_DUMP_PAYLOAD)      \
      {                                             \
        lib_dumpbuffer(msg, (uint8_t *)(buf), len); \
      }

#  define cmux_dump_data(msg, buf, len)             \
    if (CONFIG_UART_CMUX_DEBUG & CMUX_DUMP_DATA)         \
      {                                             \
        lib_dumpbuffer(msg, (uint8_t *)(buf), len); \
      }

#ifdef CONFIG_UART_CMUX_LOG
#  if defined(CONFIG_UART_CMUX_LOG_TO_CONSOLE)
#    define CMUX_LOG g_cmux_console
#  else
#    define CMUX_LOG g_cmux_log
#  endif
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct cmux_s
{
  FAR struct cmux_channel_s *channels[CMUX_CHANNEL_MAX];
  FAR const char *devname;
  mutex_t txlock;
  mutex_t rxlock;
  int refcnt;

  /* Framing Layer */

  FAR uint8_t txbuf[CMUX_MAX_MTU];
  FAR uint8_t rxbuf[CMUX_MAX_MRU];
  size_t offset;
  size_t len;
  uint8_t addr;
  uint8_t ctrl;
  uint8_t fcs;
  uint8_t state;

  /* GSM0710 protocol configuration, Retry and timeout configuration
   * are currently not supported
   */

  unsigned int encapsulation;
  unsigned int initiator;
  size_t mtu;
  size_t mru;

  /* Data transmission related */

  FAR struct uart_dev_s *uart;
  struct file filep;
  struct pollfd fds;
  struct work_s work;
};

struct cmux_channel_s
{
  struct uart_dev_s uart;
  FAR struct cmux_s *cmux;
  int dlci;
};

enum cmux_state
{
  CMUX_INIT,
  CMUX_ADDRESS,
  CMUX_CONTROL,
  CMUX_LEN0,
  CMUX_LEN1,
  CMUX_DATA,
  CMUX_FCS,
  CMUX_END,
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  cmux_tty_setup(FAR struct uart_dev_s *dev);
static void cmux_tty_shutdown(FAR struct uart_dev_s *dev);
static int  cmux_tty_attach(FAR struct uart_dev_s *dev);
static void cmux_tty_detach(FAR struct uart_dev_s *dev);
static int  cmux_tty_ioctl(FAR struct file *filep, int cmd,
                           unsigned long arg);
static int  cmux_tty_receive(FAR struct uart_dev_s *dev,
                             FAR unsigned int *status);
static void cmux_tty_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool cmux_tty_rxavailable(FAR struct uart_dev_s *dev);
static void cmux_tty_dmasend(FAR struct uart_dev_s *dev);
static void cmux_tty_dmareceive(FAR struct uart_dev_s *dev);
static void cmux_tty_dmarxfree(FAR struct uart_dev_s *dev);
static void cmux_tty_dmatxavail(FAR struct uart_dev_s *dev);
static void cmux_tty_send(FAR struct uart_dev_s *dev, int ch);
static void cmux_tty_txint(FAR struct uart_dev_s *dev, bool enable);
static bool cmux_tty_txready(FAR struct uart_dev_s *dev);
static bool cmux_tty_txempty(FAR struct uart_dev_s *dev);
static int cmux_uart_write(FAR struct cmux_s *cmux, FAR const void *buffer,
                           size_t buflen);
static int cmux_uart_open(FAR struct cmux_s *cmux);
static int cmux_uart_close(FAR struct cmux_s *cmux);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_cmux_tty_ops =
{
  cmux_tty_setup,
  cmux_tty_shutdown,
  cmux_tty_attach,
  cmux_tty_detach,
  cmux_tty_ioctl,
  cmux_tty_receive,
  cmux_tty_rxint,
  cmux_tty_rxavailable,
#ifdef CONFIG_SERIAL_IFLOWCONTROL
  NULL,
#endif
  cmux_tty_dmasend,
  cmux_tty_dmareceive,
  cmux_tty_dmarxfree,
  cmux_tty_dmatxavail,
  cmux_tty_send,
  cmux_tty_txint,
  cmux_tty_txready,
  cmux_tty_txempty,
};

static const unsigned char g_cmux_cfs_table[256] =
{
  0x00, 0x91, 0xe3, 0x72, 0x07, 0x96, 0xe4, 0x75,
  0x0e, 0x9f, 0xed, 0x7c, 0x09, 0x98, 0xea, 0x7b,
  0x1c, 0x8d, 0xff, 0x6e, 0x1b, 0x8a, 0xf8, 0x69,
  0x12, 0x83, 0xf1, 0x60, 0x15, 0x84, 0xf6, 0x67,
  0x38, 0xa9, 0xdb, 0x4a, 0x3f, 0xae, 0xdc, 0x4d,
  0x36, 0xa7, 0xd5, 0x44, 0x31, 0xa0, 0xd2, 0x43,
  0x24, 0xb5, 0xc7, 0x56, 0x23, 0xb2, 0xc0, 0x51,
  0x2a, 0xbb, 0xc9, 0x58, 0x2d, 0xbc, 0xce, 0x5f,
  0x70, 0xe1, 0x93, 0x02, 0x77, 0xe6, 0x94, 0x05,
  0x7e, 0xef, 0x9d, 0x0c, 0x79, 0xe8, 0x9a, 0x0b,
  0x6c, 0xfd, 0x8f, 0x1e, 0x6b, 0xfa, 0x88, 0x19,
  0x62, 0xf3, 0x81, 0x10, 0x65, 0xf4, 0x86, 0x17,
  0x48, 0xd9, 0xab, 0x3a, 0x4f, 0xde, 0xac, 0x3d,
  0x46, 0xd7, 0xa5, 0x34, 0x41, 0xd0, 0xa2, 0x33,
  0x54, 0xc5, 0xb7, 0x26, 0x53, 0xc2, 0xb0, 0x21,
  0x5a, 0xcb, 0xb9, 0x28, 0x5d, 0xcc, 0xbe, 0x2f,
  0xe0, 0x71, 0x03, 0x92, 0xe7, 0x76, 0x04, 0x95,
  0xee, 0x7f, 0x0d, 0x9c, 0xe9, 0x78, 0x0a, 0x9b,
  0xfc, 0x6d, 0x1f, 0x8e, 0xfb, 0x6a, 0x18, 0x89,
  0xf2, 0x63, 0x11, 0x80, 0xf5, 0x64, 0x16, 0x87,
  0xd8, 0x49, 0x3b, 0xaa, 0xdf, 0x4e, 0x3c, 0xad,
  0xd6, 0x47, 0x35, 0xa4, 0xd1, 0x40, 0x32, 0xa3,
  0xc4, 0x55, 0x27, 0xb6, 0xc3, 0x52, 0x20, 0xb1,
  0xca, 0x5b, 0x29, 0xb8, 0xcd, 0x5c, 0x2e, 0xbf,
  0x90, 0x01, 0x73, 0xe2, 0x97, 0x06, 0x74, 0xe5,
  0x9e, 0x0f, 0x7d, 0xec, 0x99, 0x08, 0x7a, 0xeb,
  0x8c, 0x1d, 0x6f, 0xfe, 0x8b, 0x1a, 0x68, 0xf9,
  0x82, 0x13, 0x61, 0xf0, 0x85, 0x14, 0x66, 0xf7,
  0xa8, 0x39, 0x4b, 0xda, 0xaf, 0x3e, 0x4c, 0xdd,
  0xa6, 0x37, 0x45, 0xd4, 0xa1, 0x30, 0x42, 0xd3,
  0xb4, 0x25, 0x57, 0xc6, 0xb3, 0x22, 0x50, 0xc1,
  0xba, 0x2b, 0x59, 0xc8, 0xbd, 0x2c, 0x5e, 0xcf
};

#if defined (CONFIG_UART_CMUX_CONSOLE) || defined(CONFIG_UART_CMUX_LOG) || \
    defined(CONFIG_UART_CMUX_GSMTTY1) || defined(CONFIG_UART_CMUX_GSMTTY2)
static FAR struct cmux_s *g_cmux;
#endif

#ifdef CONFIG_UART_CMUX_CONSOLE
static char g_cmux_console_buffer[2][CONFIG_UART_CMUX_BUFSIZE];

static struct cmux_channel_s g_cmux_console =
{
  .uart.ops = &g_cmux_tty_ops,
  .uart.isconsole = true,
  .uart.priv = &g_cmux_console,
  .uart.xmit.size = CONFIG_UART_CMUX_BUFSIZE,
  .uart.xmit.buffer = g_cmux_console_buffer[0],
  .uart.recv.size = CONFIG_UART_CMUX_BUFSIZE,
  .uart.recv.buffer = g_cmux_console_buffer[1],
  .mutex = NXMUTEX_INITIALIZER,
};
#endif

#if defined (CONFIG_UART_CMUX_LOG) && !defined(CONFIG_UART_CMUX_CONSOLE)
static char g_cmux_log_buffer[2][CONFIG_UART_CMUX_BUFSIZE];

static struct cmux_channel_s g_cmux_log =
{
  .uart.ops = &g_cmux_tty_ops,
  .uart.isconsole = false,
  .uart.priv = &g_cmux_log,
  .uart.xmit.size = CONFIG_UART_CMUX_BUFSIZE,
  .uart.xmit.buffer = g_cmux_log_buffer[0],
  .uart.recv.size = CONFIG_UART_CMUX_BUFSIZE,
  .uart.recv.buffer = g_cmux_log_buffer[1],
  .mutex = NXMUTEX_INITIALIZER,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int cmux_frame_fcs(FAR const uint8_t *buffer, int len)
{
  int fcs = 0xff;
  int i;

  for (i = 0; i < len; i++)
    {
      fcs = (fcs >> 8) ^ g_cmux_cfs_table[(fcs ^ buffer[i]) & 0xff];
    }

  return 0xff - fcs;
}

static int cmux_frame_send(FAR struct cmux_s *cmux, int dlci, int cmd,
                           int type, FAR const void *buffer, int buflen)
{
  FAR uint8_t *xmit = cmux->txbuf;
  uint8_t fcs;
  int len;
  int ret;

  xmit[0] = CMUX_FLAG;
  xmit[1] = CMUX_ADDR(cmd, dlci);
  xmit[2] = type;

  DEBUGASSERT(buflen <= 0x7fff);

  if (buflen <= 127)
    {
      xmit[3] = (buflen << 1) | CMUX_EA;
      len = 4;
    }
  else
    {
      xmit[3] = buflen << 1;
      xmit[4] = buflen >> 7;
      len = 5;
    }

  fcs = cmux_frame_fcs(xmit + 1, len - 1);
  DEBUGASSERT(cmux->encapsulation == CMUX_BASIC_MODE);
  memcpy(xmit + len, buffer, buflen);

  len += buflen;
  xmit[len++] = fcs;
  xmit[len++] = CMUX_FLAG;
  ret = cmux_uart_write(cmux, xmit, len);
  if (ret < 0)
    {
      return ret;
    }

  return buflen;
}

static int cmux_send_data_nolock(FAR struct cmux_channel_s *channel,
                                 FAR const char *buffer, int buflen)
{
  FAR struct cmux_s *cmux = channel->cmux;
  int dlci = channel->dlci;

  if (buflen == 0)
    {
      return 0;
    }

  cmux_dump_payload("UIH --->:", buffer, buflen);

  /* The length of a single send is limited to mtu, and data exceeding
   * the length needs to be sent multiple times
   */

  return cmux_frame_send(cmux, dlci, CMUX_CMD, CMUX_UIH, buffer, buflen);
}

static int cmux_send_data(FAR struct cmux_channel_s *channel,
                          FAR const char *buffer, int buflen)
{
  FAR struct cmux_s *cmux = channel->cmux;
  int slen = 0;

  /* The length of a single send is limited to mtu, and data exceeding
   * the length needs to be sent multiple times
   */

  while (buflen)
    {
      int len;
      len = buflen > cmux->mtu ? cmux->mtu : buflen;
      nxmutex_lock(&cmux->txlock);
      len = cmux_send_data_nolock(channel, buffer + slen, len);
      nxmutex_unlock(&cmux->txlock);
      slen += len;
      buflen -= len;
    }

  return slen;
}

static int cmux_frame_send_cmd(FAR struct cmux_s *cmux, int dlci, int type)
{
  if (CONFIG_UART_CMUX_DEBUG & CMUX_DUMP_PAYLOAD)
    {
      const char *cmd = type == CMUX_SABM ? "SABM" :
                        type == CMUX_DISC ? "DISC" :
                        type == CMUX_UA ? "UA" :
                        type == CMUX_DM ? "DM" : "Unknown";
      syslog(LOG_INFO, "CMD --->: (%s) %02x\n", cmd, type);
    }

  return cmux_frame_send(cmux, dlci, CMUX_CMD, type, NULL, 0);
}

static int cmux_frame_send_ctrl(FAR struct cmux_s *cmux, int dlci, int cmd,
                                void *data, int len)
{
  uint8_t buffer[len + 3];
  buffer[0] = cmd << 2 | CMUX_CMD << 1 | CMUX_EA;
  buffer[1] = (len + 1) << 1 | CMUX_EA;
  buffer[2] = dlci << 2 | 1 << 1 | CMUX_EA;
  memcpy(&buffer[3], data, len);

  if (CONFIG_UART_CMUX_DEBUG & CMUX_DUMP_PAYLOAD)
    {
      syslog(LOG_INFO, "Ctrl cmd --->: %02x\n", cmd);
    }

  return cmux_frame_send(cmux, 0, CMUX_CMD, CMUX_UIH, buffer, len + 3);
}

static void cmux_frame_data_receive(FAR struct cmux_channel_s *channel,
                                    FAR uint8_t *buffer, int buflen)
{
  FAR struct uart_dmaxfer_s *xfer = &channel->uart.dmarx;
  int len;
  int rlen;

  uart_recvchars_dma(&channel->uart);

  /* Copy data directly to the uart buffer, not processed in dmareceive */

  len = xfer->length + xfer->nlength;
  if (buflen <= xfer->length)
    {
      rlen = buflen;
      memcpy(xfer->buffer, buffer, buflen);
    }
  else
    {
      rlen = len >= buflen ? buflen : len;
      memcpy(xfer->buffer, buffer, xfer->length);
      memcpy(xfer->nbuffer, buffer + xfer->length, rlen - xfer->length);
    }

  xfer->nbytes = rlen;
  uart_recvchars_done(&channel->uart);
}

static void cmux_frame_dlci0_handle(FAR struct cmux_s *cmux,
                                    FAR uint8_t *buffer, int buflen)
{
  uint8_t cmd = buffer[0] >> 2;

  switch (cmd)
    {
    case CMD_MSC:
      {
        int dlci = buffer[1] >> 2;
        if (buffer[0] & 0x02)
          {
            buffer[0] = buffer[0] & ~(1 << 1);
            cmux_frame_send(cmux, 0, CMUX_RSP, CMUX_UIH, buffer, buflen);
            uint8_t buf[4];
            buf[0] = 0xe3;
            buf[1] = 0x05;
            buf[2] = (dlci << 2) | 0x03;
            buf[3] = 0x8d;
            cmux_frame_send(cmux, 0, CMUX_CMD, CMUX_UIH | CMUX_PF, buf,
                            sizeof(buf));
          }
      }
      break;
    }
}

static void cmux_frame_handle(FAR struct cmux_s *cmux, int addr, int ctrl,
                              FAR uint8_t *data, int len)
{
  int dlci = addr >> 2;
  FAR struct cmux_channel_s *channel;

  channel = dlci == 0 ? NULL : cmux->channels[dlci];
  if ((dlci != 0 && channel == NULL) || dlci > nitems(cmux->channels))
    {
      cmux_warn("invalid dlci: %d\n", dlci);
      cmux_frame_send_cmd(cmux, dlci, CMUX_UA);
      return;
    }

  switch (ctrl)
    {
    case CMUX_UI:
    case CMUX_UIH:
    case CMUX_UI | CMUX_PF:
    case CMUX_UIH | CMUX_PF:
      if (dlci == 0)
        {
          cmux_dump_payload("<--- ctrl cmd:", data, len);
          cmux_frame_dlci0_handle(cmux, data, len);
        }
      else if (channel != NULL)
        {
          cmux_dump_payload("<--- UIH:", data, len);
          cmux_frame_data_receive(channel, data, len);
        }
      break;
    case CMUX_SABM:
      cmux_dump_payload("<--- SABM:", data, len);
      if (dlci == 0 || channel != NULL)
        {
          cmux_frame_send_cmd(cmux, dlci, CMUX_UA);
        }
      break;
    case CMUX_DISC:
      cmux_dump_payload("<--- DISC:", data, len);
      cmux_frame_send_cmd(cmux, 0, CMUX_DM);
      break;
    case CMUX_UA:
      cmux_dump_payload("<--- UA:", data, len);
      break;
    case CMUX_DM:
      cmux_dump_payload("<--- DM:", data, len);
      break;
    default:
      cmux_warn("unknown frame type: %02x\n", ctrl);
      break;
    }
}

static uint8_t cmux_fcs_add(uint8_t fcs, uint8_t ch)
{
  return g_cmux_cfs_table[(fcs ^ ch) & 0xff];
}

void cmux_frame_received(FAR struct cmux_s *cmux, FAR uint8_t *buffer,
                         size_t buflen)
{
  int flag = CMUX_FLAG;
  int i;

  if (buffer == NULL || buflen == 0)
    {
      return;
    }

  /* GSM0710 frame format:
   *
   * | Flag | Address | Control | Length | Data | FCS | Flag |
   *
   */

  for (i = 0; i < buflen; i++)
    {
      uint8_t ch = buffer[i];

      switch (cmux->state)
        {
        case CMUX_INIT:
          cmux->fcs = 0xff;
          cmux->offset = 0;
          cmux->len = 0;
          if (ch == flag)
            {
              cmux->state = CMUX_ADDRESS;
            }
          break;
        case CMUX_ADDRESS:
          if ((ch & 0xfe) > 64)
            {
              cmux->state = CMUX_INIT;
            }
          else if (ch == flag)
            {
              cmux->state = CMUX_ADDRESS;
            }
          else
            {
              cmux->addr = ch & 0xfe;
              cmux->fcs = cmux_fcs_add(cmux->fcs, ch);
              cmux->state = CMUX_CONTROL;
            }
          break;
        case CMUX_CONTROL:
          cmux->fcs = cmux_fcs_add(cmux->fcs, ch);
          cmux->ctrl = ch;
          cmux->state = CMUX_LEN0;
          break;
        case CMUX_LEN0:
          cmux->fcs = cmux_fcs_add(cmux->fcs, ch);
          if (ch & CMUX_EA)
            {
              cmux->len = ch >> 1;
              cmux->state = cmux->len == 0 ? CMUX_FCS : CMUX_DATA;
            }
          else
            {
              cmux->len = ch << 7;
              cmux->state = CMUX_LEN1;
            }
          break;
        case CMUX_LEN1:
          cmux->fcs = cmux_fcs_add(cmux->fcs, ch);
          cmux->len = (ch << 7) | cmux->len;
          cmux->state = CMUX_DATA;
          break;
        case CMUX_DATA:
          cmux->rxbuf[cmux->offset++] = ch;
          if (cmux->offset == cmux->len)
            {
              cmux->state = CMUX_FCS;
            }
          break;
        case CMUX_FCS:
          cmux->fcs = 0xff - cmux->fcs;
          if (cmux->fcs == ch)
            {
              cmux->state = CMUX_END;
            }
          else
            {
              cmux->state = CMUX_INIT;
            }
          break;
        case CMUX_END:
          if (ch == flag)
            {
              cmux_frame_handle(cmux, cmux->addr, cmux->ctrl,
                                cmux->rxbuf, cmux->len);
              cmux->state = CMUX_INIT;
            }
          else
            {
              cmux->state = CMUX_INIT;
            }
          break;
        default:
          cmux->state = CMUX_INIT;
          break;
        }
    }
}

static int cmux_tty_register(FAR struct cmux_s *cmux, FAR const char *path,
                             FAR struct cmux_channel_s *channel)
{
  int i;
  int ret;

  for (i = 0; i < nitems(cmux->channels); i++)
    {
      if (cmux->channels[i] == NULL)
        {
          break;
        }
    }

  if (i >= nitems(cmux->channels))
    {
      return -ENOMEM;
    }

  channel->dlci = i;
  channel->cmux = cmux;
  cmux->channels[i] = channel;

  ret = uart_register(path, &channel->uart);
  if (ret < 0)
    {
      cmux->channels[i] = NULL;
      return ret;
    }

  return OK;
}

static int cmux_tty_setup(FAR struct uart_dev_s *dev)
{
  return OK;
}

static void cmux_tty_shutdown(FAR struct uart_dev_s *dev)
{
}

static int cmux_tty_attach(FAR struct uart_dev_s *dev)
{
  FAR struct cmux_channel_s *channel = dev->priv;
  FAR struct cmux_s *cmux = channel->cmux;

  if (cmux->refcnt == 0)
    {
      cmux_uart_open(cmux);
    }

  cmux->refcnt++;
  if (cmux->initiator == CMUX_MASTER)
    {
      uint8_t value = 0x8d;
      cmux_frame_send_cmd(cmux, channel->dlci, CMUX_SABM);
      cmux_frame_send_ctrl(cmux, channel->dlci, CMD_MSC, &value, 1);
    }

  return OK;
}

static void cmux_tty_detach(FAR struct uart_dev_s *dev)
{
  FAR struct cmux_channel_s *channel = dev->priv;
  FAR struct cmux_s *cmux = channel->cmux;

  cmux->refcnt--;
  if (cmux->refcnt == 0)
    {
      cmux_uart_close(cmux);
    }
}

static int cmux_tty_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  return OK;
}

static int cmux_tty_receive(FAR struct uart_dev_s *dev,
                            FAR unsigned int *status)
{
  *status = 0;
  return EOF;
}

static void cmux_tty_rxint(FAR struct uart_dev_s *dev, bool enable)
{
}

static bool cmux_tty_rxavailable(FAR struct uart_dev_s *dev)
{
  return 0;
}

static void cmux_tty_dmasend(FAR struct uart_dev_s *dev)
{
  FAR struct cmux_channel_s *channel = (FAR struct cmux_channel_s *)dev;
  FAR struct uart_dmaxfer_s *xfer = &dev->dmatx;
  size_t len = xfer->length + xfer->nlength;

  if (len > xfer->length)
    {
      cmux_send_data(channel, xfer->buffer, xfer->length);
      cmux_send_data(channel, xfer->nbuffer,
                     len - xfer->length);
    }
  else
    {
      cmux_send_data(channel, xfer->buffer, len);
    }

  xfer->nbytes = len;
  uart_xmitchars_done(dev);
}

static void cmux_tty_dmareceive(FAR struct uart_dev_s *dev)
{
}

static void cmux_tty_dmarxfree(FAR struct uart_dev_s *dev)
{
}

static void cmux_tty_dmatxavail(FAR struct uart_dev_s *dev)
{
  uart_xmitchars_dma(dev);
}

static void cmux_tty_send(FAR struct uart_dev_s *dev, int ch)
{
  FAR struct cmux_channel_s *channel = (FAR struct cmux_channel_s *)dev;
  char c = ch;

  /* Lock-free single-byte transmission */

  cmux_send_data_nolock(channel, &c, 1);
}

static void cmux_tty_txint(FAR struct uart_dev_s *dev, bool enable)
{
}

static bool cmux_tty_txready(FAR struct uart_dev_s *dev)
{
  return true;
}

static bool cmux_tty_txempty(FAR struct uart_dev_s *dev)
{
  return true;
}

/****************************************************************************
 * Name: cmux_uart_write/read
 *
 * Description:
 *  Physical layer data sending and receiving.
 *
 ****************************************************************************/

static int cmux_uart_write(FAR struct cmux_s *cmux, FAR const void *buffer,
                           size_t buflen)
{
  int len = 0;
  int ret;

  cmux_dump_data("--->", buffer, buflen);
  DEBUGASSERT(cmux != NULL);

  do
    {
      ret = file_write(&cmux->filep, buffer + len, buflen - len);
      if (ret == -EAGAIN || ret == -EINTR)
        {
          continue;
        }
      else if (ret < 0)
        {
          cmux_err("Failed to write data: %d\n", ret);
          break;
        }

      len += ret;
    }
  while (len < buflen);

  return len;
}

static void cmux_uart_read(FAR void *arg)
{
  FAR struct cmux_s *cmux = arg;
  uint8_t buffer[64];
  int ret;

  nxmutex_lock(&cmux->rxlock);

  do
    {
      ret = file_read(&cmux->filep, buffer, sizeof(buffer));
      if (ret > 0)
        {
          cmux_dump_data("<---", buffer, ret);
          cmux_frame_received(cmux, buffer, ret);
        }
      else if (ret < 0)
        {
          cmux_err("Failed to read data: %d\n", ret);
        }
    }
  while (ret > 0);

  nxmutex_unlock(&cmux->rxlock);
}

static void cmux_poll_cb(FAR struct pollfd *fds)
{
  FAR struct cmux_s *cmux = fds->arg;
  work_queue(HPWORK, &cmux->work, cmux_uart_read, cmux, 0);
}

static int cmux_uart_open(FAR struct cmux_s *cmux)
{
  struct termios term;
  int ret;

  ret = file_open(&cmux->filep, cmux->devname, O_RDWR | O_NONBLOCK);
  if (ret < 0)
    {
      cmux_err("Failed to open %s: %d\n", cmux->devname, ret);
      return ret;
    }

  /* Set the serial port to raw mode */

  file_ioctl(&cmux->filep, TCGETS, &term);
  cfmakeraw(&term);
  file_ioctl(&cmux->filep, TCSETS, &term);

  cmux->uart = cmux->filep.f_inode->i_private;
  cmux->uart->isconsole = false;

  /* Register data receiving notification */

  cmux->fds.revents = 0;
  cmux->fds.arg     = cmux;
  cmux->fds.events  = POLLIN;
  cmux->fds.cb      = cmux_poll_cb;
  file_poll(&cmux->filep, &cmux->fds, true);

  /* Unregister the serial device to ensure exclusive access to cmux */

  unregister_driver(cmux->devname);
  return OK;
}

static int cmux_uart_close(FAR struct cmux_s *cmux)
{
  file_poll(&cmux->filep, &cmux->fds, false);
  register_driver(cmux->devname, cmux->filep.f_inode->u.i_ops, 0666,
                  cmux->filep.f_inode->i_private);
  file_close(&cmux->filep);
  return OK;
}

/****************************************************************************
 * Name: cmux_initialize
 *
 * Description:
 *   Initialize the cmux structure.
 *
 ****************************************************************************/

static struct cmux_s *cmux_initialize(FAR const char *devname,
                                      FAR const struct gsm_config *config)
{
  FAR struct cmux_s *cmux;

  /* Check the validity of the parameters */

  if (devname == NULL || config->mru < CMUX_MIN_MRU ||
      config->mru > CMUX_MAX_MRU || config->mtu < CMUX_MIN_MTU ||
      config->mtu > CMUX_MAX_MTU || config->encapsulation !=
      CMUX_BASIC_MODE)
    {
      cmux_err("Invalid parameter\n");
      return NULL;
    }

  cmux = kmm_zalloc(sizeof(struct cmux_s) + sizeof(struct cmux_channel_s));
  if (cmux == NULL)
    {
      return NULL;
    }

  /* Hook data receiving callback of physical serial port */

  cmux->devname = devname;
  cmux->mru = config->mru;
  cmux->mtu = config->mtu;
  cmux->initiator = config->initiator;
  cmux->encapsulation = config->encapsulation;

  cmux->channels[0] = (FAR struct cmux_channel_s *)(cmux + 1);
  cmux->channels[0]->dlci = 0;
  cmux->channels[0]->cmux = cmux;

  nxmutex_init(&cmux->txlock);
  nxmutex_init(&cmux->rxlock);
  return cmux;
}

#ifdef CONFIG_UART_CMUX_LOG
static int cmux_uart_puts(FAR uart_dev_t *uart, FAR const char *buffer,
                          size_t buflen)
{
  FAR struct uart_buffer_s *xmit = &uart->xmit;
  size_t available;
  size_t remaining;
  size_t written;

  available = xmit->head >= xmit->tail ?
              xmit->size - xmit->head + xmit->tail - 1 :
              xmit->tail - xmit->head - 1;
  remaining = xmit->size - xmit->head;
  written = buflen > available ? available : buflen;

  if (written > remaining)
    {
      memcpy(xmit->buffer + xmit->head, buffer, remaining);
      memcpy(xmit->buffer, buffer + remaining, written - remaining);
      xmit->head = written - remaining;
    }
  else
    {
      memcpy(xmit->buffer + xmit->head, buffer, written);
      xmit->head += written;
    }

  return written;
}
#endif /* CONFIG_UART_CMUX_LOG */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: cmux_tty_initialize
 *
 * Description:
 *   Initialize a tty channel for cmux.
 *
 * Note:
 *   It allocates memory and registers the corresponding device.
 *
 ****************************************************************************/

int cmux_tty_initialize(FAR struct cmux_s *cmux, FAR const char *path)
{
  FAR struct cmux_channel_s *channel;
  int ret;

  channel = kmm_zalloc(sizeof(struct cmux_channel_s) +
                       CONFIG_UART_CMUX_BUFSIZE * 2);
  if (channel == NULL)
    {
      return -ENOMEM;
    }

  channel->uart.ops         = &g_cmux_tty_ops;
  channel->uart.priv        = channel;
  channel->uart.xmit.size   = CONFIG_UART_CMUX_BUFSIZE;
  channel->uart.xmit.buffer = (FAR char *)channel + sizeof(*channel);
  channel->uart.recv.size   = CONFIG_UART_CMUX_BUFSIZE;
  channel->uart.recv.buffer = channel->uart.xmit.buffer +
                              CONFIG_UART_CMUX_BUFSIZE;

  ret = cmux_tty_register(cmux, path, channel);
  if (ret < 0)
    {
      kmm_free(channel);
    }

  return ret;
}

/****************************************************************************
 * Name: uart_cmux_register
 *
 * Description:
 *   Register a cmux device with multiple tty channels.
 *
 * Note:
 *   This API is called by the application, initializes the serial port
 *   to cmux mode according to config,
 *   and creates the device specified by devlist, ending with NULL
 *
 ****************************************************************************/

FAR struct cmux_s *uart_cmux_register(FAR const char *devname,
                                      FAR const struct gsm_config *config,
                                      FAR const char *devlist[])
{
  FAR struct cmux_s *cmux;
  int ret;
  int i;

  cmux = cmux_initialize(devname, config);
  for (i = 0; devlist[i] != NULL; i++)
    {
      ret = cmux_tty_initialize(cmux, devlist[i]);
      if (ret < 0)
        {
          cmux_err("Failed to register %s: %d\n", devlist[i], ret);
          return NULL;
        }
    }

  return cmux;
}

/****************************************************************************
 * Name: uart_cmux_initialize
 *
 * Description:
 *   Initialize the cmux device.
 *
 * Note:
 *  This API is called when the driver is initialized.
 *  It defaults to slave mode and is used to communicate with the host.
 *
 ****************************************************************************/

int uart_cmux_initialize(void)
{
  int ret = OK;

#if defined (CONFIG_UART_CMUX_CONSOLE) || defined(CONFIG_UART_CMUX_LOG) || \
    defined(CONFIG_UART_CMUX_GSMTTY1) || defined(CONFIG_UART_CMUX_GSMTTY2)
  const struct gsm_config gsm_config =
  {
    .encapsulation = CMUX_BASIC_MODE,
    .initiator = CMUX_SLAVE,
    .mru = CMUX_DEFAULE_MRU,
    .mtu = CMUX_DEFAULE_MTU,
  };

  g_cmux = cmux_initialize(CONFIG_UART_CMUX_DEVNAME, &gsm_config);
  if (g_cmux == NULL)
    {
      cmux_err("Failed to initialize cmux\n");
      return ERROR;
    }
#endif

#ifdef CONFIG_UART_CMUX_CONSOLE
  ret = cmux_tty_register(g_cmux, "/dev/console", &g_cmux_console);
  if (ret < 0)
    {
      cmux_err("Failed to register console: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_UART_CMUX_LOG
  ret = cmux_tty_register(g_cmux, "/dev/ttylog", &g_cmux_log);
  if (ret < 0)
    {
      cmux_err("Failed to register console: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_UART_CMUX_GSMTTY1
  ret = cmux_tty_initialize(g_cmux, "/dev/gsmtty1");
  if (ret < 0)
    {
      cmux_err("Failed to register gsmtty1: %d\n", ret);
      return ret;
    }
#endif

#ifdef CONFIG_UART_CMUX_GSMTTY2
  ret = cmux_tty_initialize(g_cmux, "/dev/gsmtty2");
  if (ret < 0)
    {
      cmux_err("Failed to register gsmtty2: %d\n", ret);
      return ret;
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: up_nputs
 ****************************************************************************/

#ifdef CONFIG_UART_CMUX_LOG
void up_nputs(const char *str, size_t len)
{
  FAR struct cmux_channel_s *channel = &CMUX_LOG;
  FAR uart_dev_t *dev = &channel->uart;
  size_t written = 0;

  while (written < len)
    {
      if (up_interrupt_context() || sched_idletask())
        {
          while (!uart_txready(dev));
        }
      else if (!uart_txready(dev))
        {
          int ret;

          ret = nxsem_wait(&dev->xmitsem);
          if (ret < 0)
            {
              continue;
            }
        }

      nxmutex_lock(&dev->xmit.lock);
      written += cmux_uart_puts(dev, str + written, len - written);

      /* Enable Data transmission */

      uart_dmatxavail(dev);
      uart_enabletxint(dev);
      nxmutex_unlock(&dev->xmit.lock);
    }
}

/****************************************************************************
 * Name: up_putc
 ****************************************************************************/

int up_putc(int ch)
{
  FAR struct cmux_channel_s *channel = &CMUX_LOG;
  DEBUGASSERT(channel);

  if (OSINIT_OS_INITIALIZING())
    {
      cmux_uart_puts(&channel->uart, (FAR const char *)&ch, 1);
      return 0;
    }

  cmux_tty_send(&channel->uart, ch);
  return 0;
}

#endif /* CONFIG_UART_CMUX_LOG */
