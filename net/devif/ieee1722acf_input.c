/****************************************************************************
 * net/devif/ieee1722acf_input.c
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

#include <debug.h>
#include <errno.h>

#include <nuttx/net/can.h>
#include <nuttx/net/cangw.h>
#include <nuttx/net/ieee1722.h>
#include <nuttx/net/netdev.h>

#include "netforward/ieee1722acf.h"
#include "netforward/netforward.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ieee1722acf_can_parse
 *
 * Description:
 *   This function is to parse the CAN payload from AVTP ACF packet.
 *
 * Input Parameters:
 *   dev       - The device on which the packet was received and which
 *               contains the packet.
 *   can_iob   - The iob buffer to contain the can frame to be forwarded.
 *   start     - The data start offset of the CAN payload in AVTP ACF pkt.
 *   msg_hdr   - The AVTP ACF message header.
 *
 * Returned Value:
 *   The offset of next message in the AVTP ACF pkt if parsed successfully;
 *
 ****************************************************************************/

static int ieee1722acf_can_parse(FAR struct net_driver_s *dev,
                                 FAR struct iob_s *can_iob,
                                 int offset,
                                 FAR struct ieee1722acf_msg_hdr_s *msg_hdr)
{
#ifdef CONFIG_NET_CAN_CANFD
  FAR struct canfd_frame *can_frame =
      (FAR struct canfd_frame *)IOB_DATA(can_iob);
#else
  FAR struct can_frame *can_frame =
      (FAR struct can_frame *)IOB_DATA(can_iob);
#endif
  FAR struct ieee1722acf_can_common_hdr_s can_hdr;
  uint32_t can_id;
  size_t data_len;
  int ret;

  /* Copy CAN common header */

  ret = iob_copyout((uint8_t *)&can_hdr, dev->d_iob,
                    sizeof(struct ieee1722acf_can_common_hdr_s),
                    offset + sizeof(struct ieee1722acf_msg_hdr_s));
  if (ret != sizeof(struct ieee1722acf_can_common_hdr_s))
    {
      nerr("ERROR: Failed to copyout acf can common hdr\n");
      return ret < 0 ? ret : -EINVAL;
    }

  data_len = (msg_hdr->acf_msg_lenth_l + (msg_hdr->acf_msg_lenth_h << 8))
              * IEEE1722ACF_MSG_LEN_ALIGN - can_hdr.padding;

  switch (msg_hdr->acf_msg_type)
    {
      case IEEE1722ACF_TYPE_CAN_BRIEF:
        {
          offset += offsetof(struct ieee1722acf_canbrief_hdr_s, canid);
          data_len -= sizeof(struct ieee1722acf_canbrief_hdr_s);
        }
      break;
      case IEEE1722ACF_TYPE_CAN:
        {
          /* CAN message timestamp offset */

          offset += offsetof(struct ieee1722acf_can_hdr_s, canid);
          data_len -= sizeof(struct ieee1722acf_can_hdr_s);
        }
      break;
      default:
        {
          nerr("ERROR: Invalid acf can msg type %d\n",
               msg_hdr->acf_msg_type);
          return -EINVAL;
        }
      break;
    }

  /* Copy CAN message id */

  ret = iob_copyout((uint8_t *)&can_id, dev->d_iob,
                    sizeof(can_id), offset);
  if (ret != sizeof(can_id))
    {
      nerr("ERROR: Failed to copyout acf can id\n");
      return ret < 0 ? ret : -EINVAL;
    }

  offset += sizeof(can_id);

  can_frame->flags = 0;

  if (can_hdr.esi)
    {
      can_frame->flags |= CANFD_ESI;
    }
  else
    {
      can_frame->flags &= ~CANFD_ESI;
    }

#ifdef CONFIG_NET_CAN_CANFD
  if (can_hdr.brs)
    {
      can_frame->flags |= CANFD_BRS;
    }
  else
    {
      can_frame->flags &= ~CANFD_BRS;
    }

  if (can_hdr.fdf)
    {
      can_frame->flags |= CANFD_FDF;
    }
  else
    {
      can_frame->flags &= ~CANFD_FDF;
    }
#endif

  if (can_hdr.eff)
    {
      can_frame->can_id = HTONL(can_id) & CAN_EFF_MASK;
      can_frame->can_id |= CAN_EFF_FLAG;
    }
  else
    {
      can_frame->can_id = HTONL(can_id) & CAN_SFF_MASK;
    }

#ifdef CONFIG_NET_CAN_CANFD
  can_frame->len = data_len;
  if (can_frame->len > 64)
    {
      nerr("ERROR: Invalid CAN-FD data len %d\n", can_frame->len);
      return -EINVAL;
    }

  iob_update_pktlen(can_iob, CANFD_MTU - CANFD_MAX_DLEN + data_len, true);
#else
  can_frame->can_dlc = data_len;
  if (can_frame->can_dlc > 8)
    {
      nerr("ERROR: Invalid CAN data len %d\n", can_frame->can_dlc);
      return -EINVAL;
    }

  iob_update_pktlen(can_iob, CAN_MTU - CAN_MAX_DLEN + data_len, true);
#endif
  /* Copy CAN message data */

  ret = iob_copyout((uint8_t *)&can_frame->data, dev->d_iob,
                    data_len, offset);
  if (ret != data_len)
    {
      nerr("ERROR: Failed to copyout acf can msg data\n");
      return ret < 0 ? ret : -EINVAL;
    }

  return offset + data_len + can_hdr.padding;
}

