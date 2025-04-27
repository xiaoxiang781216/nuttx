/****************************************************************************
 * arch/xtensa/src/esp32s3/esp32s3_ioexpander.c
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

#include <nuttx/ioexpander/ioexpander.h>
#include <nuttx/nuttx.h>

#include "hardware/esp32s3_gpio_sigmap.h"
#include "esp32s3_gpio.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define ESP32S3_MAX_NPINS \
  (CONFIG_IOEXPANDER_NPINS < ESP32S3_NPINS ? CONFIG_IOEXPANDER_NPINS : ESP32S3_NPINS)

#define ESP32S3_OPT_INTCFG_SHIFT 0
#define ESP32S3_OPT_INVERT_SHIFT 4
#define ESP32S3_OPT_INVERT_MASK  (0xf << ESP32S3_OPT_INVERT_SHIFT)
#define ESP32S3_OPT_INTCFG_MASK  (0xf << ESP32S3_OPT_INTCFG_SHIFT)

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct esp32s3_ioexpander_pincfg_s
{
  uint8_t opt;
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  ioe_callback_t cb;
  FAR void *arg;
  ioe_pinset_t pinset;
#endif
};

struct esp32s3_ioexpander_dev_s
{
  struct ioexpander_dev_s dev;
  struct esp32s3_ioexpander_pincfg_s cfg[ESP32S3_MAX_NPINS];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int esp32s3_ioe_direction(FAR struct ioexpander_dev_s *dev,
                                 uint8_t pin, int direction);
static int esp32s3_ioe_option(FAR struct ioexpander_dev_s *dev,
                              uint8_t pin, int opt, FAR void *val);
static int esp32s3_ioe_writepin(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, bool value);
static int esp32s3_ioe_readpin(FAR struct ioexpander_dev_s *dev,
                               uint8_t pin, FAR bool *value);
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
FAR void *esp32s3_ioe_attach(FAR struct ioexpander_dev_s *dev,
                             ioe_pinset_t pinset, ioe_callback_t callback,
                             FAR void *arg);
static int esp32s3_ioe_detach(FAR struct ioexpander_dev_s *dev,
                              FAR void *handle);
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct esp32s3_ioexpander_dev_s *g_esp32s3_ioe = NULL;

static const struct ioexpander_ops_s g_esp32s3_ioe_ops =
{
  .ioe_direction = esp32s3_ioe_direction,
  .ioe_option = esp32s3_ioe_option,
  .ioe_writepin = esp32s3_ioe_writepin,
  .ioe_readpin = esp32s3_ioe_readpin,
  .ioe_readbuf = esp32s3_ioe_readpin,
#ifdef CONFIG_IOEXPANDER_INT_ENABLE
  .ioe_attach = esp32s3_ioe_attach,
  .ioe_detach = esp32s3_ioe_detach,
#endif
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp32s3_ioe_direction
 *
 * Description:
 *   Set the direction of an ioexpander pin. Required.
 *
 * Input Parameters:
 *   dev - Device-specific state data
 *   pin - The index of the pin to alter in this call
 *   dir - One of the IOEXPANDER_DIRECTION_ macros
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int esp32s3_ioe_direction(FAR struct ioexpander_dev_s *dev,
                                 uint8_t pin, int direction)
{
  FAR struct esp32s3_ioexpander_dev_s *priv =
    (FAR struct esp32s3_ioexpander_dev_s *)dev;
  gpio_pinattr_t attr = 0;

  DEBUGASSERT(priv != NULL && pin < ESP32S3_MAX_NPINS &&
              esp32s3_is_valid_gpio(pin));
  gpioinfo("pin=%u direction=%d\n", pin, direction);

  switch (direction)
    {
      case IOEXPANDER_DIRECTION_IN:
        attr |= INPUT;
        break;
      case IOEXPANDER_DIRECTION_IN_PULLUP:
        attr |= INPUT_PULLUP;
        break;
      case IOEXPANDER_DIRECTION_IN_PULLDOWN:
        attr |= INPUT_PULLDOWN;
        break;
      case IOEXPANDER_DIRECTION_OUT:
        attr |= OUTPUT;
        break;
      case IOEXPANDER_DIRECTION_OUT_OPENDRAIN:
        attr |= OUTPUT_OPEN_DRAIN;
        break;
      default:
        gpioerr("ERROR: Invalid direction: %d\n", direction);
        return -EINVAL;
    }

  if (direction >= IOEXPANDER_DIRECTION_OUT)
    {
      esp32s3_gpio_matrix_out(pin, SIG_GPIO_OUT_IDX, 0, 0);
    }

  esp32s3_configgpio(pin, attr);

  return 0;
}

/****************************************************************************
 * Name: esp32s3_ioe_option
 *
 * Description:
 *   Set pin options. Required.
 *   Since all IO expanders have various pin options, this API allows setting
 *     pin options in a flexible way.
 *
 * Input Parameters:
 *   dev - Device-specific state data
 *   pin - The index of the pin to alter in this call
 *   opt - One of the IOEXPANDER_OPTION_ macros
 *   val - The option's value
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int esp32s3_ioe_option(FAR struct ioexpander_dev_s *dev,
                              uint8_t pin, int opt, FAR void *val)
{
  FAR struct esp32s3_ioexpander_dev_s *priv =
    (FAR struct esp32s3_ioexpander_dev_s *)dev;
  gpio_intrtype_t intrtype;

  DEBUGASSERT(priv != NULL && pin < ESP32S3_MAX_NPINS &&
              esp32s3_is_valid_gpio(pin));
  gpioinfo("pin=%u opt=%d val=%d\n", pin, opt, (uintptr_t)val);

  if (opt != IOEXPANDER_OPTION_INVERT && opt != IOEXPANDER_OPTION_INTCFG)
    {
      gpioerr("ERROR: Invalid option: %d\n", opt);
      return -EINVAL;
    }

  if (opt == IOEXPANDER_OPTION_INVERT)
    {
      priv->cfg[pin].opt &= ~ESP32S3_OPT_INVERT_MASK;
      priv->cfg[pin].opt |= (uintptr_t)val << ESP32S3_OPT_INVERT_SHIFT;
      return 0;
    }

  priv->cfg[pin].opt &= ~ESP32S3_OPT_INTCFG_MASK;
  switch ((uintptr_t)val)
    {
      case IOEXPANDER_VAL_RISING:
        intrtype = RISING;
        break;
      case IOEXPANDER_VAL_FALLING:
        intrtype = FALLING;
        break;
      case IOEXPANDER_VAL_BOTH:
        intrtype = CHANGE;
        break;
      case IOEXPANDER_VAL_LOW:
        intrtype = ONLOW;
        break;
      case IOEXPANDER_VAL_HIGH:
        intrtype = ONHIGH;
        break;
      case IOEXPANDER_VAL_DISABLE:
        intrtype = DISABLED;
        break;
      default:
        gpioerr("ERROR: Invalid value: %d\n", (uintptr_t)val);
        return -EINVAL;
    }

  priv->cfg[pin].opt |= intrtype << ESP32S3_OPT_INTCFG_SHIFT;

  return 0;
}

/****************************************************************************
 * Name: esp32s3_ioe_writepin
 *
 * Description:
 *   Set the pin level. Required.
 *
 * Input Parameters:
 *   dev - Device-specific state data
 *   pin - The index of the pin to alter in this call
 *   val - The pin level. Usually TRUE will set the pin high,
 *         except if OPTION_INVERT has been set on this pin.
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int esp32s3_ioe_writepin(FAR struct ioexpander_dev_s *dev,
                                uint8_t pin, bool value)
{
  FAR struct esp32s3_ioexpander_dev_s *priv =
    (FAR struct esp32s3_ioexpander_dev_s *)dev;

  DEBUGASSERT(priv != NULL && pin < ESP32S3_MAX_NPINS &&
              esp32s3_is_valid_gpio(pin));
  gpioinfo("pin=%u,opt=0x%x,value=%u\n",
           pin, priv->cfg[pin].opt, (uint32_t)value);

  if ((priv->cfg[pin].opt & ESP32S3_OPT_INVERT_MASK) >>
      ESP32S3_OPT_INVERT_SHIFT)
    {
      esp32s3_gpiowrite(pin, !value);
    }
  else
    {
      esp32s3_gpiowrite(pin, value);
    }

  return 0;
}

/****************************************************************************
 * Name: esp32s3_ioe_readpin
 *
 * Description:
 *   Read the actual PIN level. This can be different from the last value
 *   written to this pin. Required.
 *
 * Input Parameters:
 *   dev    - Device-specific state data
 *   pin    - The index of the pin
 *   valptr - Pointer to a buffer where the pin level is stored. Usually TRUE
 *            if the pin is high, except if OPTION_INVERT has been set on
 *            this pin.
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int esp32s3_ioe_readpin(FAR struct ioexpander_dev_s *dev,
                               uint8_t pin, FAR bool *value)
{
  FAR struct esp32s3_ioexpander_dev_s *priv =
    (FAR struct esp32s3_ioexpander_dev_s *)dev;
  bool val;

  DEBUGASSERT(priv != NULL && pin < ESP32S3_MAX_NPINS &&
              esp32s3_is_valid_gpio(pin));
  gpioinfo("pin=%u,opt=0x%x\n", pin, priv->cfg[pin].opt);
  val = esp32s3_gpioread(pin);

  if ((priv->cfg[pin].opt & ESP32S3_OPT_INVERT_MASK) >>
      ESP32S3_OPT_INVERT_SHIFT)
    {
      *value = val;
    }
  else
    {
      *value = !val;
    }

  return 0;
}

#ifdef CONFIG_IOEXPANDER_INT_ENABLE

/****************************************************************************
 * Name: esp32s3_ioe_irq_handler
 *
 * Description:
 *   gpio pin irq handler entry .
 *
 * Input Parameters:
 *   irq     - The irq number of the interrupt
 *   context - The context pointer for the callback
 *   arg     - The argument for the callback
 *
 * Returned Value:
 *   Zero (OK) is returned on success; a negated errno value is returned
 *   on failure.
 *
 ****************************************************************************/

