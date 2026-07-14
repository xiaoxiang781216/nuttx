/****************************************************************************
 * drivers/mmcsd/mmcsd_sdhci.c
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
 * This file implements a generic SDHCI lower-half driver that conforms to
 * the SD Host Controller Standard Specification.  It translates the NuttX
 * sdio_dev_s interface into standard SDHCI register operations and handles
 * interrupt-driven PIO data transfer with event synchronization.
 *
 * The driver is designed to work with QEMU's sdhci-pci device model and
 * other controllers that implement the standard SDHCI register map.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/sdio.h>
#include <nuttx/semaphore.h>
#include <nuttx/spinlock.h>
#include <nuttx/wdog.h>
#include <nuttx/wqueue.h>

#include "mmcsd_sdhci.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Timeout values in clock ticks (1 tick = 1ms typical) */

#define SDHCI_CMDTIMEOUT          MSEC2TICK(100)
#define SDHCI_LONGTIMEOUT         MSEC2TICK(500)
#define SDHCI_DATATIMEOUT         MSEC2TICK(2000)

/* Default data timeout counter value for the Timeout Control Register.
 * Value 0x0e = 2^(0x0e+13) = 2^27 TMCLK cycles.
 */

#define SDHCI_DEFAULT_TIMEOUT     0x0e

/* CMD12 / CMD52 I/O Abort: MMCSD_STOPXFR without a data transfer */

