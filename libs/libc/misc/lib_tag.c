/****************************************************************************
 * libs/libc/misc/lib_tag.c
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

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/param.h>

#include <nuttx/tag.h>

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* Kernel tag names -- order must match enum nxtag_e exactly */

static FAR const char *const g_nxtag_names[] =
{
  "global",
  "analog",
  "audio",
  "battery",
  "binfmt",
  "can",
  "capture",
  "contactless",
  "crypto",
  "dma",
  "fs",
  "gpio",
  "graphics",
  "hwtracing",
  "i2c",
  "i2s",
  "i3c",
  "input",
  "ipc",
  "irq",
  "lcd",
  "leds",
  "lib",
  "memcard",
  "mm",
  "motor",
  "net",
  "paging",
  "pci",
  "power",
  "ptp",
  "pwm",
  "rc",
  "regmap",
  "reset",
  "rmt",
  "rpmsg",
  "rptun",
  "rtc",
  "safety",
  "sched",
  "sensors",
  "spi",
  "stepper",
  "syscall",
  "tee",
  "thermal",
  "timer",
  "usb",
  "vhost",
  "video",
  "virtio",
  "watchdog",
  "wireless",
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxtag_get_tag_len
 ****************************************************************************/

static int nxtag_get_tag_len(FAR const char *name, size_t len)
{
  size_t i;

  if (name == NULL || len == 0)
    {
      return -EINVAL;
    }

  for (i = 0; i < nitems(g_nxtag_names); i++)
    {
      if (strncmp(g_nxtag_names[i], name, len) == 0 &&
          g_nxtag_names[i][len] == '\0')
        {
          return i;
        }
    }

  return -ENOENT;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxtag_get_name
 ****************************************************************************/

FAR const char *nxtag_get_name(uint8_t tag)
{
  if (tag < nitems(g_nxtag_names))
    {
      return g_nxtag_names[tag];
    }

  return NULL;
}

/****************************************************************************
 * Name: nxtag_get_tag
 ****************************************************************************/

int nxtag_get_tag(FAR const char *name)
{
  if (name == NULL)
    {
      return -EINVAL;
    }

  return nxtag_get_tag_len(name, strlen(name));
}

/****************************************************************************
 * Name: nxtag_parse_mask
 ****************************************************************************/

ssize_t nxtag_parse_mask(FAR const char *str, FAR struct nxtag_mask_s *mask)
{
  bool invert = false;
  size_t count = 0;
  size_t len;
  int tag;

  if (str == NULL || mask == NULL)
    {
      return -EINVAL;
    }

  if (*str == '~')
    {
      invert = true;
      str++;
    }

  if (invert)
    {
      NXTAG_MASK_FILL(mask->tag_mask);
      NXTAG_MASK_CLR(mask->tag_mask, NXTAG_GLOBAL);
    }
  else
    {
      NXTAG_MASK_ZERO(mask->tag_mask);
    }

  while (*str != '\0')
    {
      str += strspn(str, " ,");
      if (*str == '\0')
        {
          break;
        }

      len = strcspn(str, ", ");
      tag = nxtag_get_tag_len(str, len);
      if (tag >= 0)
        {
          if (invert)
            {
              NXTAG_MASK_CLR(mask->tag_mask, tag);
            }
          else
            {
              NXTAG_MASK_SET(mask->tag_mask, tag);
            }

          count++;
        }

      str += len;
    }

  return count;
}

/****************************************************************************
 * Name: nxtag_format_mask
 ****************************************************************************/

ssize_t nxtag_format_mask(FAR const struct nxtag_mask_s *mask,
                          FAR char *buf, size_t buflen)
{
  FAR const char *name;
  size_t pos = 0;
  bool first = true;
  int ret;
  int t;

  if (mask == NULL || buf == NULL || buflen == 0)
    {
      return -EINVAL;
    }

  buf[0] = '\0';

  for (t = 0; t <= NXTAG_MAX; t++)
    {
      if (NXTAG_MASK_ISSET(mask->tag_mask, t))
        {
          name = nxtag_get_name(t);
          if (name)
            {
              ret = snprintf(buf + pos, buflen - pos,
                             "%s%s", first ? "" : ",", name);
            }
          else
            {
              ret = snprintf(buf + pos, buflen - pos,
                             "%s%d", first ? "" : ",", t);
            }

          if (ret > 0)
            {
              pos += ret;
            }

          first = false;
        }
    }

  if (first)
    {
      strlcpy(buf, "(none)", buflen);
      pos = strlen(buf);
    }

  return pos;
}
