/****************************************************************************
 * arch/arm/src/nrf51/nrf51_serial.c
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
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <debug.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/serial/serial.h>
#include "nrf51_serial.h"

#include <arch/board/board.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Register access macros */

#define getreg32(a)           (*(volatile uint32_t *)(a))
#define putreg32(v,a)         (*(volatile uint32_t *)(a) = (v))

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct uart_nrf51_s
{
  uint32_t uartbase;  /* Base address of UART registers */
  uint32_t baud;      /* Configured baud */
  uint8_t  irq;       /* UART IRQ number */
  bool     rxavail;   /* RX data available flag */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int  uart_nrf51_setup(FAR struct uart_dev_s *dev);
static void uart_nrf51_shutdown(FAR struct uart_dev_s *dev);
static int  uart_nrf51_attach(FAR struct uart_dev_s *dev);
static void uart_nrf51_detach(FAR struct uart_dev_s *dev);
static int  uart_nrf51_interrupt(int irq, FAR void *context,
                                  FAR void *arg);
static int  uart_nrf51_ioctl(FAR struct file *filep, int cmd,
                              unsigned long arg);
static int  uart_nrf51_receive(FAR struct uart_dev_s *dev,
                                FAR unsigned int *status);
static void uart_nrf51_rxint(FAR struct uart_dev_s *dev, bool enable);
static bool uart_nrf51_rxavailable(FAR struct uart_dev_s *dev);
static void uart_nrf51_send(FAR struct uart_dev_s *dev, int ch);
static void uart_nrf51_txint(FAR struct uart_dev_s *dev, bool enable);
static bool uart_nrf51_txready(FAR struct uart_dev_s *dev);
static bool uart_nrf51_txempty(FAR struct uart_dev_s *dev);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const struct uart_ops_s g_uart_ops =
{
  .setup       = uart_nrf51_setup,
  .shutdown    = uart_nrf51_shutdown,
  .attach      = uart_nrf51_attach,
  .detach      = uart_nrf51_detach,
  .ioctl       = uart_nrf51_ioctl,
  .receive     = uart_nrf51_receive,
  .rxint       = uart_nrf51_rxint,
  .rxavailable = uart_nrf51_rxavailable,
  .send        = uart_nrf51_send,
  .txint       = uart_nrf51_txint,
  .txready     = uart_nrf51_txready,
  .txempty     = uart_nrf51_txempty,
};

/* I/O buffers */

#ifdef CONFIG_NRF51_UART0
static char g_uart0rxbuffer[CONFIG_NRF51_UART0_RXBUFSIZE];
static char g_uart0txbuffer[CONFIG_NRF51_UART0_TXBUFSIZE];
#endif

#ifdef CONFIG_NRF51_UART1
static char g_uart1rxbuffer[CONFIG_NRF51_UART1_RXBUFSIZE];
static char g_uart1txbuffer[CONFIG_NRF51_UART1_TXBUFSIZE];
#endif

/* Console device macro */

#if defined(CONFIG_NRF51_UART0_SERIAL_CONSOLE) && defined(CONFIG_NRF51_UART0)
#  define CONSOLE_DEV     g_uart0port
#elif defined(CONFIG_NRF51_UART1_SERIAL_CONSOLE) && defined(CONFIG_NRF51_UART1)
#  define CONSOLE_DEV     g_uart1port
#endif

/* This describes the state of the nRF51 UART0 port. */

#ifdef CONFIG_NRF51_UART0
static struct uart_nrf51_s g_uart0priv =
{
  .uartbase = CONFIG_NRF51_UART0_BASE,
  .baud     = CONFIG_NRF51_UART0_BAUD,
  .irq      = CONFIG_NRF51_UART0_IRQ,
  .rxavail  = false,
};

static uart_dev_t g_uart0port =
{
  .recv     =
  {
    .size   = CONFIG_NRF51_UART0_RXBUFSIZE,
    .buffer = g_uart0rxbuffer,
  },
  .xmit     =
  {
    .size   = CONFIG_NRF51_UART0_TXBUFSIZE,
    .buffer = g_uart0txbuffer,
  },
  .ops      = &g_uart_ops,
  .priv     = &g_uart0priv,
};
#endif

/* This describes the state of the nRF51 UART1 port. */

#ifdef CONFIG_NRF51_UART1
static struct uart_nrf51_s g_uart1priv =
{
  .uartbase = CONFIG_NRF51_UART1_BASE,
  .baud     = CONFIG_NRF51_UART1_BAUD,
  .irq      = CONFIG_NRF51_UART1_IRQ,
  .rxavail  = false,
};

static uart_dev_t g_uart1port =
{
  .recv     =
  {
    .size   = CONFIG_NRF51_UART1_RXBUFSIZE,
    .buffer = g_uart1rxbuffer,
  },
  .xmit     =
  {
    .size   = CONFIG_NRF51_UART1_TXBUFSIZE,
    .buffer = g_uart1txbuffer,
  },
  .ops      = &g_uart_ops,
  .priv     = &g_uart1priv,
};
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: uart_nrf51_getreg
 ****************************************************************************/

static inline uint32_t uart_nrf51_getreg(FAR struct uart_nrf51_s *priv,
                                          uint32_t offset)
{
  return getreg32(priv->uartbase + offset);
}

/****************************************************************************
 * Name: uart_nrf51_putreg
 ****************************************************************************/

static inline void uart_nrf51_putreg(FAR struct uart_nrf51_s *priv,
                                      uint32_t offset, uint32_t value)
{
  putreg32(value, priv->uartbase + offset);
}

/****************************************************************************
 * Name: uart_nrf51_setup
 *
 * Description:
 *   Configure the UART baud, bits, parity, etc. This method is called the
 *   first time that the serial port is opened.
 *
 ****************************************************************************/

static int uart_nrf51_setup(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;
  uint32_t baudrate;

  /* Disable UART */

  uart_nrf51_putreg(priv, NRF51_UART_ENABLE_OFFSET,
                    NRF51_UART_ENABLE_DISABLED);

  /* Configure baud rate */

  switch (priv->baud)
    {
      case 1200:
        baudrate = NRF51_UART_BAUDRATE_1200;
        break;
      case 2400:
        baudrate = NRF51_UART_BAUDRATE_2400;
        break;
      case 4800:
        baudrate = NRF51_UART_BAUDRATE_4800;
        break;
      case 9600:
        baudrate = NRF51_UART_BAUDRATE_9600;
        break;
      case 14400:
        baudrate = NRF51_UART_BAUDRATE_14400;
        break;
      case 19200:
        baudrate = NRF51_UART_BAUDRATE_19200;
        break;
      case 28800:
        baudrate = NRF51_UART_BAUDRATE_28800;
        break;
      case 38400:
        baudrate = NRF51_UART_BAUDRATE_38400;
        break;
      case 57600:
        baudrate = NRF51_UART_BAUDRATE_57600;
        break;
      case 115200:
        baudrate = NRF51_UART_BAUDRATE_115200;
        break;
      default:
        baudrate = NRF51_UART_BAUDRATE_115200;
        break;
    }

  uart_nrf51_putreg(priv, NRF51_UART_BAUDRATE_OFFSET, baudrate);

  /* Configure parity - default to no parity */

  uart_nrf51_putreg(priv, NRF51_UART_CONFIG_OFFSET,
                    NRF51_UART_CONFIG_PARITY_NONE);

  /* Clear events */

  uart_nrf51_putreg(priv, NRF51_UART_RXDRDY_OFFSET, 0);
  uart_nrf51_putreg(priv, NRF51_UART_ERROR_OFFSET, 0);
  priv->rxavail = false;

  /* Enable UART */

  uart_nrf51_putreg(priv, NRF51_UART_ENABLE_OFFSET,
                    NRF51_UART_ENABLE_ENABLED);

  /* Start RX and TX */

  uart_nrf51_putreg(priv, NRF51_UART_STARTRX_OFFSET, 1);
  uart_nrf51_putreg(priv, NRF51_UART_STARTTX_OFFSET, 1);

  /* Set TXDRDY to 1 to indicate transmitter is ready for first byte */

  uart_nrf51_putreg(priv, NRF51_UART_TXDRDY_OFFSET, 1);

  return OK;
}

/****************************************************************************
 * Name: uart_nrf51_shutdown
 *
 * Description:
 *   Disable the UART.  This method is called when the serial
 *   port is closed
 *
 ****************************************************************************/

static void uart_nrf51_shutdown(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  /* Disable interrupts */

  uart_nrf51_putreg(priv, NRF51_UART_INTENCLR_OFFSET, 0xffffffff);

  /* Stop RX and TX */

  uart_nrf51_putreg(priv, NRF51_UART_STOPRX_OFFSET, 1);
  uart_nrf51_putreg(priv, NRF51_UART_STOPTX_OFFSET, 1);

  /* Disable UART */

  uart_nrf51_putreg(priv, NRF51_UART_ENABLE_OFFSET,
                    NRF51_UART_ENABLE_DISABLED);
}

/****************************************************************************
 * Name: uart_nrf51_attach
 *
 * Description:
 *   Configure the UART to operation in interrupt driven mode.  This method
 *   is called when the serial port is opened.  Normally, this is just after
 *   the the setup() method is called, however, the serial console may
 *   operate in a non-interrupt driven mode during the boot phase.
 *
 *   RX and TX interrupts are not enabled when by the attach method (unless
 *   the hardware supports multiple levels of interrupt enabling).  The RX
 *   and TX interrupts are not enabled until the txint() and rxint() methods
 *   are called.
 *
 ****************************************************************************/

static int uart_nrf51_attach(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;
  int ret;

  /* Attach and enable the IRQ */

  ret = irq_attach(priv->irq, uart_nrf51_interrupt, dev);
  if (ret == OK)
    {
      /* Enable the interrupt (RX and TX interrupts are still disabled
       * in the UART
       */

      up_enable_irq(priv->irq);
    }

  return ret;
}

/****************************************************************************
 * Name: uart_nrf51_detach
 *
 * Description:
 *   Detach UART interrupts.  This method is called when the serial port is
 *   closed normally just before the shutdown method is called.  The
 *   exception is the serial console which is never shutdown.
 *
 ****************************************************************************/

static void uart_nrf51_detach(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  /* Disable all interrupts */

  uart_nrf51_putreg(priv, NRF51_UART_INTENCLR_OFFSET, 0xffffffff);
  up_disable_irq(priv->irq);

  /* Detach the interrupt handler */

  irq_detach(priv->irq);
}

/****************************************************************************
 * Name: uart_nrf51_interrupt
 *
 * Description:
 *   This is the UART interrupt handler.  It will be invoked when an
 *   interrupt is received on the 'irq'.  It should call uart_xmitchars or
 *   uart_recvchars to perform the appropriate data transfers.  The
 *   interrupt handling logic must be able to map the 'arg' to the
 *   appropriate uart_dev_s structure in order to call these functions.
 *
 ****************************************************************************/

static int uart_nrf51_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct uart_dev_s *dev = (FAR struct uart_dev_s *)arg;
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;
  uint32_t rxdrdy;

  /* Check for RX data ready */

  rxdrdy = uart_nrf51_getreg(priv, NRF51_UART_RXDRDY_OFFSET);
  if (rxdrdy != 0)
    {
      uart_nrf51_putreg(priv, NRF51_UART_RXDRDY_OFFSET, 0);
      priv->rxavail = true;
      uart_recvchars(dev);
    }

  return OK;
}