#define SDHCI_IS_ABORT_CMD(c) \
  (((c) & (MMCSD_STOPXFR | MMCSD_DATAXFR)) == MMCSD_STOPXFR)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sdhci_dev_s
{
  struct sdio_dev_s  dev;             /* Standard SDIO interface */

  /* Hardware configuration */

  uintptr_t          base;            /* MMIO base address */
  int                irq;             /* IRQ number */
  spinlock_t         lock;            /* Protects shared wait/xfr state */

  /* Event support */

  sem_t              waitsem;         /* Implements event waiting */
  sdio_eventset_t    waitevents;      /* Events to wait for */
  uint32_t           waitints;        /* Interrupt enables for waiting */
  volatile sdio_eventset_t wkupevent; /* Wakeup event */
  struct wdog_s      waitwdog;        /* Watchdog for event timeouts */

  /* Callback support */

  sdio_statset_t     cdstatus;        /* Card status */
  sdio_eventset_t    cbevents;        /* Set of events to cause callbacks */
  worker_t           callback;        /* Registered callback function */
  FAR void          *cbarg;           /* Registered callback argument */
  struct work_s      cbwork;          /* Callback work queue structure */

  /* Interrupt-mode PIO data transfer support.
   * A union is used so that the send path can preserve the const
   * qualifier of the caller's buffer, while the receive path uses
   * a mutable pointer.  Both members alias the same storage and
   * are selected based on transfer direction.
   */

  union
  {
    FAR uint32_t       *rx;          /* Receive buffer (ISR writes to it) */
    FAR const uint32_t *tx;          /* Send buffer (ISR reads from it) */
  } buffer;
  size_t             remaining;      /* Number of bytes remaining */
  uint32_t           xfrints;        /* Interrupt enables for data transfer */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* Helper functions */

static inline uint8_t  sdhci_get8(FAR struct sdhci_dev_s *priv,
                                  unsigned int offset);
static inline void sdhci_put8(FAR struct sdhci_dev_s *priv,
                              unsigned int offset, uint8_t value);
static inline uint16_t sdhci_get16(FAR struct sdhci_dev_s *priv,
                                   unsigned int offset);
static inline void sdhci_put16(FAR struct sdhci_dev_s *priv,
                               unsigned int offset, uint16_t value);
static inline uint32_t sdhci_get32(FAR struct sdhci_dev_s *priv,
                                   unsigned int offset);
static inline void sdhci_put32(FAR struct sdhci_dev_s *priv,
                               unsigned int offset, uint32_t value);
static inline void sdhci_modify8(FAR struct sdhci_dev_s *priv,
                                 unsigned int offset, uint8_t mask,
                                 uint8_t value);
static inline void sdhci_modify16(FAR struct sdhci_dev_s *priv,
                                  unsigned int offset, uint16_t mask,
                                  uint16_t value);
static void sdhci_configwaitints(FAR struct sdhci_dev_s *priv,
                                 uint32_t waitints,
                                 sdio_eventset_t waitevents,
                                 sdio_eventset_t wkupevent);
static void sdhci_configxfrints(FAR struct sdhci_dev_s *priv,
                                uint32_t xfrints);
static void sdhci_endwait(FAR struct sdhci_dev_s *priv,
                          sdio_eventset_t wkupevent);
static void sdhci_endtransfer(FAR struct sdhci_dev_s *priv,
                              sdio_eventset_t wkupevent);
static void sdhci_callback(FAR struct sdhci_dev_s *priv);

/* SDIO interface methods */

static void sdhci_reset(FAR struct sdio_dev_s *dev);
static sdio_capset_t sdhci_capabilities(FAR struct sdio_dev_s *dev);
static sdio_statset_t sdhci_status(FAR struct sdio_dev_s *dev);
static void sdhci_widebus(FAR struct sdio_dev_s *dev, bool enable);
static void sdhci_clock(FAR struct sdio_dev_s *dev,
                        enum sdio_clock_e rate);
static int  sdhci_attach(FAR struct sdio_dev_s *dev);

static int  sdhci_sendcmd(FAR struct sdio_dev_s *dev, uint32_t cmd,
                          uint32_t arg);
#ifdef CONFIG_SDIO_BLOCKSETUP
static void sdhci_blocksetup(FAR struct sdio_dev_s *dev,
                             unsigned int blocklen,
                             unsigned int nblocks);
#endif
static int  sdhci_recvsetup(FAR struct sdio_dev_s *dev,
                            FAR uint8_t *buffer, size_t nbytes);
static int  sdhci_sendsetup(FAR struct sdio_dev_s *dev,
                            FAR const uint8_t *buffer, size_t nbytes);
static int  sdhci_cancel(FAR struct sdio_dev_s *dev);

static int  sdhci_waitresponse(FAR struct sdio_dev_s *dev, uint32_t cmd);
static int  sdhci_recvshortcrc(FAR struct sdio_dev_s *dev, uint32_t cmd,
                               FAR uint32_t *rshort);
static int  sdhci_recvlong(FAR struct sdio_dev_s *dev, uint32_t cmd,
                           FAR uint32_t rlong[4]);
static int  sdhci_recvshort(FAR struct sdio_dev_s *dev, uint32_t cmd,
                            FAR uint32_t *rshort);
static int  sdhci_recvnotimpl(FAR struct sdio_dev_s *dev, uint32_t cmd,
                              FAR uint32_t *rnotimpl);

static void sdhci_waitenable(FAR struct sdio_dev_s *dev,
                             sdio_eventset_t eventset,
                             uint32_t timeout);
static sdio_eventset_t sdhci_eventwait(FAR struct sdio_dev_s *dev);
static void sdhci_callbackenable(FAR struct sdio_dev_s *dev,
                                 sdio_eventset_t eventset);
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int  sdhci_registercallback(FAR struct sdio_dev_s *dev,
                                   worker_t callback, FAR void *arg);
#endif

static void sdhci_gotextcsd(FAR struct sdio_dev_s *dev,
                            FAR const uint8_t *buffer);

/* Interrupt handler and watchdog */

static int  sdhci_interrupt(int irq, FAR void *context, FAR void *arg);
static void sdhci_eventtimeout(wdparm_t arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sdhci_get8 / sdhci_put8
 *
 * Description:
 *   Register access helpers.  SDHCI registers are little-endian and may
 *   require 8-bit, 16-bit, or 32-bit access depending on the register.
 *
 ****************************************************************************/

static inline uint8_t sdhci_get8(FAR struct sdhci_dev_s *priv,
                                 unsigned int offset)
{
  return *(FAR volatile uint8_t *)(priv->base + offset);
}

static inline void sdhci_put8(FAR struct sdhci_dev_s *priv,
                              unsigned int offset, uint8_t value)
{
  *(FAR volatile uint8_t *)(priv->base + offset) = value;
}

static inline uint16_t sdhci_get16(FAR struct sdhci_dev_s *priv,
                                   unsigned int offset)
{
  return *(FAR volatile uint16_t *)(priv->base + offset);
}

static inline void sdhci_put16(FAR struct sdhci_dev_s *priv,
                               unsigned int offset, uint16_t value)
{
  *(FAR volatile uint16_t *)(priv->base + offset) = value;
}

static inline uint32_t sdhci_get32(FAR struct sdhci_dev_s *priv,
                                   unsigned int offset)
{
  return *(FAR volatile uint32_t *)(priv->base + offset);
}

static inline void sdhci_put32(FAR struct sdhci_dev_s *priv,
                               unsigned int offset, uint32_t value)
{
  *(FAR volatile uint32_t *)(priv->base + offset) = value;
}

static inline void sdhci_modify8(FAR struct sdhci_dev_s *priv,
                                 unsigned int offset, uint8_t mask,
                                 uint8_t value)
{
  uint8_t regval;

  regval = sdhci_get8(priv, offset);
  regval &= ~mask;
  regval |= (value & mask);
  sdhci_put8(priv, offset, regval);
}

static inline void sdhci_modify16(FAR struct sdhci_dev_s *priv,
                                  unsigned int offset, uint16_t mask,
                                  uint16_t value)
{
  uint16_t regval;

  regval = sdhci_get16(priv, offset);
  regval &= ~mask;
  regval |= (value & mask);
  sdhci_put16(priv, offset, regval);
}

/****************************************************************************
 * Name: sdhci_configwaitints
 *
 * Description:
 *   Configure interrupt signal enable register for event waiting and update
 *   wait state tracking.
 *
 ****************************************************************************/

static void sdhci_configwaitints(FAR struct sdhci_dev_s *priv,
                                 uint32_t waitints,
                                 sdio_eventset_t waitevents,
                                 sdio_eventset_t wkupevent)
{
  irqstate_t flags;
  uint32_t combined;

  flags = spin_lock_irqsave(&priv->lock);
  priv->waitevents = waitevents;
  priv->waitints   = waitints;
  priv->wkupevent  = wkupevent;

  /* Enable or disable the relevant interrupt signals.
   * The Normal Interrupt Signal Enable register (038h) controls which
   * status bits generate an IRQ to the host.  We OR in the transfer
   * interrupts (xfrints) so data transfer and event waiting can coexist.
   */

  combined = waitints | priv->xfrints;

  sdhci_put16(priv, SDHCI_INT_SIGNAL_EN_OFFSET,
              (uint16_t)combined);

  /* The Error Interrupt Signal Enable (03Ah) must be set when either
   * waitints or xfrints includes SDHCI_INT_ERROR (bit 15 of the normal
   * status).  This is a shared resource between wait and transfer paths;
   * both must be considered to avoid one path clobbering the other.
   */

  if ((combined & SDHCI_INT_ERROR) != 0)
    {
      sdhci_put16(priv, SDHCI_ERR_INT_SIGNAL_EN_OFFSET,
                  (uint16_t)SDHCI_ERR_INT_ALL_MASK);
    }
  else
    {
      sdhci_put16(priv, SDHCI_ERR_INT_SIGNAL_EN_OFFSET, 0);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}

/****************************************************************************
 * Name: sdhci_configxfrints
 *
 * Description:
 *   Configure interrupt signal enable register for data transfer.
 *
 ****************************************************************************/

static void sdhci_configxfrints(FAR struct sdhci_dev_s *priv,
                                uint32_t xfrints)
{
  irqstate_t flags;
  uint32_t combined;

  flags = spin_lock_irqsave(&priv->lock);
  priv->xfrints = xfrints;

  /* OR transfer interrupts with wait interrupts in the signal enable */

  combined = priv->waitints | xfrints;

  sdhci_put16(priv, SDHCI_INT_SIGNAL_EN_OFFSET,
              (uint16_t)combined);

  /* Error interrupt signal enable is shared with configwaitints.
   * Must consider both wait and transfer paths to avoid clobbering.
   */

  if ((combined & SDHCI_INT_ERROR) != 0)
    {
      sdhci_put16(priv, SDHCI_ERR_INT_SIGNAL_EN_OFFSET,
                  (uint16_t)SDHCI_ERR_INT_ALL_MASK);
    }
  else
    {
      sdhci_put16(priv, SDHCI_ERR_INT_SIGNAL_EN_OFFSET, 0);
    }

  spin_unlock_irqrestore(&priv->lock, flags);
}

/****************************************************************************
 * Name: sdhci_endwait
 *
 * Description:
 *   Wake up the thread waiting in eventwait().
 *
 ****************************************************************************/

static void sdhci_endwait(FAR struct sdhci_dev_s *priv,
                          sdio_eventset_t wkupevent)
{
  wd_cancel(&priv->waitwdog);
  sdhci_configwaitints(priv, 0, 0, wkupevent);
  nxsem_post(&priv->waitsem);
}

/****************************************************************************
 * Name: sdhci_endtransfer
 *
 * Description:
 *   Complete a data transfer and optionally wake the waiting thread.
 *
 ****************************************************************************/

static void sdhci_endtransfer(FAR struct sdhci_dev_s *priv,
                              sdio_eventset_t wkupevent)
{
  /* Disable all transfer related interrupts */

  sdhci_configxfrints(priv, 0);

  /* Clear pending transfer interrupt status (write-1-to-clear) */

  sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET,
              (uint16_t)SDHCI_XFRDONE_INTS);
  sdhci_put16(priv, SDHCI_ERR_INT_STATUS_OFFSET,
              (uint16_t)SDHCI_XFRERR_INTS);

  priv->remaining = 0;

  /* Wake waiting thread if transfer done event is expected */

  if ((priv->waitevents & wkupevent) != 0)
    {
      sdhci_endwait(priv, wkupevent);
    }
}

/****************************************************************************
 * Name: sdhci_callback
 *
 * Description:
 *   Check if a media change callback should be dispatched.
 *
 ****************************************************************************/

static void sdhci_callback(FAR struct sdhci_dev_s *priv)
{
  if (priv->callback != NULL)
    {
      /* Check if the current card state matches an enabled event */

      if ((priv->cdstatus & SDIO_STATUS_PRESENT) != 0)
        {
          if ((priv->cbevents & SDIOMEDIA_INSERTED) == 0)
            {
              return;
            }
        }
      else
        {
          if ((priv->cbevents & SDIOMEDIA_EJECTED) == 0)
            {
              return;
            }
        }

      /* Disable further callbacks (one-shot) */

      priv->cbevents = 0;

      /* In ISR context, defer to HPWORK; otherwise call directly */

      if (up_interrupt_context())
        {
          work_queue(HPWORK, &priv->cbwork, priv->callback,
                     priv->cbarg, 0);
        }
      else
        {
          priv->callback(priv->cbarg);
        }
    }
}

/****************************************************************************
 * Name: sdhci_reset
 *
 * Description:
 *   Reset the SDHCI controller to its initial state.
 *
 ****************************************************************************/

static void sdhci_reset(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  /* Disable all interrupt signals */

  sdhci_put16(priv, SDHCI_INT_SIGNAL_EN_OFFSET, 0);
  sdhci_put16(priv, SDHCI_ERR_INT_SIGNAL_EN_OFFSET, 0);

  /* Software Reset All: write bit 0 of SWRST register (02Fh).
   * This bit auto-clears when the reset is complete.
   */

  sdhci_put8(priv, SDHCI_SOFTWARE_RESET_OFFSET,
             SDHCI_SOFTWARE_RESET_ALL);

  while ((sdhci_get8(priv, SDHCI_SOFTWARE_RESET_OFFSET) &
          SDHCI_SOFTWARE_RESET_ALL) != 0)
    {
      /* Wait for reset to complete */
    }

  /* Make sure all clocking is disabled */

  sdhci_clock(dev, CLOCK_SDIO_DISABLED);

  /* Enable all status bits in the Status Enable registers so that
   * status can be polled even without interrupts.
   */

  sdhci_put16(priv, SDHCI_INT_STATUS_EN_OFFSET, 0xffff);
  sdhci_put16(priv, SDHCI_ERR_INT_STATUS_EN_OFFSET, 0xffff);

  /* Set data timeout counter value */

  sdhci_put8(priv, SDHCI_TIMEOUT_CONTROL_OFFSET, SDHCI_DEFAULT_TIMEOUT);

  /* Reset state data */

  priv->waitevents = 0;
  priv->waitints   = 0;
  priv->wkupevent  = 0;
  priv->buffer.rx  = NULL;
  priv->remaining  = 0;
  priv->xfrints    = 0;

  wd_cancel(&priv->waitwdog);

  /* Read card present and write protect state from Present State register.
   * Per specification Section 2.2.9:
   *   Bit 19 = Write Protect Switch Pin Level (1=write enabled)
   *   Bit 16 = Card Inserted (1=card present)
   */

  uint32_t prnsts = sdhci_get32(priv, SDHCI_PRESENT_STATE_OFFSET);

  if ((prnsts & SDHCI_PRESENT_STATE_CARD_INSERTED) != 0)
    {
      priv->cdstatus |= SDIO_STATUS_PRESENT;
    }
  else
    {
      priv->cdstatus &= ~SDIO_STATUS_PRESENT;
    }

  if ((prnsts & SDHCI_PRESENT_STATE_WRITE_PROTECT) != 0)
    {
      priv->cdstatus &= ~SDIO_STATUS_WRPROTECTED;
    }
  else
    {
      priv->cdstatus |= SDIO_STATUS_WRPROTECTED;
    }
}

/****************************************************************************
 * Name: sdhci_capabilities
 *
 * Description:
 *   Return the controller capability set.
 *
 ****************************************************************************/

static sdio_capset_t sdhci_capabilities(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  sdio_capset_t capset = 0;
  uint64_t caps;

  /* Read the 64-bit Capabilities register and map SDHCI capability
   * bits to NuttX sdio_capset_t bits.
   */

  caps = ((uint64_t)sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET + 4) << 32) |
         sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET);

  /* Bit 22 (SDMA) or Bit 19 (ADMA2) -> SDIO_CAPS_DMASUPPORTED */

  if ((caps & (SDHCI_CAPABILITIES_SDMA | SDHCI_CAPABILITIES_ADMA2)) != 0)
    {
      capset |= SDIO_CAPS_DMASUPPORTED;
    }

  /* Bit 21 (High Speed) -> SDIO_CAPS_MMC_HS_MODE */

  if ((caps & SDHCI_CAPABILITIES_HIGH_SPEED) != 0)
    {
      capset |= SDIO_CAPS_MMC_HS_MODE;
    }

  /* Bit 18 (8-bit Support) -> SDIO_CAPS_8BIT */

  if ((caps & SDHCI_CAPABILITIES_8BIT) != 0)
    {
      capset |= SDIO_CAPS_8BIT;
    }

  /* No dedicated bit for 4-bit in SDHCI spec; it is implied.
   * SDIO_CAPS_1BIT_ONLY / SDIO_CAPS_4BIT_ONLY have no SDHCI mapping.
   */

  capset |= SDIO_CAPS_4BIT;

  /* SDHCI requires BLOCK_SIZE/BLOCK_COUNT to be programmed before
   * the Command Register is written (which triggers the transfer).
   * The upper-half uses SDIO_CAPS_DMABEFOREWRITE to select the
   * ordering: when set, BLOCKSETUP/SENDSETUP are called before the
   * write command instead of after.  This is needed for PIO mode
   * as well as DMA, despite the flag name.
   */

  capset |= SDIO_CAPS_DMABEFOREWRITE;

  return capset;
}

