/****************************************************************************
 * drivers/input/goldfish_events.c
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

#include <debug.h>

#include <nuttx/arch.h>
#include <nuttx/irq.h>
#include <nuttx/queue.h>
#include <nuttx/kmalloc.h>
#include <nuttx/wqueue.h>

#include <nuttx/input/goldfish_events.h>
#include <nuttx/input/mouse.h>
#include <nuttx/input/touchscreen.h>
#include <nuttx/input/keyboard.h>
#include <nuttx/input/kbd_codec.h>
#include <nuttx/input/virtio-input-event-codes.h>

/****************************************************************************
 * Pre-processor definitions
 ****************************************************************************/

#ifndef putreg32
#define putreg32(v, x) (*(FAR volatile uint32_t *)(x) = (v))
#endif

#ifndef getreg32
#define getreg32(x) (*(FAR volatile uint32_t *)(x))
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum
{
  GOLDFISH_EVENTS_READ         = 0x00,    /* Read next event type, code or value. */
  GOLDFISH_EVENTS_SET_PAGE     = 0x00,    /* Set page index. */
  GOLDFISH_EVENTS_LEN          = 0x04,    /* Read length of page data. */
  GOLDFISH_EVENTS_DATA         = 0x08,    /* Read page data. */

  GOLDFISH_EVENTS_PAGE_NAME    = 0x0000,  /* Keyboard charmap name. */
  GOLDFISH_EVENTS_PAGE_EVBITS  = 0x10000, /* Event code supported sets. */
  GOLDFISH_EVENTS_PAGE_ABSDATA = 0x20003  /* (really 0x20000 + EV_ABS) EV_ABS min/max values. */
};

struct goldfish_input_event
{
  uint32_t type;
  uint32_t code;
  int32_t  value;
};

