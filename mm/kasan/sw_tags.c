/****************************************************************************
 * mm/kasan/sw_tags.c
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

#include <nuttx/mm/kasan.h>
#include <nuttx/compiler.h>
#include <nuttx/spinlock.h>

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "kasan/kasan.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define KASAN_TAG_SHIFT 56

#define KASAN_SHADOW_SCALE (sizeof(uintptr_t))

#define KASAN_SHADOW_SIZE(size) \
  ((size) + KASAN_SHADOW_SCALE - 1) / KASAN_SHADOW_SCALE
#define KASAN_REGION_SIZE(size) \
  (sizeof(struct kasan_region_s) + KASAN_SHADOW_SIZE(size))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static FAR struct list_node g_region = LIST_INITIAL_VALUE(g_region);
static spinlock_t g_lock;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint8_t kasan_random_tag(void)
{
  /* Use a simple LCG that does not depend on TLS (rand() requires
   * per-task seed via tl_task which is NULL during early boot).
   */

  static unsigned long seed = 1;
  seed = seed * 1103515245 + 12345;
  return 1 + (seed >> 16) % ((1 << (64 - KASAN_TAG_SHIFT)) - 2);
}

static inline_function FAR uint8_t *
kasan_region_shadow(FAR struct kasan_region_s *region,
                    uintptr_t addr, size_t size)
{
  if (addr >= region->begin && addr < region->end)
    {
      DEBUGASSERT(addr + size <= region->end);
      addr -= region->begin;
      return &region->shadow[addr / KASAN_SHADOW_SCALE];
    }

  return NULL;
}

static inline_function FAR uint8_t *
kasan_mem_to_shadow(FAR const void *ptr, size_t size)
{
  struct kasan_region_s *r;
  FAR uint8_t *p = NULL;
  uintptr_t addr;
  irqstate_t flags;

  addr =  (uintptr_t)kasan_clear_tag(ptr);

  flags = spin_lock_irqsave_notrace(&g_lock);
  list_for_every_entry(&g_region, r, struct kasan_region_s, node)
    {
      p = kasan_region_shadow(r, addr, size);
      if (p)
        {
          break;
        }
    }

  spin_unlock_irqrestore_notrace(&g_lock, flags);
  return p;
}

static inline_function FAR uint8_t *
kasan_mem_to_shadow2(FAR struct kasan_region_s *region[],
                    FAR const void *ptr, size_t size)
{
  FAR uint8_t *p = NULL;
  uintptr_t addr;
  int i;

  addr = (uintptr_t)kasan_clear_tag(ptr);

  for (i = 0; i < CONFIG_MM_REGIONS && region[i]; i++)
    {
      p = kasan_region_shadow(region[i], addr, size);
      if (p)
        {
          break;
        }
    }

  return p;
}

static inline_function bool
kasan_is_poisoned(FAR const void *addr, size_t size)
{
  FAR uint8_t *p;
  uint8_t tag;

  tag = kasan_get_tag(addr);

#ifdef CONFIG_MM_KASAN_SKIP_ZERO_TAGS
  if (tag == 0)
    {
      return false;
    }
#endif

  p = kasan_mem_to_shadow(addr, size);
  if (p == NULL)
    {
      return kasan_global_is_poisoned(addr, size);
    }

  size = KASAN_SHADOW_SIZE(size);
  while (size--)
    {
      if (p[size] != tag)
        {
          return true;
        }
    }

  return false;
}

static void kasan_do_poison(FAR uint8_t *p, size_t size, uint8_t value)
{
  irqstate_t flags;

  size = KASAN_SHADOW_SIZE(size);
  flags = spin_lock_irqsave(&g_lock);

  while (size--)
    {
      p[size] = value;
    }

  spin_unlock_irqrestore(&g_lock, flags);
}

static void kasan_set_poison(FAR const void *addr,
                             size_t size, uint8_t value)
{
  FAR uint8_t *p;

  p = kasan_mem_to_shadow(addr, size);
  if (p)
    {
      kasan_do_poison(p, size, value);
    }
}

static void kasan_set_poison2(FAR struct kasan_region_s *region[],
                              FAR const void *addr,
                              size_t size, uint8_t value)
{
  FAR uint8_t *p;

  p = kasan_mem_to_shadow2(region, addr, size);
  if (p)
    {
      kasan_do_poison(p, size, value);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

FAR void *kasan_set_tag(FAR const void *addr, uint8_t tag)
{
  return (FAR void *)((uint64_t)kasan_clear_tag(addr) |
                      (((uint64_t)tag) << KASAN_TAG_SHIFT));
}

void kasan_poison(FAR const void *addr, size_t size)
{
  kasan_set_poison(addr, size, 0xff);
}

void kasan_poison2(FAR struct kasan_region_s *region[],
                   FAR const void *addr, size_t size)
{
  kasan_set_poison2(region, addr, size, 0xff);
}

FAR void *kasan_unpoison2(FAR struct kasan_region_s *region[],
                          FAR const void *addr, size_t size)
{
  uint8_t tag = kasan_random_tag();

  kasan_set_poison2(region, addr, size, tag);
  return kasan_set_tag(addr, tag);
}

FAR void *kasan_clear_tag(FAR const void *addr)
{
  return (FAR void *)
         (((uint64_t)addr) & ~((uint64_t)0xff << KASAN_TAG_SHIFT));
}

uint8_t kasan_get_tag(FAR const void *addr)
{
  return (uint8_t)((uint64_t)addr >> KASAN_TAG_SHIFT);
}

bool kasan_bypass(bool state)
{
  return false;
}

FAR void *kasan_unpoison(FAR const void *addr, size_t size)
{
  uint8_t tag = kasan_random_tag();

  kasan_set_poison(addr, size, tag);
  return kasan_set_tag(addr, tag);
}

FAR void *kasan_register(FAR void *addr, FAR size_t *size)
{
  FAR struct kasan_region_s *region;
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + *size;
  irqstate_t flags = spin_lock_irqsave(&g_lock);

  list_for_every_entry(&g_region, region, struct kasan_region_s, node)
    {
      if (start >= region->begin && end <= region->end)
        {
          /* Already registered */

          spin_unlock_irqrestore(&g_lock, flags);
          kasan_poison(addr, *size);
          return region;
        }
    }

  region = (FAR struct kasan_region_s *)
    ((FAR char *)addr + *size - KASAN_REGION_SIZE(*size));

  region->begin = start;
  region->end   = region->begin + *size;

  list_add_tail(&g_region, &region->node);
  spin_unlock_irqrestore(&g_lock, flags);

  kasan_start();
  kasan_poison(addr, *size);
  *size -= KASAN_REGION_SIZE(*size);
  return region;
}

void kasan_unregister(FAR void *addr, size_t size)
{
  struct kasan_region_s *region;
  irqstate_t flags;

  flags = spin_lock_irqsave(&g_lock);
  list_for_every_entry(&g_region, region, struct kasan_region_s, node)
    {
      if (region->begin == (uintptr_t)addr)
        {
          list_delete(&region->node);
          spin_unlock_irqrestore(&g_lock, flags);
          return;
        }
    }

  spin_unlock_irqrestore(&g_lock, flags);
  kasan_unpoison(addr, size);
}