/****************************************************************************
 * Name: uart_nrf51_ioctl
 *
 * Description:
 *   All ioctl calls will be routed through this method
 *
 ****************************************************************************/

static int uart_nrf51_ioctl(FAR struct file *filep, int cmd,
                             unsigned long arg)
{
  return -ENOTTY;
}

/****************************************************************************
 * Name: uart_nrf51_receive
 *
 * Description:
 *   Called (usually) from the interrupt level to receive one
 *   character from the UART.  Error bits associated with the
 *   receipt are provided in the return 'status'.
 *
 ****************************************************************************/

static int uart_nrf51_receive(FAR struct uart_dev_s *dev,
                               FAR unsigned int *status)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;
  uint32_t rxd;

  /* Read the received character */

  rxd = uart_nrf51_getreg(priv, NRF51_UART_RXD_OFFSET);
  priv->rxavail = false;

  /* Return status */

  *status = 0;
  return rxd & 0xff;
}

/****************************************************************************
 * Name: uart_nrf51_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts
 *
 ****************************************************************************/

static void uart_nrf51_rxint(FAR struct uart_dev_s *dev, bool enable)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  if (enable)
    {
      /* Enable RX interrupt */

      uart_nrf51_putreg(priv, NRF51_UART_INTENSET_OFFSET,
                        NRF51_UART_INT_RXDRDY);
    }
  else
    {
      /* Disable RX interrupt */

      uart_nrf51_putreg(priv, NRF51_UART_INTENCLR_OFFSET,
                        NRF51_UART_INT_RXDRDY);
    }
}