/****************************************************************************
 * Name: sdhci_status
 *
 * Description:
 *   Return the current card status (present / write protected).
 *
 ****************************************************************************/

static sdio_statset_t sdhci_status(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  return priv->cdstatus;
}

/****************************************************************************
 * Name: sdhci_widebus
 *
 * Description:
 *   Switch between 1-bit and wide bus (4-bit or 8-bit) operation.
 *
 *   The upper-half only passes true/false.  When true, the lower-half
 *   must decide whether to select 4-bit or 8-bit based on controller
 *   capabilities.
 *
 ****************************************************************************/

static void sdhci_widebus(FAR struct sdio_dev_s *dev, bool enable)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint8_t regval = 0;
  uint64_t caps;

  /* Read capabilities to check 8-bit support */

  caps = ((uint64_t)sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET + 4) << 32) |
         sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET);

  /* Clear both width bits first, then select the new width */

  if (enable)
    {
      /* Select 8-bit if controller supports it, otherwise 4-bit */

      if ((caps & SDHCI_CAPABILITIES_8BIT) != 0)
        {
          regval = SDHCI_HOST_CONTROL_8BIT_BUS;
        }
      else
        {
          regval = SDHCI_HOST_CONTROL_4BIT_BUS;
        }
    }

  /* else: both bits cleared = 1-bit mode */

  sdhci_modify8(priv, SDHCI_HOST_CONTROL_OFFSET,
                SDHCI_HOST_CONTROL_4BIT_BUS | SDHCI_HOST_CONTROL_8BIT_BUS,
                regval);
}

/****************************************************************************
 * Name: sdhci_clock
 *
 * Description:
 *   Configure the SD clock frequency.
 *
 ****************************************************************************/

