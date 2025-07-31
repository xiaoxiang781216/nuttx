/****************************************************************************
 * boards/xtensa/esp32s3/esp32s3-devkit/src/esp32s3_rpmsg.c
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

#include <stdint.h>
#include <stdbool.h>
#include <debug.h>

#include <nuttx/drivers/drivers.h>
#include <nuttx/drivers/rpmsgblk.h>
#include <nuttx/drivers/rpmsgdev.h>
#include <nuttx/rpmsg/rpmsg_port.h>
#include <nuttx/serial/uart_rpmsg.h>

#include "esp32s3-devkit.h"
#include "esp32s3_ioexpander.h"
#include "esp32s3_spi.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_port_spi_slave_init
 *
 * Description:
 *   This is a platform-specific function whose configures, such as
 *   rpmsg_port_config_s and rpmsg_port_spi_config_s, should be modified to
 *   meet the real platform needs. And current configuration in this function
 *   is an example.
 *
 * Input Parameters:
 *   NULL.
 *
 * Returned Value:
 *   NULL.
 *
 ****************************************************************************/

#if defined(CONFIG_ESP32S3_SPI) && defined(CONFIG_RPMSG_PORT_SPI_SLAVE)
static void rpmsg_port_spi_slave_init(void)
{
  const struct rpmsg_port_config_s rpmsg_port_cfg =
    {
      .txnum = 8,
      .rxnum = 8,
      .txlen = 1024,
      .rxlen = 1024,
      .txbuf = NULL,
      .rxbuf = NULL,
      .remotecpu = "ap",
    };

  const struct rpmsg_port_spi_config_s spicfg =
    {
      .mreq_pin = 2,
      .mreq_invert = IOEXPANDER_VAL_NORMAL,
      .sreq_pin = 1,
      .sreq_invert = IOEXPANDER_VAL_NORMAL,
      .nbits = 8,
      .mode = SPISLAVE_MODE0,
    };

  FAR struct spi_slave_ctrlr_s *ctrlr =
    esp32s3_spislave_ctrlr_initialize(ESP32S3_SPI2);
  FAR struct ioexpander_dev_s *ioe = esp32s3_ioexpander_initialize();
  int ret;

  if (ioe == NULL || ctrlr == NULL)
    {
      syslog(LOG_ERR, "invaild ioe: %p ctrlr :%p\n", ioe, ctrlr);
      return;
    }

  ret = rpmsg_port_spi_slave_initialize(&rpmsg_port_cfg, &spicfg,
                                        ctrlr, ioe);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Failed to init rpmsg port spi slave\n");
    }
  else
    {
      syslog(LOG_ERR, "init rpmsg port spi slave success\n");
    }
}
#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsg_serialinit
 ****************************************************************************/

#ifdef CONFIG_RPMSG_UART
void rpmsg_serialinit(void)
{
  uart_rpmsg_init("ap", "remote", 4096, true);
}
#endif

/****************************************************************************
 * Name: esp32s3_rpmsg_initialize
 ****************************************************************************/

void esp32s3_rpmsg_initialize(void)
{
#if defined(CONFIG_ESP32S3_SPI) && defined(CONFIG_RPMSG_PORT_SPI_SLAVE)
  rpmsg_port_spi_slave_init();
#endif

#ifdef CONFIG_DEV_RPMSG
  rpmsgdev_register("ap", "/dev/console", "/dev/ap-console");
  rpmsgdev_register("ap", "/dev/null", "/dev/ap-null");
#endif

#ifdef CONFIG_BLK_RPMSG
  rpmsgblk_register("ap", "/dev/ram2", NULL);
#endif

#ifdef CONFIG_RPMSGMTD
  rpmsgmtd_register("ap", "/dev/rammtd", NULL);
#endif
}
