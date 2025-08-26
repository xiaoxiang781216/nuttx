/****************************************************************************
 * arch/sim/src/sim/sim_spi.c
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

#include <nuttx/kmalloc.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spinlock.h>
#include <nuttx/wqueue.h>

#include "sim_internal.h"
#include "sim_hostspi.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct sim_spi_dev_s
{
  const struct spi_ops_s *ops; /* SPI vtable */
  int fd;                      /* SPI device file descriptor */
#ifdef CONFIG_SPI_HWFEATURES
  spi_hwfeatures_t        hwfeatures; /* Some hardware features. */
#endif
#ifdef CONFIG_SPI_CALLBACK
  struct work_s           work;
  spi_mediachange_t       cb;
  void                   *cb_args;
  const void             *txbuffer;
  void                   *rxbuffer;
  size_t                  nwords;
#endif
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int sim_spi_lock(struct spi_dev_s *dev, bool lock);
static void sim_spi_select(struct spi_dev_s *dev, uint32_t devid,
                           bool selected);
static uint32_t sim_spi_setfrequency(struct spi_dev_s *dev,
                                     uint32_t frequency);
#ifdef CONFIG_SPI_CS_DELAY_CONTROL
static int sim_spi_setdelay(struct spi_dev_s *dev, uint32_t a, uint32_t b,
                            uint32_t c);
#endif
static void sim_spi_setmode(struct spi_dev_s *dev, enum spi_mode_e mode);
static void sim_spi_setbits(struct spi_dev_s *dev, int nbits);
#ifdef CONFIG_SPI_HWFEATURES
static int sim_spi_hwfeatures(struct spi_dev_s *dev,
                              spi_hwfeatures_t features);
#endif
static uint8_t sim_spi_status(struct spi_dev_s *dev, uint32_t devid);
#ifdef CONFIG_SPI_CMDDATA
static int sim_spi_cmddata(struct spi_dev_s *dev, uint32_t devid,
                           bool cmd);
#endif
static uint32_t sim_spi_send(struct spi_dev_s *dev, uint32_t wd);
#ifdef CONFIG_SPI_EXCHANGE
static void sim_spi_exchange(struct spi_dev_s *dev, const void *txbuffer,
                             void *rxbuffer, size_t nwords);
#else
static void sim_spi_sndblock(struct spi_dev_s *dev, const void *buffer,
                             size_t nwords);
static void sim_spi_recvblock(struct spi_dev_s *dev, void *buffer,
                              size_t nwords);