static void sdhci_clock(FAR struct sdio_dev_s *dev, enum sdio_clock_e rate)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint64_t caps;
  uint32_t base_clock;
  uint16_t regval;
  uint16_t divisor;
  uint16_t ver;
  unsigned int target_freq;
  int i;

  /* Read base clock frequency from Capabilities register.
   * Bits [15:8] = base clock in MHz.  0 means unknown.
   */

  caps = ((uint64_t)sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET + 4) << 32) |
         sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET);

  base_clock = (caps & SDHCI_CAPABILITIES_BASE_CLOCK_MASK) >>
               SDHCI_CAPABILITIES_BASE_CLOCK_SHIFT;

  if (base_clock == 0)
    {
      /* Base clock field is 0: per specification the Host System shall
       * provide the clock information by another method.  Default to the
       * maximum SD clock frequency for high-speed mode (50MHz) so that
       * the divisor calculation still yields a valid configuration.
       */

      base_clock = 50;
    }

  /* Disable SD clock before changing frequency */

  sdhci_modify16(priv, SDHCI_CLOCK_CONTROL_OFFSET,
                 SDHCI_CLOCK_CONTROL_SD_CLOCK_ENABLE, 0);

  switch (rate)
    {
    default:
    case CLOCK_SDIO_DISABLED:
      {
        /* Stop the internal clock; SD clock was disabled above */

        sdhci_modify16(priv, SDHCI_CLOCK_CONTROL_OFFSET,
                       SDHCI_CLOCK_CONTROL_INT_ENABLE, 0);
        return;
      }

    case CLOCK_IDMODE:
      {
        /* Identification mode: clock <= 400kHz */

        target_freq = 400000;
        break;
      }

    case CLOCK_MMC_TRANSFER:
    case CLOCK_SD_TRANSFER_1BIT:
    case CLOCK_SD_TRANSFER_4BIT:
      {
        /* Normal speed: clock up to 25MHz */

        target_freq = 25000000;
        break;
      }
    }

  /* Read Host Controller Version to determine clock mode.
   * V3.00+ uses 10-bit divided clock mode.
   * V1.00/V2.00 uses 8-bit divided clock mode.
   */

  ver = sdhci_get16(priv, SDHCI_HOST_VERSION_OFFSET);
  ver &= SDHCI_HOST_VERSION_SPEC_MASK;

  if (ver >= SDHCI_HOST_VERSION_SPEC_3)
    {
       /* 10-bit Divided Clock Mode (V3.00+).
        * Per specification: N=0 means Base Clock (no division),
        * N>=1 means Clock = BaseClock / (2 * N).
        * Choose the smallest N such that the resulting clock <= target.
        */

      if (base_clock * 1000000 <= target_freq)
        {
          /* Base clock already satisfies the target: no division */

          divisor = 0;
        }
      else
        {
          divisor = (base_clock * 1000000 + (2 * target_freq) - 1) /
                    (2 * target_freq);
        }

      if (divisor > 0x3ff)
        {
          divisor = 0x3ff;
        }

      /* N=0 = Base Clock (no division) is valid per spec */

      regval = SDHCI_CLOCK_CONTROL_INT_ENABLE |
               ((divisor & 0xff) << SDHCI_CLOCK_CONTROL_FREQ_SHIFT) |
               (((divisor >> 8) & 0x3) <<
                SDHCI_CLOCK_CONTROL_FREQ_UPPER_SHIFT);
    }
  else
    {
      /* 8-bit Divided Clock Mode (V1.00/V2.00).
       * Divisor is a power-of-2 bitmask: 00h=divide by 1, 01h=divide
       * by 2, 02h=divide by 4, ..., 80h=divide by 256.
       * Clock = BaseClock / (1 << bit_position).
       * Choose the smallest divisor (highest frequency) such that
       * the resulting clock <= target.
       */

      divisor = 0;
      for (i = 0; i < 9; i++)
        {
          if ((base_clock * 1000000) / (1u << i) <= target_freq)
            {
              divisor = (i == 0) ? 0 : (1 << (i - 1));
              break;
            }
        }

      if (i == 9)
        {
          divisor = 0x80;
        }

      regval = SDHCI_CLOCK_CONTROL_INT_ENABLE |
               ((divisor & 0xff) << SDHCI_CLOCK_CONTROL_FREQ_SHIFT);
    }

  sdhci_put16(priv, SDHCI_CLOCK_CONTROL_OFFSET, regval);

  /* Wait for internal clock to stabilize */

  while ((sdhci_get16(priv, SDHCI_CLOCK_CONTROL_OFFSET) &
          SDHCI_CLOCK_CONTROL_INT_STABLE) == 0)
    {
      /* Wait for clock stable */
    }

  /* Enable SD clock */

  sdhci_modify16(priv, SDHCI_CLOCK_CONTROL_OFFSET,
                 SDHCI_CLOCK_CONTROL_SD_CLOCK_ENABLE,
                 SDHCI_CLOCK_CONTROL_SD_CLOCK_ENABLE);
}

/****************************************************************************
 * Name: sdhci_attach
 *
 * Description:
 *   Attach the SDHCI interrupt handler.
 *
 ****************************************************************************/

static int sdhci_attach(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  int ret;

  /* Attach the SDHCI interrupt handler */

  ret = irq_attach(priv->irq, sdhci_interrupt, priv);
  if (ret == OK)
    {
      /* Disable all interrupt signals and clear all pending status */

      sdhci_put16(priv, SDHCI_INT_SIGNAL_EN_OFFSET, 0);
      sdhci_put16(priv, SDHCI_ERR_INT_SIGNAL_EN_OFFSET, 0);

      sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET, 0xffff);
      sdhci_put16(priv, SDHCI_ERR_INT_STATUS_OFFSET, 0xffff);

      /* Enable the IRQ at the NVIC */

      up_enable_irq(priv->irq);
    }

  return ret;
}

/****************************************************************************
 * Name: sdhci_sendcmd
 *
 * Description:
 *   Send an MMC/SD command to the card.
 *
 ****************************************************************************/

