/****************************************************************************
 * arch/sim/src/sim/posix/sim_linuxspi.c
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

#include <sys/types.h>
#include <sys/ioctl.h>

#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

#include <linux/spi/spidev.h>

#include "sim_hostspi.h"
#include "sim_internal.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: host_spi_setfrequency
 *
 * Description:
 *   Provide spi setfrequency, used for set SPI clock frequency.
 *   Note that only MAX_SPEED_HZ could be configured out of a transfer for a
 *   Linux SPI port. The Linux SPI may set a exact frequecy using the value
 *   of spi_ioc_transfer.speed_hz when transferring. If the
 *   spi_ioc_transfer.speed_hz is 0, the MAX_SPEED_HZ is used. In practice,
 *   the real frequecy on the CLK wire will be affected by the hardware.
 *
 * Input Parameters:
 *   fd        - SPI device opened.
 *   frequency - The frequencey of SPI clock in Hz.
 *
 * Returned Value:
 *   Returns the actual frequency in Hz.
 *
 ****************************************************************************/

uint32_t host_spi_setfrequency(int fd, uint32_t frequency)
{
  uint32_t actualfreq;

  host_uninterruptible(ioctl, fd, SPI_IOC_WR_MAX_SPEED_HZ, &frequency);
  host_uninterruptible(ioctl, fd, SPI_IOC_RD_MAX_SPEED_HZ, &actualfreq);

  return actualfreq;
}

/****************************************************************************
 * Name: host_spi_setmode
 *
 * Description:
 *   Provide spi setmode.
 *   SPI mode defination in nuttx is almost the same to that in Linux.
 *
 * Input Parameters:
 *   fd   - SPI device opened.
 *   cpol - cpol of SPI mode.
 *   cpha - cpha of SPI mode.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void host_spi_setmode(int fd, bool cpol, bool cpha)
{
  uint8_t mode;

  if (cpol)
    {
      mode = cpha ? SPI_MODE_3 : SPI_MODE_2;
    }
  else
    {
      mode = cpha ? SPI_MODE_1 : SPI_MODE_0;
    }

  host_uninterruptible(ioctl, fd, SPI_IOC_WR_MODE, &mode);
}

/****************************************************************************
 * Name: host_spi_setbits
 *
 * Description:
 *   Provide spi setbits, used for set bits per word during a transfer.
 *
 * Input Parameters:
 *   fd    - SPI device opened.
 *   nbits - The number of bits in an SPI word.
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void host_spi_setbits(int fd, int nbits)
{
  uint8_t bits_per_word = (uint8_t)nbits;

  host_uninterruptible(ioctl, fd, SPI_IOC_WR_BITS_PER_WORD,
                       &bits_per_word);
}

/****************************************************************************
 * Name: host_spi_setlsbfirst
 *
 * Description:
 *   Set SPI LSB first transfer which default to be MSB first.
 *
 * Input Parameters:
 *   fd  - SPI device opened.
 *   lsb - Set LSB first if true, otherwise set to MSB first.
 *
 * Returned Value:
 *   0 for success, and negated errno for error.
 *
 ****************************************************************************/

int host_spi_setlsbfirst(int fd, bool lsb)
{
  uint8_t lsbfirst = lsb ? 1 : 0;

  return host_uninterruptible(ioctl, fd, SPI_IOC_WR_LSB_FIRST, &lsbfirst);
}

/****************************************************************************
 * Name: host_spi_transfer
 *
 * Description:
 *   Transfer buffer though the SPI device.
 *
 * Input Parameters:
 *   fd       - SPI device opened.
 *   txbuffer - Pointer to the buffer to be sent.
 *   txbuffer - Pointer to the buffer to receive data.
 *   nwords   - Number of words to transfer.
 *
 * Returned Value:
 *   Received word value.
 *
 ****************************************************************************/

int host_spi_transfer(int fd, const void *txbuffer, void *rxbuffer,
                      size_t nwords)
{
  struct spi_ioc_transfer transfer_data =
    {
      .tx_buf = (unsigned long)txbuffer,
      .rx_buf = (unsigned long)rxbuffer,
      .len = (uint32_t)nwords,
      .cs_change = true,
    };

  return host_uninterruptible(ioctl, fd, SPI_IOC_MESSAGE(1),
                              &transfer_data);
}

/****************************************************************************
 * Name: host_spi_open
 *
 * Description:
 *   Initialize one SPI port
 *
 * Input Parameters:
 *   path - The spi device path.
 *
 * Returned Value:
 *   File descriptor of SPI device for success, negated errno for error.
 *
 ****************************************************************************/

int host_spi_open(const char *path)
{
  return host_uninterruptible(open, path, O_RDWR);
}

/****************************************************************************
 * Name: host_spi_close
 *
 * Description:
 *   Uninitialize an SPI port
 *
 * Input Parameters:
 *   fd - The SPI device opened.
 *
 * Returned Value:
 *   0 for OK.
 *
 ****************************************************************************/

int host_spi_close(int fd)
{
  return host_uninterruptible(close, fd);
}