static int esp32s3_ioe_irq_handler(int irq, void *context, void *arg)
{
  FAR struct esp32s3_ioexpander_pincfg_s *cfg = arg;
  uint8_t pin = ESP32S3_IRQ2PIN(irq);
  FAR struct esp32s3_ioexpander_dev_s *priv =
    container_of(cfg - pin, struct esp32s3_ioexpander_dev_s, cfg);

  DEBUGASSERT(cfg != NULL && cfg->cb != NULL);
  gpioinfo("dev=%p,pin=%u,callback=%p\n", priv, pin, cfg->cb);

  cfg->cb(&priv->dev, pin, cfg->arg);
  return 0;
}

/****************************************************************************
 * Name: esp32s3_ioe_attach
 *
 * Description:
 *   Attach and enable a pin interrupt callback function.
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *   pinset   - The set of pin events that will generate the callback
 *   callback - The pointer to callback function.  NULL will detach the
 *              callback.
 *   arg      - User-provided callback argument
 *
 * Returned Value:
 *   A non-NULL handle value is returned on success.  This handle may be
 *   used later to detach and disable the pin interrupt.
 *
 ****************************************************************************/

FAR void *esp32s3_ioe_attach(FAR struct ioexpander_dev_s *dev,
                             ioe_pinset_t pinset, ioe_callback_t callback,
                             FAR void *arg)
{
  FAR struct esp32s3_ioexpander_dev_s *priv =
    (FAR struct esp32s3_ioexpander_dev_s *)dev;
  FAR void *handle = NULL;
  int irq;
  int ret;
  int i;

  DEBUGASSERT(priv != NULL && callback != NULL);
  gpioinfo("pin=0x%llx", (uint64_t)pinset);

  for (i = 0; i < ESP32S3_MAX_NPINS; i++)
    {
      if (!(pinset & (1 << i)))
        {
          continue;
        }

      priv->cfg[i].cb = callback;
      priv->cfg[i].arg = arg;
      priv->cfg[i].pinset = pinset;
      handle = &priv->cfg[pinset];
      irq = ESP32S3_PIN2IRQ(i);
      esp32s3_gpioirqdisable(irq);
      ret = irq_attach(irq, esp32s3_ioe_irq_handler, &priv->cfg[i]);
      if (ret < 0)
        {
          gpioerr("irq_attach failed: %d\n", ret);
          return handle;
        }

      esp32s3_gpioirqenable(irq,
                            priv->cfg[i].opt & ESP32S3_OPT_INTCFG_MASK);
    }

  return handle;
}