static int sdhci_sendcmd(FAR struct sdio_dev_s *dev, uint32_t cmd,
                         uint32_t arg)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint16_t transfer_mode = 0;
  clock_t timeout;
  clock_t start;
  clock_t elapsed;
  uint32_t inhibit;
  uint16_t cmd_reg;

  /* Initialize the command index */

  cmd_reg = (cmd & MMCSD_CMDIDX_MASK) << SDHCI_COMMAND_INDEX_SHIFT;

  /* Command Type: Abort (11b) per specification Table 2-10 */

  if (SDHCI_IS_ABORT_CMD(cmd))
    {
      cmd_reg |= SDHCI_COMMAND_TYPE_ABORT;
    }

  /* Does a data transfer accompany the command? */

  if ((cmd & MMCSD_DATAXFR) != 0)
    {
      /* Set Data Present Select */

      cmd_reg |= SDHCI_COMMAND_DATA_PRESENT;

      /* Set transfer direction: read = 1, write = 0 */

      if ((cmd & MMCSD_WRXFR) == 0)
        {
          transfer_mode |= SDHCI_TRANSFER_MODE_READ;
        }

      /* Multi-block transfer? */

      if ((cmd & MMCSD_MULTIBLOCK) != 0)
        {
          transfer_mode |= SDHCI_TRANSFER_MODE_MULTI_BLOCK |
                           SDHCI_TRANSFER_MODE_BLOCK_COUNT_EN;

          if ((cmd & MMCSD_STOPXFR) != 0)
            {
              /* Indefinite block transfer with auto CMD12 */

              transfer_mode |= SDHCI_TRANSFER_MODE_AUTO_CMD12;
            }
        }
    }

  /* Configure response type bits */

  switch (cmd & MMCSD_RESPONSE_MASK)
    {
    case MMCSD_NO_RESPONSE:
      cmd_reg |= SDHCI_COMMAND_RESPONSE_NONE;
      break;

    case MMCSD_R1B_RESPONSE:
      cmd_reg |= SDHCI_COMMAND_RESPONSE_R1B_R5B |
                 SDHCI_COMMAND_INDEX_CHECK_EN |
                 SDHCI_COMMAND_CRC_CHECK_EN;
      break;

    case MMCSD_R1_RESPONSE:
    case MMCSD_R5_RESPONSE:
    case MMCSD_R6_RESPONSE:
    case MMCSD_R7_RESPONSE:
      cmd_reg |= SDHCI_COMMAND_RESPONSE_R1_R3_R4_R5 |
                 SDHCI_COMMAND_INDEX_CHECK_EN |
                 SDHCI_COMMAND_CRC_CHECK_EN;
      break;

    case MMCSD_R2_RESPONSE:
      cmd_reg |= SDHCI_COMMAND_RESPONSE_R2 |
                 SDHCI_COMMAND_CRC_CHECK_EN;
      break;

    case MMCSD_R3_RESPONSE:
    case MMCSD_R4_RESPONSE:
      cmd_reg |= SDHCI_COMMAND_RESPONSE_R1_R3_R4_R5;
      break;

    default:
      return -EINVAL;
    }

  /* Wait for Command Inhibit (CMD) to clear, plus Command Inhibit (DAT)
   * for non-abort commands.
   */

  inhibit = SDHCI_PRESENT_STATE_CMD_INHIBIT_CMD;
  if (!SDHCI_IS_ABORT_CMD(cmd))
    {
      inhibit |= SDHCI_PRESENT_STATE_CMD_INHIBIT_DAT;
    }

  timeout = SDHCI_CMDTIMEOUT;
  start   = clock_systime_ticks();

  while ((sdhci_get32(priv, SDHCI_PRESENT_STATE_OFFSET) & inhibit) != 0)
    {
      elapsed = clock_systime_ticks() - start;
      if (elapsed >= timeout)
        {
          return -EBUSY;
        }
    }

  /* Set the argument register */

  sdhci_put32(priv, SDHCI_ARGUMENT_OFFSET, arg);

  /* Clear command complete and error status (write-1-to-clear) */

  sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET,
              (uint16_t)SDHCI_RESPDONE_INTS);
  sdhci_put16(priv, SDHCI_ERR_INT_STATUS_OFFSET,
              (uint16_t)SDHCI_RESPERR_INTS);

  /* Write Transfer Mode register first, then Command register.
   * Writing the Command Register triggers command execution.
   */

  sdhci_put16(priv, SDHCI_TRANSFER_MODE_OFFSET, transfer_mode);
  sdhci_put16(priv, SDHCI_COMMAND_OFFSET, cmd_reg);

  return OK;
}

/****************************************************************************
 * Name: sdhci_blocksetup
 *
 * Description:
 *   Set the block size and block count for the next data transfer.
 *
 ****************************************************************************/

#ifdef CONFIG_SDIO_BLOCKSETUP
static void sdhci_blocksetup(FAR struct sdio_dev_s *dev,
                             unsigned int blocklen, unsigned int nblocks)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  /* Block Size Register (004h): bits 11-0 = transfer block size */

  sdhci_put16(priv, SDHCI_BLOCK_SIZE_OFFSET, blocklen);

  /* Block Count Register (006h): bits 15-0 = block count */

  sdhci_put16(priv, SDHCI_BLOCK_COUNT_OFFSET, nblocks);
}
#endif

/****************************************************************************
 * Name: sdhci_recvsetup
 *
 * Description:
 *   Configure for PIO read data transfer.
 *
 ****************************************************************************/

static int sdhci_recvsetup(FAR struct sdio_dev_s *dev,
                           FAR uint8_t *buffer, size_t nbytes)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  DEBUGASSERT(priv != NULL && buffer != NULL && nbytes > 0);

  /* Save the destination buffer for ISR data movement */

  priv->buffer.rx  = (uint32_t *)buffer;
  priv->remaining = nbytes;

  /* Enable read transfer interrupts: BRR, TC, and data errors.
   * Normal bits (BRR/TC) go through xfrints; error bits are implicitly
   * enabled via SDHCI_INT_ERROR in configxfrints.
   */

  sdhci_configxfrints(priv, SDHCI_RCV_XFR_INTS | SDHCI_INT_ERROR);
  return OK;
}

/****************************************************************************
 * Name: sdhci_sendsetup
 *
 * Description:
 *   Configure for PIO write data transfer.
 *
 ****************************************************************************/

static int sdhci_sendsetup(FAR struct sdio_dev_s *dev,
                           FAR const uint8_t *buffer, size_t nbytes)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  DEBUGASSERT(priv != NULL && buffer != NULL && nbytes > 0);

  /* Save the source buffer for ISR data movement. */

  priv->buffer.tx  = (const uint32_t *)buffer;
  priv->remaining = nbytes;

  /* Enable write transfer interrupts: BWR, TC, and data errors */

  sdhci_configxfrints(priv, SDHCI_SND_XFR_INTS | SDHCI_INT_ERROR);
  return OK;
}

/****************************************************************************
 * Name: sdhci_cancel
 *
 * Description:
 *   Cancel the current data transfer.
 *
 ****************************************************************************/

static int sdhci_cancel(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  /* Disable all transfer and event interrupts */

  sdhci_configxfrints(priv, 0);
  sdhci_configwaitints(priv, 0, 0, 0);

  /* Clear all pending interrupt status */

  sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET, 0xffff);
  sdhci_put16(priv, SDHCI_ERR_INT_STATUS_OFFSET, 0xffff);

  /* Cancel any watchdog timeout */

  wd_cancel(&priv->waitwdog);

  /* Software reset the data lines */

  sdhci_put8(priv, SDHCI_SOFTWARE_RESET_OFFSET,
             SDHCI_SOFTWARE_RESET_DAT);

  while ((sdhci_get8(priv, SDHCI_SOFTWARE_RESET_OFFSET) &
          SDHCI_SOFTWARE_RESET_DAT) != 0)
    {
      /* Wait for data line reset to complete */
    }

  priv->remaining = 0;
  return OK;
}

/****************************************************************************
 * Name: sdhci_waitresponse
 *
 * Description:
 *   Poll-wait for the command complete (CC) status bit.
 *
 ****************************************************************************/