/****************************************************************************
 * Name: uart_nrf51_rxavailable
 *
 * Description:
 *   Return true if the receive register is not empty
 *
 ****************************************************************************/

static bool uart_nrf51_rxavailable(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  return priv->rxavail;
}

/****************************************************************************
 * Name: uart_nrf51_send
 *
 * Description:
 *   This method will send one byte on the UART.
 *
 ****************************************************************************/

static void uart_nrf51_send(FAR struct uart_dev_s *dev, int ch)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  /* Clear TXDRDY event before starting transmission */

  uart_nrf51_putreg(priv, NRF51_UART_TXDRDY_OFFSET, 0);

  /* Write the character to the TXD register - this triggers transmission */

  uart_nrf51_putreg(priv, NRF51_UART_TXD_OFFSET, (uint32_t)ch);

  /* Wait for transmission to complete (TXDRDY will be set by hardware) */

  while (uart_nrf51_getreg(priv, NRF51_UART_TXDRDY_OFFSET) == 0)
    {
    }
}

/****************************************************************************
 * Name: uart_nrf51_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts
 *
 ****************************************************************************/

static void uart_nrf51_txint(FAR struct uart_dev_s *dev, bool enable)
{
  /* TX interrupt is not used - we use polling mode for transmission.
   * Just fake a TX interrupt by calling uart_xmitchars().
   */

  if (enable)
    {
      /* Fake a TX interrupt here by just calling uart_xmitchars() with
       * interrupts disabled (note this may recurse).
       */

      uart_xmitchars(dev);
    }
}

