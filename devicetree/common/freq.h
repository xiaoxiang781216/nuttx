/****************************************************************************
 * devicetree/common/freq.h
 *
 * Copyright (c) 2021 Linaro Limited
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 ****************************************************************************/

#ifndef __DEVICETREE_COMMON_FREQ_H__
#define __DEVICETREE_COMMON_FREQ_H__

#define DT_FREQ_K(x) ((x) * 1000)
#define DT_FREQ_M(x) (DT_FREQ_K(x) * 1000)

#endif /* __DEVICETREE_COMMON_FREQ_H__ */