static int sdhci_waitresponse(FAR struct sdio_dev_s *dev, uint32_t cmd)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  clock_t timeout;
  clock_t start;
  clock_t elapsed;
  uint16_t regval;
  int ret = OK;

  switch (cmd & MMCSD_RESPONSE_MASK)
    {
    case MMCSD_NO_RESPONSE:
      return OK;

    case MMCSD_R1_RESPONSE:
    case MMCSD_R1B_RESPONSE:
    case MMCSD_R2_RESPONSE:
    case MMCSD_R6_RESPONSE:
      timeout = SDHCI_LONGTIMEOUT;
      break;

    case MMCSD_R4_RESPONSE:
    case MMCSD_R5_RESPONSE:
      return -ENOSYS;

    case MMCSD_R3_RESPONSE:
    case MMCSD_R7_RESPONSE:
      timeout = SDHCI_CMDTIMEOUT;
      break;

    default:
      return -EINVAL;
    }

  /* Poll-wait for Command Complete (CC) bit in Normal Interrupt Status.
   * Per specification Section 2.2.18, Command Timeout Error has higher
   * priority than Command Complete: if the hardware reports a timeout,
   * the response was not received correctly even if CC is set.
   */

  start = clock_systime_ticks();

  while ((sdhci_get16(priv, SDHCI_INT_STATUS_OFFSET) &
          SDHCI_INT_CMD_COMPLETE) == 0)
    {
      if ((sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET) &
           SDHCI_ERR_INT_CMD_TIMEOUT) != 0)
        {
          return -ETIMEDOUT;
        }

      elapsed = clock_systime_ticks() - start;
      if (elapsed >= timeout)
        {
          return -ETIMEDOUT;
        }
    }

  /* Check for hardware detected errors.  Command Timeout Error is checked
   * first because it has priority over Command Complete (Section 2.2.18).
   */

  regval = sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET);
  if ((regval & SDHCI_ERR_INT_CMD_TIMEOUT) != 0)
    {
      ret = -ETIMEDOUT;
    }
  else if ((regval & SDHCI_RESPERR_INTS) != 0)
    {
      ret = -EIO;
    }

  /* For R1b responses, wait for Transfer Complete to indicate the
   * card is no longer busy (DAT0 line released).
   */

  if ((cmd & MMCSD_RESPONSE_MASK) == MMCSD_R1B_RESPONSE)
    {
      /* Clear CC first so we can detect TC */

      sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET,
                  SDHCI_INT_CMD_COMPLETE);

      start = clock_systime_ticks();

      while ((sdhci_get16(priv, SDHCI_INT_STATUS_OFFSET) &
              SDHCI_INT_TRANSFER_COMPLETE) == 0)
        {
          /* A Data Timeout Error indicates the busy signal was not
           * released within the data timeout window.
           */

          if ((sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET) &
               SDHCI_ERR_INT_DATA_TIMEOUT) != 0)
            {
              return -ETIMEDOUT;
            }

          elapsed = clock_systime_ticks() - start;
          if (elapsed >= SDHCI_DATATIMEOUT)
            {
              return -ETIMEDOUT;
            }
        }

      /* Clear TC */

      sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET,
                  SDHCI_INT_TRANSFER_COMPLETE);
    }

  /* Clear the response wait status bits (write-1-to-clear) */

  sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET,
              (uint16_t)SDHCI_RESPDONE_INTS);
  sdhci_put16(priv, SDHCI_ERR_INT_STATUS_OFFSET,
              (uint16_t)SDHCI_RESPERR_INTS);

  return ret;
}

/****************************************************************************
 * Name: sdhci_recvshortcrc
 *
 * Description:
 *   Read a 48-bit response with CRC (R1, R1b, R6) from RSPREG0.
 *
 ****************************************************************************/

static int sdhci_recvshortcrc(FAR struct sdio_dev_s *dev, uint32_t cmd,
                              FAR uint32_t *rshort)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint16_t errval;
  int ret = OK;

  errval = sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET);

  if ((errval & SDHCI_ERR_INT_CMD_TIMEOUT) != 0)
    {
      ret = -ETIMEDOUT;
    }
  else if ((errval & SDHCI_ERR_INT_CMD_CRC) != 0)
    {
      ret = -EIO;
    }

  /* R1/R1b/R6 returned in RSPREG0 */

  if (rshort != NULL)
    {
      *rshort = sdhci_get32(priv, SDHCI_RESPONSE0_OFFSET);
    }

  return ret;
}

/****************************************************************************
 * Name: sdhci_recvlong
 *
 * Description:
 *   Read a 136-bit response (R2 = CID/CSD) from RSPREG0-RSPREG3.
 *
 ****************************************************************************/

static int sdhci_recvlong(FAR struct sdio_dev_s *dev, uint32_t cmd,
                          FAR uint32_t rlong[4])
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint16_t errval;
  int ret = OK;

  errval = sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET);

  if ((errval & SDHCI_ERR_INT_CMD_TIMEOUT) != 0)
    {
      ret = -ETIMEDOUT;
    }
  else if ((errval & SDHCI_ERR_INT_CMD_CRC) != 0)
    {
      ret = -EIO;
    }

  /* Read the 4 x 32-bit response registers and shift out the
   * 8 overhead bits (start + transmission + reserved) of the
   * 136-bit response to reconstruct the 128-bit CID/CSD payload.
   */

  if (rlong != NULL)
    {
      uint32_t rsp0 = sdhci_get32(priv, SDHCI_RESPONSE0_OFFSET);
      uint32_t rsp1 = sdhci_get32(priv, SDHCI_RESPONSE1_OFFSET);
      uint32_t rsp2 = sdhci_get32(priv, SDHCI_RESPONSE2_OFFSET);
      uint32_t rsp3 = sdhci_get32(priv, SDHCI_RESPONSE3_OFFSET);

      rlong[0] = (rsp3 << 8) | (rsp2 >> 24);
      rlong[1] = (rsp2 << 8) | (rsp1 >> 24);
      rlong[2] = (rsp1 << 8) | (rsp0 >> 24);
      rlong[3] = (rsp0 << 8);
    }

  return ret;
}

/****************************************************************************
 * Name: sdhci_recvshort
 *
 * Description:
 *   Read a 48-bit response without CRC check (R3, R7) from RSPREG0.
 *
 ****************************************************************************/

static int sdhci_recvshort(FAR struct sdio_dev_s *dev, uint32_t cmd,
                           FAR uint32_t *rshort)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint16_t errval;
  int ret = OK;

  errval = sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET);

  if ((errval & SDHCI_ERR_INT_CMD_TIMEOUT) != 0)
    {
      ret = -ETIMEDOUT;
    }

  /* Note: CRC error is NOT checked for R3/R7 */

  if (rshort != NULL)
    {
      *rshort = sdhci_get32(priv, SDHCI_RESPONSE0_OFFSET);
    }

  return ret;
}

/****************************************************************************
 * Name: sdhci_recvnotimpl
 *
 * Description:
 *   R4/R5 responses (SDIO) are not used for eMMC.
 *
 ****************************************************************************/

static int sdhci_recvnotimpl(FAR struct sdio_dev_s *dev, uint32_t cmd,
                             FAR uint32_t *rnotimpl)
{
  return -ENOSYS;
}

/****************************************************************************
 * Name: sdhci_waitenable
 *
 * Description:
 *   Enable event-wait interrupts and start the timeout watchdog.
 *
 ****************************************************************************/