struct goldfish_events_s
{
  FAR void                      *base;
  int                           irq;
  struct work_s                 work;           /* Supports the interrupt handling "bottom half" */
  struct mouse_lowerhalf_s      mouselower;     /* Mouse device lowerhalf instance */
  struct mouse_report_s         mousesample;    /* Mouse event */
  struct keyboard_lowerhalf_s   keyboardlower;  /* Keyboard device lowerhalf instance */
  struct touch_lowerhalf_s      touchlower;     /* Touchpad device lowerhalf instance */
  int                           currentslot;    /* Multi touch point number */
  bool                          validevent;     /* Touch point valid */
  struct touch_sample_s         touchsample[0]; /* Touchpad event */
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int
goldfish_events_interrupt(int irq, FAR void *dev_id, FAR void *arg);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: goldfish_events_send_keyboard_event
 ****************************************************************************/

static bool
goldfish_events_send_keyboard_event(FAR struct goldfish_events_s *events,
                                    FAR struct goldfish_input_event *evt)
{
  if (events->keyboardlower.priv == NULL)
    {
      return false;
    }

  if (evt->type == EV_KEY)
    {
      keyboard_event(&(events->keyboardlower),
                     keyboard_translate_virtio_code(evt->code),
                     !evt->value);
      return true;
    }

  return false;
}

/****************************************************************************
 * Name: goldfish_events_send_mouse_event
 ****************************************************************************/

static bool
goldfish_events_send_mouse_event(FAR struct goldfish_events_s *events,
                                 FAR struct goldfish_input_event *evt)
{
  if (events->mouselower.priv == NULL)
    {
      return false;
    }

  if (evt->type == EV_REL)
    {
      switch (evt->code)
        {
          case REL_X:
            events->mousesample.x = evt->value;
            return true;

          case REL_Y:
            events->mousesample.y = evt->value;
            return true;

        #ifdef CONFIG_INPUT_MOUSE_WHEEL
          case REL_WHEEL:
            events->mousesample.wheel = evt->value;
            return true;
        #endif
        }
    }
  else if (evt->type == EV_KEY && evt->value != 0)
    {
      switch (evt->code)
        {
          case BTN_LEFT:
            events->mousesample.buttons |= MOUSE_BUTTON_1;
            return true;

          case BTN_RIGHT:
            events->mousesample.buttons |= MOUSE_BUTTON_2;
            return true;

          case BTN_MIDDLE:
            events->mousesample.buttons |= MOUSE_BUTTON_3;
            return true;
        }
    }

  else if (evt->type == EV_SYN && evt->code == SYN_REPORT &&
           ((events->mousesample.x != 0) ||
           (events->mousesample.y != 0)
#ifdef CONFIG_INPUT_MOUSE_WHEEL
           || (events->mousesample.wheel != 0)
#endif
            ))
    {
      mouse_event(events->mouselower.priv, &events->mousesample);
      memset(&events->mousesample, 0, sizeof(events->mousesample));
    }

  return false;
}

/****************************************************************************
 * Name: goldfish_events_send_touch_event
 ****************************************************************************/

static bool
goldfish_events_send_touch_event(FAR struct goldfish_events_s *events,
                                 FAR struct goldfish_input_event *evt)
{
  FAR struct touch_sample_s *touchsample = &events->touchsample[0];

  if (events->touchlower.priv == NULL)
    {
      return false;
    }

  if (evt->type == EV_ABS)
    {
      /* Single touch event sequence example:
       * events(EV_ABS, ABS_X,      952)
       * events(EV_ABS, ABS_Y,      473)
       * events(EV_ABS, ABS_Z,      0)
       * events(EV_KEY, BTN_TOUCH,  TOUCH_DOWN)
       * events(EV_SYN, SYN_REPORT, 0)
       * events(EV_SYN, SYN_REPORT, 0)
       *
       * events(EV_ABS, ABS_X,      957)
       * events(EV_ABS, ABS_Y,      462)
       * events(EV_ABS, ABS_Z,      0)
       * events(EV_KEY, BTN_TOUCH,  TOUCH_UP)
       * events(EV_SYN, SYN_REPORT, 0)
       * events(EV_SYN, SYN_REPORT, 0)
       *
       * validevent flag is used to:
       * Filter consecutive invalid SYN events
       * */

      switch (evt->code)
        {
          /* Single touch events */

          case ABS_PRESSURE:
            touchsample->point[0].flags |= TOUCH_PRESSURE_VALID;
            touchsample->point[0].pressure = evt->value;
            break;

          case ABS_X:
            touchsample->point[0].flags = TOUCH_POS_VALID;
            touchsample->point[0].x = evt->value;
            break;

          case ABS_Y:
            touchsample->point[0].flags = TOUCH_POS_VALID;
            touchsample->point[0].y = evt->value;
            break;

          /* Multi-touch event sequence example:
           *
           * // one touch down event sequence:
           * events(EV_ABS, ABS_MT_SLOT,        0)
           * events(EV_ABS, ABS_MT_TRACKING_ID, 0)
           * events(EV_ABS, ABS_MT_POSITION_X,  508)
           * events(EV_ABS, ABS_MT_POSITION_Y,  475)
           * events(EV_ABS, ABS_MT_TOOL_TYPE,   0)
           * events(EV_ABS, ABS_MT_PRESSURE,    1024)
           * events(EV_ABS, ABS_MT_ORIENTATION, 0)
           * events(EV_ABS, ABS_MT_TOUCH_MAJOR, 1280)
           * events(EV_ABS, ABS_MT_TOUCH_MINOR, 1280)
           *
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           *
           * // one touch move event sequence:
           * events(EV_ABS, ABS_MT_POSITION_X,  888)
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           *
           * events(EV_ABS, ABS_MT_POSITION_Y,  268)
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           *
           * // one touch up event sequence:
           * events(EV_ABS, ABS_MT_PRESSURE,    0)
           * events(EV_ABS, ABS_MT_TRACKING_ID, -1)
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           *
           * // two touch down event sequence:
           * events(EV_ABS, ABS_MT_SLOT,        0)
           * events(EV_ABS, ABS_MT_TRACKING_ID, 0)
           * events(EV_ABS, ABS_MT_POSITION_X,  508)
           * events(EV_ABS, ABS_MT_POSITION_Y,  475)
           * events(EV_ABS, ABS_MT_TOOL_TYPE,   0)
           * events(EV_ABS, ABS_MT_PRESSURE,    1024)
           * events(EV_ABS, ABS_MT_ORIENTATION, 0)
           * events(EV_ABS, ABS_MT_TOUCH_MAJOR, 1280)
           * events(EV_ABS, ABS_MT_TOUCH_MINOR, 1280)
           *
           * events(EV_ABS, ABS_MT_SLOT,        1)
           * events(EV_ABS, ABS_MT_TRACKING_ID, 1)
           * events(EV_ABS, ABS_MT_POSITION_X,  829)
           * events(EV_ABS, ABS_MT_POSITION_Y,  265)
           * events(EV_ABS, ABS_MT_TOOL_TYPE,   0)
           * events(EV_ABS, ABS_MT_PRESSURE,    1024)
           * events(EV_ABS, ABS_MT_ORIENTATION, 0)
           * events(EV_ABS, ABS_MT_TOUCH_MAJOR, 1280)
           * events(EV_ABS, ABS_MT_TOUCH_MINOR, 1280)
           *
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           *
           * // two touch move event sequence:
           * events(EV_ABS, ABS_MT_SLOT,        0)
           * events(EV_ABS, ABS_MT_POSITION_X,  888)
           * events(EV_ABS, ABS_MT_SLOT,        1)
           * events(EV_ABS, ABS_MT_POSITION_Y,  268)
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           *
           * // two touch up event sequence:
           * events(EV_ABS, ABS_MT_SLOT,        0)
           * events(EV_ABS, ABS_MT_PRESSURE,    0)
           * events(EV_ABS, ABS_MT_TRACKING_ID, -1)
           *
           * events(EV_ABS, ABS_MT_SLOT,        1)
           * events(EV_ABS, ABS_MT_PRESSURE,    0)
           * events(EV_ABS, ABS_MT_TRACKING_ID, -1)
           *
           * events(EV_SYN, SYN_REPORT, 0)
           * events(EV_SYN, SYN_REPORT, 0)
           * */

          case ABS_MT_SLOT:
            events->currentslot = evt->value;
            break;

          case ABS_MT_TRACKING_ID:
            touchsample->point[events->currentslot].id = events->currentslot;
            if (evt->value >= 0)
              {
                touchsample->point[events->currentslot].flags = TOUCH_DOWN;
              }
            else
              {
                touchsample->point[events->currentslot].flags = TOUCH_UP;
                touchsample->point[events->currentslot].x = 0;
                touchsample->point[events->currentslot].y = 0;
                touchsample->point[events->currentslot].pressure = 0;
              }

            break;

          case ABS_MT_POSITION_X:
            touchsample->point[events->currentslot].flags |= TOUCH_POS_VALID;
            touchsample->point[events->currentslot].x = evt->value;
            break;

          case ABS_MT_POSITION_Y:
            touchsample->point[events->currentslot].flags |= TOUCH_POS_VALID;
            touchsample->point[events->currentslot].y = evt->value;
            break;

          case ABS_MT_PRESSURE:
            touchsample->point[events->currentslot].flags |=
              TOUCH_PRESSURE_VALID;
            touchsample->point[events->currentslot].pressure = evt->value;
              break;

          default:
              return false;
        }

      events->validevent = true;
      return true;
    }
  else if (evt->type == EV_KEY)
    {
      if (evt->code == BTN_TOUCH)
        {
          if (evt->value)
            {
              touchsample->point[0].flags |= TOUCH_DOWN;
            }
          else
            {
              touchsample->point[0].flags |= TOUCH_UP;
            }

          events->validevent = true;
          return true;
        }
    }
  else if (evt->type == EV_SYN && evt->code == SYN_REPORT &&
           events->validevent)
    {
      events->validevent = false;
      touchsample->point[events->currentslot].timestamp = touch_get_time();

      touch_event(events->touchlower.priv, touchsample);
    }

  return false;
}

/****************************************************************************
 * Name: goldfish_events_worker
 ****************************************************************************/

static void goldfish_events_worker(FAR void *arg)
{
  FAR struct goldfish_events_s *events = (FAR struct goldfish_events_s *)arg;
  struct goldfish_input_event evt;

  up_enable_irq(events->irq);

  evt.type = getreg32(events->base + GOLDFISH_EVENTS_READ);
  putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
           events->base + GOLDFISH_EVENTS_SET_PAGE);
  evt.code = getreg32(events->base + GOLDFISH_EVENTS_READ);
  putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
           events->base + GOLDFISH_EVENTS_SET_PAGE);
  evt.value = getreg32(events->base + GOLDFISH_EVENTS_READ);
  putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
           events->base + GOLDFISH_EVENTS_SET_PAGE);

  iinfo("goldfish_events_interrupt events(%" PRIu32 ", %" PRIu32 ", \
        %" PRIu32 ").\n", evt.type, evt.code, evt.value);

  if (goldfish_events_send_touch_event(events, &evt))
    {
      return;
    }

  if (goldfish_events_send_mouse_event(events, &evt))
    {
      return;
    }

  goldfish_events_send_keyboard_event(events, &evt);
}

