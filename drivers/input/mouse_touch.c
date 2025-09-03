/****************************************************************************
 * drivers/input/mouse_touch.c
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
#include <ctype.h>
#include <sys/param.h>

#include <nuttx/input/mouse.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/kmalloc.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* This structure is for mouse upper half driver */

struct mouse_upperhalf_s
{
  FAR struct mouse_lowerhalf_s *lower; /* A pointer of lower half instance */

#ifdef CONFIG_INPUT_MOUSE_TOUCH_ADJUST_NAME
  char                         name[NAME_MAX]; /* Device name */
#endif

  struct touch_lowerhalf_s     touchlower;     /* Touch device lower half instance */
  struct touch_sample_s        touchsample;    /* touch sample */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mouse_event
 ****************************************************************************/

void mouse_event(FAR void *priv, FAR const struct mouse_report_s *sample)
{
  FAR struct mouse_upperhalf_s *upper = priv;

  upper->touchsample.npoints = 1;
  upper->touchsample.point[0].x =
    CLAMP(0, upper->touchsample.point[0].x + sample->x,
             CONFIG_INPUT_MOUSE_TOUCH_XMAX);
  upper->touchsample.point[0].y =
    CLAMP(0, upper->touchsample.point[0].y + sample->y,
             CONFIG_INPUT_MOUSE_TOUCH_YMAX);

  if ((sample->buttons & MOUSE_BUTTON_1) ||
      (sample->buttons & MOUSE_BUTTON_2))
    {
      upper->touchsample.point[0].flags |= TOUCH_DOWN;
    }
  else
    {
      upper->touchsample.point[0].flags = TOUCH_UP;
    }

  upper->touchsample.point[0].timestamp = touch_get_time();
  touch_event(upper->touchlower.priv, &upper->touchsample);

  upper->touchsample.point[0].flags = 0;
}

/****************************************************************************
 * Public Function
 ****************************************************************************/

/****************************************************************************
 * Name: mouse_register
 ****************************************************************************/

int mouse_register(FAR struct mouse_lowerhalf_s *lower,
                   FAR const char *path, uint8_t nums)
{
  FAR struct mouse_upperhalf_s *upper;
  int ret;

  iinfo("Registering %s\n", path);

  if (lower == NULL || nums == 0)
    {
      ierr("ERROR: invalid mouse device\n");
      return -EINVAL;
    }

  upper = kmm_zalloc(sizeof(struct mouse_upperhalf_s));
  if (!upper)
    {
      ierr("ERROR: Failed to mem alloc!\n");
      return -ENOMEM;
    }

  lower->priv  = upper;
  upper->lower = lower;
  upper->touchlower.maxpoint = 1;

#ifdef CONFIG_INPUT_MOUSE_TOUCH_ADJUST_NAME
  path = strrchr(path, '/');
  if (path != NULL)
    {
      path++;
      while (*path != '\0' && !isdigit(*path))
        {
          path++;
        }
    }

  if (path != NULL && *path != '\0')
    {
      snprintf(upper->name, NAME_MAX, "/dev/input%s", path);
    }
  else
    {
      snprintf(upper->name, NAME_MAX, "/dev/input0");
    }

  ret = touch_register(&upper->touchlower, upper->name, nums);
#else
  ret = touch_register(&upper->touchlower, path, nums);
#endif

  return ret;
}

/****************************************************************************
 * Name: mouse_unregister
 ****************************************************************************/

void mouse_unregister(FAR struct mouse_lowerhalf_s *lower,
                      FAR const char *path)
{
  FAR struct mouse_upperhalf_s *upper;

  DEBUGASSERT(lower != NULL);
  DEBUGASSERT(lower->priv != NULL);

  upper = lower->priv;

#ifdef CONFIG_INPUT_MOUSE_TOUCH_ADJUST_NAME
  touch_unregister(&upper->touchlower, upper->name);
#else
  touch_unregister(&upper->touchlower, path);
#endif

  kmm_free(upper);
}
