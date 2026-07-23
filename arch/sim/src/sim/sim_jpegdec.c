/****************************************************************************
 * arch/sim/src/sim/sim_jpegdec.c
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

#include <nuttx/video/video.h>
#include <turbojpeg.h>

#include "sim_jpegdec.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define tj_init_decompress        tjInitDecompress
#define tj_destroy                tjDestroy
#define tj_decompress_header3     tjDecompressHeader3
#define tj_decompress_to_yuv      tjDecompressToYUVPlanes
#define tj_decompress2            tjDecompress2

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int sim_jpegdec_to_yuv420p(tjhandle handle,
                                  const void *src, uint32_t src_size,
                                  void *dst, uint32_t *dst_size,
                                  int w, int h)
{
  unsigned char *planes[3];
  uint32_t cw = (w + 1) / 2;
  uint32_t ch = (h + 1) / 2;

  planes[0] = dst;
  planes[1] = planes[0] + w * h;
  planes[2] = planes[1] + cw * ch;

  if (tj_decompress_to_yuv(handle, src, src_size,
                           planes, w, NULL, h, 0) != 0)
    {
      return -EIO;
    }

  *dst_size = w * h + 2 * cw * ch;
  return 0;
}

static int sim_jpegdec_to_nv12(tjhandle handle,
                               const void *src, uint32_t src_size,
                               void *dst, uint32_t *dst_size,
                               int w, int h)
{
  unsigned char *planes[3];
  unsigned char *uv_dst;
  uint32_t cw = (w + 1) / 2;
  uint32_t ch = (h + 1) / 2;
  int i;

  uv_dst = (unsigned char *)dst + w * h;
  planes[0] = dst;
  planes[1] = uv_dst;
  planes[2] = uv_dst + cw * ch;

  if (tj_decompress_to_yuv(handle, src, src_size,
                           planes, w, NULL, h, 0) != 0)
    {
      return -EIO;
    }

  /* In-place interleave U and V backward to avoid overwrite */

  for (i = cw * ch - 1; i >= 0; i--)
    {
      uv_dst[i * 2 + 1] = planes[2][i];
      uv_dst[i * 2 + 0] = planes[1][i];
    }

  *dst_size = w * h + 2 * cw * ch;
  return 0;
}

static int sim_jpegdec_to_rgb(tjhandle handle,
                              const void *src, uint32_t src_size,
                              void *dst, uint32_t *dst_size,
                              int w, int h, int tjpf, int bpp)
{
  if (tj_decompress2(handle, src, src_size, dst,
                     w, 0, h, tjpf, 0) != 0)
    {
      return -EIO;
    }

  *dst_size = w * h * bpp;
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void *sim_jpegdec_open(void)
{
  return tj_init_decompress();
}

void sim_jpegdec_close(void *dec)
{
  if (dec != NULL)
    {
      tj_destroy(dec);
    }
}

int sim_jpegdec_decode(void *dec,
                       const void *src, uint32_t src_size,
                       void *dst, uint32_t *dst_size,
                       uint32_t *width, uint32_t *height,
                       uint32_t out_fmt)
{
  int w;
  int h;
  int subsamp;
  int colorspace;
  int ret;

  if (dec == NULL || src == NULL || dst == NULL)
    {
      return -EINVAL;
    }

  if (tj_decompress_header3(dec, src, src_size,
                            &w, &h, &subsamp, &colorspace) != 0)
    {
      return -EIO;
    }

  switch (out_fmt)
    {
      case V4L2_PIX_FMT_YUV420:
        ret = sim_jpegdec_to_yuv420p(dec, src, src_size, dst, dst_size,
                                     w, h);
        break;

      case V4L2_PIX_FMT_NV12:
        ret = sim_jpegdec_to_nv12(dec, src, src_size, dst, dst_size,
                                  w, h);
        break;

      case V4L2_PIX_FMT_RGB24:
        ret = sim_jpegdec_to_rgb(dec, src, src_size, dst, dst_size,
                                 w, h, TJPF_RGB, 3);
        break;

      case V4L2_PIX_FMT_BGR24:
        ret = sim_jpegdec_to_rgb(dec, src, src_size, dst, dst_size,
                                 w, h, TJPF_BGR, 3);
        break;

      case V4L2_PIX_FMT_XRGB32:
        ret = sim_jpegdec_to_rgb(dec, src, src_size, dst, dst_size,
                                 w, h, TJPF_XRGB, 4);
        break;

      case V4L2_PIX_FMT_XBGR32:
        ret = sim_jpegdec_to_rgb(dec, src, src_size, dst, dst_size,
                                 w, h, TJPF_XBGR, 4);
        break;

      default:
        return -EINVAL;
    }

  if (ret == 0)
    {
      *width = w;
      *height = h;
    }

  return ret;
}