/****************************************************************************
 * Name: esp32s3_ioe_detach
 *
 * Description:
 *   Detach and disable a pin interrupt callback function.
 *
 * Input Parameters:
 *   dev      - Device-specific state data
 *   handle   - The non-NULL opaque value return by esp32s3_ioe_attach()
 *
 * Returned Value:
 *   0 on success, else a negative error code
 *
 ****************************************************************************/

static int esp32s3_ioe_detach(FAR struct ioexpander_dev_s *dev,
                              FAR void *handle)
{
  FAR struct esp32s3_ioexpander_dev_s *priv =
    (FAR struct esp32s3_ioexpander_dev_s *)dev;
  FAR struct esp32s3_ioexpander_pincfg_s *cfg = handle;
  ioe_pinset_t pinset = cfg->pinset;
  int i;

  DEBUGASSERT(priv != NULL && cfg != NULL);
  gpioinfo("handle=%p\n", handle);

  for (i = 0; i < ESP32S3_MAX_NPINS; i++)
    {
      if (pinset & (1 << i))
        {
          esp32s3_gpioirqdisable(ESP32S3_PIN2IRQ(i));
          priv->cfg[i].cb = NULL;
          priv->cfg[i].arg = NULL;
          priv->cfg[i].pinset = 0;
        }
    }

  return 0;
}

#endif

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: esp32s3_ioexpander_initialize
 *
 * Description:
 *   Instantiate and configure the I/O Expander device driver to use the
 *   provided gpio device instance.
 *
 * Input Parameters:
 *
 * Returned Value:
 *   an ioexpander_dev_s instance on success, NULL on failure.
 *
 ****************************************************************************/

FAR struct ioexpander_dev_s *esp32s3_ioexpander_initialize(void)
{
  if (g_esp32s3_ioe == NULL)
    {
      g_esp32s3_ioe = kmm_zalloc(sizeof(struct esp32s3_ioexpander_dev_s));
      if (g_esp32s3_ioe == NULL)
        {
          gpioerr("Failed to allocate memory for the I/O Expander ");
          return NULL;
        }

      g_esp32s3_ioe->dev.ops = &g_esp32s3_ioe_ops;
    }

  return &g_esp32s3_ioe->dev;
}
