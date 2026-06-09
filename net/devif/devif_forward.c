/****************************************************************************
 * net/devif/devif_forward.c
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

#include <string.h>
#include <assert.h>
#include <debug.h>

#include <nuttx/mm/iob.h>
#include <nuttx/net/netdev.h>

#include "netforward/netforward.h"
#include "devif/devif.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: devif_forward
 *
 * Description:
 *   Called from protocol-specific IP forwarding logic to re-send a packet.
 *
 * Input Parameters:
 *   fwd - An initialized instance of the common forwarding structure that
 *         includes everything needed to perform the forwarding operation.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   The network is locked.
 *
 ****************************************************************************/

void devif_forward(FAR struct forward_s *fwd)
{
  FAR struct net_driver_s *dev = fwd->f_dev;
  FAR struct iob_s *iob = fwd->f_iob;

#if CONFIG_IOB_EXTRA_HEADS > 0
  if (dev->d_alloc != NULL)
    {
      /* The egress device supplies its own (e.g. DMA-capable / zero-copy)
       * IOBs through d_alloc.  The forwarded chain originates from the
       * ingress device or the global IOB pool, so it cannot be installed
       * directly via netdev_iob_replace. Obtain a device-owned chain through
       * netdev_iob_prepare / netdev_iob_update_pktlen (honoring d_alloc)
       * and copy the forwarded packet into it, then release the original
       * chain.
       */

      unsigned int len = iob->io_pktlen;

      if (netdev_iob_prepare(dev, false, 0) != OK ||
          iob_clone_partial(iob, len, 0, dev->d_iob, 0, false, false) != OK)
        {
          nerr("ERROR: Failed to migrate forward IOB to device %s\n",
               dev->d_ifname);
          netdev_iob_release(dev);
        }
      else
        {
          /* Match netdev_iob_replace(): d_len reflects the L3 packet len. */

          dev->d_len = len;
        }

      iob_free_chain(iob);
    }
  else
#endif
    {
      /* Move the IOB chain that contains the L3 header and any data
       * payload
       */

      netdev_iob_replace(dev, iob);
    }

  dev->d_sndlen = 0;
  fwd->f_iob = NULL;
}