/****************************************************************************
 * Name: goldfish_events_interrupt
 ****************************************************************************/

static int
goldfish_events_interrupt(int irq, FAR void *dev_id, FAR void *arg)
{
  FAR struct goldfish_events_s *events = arg;

  work_queue(HPWORK, &events->work, goldfish_events_worker, events, 0);
  up_disable_irq(events->irq);

  return 0;
}

/****************************************************************************
 * Name: goldfish_drivers_register
 ****************************************************************************/

static void goldfish_drivers_register(FAR struct goldfish_events_s *events)
{
  putreg32(GOLDFISH_EVENTS_PAGE_EVBITS | EV_KEY,
           events->base + GOLDFISH_EVENTS_SET_PAGE);
  if (getreg32(events->base + GOLDFISH_EVENTS_LEN))
    {
      putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
               events->base + GOLDFISH_EVENTS_SET_PAGE);
      if (getreg32(events->base + GOLDFISH_EVENTS_LEN))
        {
          keyboard_register(&(events->keyboardlower),
                            "/dev/kbd0",
                            CONFIG_INPUT_GOLDFISH_NBUFFER);
        }
    }

  putreg32(GOLDFISH_EVENTS_PAGE_EVBITS | EV_REL,
           events->base + GOLDFISH_EVENTS_SET_PAGE);
  if (getreg32(events->base + GOLDFISH_EVENTS_LEN))
    {
      putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
               events->base + GOLDFISH_EVENTS_SET_PAGE);
      if (getreg32(events->base + GOLDFISH_EVENTS_LEN))
        {
          mouse_register(&(events->mouselower),
                         "/dev/mouse0",
                         CONFIG_INPUT_GOLDFISH_NBUFFER);
        }
    }

  putreg32(GOLDFISH_EVENTS_PAGE_EVBITS | EV_ABS,
           events->base + GOLDFISH_EVENTS_SET_PAGE);
  if (getreg32(events->base + GOLDFISH_EVENTS_LEN))
    {
      putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
               events->base + GOLDFISH_EVENTS_SET_PAGE);
      if (getreg32(events->base + GOLDFISH_EVENTS_LEN))
        {
          touch_register(&(events->touchlower),
                         "/dev/input0",
                         CONFIG_INPUT_GOLDFISH_NBUFFER);
        }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: goldfish_events_register
 ****************************************************************************/