static void sdhci_waitenable(FAR struct sdio_dev_s *dev,
                             sdio_eventset_t eventset,
                             uint32_t timeout)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  uint32_t waitints = 0;

  /* Disable event-related interrupts first */

  sdhci_configwaitints(priv, 0, 0, 0);

  /* Select the interrupt mask for the requested events */

  if ((eventset & (SDIOWAIT_CMDDONE | SDIOWAIT_RESPONSEDONE)) != 0)
    {
      waitints |= SDHCI_RESPDONE_INTS;
    }

  if ((eventset & SDIOWAIT_TRANSFERDONE) != 0)
    {
      waitints |= SDHCI_XFRDONE_INTS;
    }

  /* Enable event-related interrupts */

  sdhci_configwaitints(priv, waitints, eventset, 0);

  /* Start watchdog if timeout is requested.
   * Check priv->waitevents instead of the eventset parameter because
   * the ISR may have already fired and cleared waitevents between
   * configwaitints and here.
   */

  if ((priv->waitevents & SDIOWAIT_TIMEOUT) != 0)
    {
      if (timeout == 0)
        {
          priv->wkupevent = SDIOWAIT_TIMEOUT;
          return;
        }

      wd_start(&priv->waitwdog, MSEC2TICK(timeout),
               sdhci_eventtimeout, (wdparm_t)priv);
    }
}

/****************************************************************************
 * Name: sdhci_eventwait
 *
 * Description:
 *   Block the calling thread until an event or timeout occurs.
 *
 ****************************************************************************/

static sdio_eventset_t sdhci_eventwait(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;
  sdio_eventset_t wkupevent = 0;
  int ret;

  for (; ; )
    {
      ret = nxsem_wait_uninterruptible(&priv->waitsem);
      if (ret < 0)
        {
          /* Clean up wait state before returning */

          wd_cancel(&priv->waitwdog);
          sdhci_configwaitints(priv, 0, 0, 0);
          return SDIOWAIT_ERROR;
        }

      wkupevent = priv->wkupevent;
      if (wkupevent != 0)
        {
          break;
        }
    }

  sdhci_configwaitints(priv, 0, 0, 0);
  return wkupevent;
}

/****************************************************************************
 * Name: sdhci_callbackenable
 *
 * Description:
 *   Enable media change callback events.
 *
 ****************************************************************************/

static void sdhci_callbackenable(FAR struct sdio_dev_s *dev,
                                 sdio_eventset_t eventset)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  priv->cbevents = eventset;
  sdhci_callback(priv);
}

/****************************************************************************
 * Name: sdhci_registercallback
 *
 * Description:
 *   Register a callback function for media change events.
 *
 ****************************************************************************/

#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
static int sdhci_registercallback(FAR struct sdio_dev_s *dev,
                                  worker_t callback, FAR void *arg)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  priv->cbevents = 0;
  priv->cbarg    = arg;
  priv->callback = callback;
  return OK;
}
#endif

/****************************************************************************
 * Name: sdhci_gotextcsd
 *
 * Description:
 *   Called by upper-half after reading EXT_CSD.  No special handling needed.
 *
 ****************************************************************************/

static void sdhci_gotextcsd(FAR struct sdio_dev_s *dev,
                            FAR const uint8_t *buffer)
{
  /* No-op for standard SDHCI */
}

/****************************************************************************
 * Name: sdhci_eventtimeout
 *
 * Description:
 *   Watchdog timeout handler for event waiting.
 *
 ****************************************************************************/

static void sdhci_eventtimeout(wdparm_t arg)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)arg;

  if ((priv->waitevents & SDIOWAIT_TIMEOUT) != 0)
    {
      sdhci_endwait(priv, SDIOWAIT_TIMEOUT);
    }
}

/****************************************************************************
 * Name: sdhci_interrupt
 *
 * Description:
 *   SDHCI interrupt service routine.
 *
 ****************************************************************************/

static int sdhci_interrupt(int irq, FAR void *context, FAR void *arg)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)arg;
  uint32_t pending;
  uint16_t normal_status;
  uint16_t error_status;
  uint16_t clr_mask = 0;

  /* Read interrupt status from both registers.
   * Normal Interrupt Status (030h) and Error Interrupt Status (032h)
   * are separate 16-bit registers with different bit definitions.
   */

  normal_status = sdhci_get16(priv, SDHCI_INT_STATUS_OFFSET);
  error_status  = sdhci_get16(priv, SDHCI_ERR_INT_STATUS_OFFSET);

  /* Clear only the interrupt bits we handle in the ISR.
   * CC (Command Complete) is NOT cleared here because waitresponse()
   * polls it directly.
   * We clear: card events, BRR, BWR, TC, and error bits.
   */

  /* Card insertion/removal */

  clr_mask |= SDHCI_INT_CARD_INSERTION | SDHCI_INT_CARD_REMOVAL;

  /* Transfer interrupts that the ISR handles */

  clr_mask |= normal_status & (SDHCI_INT_READ_READY |
                               SDHCI_INT_WRITE_READY |
                               SDHCI_INT_TRANSFER_COMPLETE);

  sdhci_put16(priv, SDHCI_INT_STATUS_OFFSET, clr_mask);

  /* Clear error bits if any */

  if (error_status != 0)
    {
      sdhci_put16(priv, SDHCI_ERR_INT_STATUS_OFFSET, error_status);
    }

  /* ---- Handle card insertion/removal ---- */

  if ((normal_status & SDHCI_INT_CARD_INSERTION) != 0)
    {
      priv->cdstatus |= SDIO_STATUS_PRESENT;
      sdhci_callback(priv);
    }

  if ((normal_status & SDHCI_INT_CARD_REMOVAL) != 0)
    {
      priv->cdstatus &= ~SDIO_STATUS_PRESENT;
      sdhci_callback(priv);
    }

  /* ---- Handle data transfer interrupts ---- */

  pending = normal_status & priv->xfrints;
  if (pending != 0)
    {
      /* Buffer Read Ready: move data from card to memory */

      if ((pending & SDHCI_INT_READ_READY) != 0)
        {
          while (priv->remaining > 0 &&
                 (sdhci_get32(priv, SDHCI_PRESENT_STATE_OFFSET) &
                  SDHCI_PRESENT_STATE_BUF_READ_ENABLE) != 0)
            {
              uint32_t data = sdhci_get32(priv,
                                          SDHCI_BUFFER_DATA_PORT_OFFSET);

              if (priv->remaining >= sizeof(uint32_t))
                {
                  *priv->buffer.rx++ = data;
                  priv->remaining -= sizeof(uint32_t);
                }
              else
                {
                  FAR uint8_t *ptr = (FAR uint8_t *)priv->buffer.rx;
                  FAR uint8_t *src = (FAR uint8_t *)&data;
                  size_t i;

                  for (i = 0; i < priv->remaining; i++)
                    {
                      *ptr++ = *src++;
                    }

                  priv->remaining = 0;
                }
            }
        }

      /* Buffer Write Ready: move data from memory to card */

      if ((pending & SDHCI_INT_WRITE_READY) != 0)
        {
          while (priv->remaining > 0 &&
                 (sdhci_get32(priv, SDHCI_PRESENT_STATE_OFFSET) &
                  SDHCI_PRESENT_STATE_BUF_WRITE_ENABLE) != 0)
            {
              uint32_t data = 0;

              if (priv->remaining >= sizeof(uint32_t))
                {
                  data = *priv->buffer.tx++;
                  priv->remaining -= sizeof(uint32_t);
                }
              else
                {
                  FAR const uint8_t *ptr =
                                    (FAR const uint8_t *)priv->buffer.tx;
                  FAR uint8_t *dst = (FAR uint8_t *)&data;
                  size_t i;

                  for (i = 0; i < priv->remaining; i++)
                    {
                      *dst++ = *ptr++;
                    }

                  priv->remaining = 0;
                }

              sdhci_put32(priv, SDHCI_BUFFER_DATA_PORT_OFFSET, data);
            }
        }

      /* Per specification Section 2.2.18, Transfer Complete has higher
       * priority than Data Timeout Error.  If both are set, command
       * execution is considered complete.  Check TC first, then errors
       * only when TC is not set.
       */

      if ((pending & SDHCI_INT_TRANSFER_COMPLETE) != 0)
        {
          sdhci_endtransfer(priv, SDIOWAIT_TRANSFERDONE);
        }
      else if ((error_status & SDHCI_XFRERR_INTS) != 0)
        {
          if ((error_status & SDHCI_ERR_INT_DATA_TIMEOUT) != 0)
            {
              sdhci_endtransfer(priv, SDIOWAIT_TRANSFERDONE |
                                     SDIOWAIT_TIMEOUT);
            }
          else
            {
              sdhci_endtransfer(priv, SDIOWAIT_TRANSFERDONE |
                                     SDIOWAIT_ERROR);
            }
        }
    }

  /* ---- Handle wait events (command response) ---- */

  pending = normal_status & priv->waitints;
  if (pending != 0)
    {
      if ((pending & SDHCI_INT_CMD_COMPLETE) != 0)
        {
          if ((priv->waitevents & (SDIOWAIT_CMDDONE |
                                   SDIOWAIT_RESPONSEDONE)) != 0)
            {
              sdhci_endwait(priv, SDIOWAIT_RESPONSEDONE);
            }
        }

      if ((pending & SDHCI_INT_TRANSFER_COMPLETE) != 0)
        {
          if ((priv->waitevents & SDIOWAIT_TRANSFERDONE) != 0)
            {
              sdhci_endwait(priv, SDIOWAIT_TRANSFERDONE);
            }
        }
    }

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sdhci_initialize
 *
 * Description:
 *   Create and initialize an SDHCI device instance.  This is the public
 *   entry point called by the board-level glue layer (e.g. PCI probe).
 *
 *   The caller provides the MMIO base address and IRQ number of the SDHCI
 *   controller.  The function returns a pointer to the sdio_dev_s interface
 *   which can be passed to mmcsd_slotinitialize().
 *
 * Input Parameters:
 *   base - MMIO base address of the SDHCI controller
 *   irq  - Interrupt number for the SDHCI controller
 *
 * Returned Value:
 *   Pointer to sdio_dev_s on success, NULL on failure.
 *
 ****************************************************************************/