/****************************************************************************
 * Name: ieee1722acf_canforward
 *
 * Description:
 *   This function is called from ieee1722acf_input when it is necessary to
 *   forward a can packet to different device.  In this case, the forwarding
 *   operation must be performed asynchronously when the TX poll is received
 *   from the forwarding device.
 *
 * Input Parameters:
 *   dev       - The device on which the packet was received and which
 *               contains the packet.
 *   can_iob   - The can iob to be forwarded.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static inline void ieee1722acf_canforward(FAR struct net_driver_s *dev,
                                          FAR struct iob_s *can_iob)
{
  FAR struct iob_s *o_iob = dev->d_iob;
  uint8_t d_llhdrlen = dev->d_llhdrlen;

  dev->d_iob = can_iob;
  dev->d_llhdrlen = 0;

  can_input(dev);

  /* Release old buffer */

  netdev_iob_release(dev);

  /* Set old buffer */

  dev->d_iob = o_iob;
  dev->d_len = o_iob->io_pktlen;
  dev->d_llhdrlen = d_llhdrlen;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ieee1722acf_input
 *
 * Description:
 *   This function is called from eth_input when avtp acf frame received.
 *   In this case, the packet need to be parsed to get the CAN frame and
 *   forward to target CAN device.
 *
 * Input Parameters:
 *   dev      - The device on which the packet was received and which
 *              contains the packet.
 *   subtype  - 1722 ACF frame subtype.
 *
 * Returned Value:
 *   Zero is returned if the packet was successfully parsed;  A negated
 *   errno value is returned if the packet is not forwardable.
 *
 ****************************************************************************/

int ieee1722acf_input(FAR struct net_driver_s *dev, uint8_t subtype)
{
  struct ieee1722acf_msg_hdr_s msg_hdr;
  FAR struct iob_s *can_iob = NULL;
  size_t payload_len;
  int offset;
  int ret = OK;

  switch (subtype)
  {
    case IEEE1722_SUBTYPE_NTSCF:
      {
        FAR struct ieee1722acf_ntscf_hdr_s *packt_hdr =
          (FAR struct ieee1722acf_ntscf_hdr_s *)IOB_DATA(dev->d_iob);
        payload_len = packt_hdr->ntscf_data_length_l +
                      (packt_hdr->ntscf_data_length_h << 8);

        offset = sizeof(struct ieee1722acf_ntscf_hdr_s);
      }
    break;

    case IEEE1722_SUBTYPE_TSCF:
      {
        FAR struct ieee1722acf_tscf_hdr_s *packt_hdr =
          (FAR struct ieee1722acf_tscf_hdr_s *)IOB_DATA(dev->d_iob);
        payload_len = HTONS(packt_hdr->stream_data_length);

        offset = sizeof(struct ieee1722acf_tscf_hdr_s);
      }
    break;

    default:
      {
        nwarn("WARNING: Not supported avtp subtype %02x\n", subtype);
        ret = -ENOTSUP;
        goto out;
      }
    break;
  }

  if (dev->d_iob->io_pktlen != offset + payload_len)
    {
      nerr("ERROR: Invalid acf packet, pktlen %u, payload len %zu\n",
            dev->d_iob->io_pktlen, payload_len);
      ret = -EINVAL;
      goto out;
    }

  while (dev->d_iob->io_pktlen - offset >
         sizeof(struct ieee1722acf_msg_hdr_s))
    {
      ret = iob_copyout((uint8_t *)&msg_hdr, dev->d_iob,
                        sizeof(struct ieee1722acf_msg_hdr_s), offset);

      if (ret != sizeof(struct ieee1722acf_msg_hdr_s))
        {
          nerr("ERROR: Failed to copyout acf msg hdr\n");
          ret = ret < 0 ? ret : -EINVAL;
          goto out;
        }

      if (msg_hdr.acf_msg_type != IEEE1722ACF_TYPE_CAN &&
          msg_hdr.acf_msg_type != IEEE1722ACF_TYPE_CAN_BRIEF)
        {
          nwarn("WARNING: Not support acf msg type %d\n",
                msg_hdr.acf_msg_type);
          ret = -ENOTSUP;
          goto out;
        }

      can_iob = iob_tryalloc_with_size(NET_CAN_BUFSIZE, true);
      if (can_iob == NULL)
        {
          nerr("ERROR: Failed to allocate CAN IOB\n");
          ret = -ENOMEM;
          goto out;
        }

      IOB_SET_OWNER(can_iob, "ieee1722acf forward");

#ifdef CONFIG_NET_TIMESTAMP
      if ((dev->d_features & NETDEV_RX_STAMP) == 0)
        {
          clock_gettime(CLOCK_REALTIME, &can_iob->io_time);
        }
      else
        {
          can_iob->io_time = dev->d_iob->io_time;
        }
#endif

      offset = ieee1722acf_can_parse(dev, can_iob, offset, &msg_hdr);
      if (offset < 0)
        {
          nerr("ERROR: Failed to parse acf can payload\n");
          iob_free_chain(can_iob);
          ret = offset;
          goto out;
        }

      ieee1722acf_canforward(dev, can_iob);
    }

out:
  dev->d_len = 0;
  return ret;
}
