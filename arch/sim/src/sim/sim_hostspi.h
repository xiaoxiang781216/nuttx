/****************************************************************************
 * arch/sim/src/sim/sim_hostspi.h
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

#ifndef __ARCH_SIM_SRC_SIM_SPI_H
#define __ARCH_SIM_SRC_SIM_SPI_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#ifdef __SIM__
#  include "config.h"
#endif

#include <stdint.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

uint32_t host_spi_setfrequency(int fd, uint32_t frequency);
void host_spi_setmode(int fd, bool cpol, bool cpha);
void host_spi_setbits(int fd, int nbits);
int host_spi_setlsbfirst(int fd, bool lsb);
int host_spi_transfer(int fd, const void *txbuffer, void *rxbuffer,
                      size_t nwords);
int host_spi_open(const char *path);
int host_spi_close(int fd);

#endif