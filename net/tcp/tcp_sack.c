/****************************************************************************
 * net/tcp/tcp_sack.c
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "tcp/tcp.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define DUPTHRESH  TCP_FAST_RETRANSMISSION_THRESH  /* 3 */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: tcp_sack_update
 *
 * Description:
 *   Update the sender-side scoreboard with SACK blocks received in an ACK.
 *   Inserts new ranges, merges overlapping/adjacent entries, maintains
 *   sorted order by left_edge.  When full, evicts the lowest entry.
 *
 * Input Parameters:
 *   conn   - The TCP connection
 *   sacks  - Array of SACK ranges parsed from the ACK
 *   nsacks - Number of SACK ranges
 *
 * Returned Value:
 *   true if new information was merged into the scoreboard
 *
 ****************************************************************************/

bool tcp_sack_update(FAR struct tcp_conn_s *conn,
                     FAR const struct tcp_sack_s *sacks,
                     uint8_t nsacks)
{
  FAR struct tcp_sack_s *sr = conn->sack_ranges;
  bool newinfo = false;
  uint8_t i;
  uint8_t k;

  for (k = 0; k < nsacks; k++)
    {
      uint32_t left = sacks[k].left;
      uint32_t right = sacks[k].right;
      uint8_t n;
      uint8_t j;
      uint8_t merge_start;
      uint8_t merge_end;
      bool already_covered;

      /* Validate: discard invalid ranges */

      if (TCP_SEQ_GTE(left, right))
        {
          /* Empty or invalid range */

          continue;
        }

      /* Check if this range is already fully covered by an existing
       * scoreboard entry.  If so, skip (no new information).
       */

      already_covered = false;
      n = conn->nsacks;

      for (i = 0; i < n; i++)
        {
          if (TCP_SEQ_LTE(sr[i].left, left) &&
              TCP_SEQ_GTE(sr[i].right, right))
            {
              already_covered = true;
              break;
            }
        }

      if (already_covered)
        {
          continue;
        }

      /* This range brings new information */

      newinfo = true;

      /* Find all entries that overlap or are adjacent to [left, right).
       * Two ranges overlap/adjacent when:
       *   entry.left <= right && entry.right >= left
       */

      merge_start = n;  /* First overlapping entry index */
      merge_end = 0;    /* Count of overlapping entries */

      for (i = 0; i < n; i++)
        {
          if (TCP_SEQ_LTE(sr[i].left, right) &&
              TCP_SEQ_GTE(sr[i].right, left))
            {
              /* Expand the new range to cover this entry */

              if (TCP_SEQ_LT(sr[i].left, left))
                {
                  left = sr[i].left;
                }

              if (TCP_SEQ_GT(sr[i].right, right))
                {
                  right = sr[i].right;
                }

              /* Track merge region */

              if (i < merge_start)
                {
                  merge_start = i;
                }

              merge_end = i + 1;
            }
        }

      if (merge_end > merge_start)
        {
          /* Remove merged entries by compacting */

          uint8_t removed = merge_end - merge_start;

          for (j = merge_start; j + removed < n; j++)
            {
              sr[j] = sr[j + removed];
            }

          n -= removed;
        }

      /* Insert [left, right) in sorted position */

      if (n < TCP_SACK_RANGES_MAX)
        {
          /* Find insertion point (sorted by left_edge) */

          for (i = 0; i < n; i++)
            {
              if (TCP_SEQ_LT(left, sr[i].left))
                {
                  break;
                }
            }

          /* Shift entries to make room */

          for (j = n; j > i; j--)
            {
              sr[j] = sr[j - 1];
            }

          sr[i].left = left;
          sr[i].right = right;
          n++;
        }
      else
        {
          /* Scoreboard is full.  Evict the lowest (first) entry if the
           * new range is higher than it, otherwise evict the new range.
           */

          if (TCP_SEQ_GT(left, sr[0].left))
            {
              /* Remove lowest entry, insert new one in sorted position */

              for (i = 0; i + 1 < n; i++)
                {
                  if (TCP_SEQ_LT(left, sr[i + 1].left))
                    {
                      break;
                    }

                  sr[i] = sr[i + 1];
                }

              sr[i].left = left;
              sr[i].right = right;
            }

          /* else: new range is lower than all existing entries, discard */
        }

      conn->nsacks = n;
    }

  return newinfo;
}