FAR struct sdio_dev_s *sdhci_initialize(uintptr_t base, int irq)
{
  FAR struct sdhci_dev_s *priv;
  uint64_t caps;

  DEBUGASSERT(base != 0 && irq >= 0);

  /* Allocate and initialize the device instance */

  priv = kmm_malloc(sizeof(*priv));
  if (priv == NULL)
    {
      return NULL;
    }

  memset(priv, 0, sizeof(*priv));

  /* Initialize the sdio_dev_s interface and runtime state */

  priv->dev = (struct sdio_dev_s)
    {
      .mutex            = NXMUTEX_INITIALIZER,
      .reset            = sdhci_reset,
      .capabilities     = sdhci_capabilities,
      .status           = sdhci_status,
      .widebus          = sdhci_widebus,
      .clock            = sdhci_clock,
      .attach           = sdhci_attach,
      .sendcmd          = sdhci_sendcmd,
#ifdef CONFIG_SDIO_BLOCKSETUP
      .blocksetup       = sdhci_blocksetup,
#endif
      .recvsetup        = sdhci_recvsetup,
      .sendsetup        = sdhci_sendsetup,
      .cancel           = sdhci_cancel,
      .waitresponse     = sdhci_waitresponse,
      .recv_r1          = sdhci_recvshortcrc,
      .recv_r2          = sdhci_recvlong,
      .recv_r3          = sdhci_recvshort,
      .recv_r4          = sdhci_recvnotimpl,
      .recv_r5          = sdhci_recvnotimpl,
      .recv_r6          = sdhci_recvshortcrc,
      .recv_r7          = sdhci_recvshort,
      .waitenable       = sdhci_waitenable,
      .eventwait        = sdhci_eventwait,
      .callbackenable   = sdhci_callbackenable,
#if defined(CONFIG_SCHED_WORKQUEUE) && defined(CONFIG_SCHED_HPWORK)
      .registercallback = sdhci_registercallback,
#endif
      .gotextcsd        = sdhci_gotextcsd,
    };

  priv->base    = base;
  priv->irq     = irq;

  nxsem_init(&priv->waitsem, 0, 0);
  spin_lock_init(&priv->lock);

  /* Reset the controller to initial state first.
   * Per specification Section 3.1, software reset should be performed
   * before any other initialization.
   */

  sdhci_reset(&priv->dev);

  /* Power on the SD bus.
   * Per specification Section 2.2.12: "Before setting this register,
   * the Host Driver shall check the Voltage Support bits in the
   * Capabilities register."
   */

  caps = ((uint64_t)sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET + 4) << 32) |
         sdhci_get32(priv, SDHCI_CAPABILITIES_OFFSET);

  if ((caps & SDHCI_CAPABILITIES_VOLTAGE_3_3V) != 0)
    {
      sdhci_put8(priv, SDHCI_POWER_CONTROL_OFFSET,
                 SDHCI_POWER_CONTROL_VDD1_3_3V |
                 SDHCI_POWER_CONTROL_VDD1_POWER);
    }
  else if ((caps & SDHCI_CAPABILITIES_VOLTAGE_3_0V) != 0)
    {
      sdhci_put8(priv, SDHCI_POWER_CONTROL_OFFSET,
                 SDHCI_POWER_CONTROL_VDD1_3_0V |
                 SDHCI_POWER_CONTROL_VDD1_POWER);
    }
  else if ((caps & SDHCI_CAPABILITIES_VOLTAGE_1_8V) != 0)
    {
      sdhci_put8(priv, SDHCI_POWER_CONTROL_OFFSET,
                 SDHCI_POWER_CONTROL_VDD1_1_8V |
                 SDHCI_POWER_CONTROL_VDD1_POWER);
    }

  return &priv->dev;
}

/****************************************************************************
 * Name: sdhci_uninitialize
 *
 * Description:
 *   Release the resources allocated by sdhci_initialize() and disable the
 *   controller.  This is the counterpart used by the glue layer (e.g. PCI
 *   remove) to tear down the SDHCI instance.
 *
 * Input Parameters:
 *   dev - Pointer to the sdio_dev_s interface returned by sdhci_initialize()
 *
 ****************************************************************************/

void sdhci_uninitialize(FAR struct sdio_dev_s *dev)
{
  FAR struct sdhci_dev_s *priv = (FAR struct sdhci_dev_s *)dev;

  if (priv == NULL)
    {
      return;
    }

  /* Disable and detach the interrupt handler */

  if (priv->irq >= 0)
    {
      up_disable_irq(priv->irq);
      irq_detach(priv->irq);
    }

  /* Cancel any pending event watchdog and release resources */

  wd_cancel(&priv->waitwdog);
  nxsem_destroy(&priv->waitsem);
  kmm_free(priv);
}