/****************************************************************************
 * Name: uart_nrf51_txready
 *
 * Description:
 *   Return true if the transmit data register is not full
 *
 ****************************************************************************/

static bool uart_nrf51_txready(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  /* Return true if the TXDRDY event is set */

  return uart_nrf51_getreg(priv, NRF51_UART_TXDRDY_OFFSET) != 0;
}

/****************************************************************************
 * Name: uart_nrf51_txempty
 *
 * Description:
 *   Return true if the transmit data register is empty
 *
 ****************************************************************************/

static bool uart_nrf51_txempty(FAR struct uart_dev_s *dev)
{
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)dev->priv;

  /* Return true if the TXDRDY event is set */

  return uart_nrf51_getreg(priv, NRF51_UART_TXDRDY_OFFSET) != 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: arm_lowputc
 *
 * Description:
 *   Output one byte on the serial console
 *
 ****************************************************************************/

void arm_lowputc(char ch)
{
#ifdef HAVE_NRF51_CONSOLE
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)
                                   CONSOLE_DEV.priv;

  /* Wait for TXDRDY - indicates transmitter is ready */

  while (!uart_nrf51_getreg(priv, NRF51_UART_TXDRDY_OFFSET))
    {
    }

  /* Send the character - this will trigger transmission
   * QEMU will clear pending_tx_byte and set TXDRDY again when done
   */

  uart_nrf51_putreg(priv, NRF51_UART_TXD_OFFSET, (uint32_t)ch);

  /* Wait for transmission to complete by waiting for TXDRDY again
   * Note: In QEMU, writing to TXD immediately sends and sets TXDRDY,
   * but on real hardware we might need to wait
   */

  while (!uart_nrf51_getreg(priv, NRF51_UART_TXDRDY_OFFSET))
    {
    }