#endif
#ifdef CONFIG_SPI_TRIGGER
static int sim_spi_trigger(struct spi_dev_s *dev);
#endif
#ifdef CONFIG_SPI_CALLBACK
static int sim_spi_registercallback(struct spi_dev_s *dev,
                                    spi_mediachange_t callback, void *arg);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct spi_ops_s g_sim_spi_ops =
{
  .lock             = sim_spi_lock,
  .select           = sim_spi_select,
  .setfrequency     = sim_spi_setfrequency,
#ifdef CONFIG_SPI_CS_DELAY_CONTROL
  .setdelay         = sim_spi_setdelay,
#endif
  .setmode          = sim_spi_setmode,
  .setbits          = sim_spi_setbits,
#ifdef CONFIG_SPI_HWFEATURES
  .hwfeatures       = sim_spi_hwfeatures,
#endif
  .status           = sim_spi_status,
#ifdef CONFIG_SPI_CMDDATA
  .cmddata          = sim_spi_cmddata,
#endif
  .send             = sim_spi_send,
#ifdef CONFIG_SPI_EXCHANGE
  .exchange         = sim_spi_exchange,
#else
  .sndblock         = sim_spi_sndblock,
  .recvblock        = sim_spi_recvblock,
#endif
#ifdef CONFIG_SPI_TRIGGER
  .trigger          = sim_spi_trigger,
#endif
#ifdef CONFIG_SPI_CALLBACK
  .registercallback = sim_spi_registercallback,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sim_spi_lock
 ****************************************************************************/

static int sim_spi_lock(struct spi_dev_s *dev, bool lock)
{
  return 0;
}

/****************************************************************************
 * Name: sim_spi_select
 ****************************************************************************/

static void sim_spi_select(struct spi_dev_s *dev, uint32_t devid,
                           bool selected)
{
}

/****************************************************************************
 * Name: sim_spi_setfrequency
 ****************************************************************************/

static uint32_t sim_spi_setfrequency(struct spi_dev_s *dev,
                                     uint32_t frequency)
{
  struct sim_spi_dev_s *priv = (struct sim_spi_dev_s *)dev;

  return host_spi_setfrequency(priv->fd, frequency);
}

/****************************************************************************
 * Name: sim_spi_setdelay
 ****************************************************************************/

#ifdef CONFIG_SPI_CS_DELAY_CONTROL
static int sim_spi_setdelay(struct spi_dev_s *dev, uint32_t startdelay,
                            uint32_t stopdelay, uint32_t csdelay)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name: sim_spi_setmode
 ****************************************************************************/

static void sim_spi_setmode(struct spi_dev_s *dev, enum spi_mode_e mode)
{
  struct sim_spi_dev_s *priv = (struct sim_spi_dev_s *)dev;

  switch (mode)
    {
      case SPIDEV_MODE0:
        host_spi_setmode(priv->fd, false, false);
        break;

      /* In fact SPIDEV_MODETI is equal to SPIDEV_MODE1 (CPOL=0 CPHA=1). */

      case SPIDEV_MODETI:
      case SPIDEV_MODE1:
        host_spi_setmode(priv->fd, false, true);
        break;

      case SPIDEV_MODE2:
        host_spi_setmode(priv->fd, true, false);
        break;

      case SPIDEV_MODE3:
        host_spi_setmode(priv->fd, true, true);
        break;

      default:
        host_spi_setmode(priv->fd, false, false);
        break;
    }
}

/****************************************************************************
 * Name: sim_spi_setbits
 ****************************************************************************/

static void sim_spi_setbits(struct spi_dev_s *dev, int nbits)
{
  struct sim_spi_dev_s *priv = (struct sim_spi_dev_s *)dev;

  return host_spi_setbits(priv->fd, nbits);
}

/****************************************************************************
 * Name: sim_spi_hwfeatures
 ****************************************************************************/

#ifdef CONFIG_SPI_HWFEATURES
static int sim_spi_hwfeatures(struct spi_dev_s *dev,
                              spi_hwfeatures_t features)
{
  struct host_spi_dev_s *priv = (struct host_spi_dev_s *)dev;

  /* Only support HWFEAT_LSBFIRST */

  if (features & HWFEAT_LSBFIRST)
    {
      return host_spi_setlsbfirst(priv->fd, true);
    }

  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name: sim_spi_status
 ****************************************************************************/

static uint8_t sim_spi_status(struct spi_dev_s *dev, uint32_t devid)
{
  return 0;
}

/****************************************************************************
 * Name: sim_spi_cmddata
 ****************************************************************************/

#ifdef CONFIG_SPI_CMDDATA
static int sim_spi_cmddata(struct spi_dev_s *dev, uint32_t devid, bool cmd)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name: sim_spi_transfer_worker
 ****************************************************************************/

#ifdef CONFIG_SPI_CALLBACK
static void sim_spi_transfer_worker(FAR void *arg)
{
  struct sim_spi_dev_s *priv = arg;
  irqstate_t flags = irq_save_nopreempt();

  host_spi_transfer(priv->fd, priv->txbuffer, priv->rxbuffer, priv->nwords);
  irq_restore_nopreempt(flags);
  priv->cb(priv->cb_args);
}
#endif

/****************************************************************************
 * Name: sim_spi_transfer
 ****************************************************************************/

static void sim_spi_transfer(struct spi_dev_s *dev, const void *txbuffer,
                             void *rxbuffer, size_t nwords)
{
  struct sim_spi_dev_s *priv = (struct sim_spi_dev_s *)dev;

  /* When CONFIG_SPI_CALLBACK is configured, use aysnc or sync api according
   * to the priv->cb
   */

#ifdef CONFIG_SPI_CALLBACK
  if (priv->cb)
    {
      priv->txbuffer = txbuffer;
      priv->rxbuffer = rxbuffer;
      priv->nwords = nwords;
      work_queue(HPWORK, &priv->work, sim_spi_transfer_worker, priv, 0);
    }
  else
    {
      host_spi_transfer(priv->fd, txbuffer, rxbuffer, nwords);
    }
#else
  host_spi_transfer(priv->fd, txbuffer, rxbuffer, nwords);
#endif
}

/****************************************************************************
 * Name: sim_spi_send
 ****************************************************************************/

static uint32_t sim_spi_send(struct spi_dev_s *dev, uint32_t wd)
{
  uint32_t recvwd = 0;

  sim_spi_transfer(dev, &wd, &recvwd, 1);

  return recvwd;
}

/****************************************************************************
 * Name: sim_spi_exchange
 ****************************************************************************/

#ifdef CONFIG_SPI_EXCHANGE
static void sim_spi_exchange(struct spi_dev_s *dev, const void *txbuffer,
                             void *rxbuffer, size_t nwords)
{
  sim_spi_transfer(dev, txbuffer, rxbuffer, nwords);
}
#endif

/****************************************************************************
 * Name: sim_spi_sndblock
 ****************************************************************************/

#ifndef CONFIG_SPI_EXCHANGE
static void sim_spi_sndblock(struct spi_dev_s *dev, const void *buffer,
                             size_t nwords)
{
  sim_spi_transfer(dev, txbuffer, NULL, nwords);
}
#endif

/****************************************************************************
 * Name: host_spi_recvblock
 ****************************************************************************/

#ifndef CONFIG_SPI_EXCHANGE
static void host_spi_recvblock(struct spi_dev_s *dev, void *buffer,
                               size_t nwords)
{
  sim_spi_transfer(dev, NULL, rxbuffer, nwords);
}
#endif

/****************************************************************************
 * Name: sim_spi_trigger
 ****************************************************************************/

#ifdef CONFIG_SPI_TRIGGER
static int sim_spi_trigger(struct spi_dev_s *dev)
{
  return -ENOSYS;
}
#endif

/****************************************************************************
 * Name: sim_spi_registercallback
 ****************************************************************************/

#ifdef CONFIG_SPI_CALLBACK
static int sim_spi_registercallback(struct spi_dev_s *dev,
                                    spi_mediachange_t callback, void *arg)
{
  struct sim_spi_dev_s *priv = (struct sim_spi_dev_s *)dev;

  priv->cb = callback;
  priv->cb_args = arg;
  return 0;
}
#endif

/****************************************************************************
 * Name: sim_spi_initialize
 ****************************************************************************/

struct spi_dev_s *sim_spi_initialize(const char *path)
{
  struct sim_spi_dev_s *priv;

  priv = kmm_zalloc(sizeof(*priv));
  if (priv == NULL)
    {
      spiinfo("Failed to allocate private spi master driver");
      return NULL;
    }

  priv->fd = host_spi_open(path);
  if (priv->fd < 0)
    {
      spiinfo("Failed to open %s: %d", path, priv->fd);
      free(priv);
      return NULL;
    }

  priv->ops = &g_sim_spi_ops;

  return (struct spi_dev_s *)priv;
}

/****************************************************************************
 * Name: sim_spi_uninitialize
 ****************************************************************************/

void sim_spi_uninitialize(struct spi_dev_s *dev)
{
  struct sim_spi_dev_s *priv = (struct sim_spi_dev_s *)dev;
  if (priv->fd >= 0)
    {
      host_spi_close(priv->fd);
    }

  kmm_free(priv);
}