/****************************************************************************
 * Name: tcp_sack_advance
 *
 * Description:
 *   Advance the scoreboard when the cumulative ACK moves forward.
 *   Removes entries fully below highack, trims entries partially below.
 *
 * Input Parameters:
 *   conn    - The TCP connection
 *   highack - The new cumulative acknowledgment number
 *
 ****************************************************************************/

void tcp_sack_advance(FAR struct tcp_conn_s *conn, uint32_t highack)
{
  FAR struct tcp_sack_s *sr = conn->sack_ranges;
  uint8_t n = conn->nsacks;
  uint8_t i = 0;
  uint8_t j;

  while (i < n)
    {
      if (TCP_SEQ_LTE(sr[i].right, highack))
        {
          /* Entry is completely below highack, remove it */

          for (j = i; j + 1 < n; j++)
            {
              sr[j] = sr[j + 1];
            }

          n--;

          /* Don't increment i, re-check the same index */
        }
      else if (TCP_SEQ_LT(sr[i].left, highack))
        {
          /* Entry partially below highack, trim left edge */

          sr[i].left = highack;
          i++;
        }
      else
        {
          /* Entry is entirely above highack, done (sorted order) */

          break;
        }
    }

  conn->nsacks = n;
}

/****************************************************************************
 * Name: tcp_sack_reset
 *
 * Description:
 *   Completely reset the scoreboard (called on RTO).
 *   Clears all entries and resets HighRxt.
 *
 * Input Parameters:
 *   conn - The TCP connection
 *
 ****************************************************************************/

void tcp_sack_reset(FAR struct tcp_conn_s *conn)
{
  conn->nsacks = 0;
  conn->newsack = false;
  conn->highrxt = 0;
}

/****************************************************************************
 * Name: tcp_sack_islost
 *
 * Description:
 *   Determine if a segment at the given sequence number is lost per
 *   RFC 6675 IsLost.  A segment is lost if:
 *   (a) DupThresh (3) or more SACK ranges have left_edge strictly
 *       greater than seqno; OR
 *   (b) Total bytes SACKed above seqno exceeds 2 * conn->mss.
 *
 * Input Parameters:
 *   conn  - The TCP connection
 *   seqno - Start sequence number of the segment
 *
 * Returned Value:
 *   true if the segment is considered lost
 *
 ****************************************************************************/

bool tcp_sack_islost(FAR const struct tcp_conn_s *conn, uint32_t seqno)
{
  FAR const struct tcp_sack_s *sr = conn->sack_ranges;
  uint8_t n = conn->nsacks;
  uint8_t above_count = 0;
  uint32_t above_bytes = 0;
  uint8_t i;

  for (i = 0; i < n; i++)
    {
      if (TCP_SEQ_GT(sr[i].left, seqno))
        {
          above_count++;
          above_bytes += sr[i].right - sr[i].left;
        }
    }

  /* Condition (a): 3+ distinct SACK ranges above */

  if (above_count >= DUPTHRESH)
    {
      return true;
    }

  /* Condition (b): total SACKed bytes above > 2 * MSS */

  if (above_bytes > 2u * (uint32_t)conn->mss)
    {
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: tcp_sack_issacked
 *
 * Description:
 *   Check if a segment [seqno, seqno+len) is fully covered by a single
 *   scoreboard entry (already SACKed by the receiver).
 *
 * Input Parameters:
 *   conn  - The TCP connection
 *   seqno - Start sequence number of the segment
 *   len   - Length of the segment in bytes
 *
 * Returned Value:
 *   true if the segment is completely covered by a SACK range
 *
 ****************************************************************************/

bool tcp_sack_issacked(FAR const struct tcp_conn_s *conn,
                       uint32_t seqno, uint16_t len)
{
  FAR const struct tcp_sack_s *sr = conn->sack_ranges;
  uint8_t n = conn->nsacks;
  uint32_t seg_end = TCP_SEQ_ADD(seqno, len);
  uint8_t i;

  for (i = 0; i < n; i++)
    {
      if (TCP_SEQ_GTE(seqno, sr[i].left) &&
          TCP_SEQ_LTE(seg_end, sr[i].right))
        {
          return true;
        }
    }

  return false;
}