#endif
}

/****************************************************************************
 * Name: nrf51_earlyserialinit
 *
 * Description:
 *   Performs the low level UART initialization early in debug so that the
 *   serial console will be available during bootup.  This must be called
 *   before nrf51_serialinit.  NOTE:  This function depends on GPIO pin
 *   configuration performed in arm_lowsetup() and main clock initialization
 *   performed in arm_clock_configure().
 *
 ****************************************************************************/

void nrf51_earlyserialinit(void)
{
  /* Disable all UARTs */

#ifdef CONFIG_NRF51_UART0
  uart_nrf51_putreg(&g_uart0priv, NRF51_UART_ENABLE_OFFSET,
                    NRF51_UART_ENABLE_DISABLED);
#endif

#ifdef CONFIG_NRF51_UART1
  uart_nrf51_putreg(&g_uart1priv, NRF51_UART_ENABLE_OFFSET,
                    NRF51_UART_ENABLE_DISABLED);
#endif

  /* Configuration whichever one is the console */

#ifdef HAVE_NRF51_CONSOLE
  CONSOLE_DEV.isconsole = true;
  uart_nrf51_setup(&CONSOLE_DEV);
#endif
}

/****************************************************************************
 * Name: nrf51_serialinit
 *
 * Description:
 *   Register serial console and serial ports.  This assumes
 *   that nrf51_earlyserialinit was called previously.
 *
 ****************************************************************************/

void nrf51_serialinit(void)
{
#ifdef HAVE_NRF51_CONSOLE
  uart_register("/dev/console", &CONSOLE_DEV);
#endif

#ifdef CONFIG_NRF51_UART0
  uart_register("/dev/ttyS0", &g_uart0port);
#endif

#ifdef CONFIG_NRF51_UART1
  uart_register("/dev/ttyS1", &g_uart1port);
#endif
}

/****************************************************************************
 * Name: up_putc
 *
 * Description:
 *   Provide priority, low-level access to support OS debug writes
 *
 ****************************************************************************/

void up_putc(int ch)
{
#ifdef HAVE_NRF51_CONSOLE
  FAR struct uart_nrf51_s *priv = (FAR struct uart_nrf51_s *)
                                   CONSOLE_DEV.priv;
  uint32_t inten;

  /* Disable UART interrupts */

  inten = uart_nrf51_getreg(priv, NRF51_UART_INTEN_OFFSET);
  uart_nrf51_putreg(priv, NRF51_UART_INTENCLR_OFFSET, inten);

  /* Check for LF */

  if (ch == '\n')
    {
      /* Add CR before LF */

      arm_lowputc('\r');
    }

  arm_lowputc(ch);

  /* Restore UART interrupts */

  uart_nrf51_putreg(priv, NRF51_UART_INTENSET_OFFSET, inten);
#endif
}

/****************************************************************************
 * Name: arm_earlyserialinit
 *
 * Description:
 *   Wrapper function for nrf51_earlyserialinit
 *
 ****************************************************************************/

void arm_earlyserialinit(void)
{
  nrf51_earlyserialinit();
}

/****************************************************************************
 * Name: arm_serialinit
 *
 * Description:
 *   Wrapper function for nrf51_serialinit
 *
 ****************************************************************************/

void arm_serialinit(void)
{
  nrf51_serialinit();
}