int goldfish_events_register(FAR void *base, int irq)
{
  FAR struct goldfish_events_s *events;
  uint32_t max_slot;
  int ret;

  /* [ABSEntry Structure]
   * Each EV_ABS event code corresponds to an ABSEntry containing four
   * 32-bit parameters:
   *   typedef struct ABSEntry {
   *       uint32_t min;    // Minimum value (offset +0)
   *       uint32_t max;    // Maximum value (offset +4)
   *       uint32_t fuzz;   // Input noise filter (offset +8)
   *       uint32_t flat;   // Contact size threshold (offset +12)
   *   } ABSEntry;          // Total size 16 bytes
   *
   * [Offset Calculation]
   * 1. abs_slot_offset = ABS_MT_SLOT << 4
   *    (Equivalent to ABS_MT_SLOT * sizeof(ABSEntry))
   * 2. max_slot_offset = abs_slot_offset + 4
   *    (Points to max field in ABSEntry)
   *
   * [Multi-touch Detection]
   * Verify through GOLDFISH_EVENTS_PAGE_ABSDATA page length:
   * - Multi-touch: abs_info_count = ABS_MAX * 4 (4 params per slot)
   * - Single-touch: abs_info_count = 3 * 4 (X/Y/Z axes)
   *
   * The code is as follows：
   * abs_info_count = isScreenMultiTouch(config) ? ABS_MAX * 4 : 3 * 4;
   *
   * get_page_len(events_state *s)
   * {
   * ...
   *    if (page == PAGE_ABSDATA)
   *      return abs_info_count * sizeof(int32_t);
   * ...
   * }
   * */

  putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA, base + GOLDFISH_EVENTS_SET_PAGE);

  if (getreg32(base + GOLDFISH_EVENTS_LEN) == 4 * ABS_MAX * sizeof(int32_t))
    {
      const int abs_slot_offset = ABS_MT_SLOT << 4;
      putreg32(GOLDFISH_EVENTS_PAGE_ABSDATA,
               base + GOLDFISH_EVENTS_SET_PAGE);
      max_slot = getreg32(base + GOLDFISH_EVENTS_DATA + abs_slot_offset + 4);
      iinfo("Multi-touch enabled, maxpoint:%" PRIu32 ".\n", max_slot);
    }
  else
    {
      max_slot = 1;
      iinfo("Single-touch mode.\n");
    }

  events = kmm_zalloc(sizeof(*events) + (SIZEOF_TOUCH_SAMPLE_S(max_slot)));

  if (events == NULL)
    {
      return -ENOMEM;
    }

  events->touchlower.maxpoint = max_slot;
  events->touchsample->npoints = events->touchlower.maxpoint;
  events->base = base;
  events->irq = irq;

  ret = irq_attach(events->irq, goldfish_events_interrupt, events);
  if (ret < 0)
    {
      goto err_free_events;
    }

  /* Register lower half drivers */

  goldfish_drivers_register(events);
  up_enable_irq(events->irq);

  return 0;

err_free_events:
  kmm_free(events);
  return ret;
}
