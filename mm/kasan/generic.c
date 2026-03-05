/****************************************************************************
 * mm/kasan/generic.c
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

#include <nuttx/nuttx.h>
#include <nuttx/mm/kasan.h>
#include <nuttx/compiler.h>
#include <nuttx/spinlock.h>

#include <assert.h>
#include <stdint.h>

#include "kasan/kasan.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define KASAN_BYTES_PER_WORD (sizeof(uintptr_t))
#define KASAN_BITS_PER_WORD  (KASAN_BYTES_PER_WORD * 8)

#define KASAN_FIRST_WORD_MASK(start) \
  (UINTPTR_MAX << ((start) & (KASAN_BITS_PER_WORD - 1)))
#define KASAN_LAST_WORD_MASK(end) \
  (UINTPTR_MAX >> (-(end) & (KASAN_BITS_PER_WORD - 1)))

#define KASAN_SHADOW_SCALE (sizeof(uintptr_t))

#define KASAN_SHADOW_SIZE(size) \
  (KASAN_BYTES_PER_WORD * ((size) / KASAN_SHADOW_SCALE / KASAN_BITS_PER_WORD))
#define KASAN_REGION_SIZE(size) \
  (sizeof(struct kasan_region_s) + KASAN_SHADOW_SIZE(size))

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct list_node g_region = LIST_INITIAL_VALUE(g_region);
static spinlock_t g_lock;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline_function FAR uintptr_t *
kasan_region_shadow(FAR struct kasan_region_s *region,
                    uintptr_t addr, size_t size, FAR unsigned int *bit)
{
  if (addr >= region->begin && addr < region->end)
    {
      DEBUGASSERT(addr + size <= region->end);
      addr -= region->begin;
      addr /= KASAN_SHADOW_SCALE;
      *bit  = addr % KASAN_BITS_PER_WORD;
      return &region->shadow[addr / KASAN_BITS_PER_WORD];
    }

  return NULL;
}

static inline_function FAR uintptr_t *
kasan_mem_to_shadow(FAR const void *ptr, size_t size, FAR unsigned int *bit)
{
  FAR struct kasan_region_s *r;
  uintptr_t addr = (uintptr_t)ptr;
  FAR uintptr_t *p = NULL;
  irqstate_t flags;

  flags = spin_lock_irqsave_notrace(&g_lock);
  list_for_every_entry(&g_region, r, struct kasan_region_s, node)
    {
      p = kasan_region_shadow(r, addr, size, bit);
      if (p)
        {
          break;
        }
    }

  spin_unlock_irqrestore_notrace(&g_lock, flags);
  return p;
}

static inline_function FAR uintptr_t *
kasan_mem_to_shadow2(FAR struct kasan_region_s *region[],
                     FAR const void *ptr, size_t size, FAR unsigned int *bit)
{
  uintptr_t addr = (uintptr_t)ptr;
  FAR uintptr_t *p = NULL;
  int i;

  for (i = 0; i < CONFIG_MM_REGIONS && region[i]; i++)
    {
      p = kasan_region_shadow(region[i], addr, size, bit);
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
  FAR uintptr_t *p;
  unsigned int bit;
  unsigned int nbit;
  uintptr_t mask;

  p = kasan_mem_to_shadow(addr, size, &bit);
  if (p == NULL)
    {
      return kasan_global_is_poisoned(addr, size);
    }

  if (size <= KASAN_SHADOW_SCALE)
    {
      return ((*p >> bit) & 1);
    }

  nbit = KASAN_BITS_PER_WORD - bit % KASAN_BITS_PER_WORD;
  mask = KASAN_FIRST_WORD_MASK(bit);
  size = ALIGN_UP(size, KASAN_SHADOW_SCALE);
  size /= KASAN_SHADOW_SCALE;

  while (size >= nbit)
    {
      if ((*p++ & mask) != 0)
        {
          return true;
        }

      bit  += nbit;
      size -= nbit;

      nbit = KASAN_BITS_PER_WORD;
      mask = UINTPTR_MAX;
    }

  if (size)
    {
      mask &= KASAN_LAST_WORD_MASK(bit + size);
      if ((*p & mask) != 0)
        {
          return true;
        }
    }

  return false;
}

static void kasan_do_poison(FAR uintptr_t *p, size_t size, unsigned int bit,
                            bool poisoned)
{
  unsigned int nbit = KASAN_BITS_PER_WORD - bit % KASAN_BITS_PER_WORD;
  uintptr_t    mask = KASAN_FIRST_WORD_MASK(bit);
  irqstate_t flags;

  size /= KASAN_SHADOW_SCALE;

  flags = spin_lock_irqsave_notrace(&g_lock);
  while (size >= nbit)
    {
      if (poisoned)
        {
          *p++ |= mask;
        }
      else
        {
          *p++ &= ~mask;
        }

      bit  += nbit;
      size -= nbit;

      nbit = KASAN_BITS_PER_WORD;
      mask = UINTPTR_MAX;
    }

  if (size)
    {
      mask &= KASAN_LAST_WORD_MASK(bit + size);
      if (poisoned)
        {
          *p |= mask;
        }
      else
        {
          *p &= ~mask;
        }
    }

  spin_unlock_irqrestore_notrace(&g_lock, flags);
}

static void kasan_set_poison(FAR const void *addr, size_t size,
                             bool poisoned)
{
  FAR uintptr_t *p;
  unsigned int bit;

  p = kasan_mem_to_shadow(addr, size, &bit);
  if (p)
    {
      kasan_do_poison(p, size, bit, poisoned);
    }
}

static void kasan_set_poison2(FAR struct kasan_region_s *region[],
                              FAR const void *addr, size_t size,
                              bool poisoned)
{
  FAR uintptr_t *p;
  unsigned int bit;

  p = kasan_mem_to_shadow2(region, addr, size, &bit);
  if (p)
    {
      kasan_do_poison(p, size, bit, poisoned);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

uint8_t kasan_get_tag(FAR const void *addr)
{
  return 0;
}

FAR void *kasan_set_tag(FAR const void *addr, uint8_t tag)
{
  return (FAR void *)addr;
}

FAR void *kasan_clear_tag(FAR const void *addr)
{
  return (FAR void *)addr;
}

void kasan_poison2(FAR struct kasan_region_s *region[],
                   FAR const void *addr, size_t size)
{
  kasan_set_poison2(region, addr, size, true);
}

FAR void *kasan_unpoison2(FAR struct kasan_region_s *region[],
                          FAR const void *addr, size_t size)
{
  kasan_set_poison2(region, addr, size, false);
  return (FAR void *)addr;
}

void kasan_poison(FAR const void *addr, size_t size)
{
  kasan_set_poison(addr, size, true);
}

FAR void *kasan_unpoison(FAR const void *addr, size_t size)
{
  kasan_set_poison(addr, size, false);
  return (FAR void *)addr;
}

bool kasan_bypass(bool state)
{
  return false;
}

FAR void *kasan_register(FAR void *addr, FAR size_t *size)
{
  FAR struct kasan_region_s *region;
  uintptr_t start = (uintptr_t)addr;
  uintptr_t end = start + *size;
  irqstate_t flags = spin_lock_irqsave_notrace(&g_lock);

  list_for_every_entry(&g_region, region, struct kasan_region_s, node)
    {
      if (start >= region->begin && end <= region->end)
        {
          /* Already registered */

          spin_unlock_irqrestore_notrace(&g_lock, flags);
          kasan_poison(addr, *size);
          return region;
        }
    }

  region = (FAR struct kasan_region_s *)
    ((FAR char *)addr + *size - KASAN_REGION_SIZE(*size));

  region->begin = start;
  region->end   = region->begin + *size;

  list_add_tail(&g_region, &region->node);
  spin_unlock_irqrestore_notrace(&g_lock, flags);

  kasan_start();
  kasan_poison(addr, *size);
  *size -= KASAN_REGION_SIZE(*size);
  return region;
}

void kasan_unregister(FAR void *addr, size_t size)
{
  FAR struct kasan_region_s *region;
  irqstate_t flags;

  flags = spin_lock_irqsave_notrace(&g_lock);
  list_for_every_entry(&g_region, region, struct kasan_region_s, node)
    {
      if (region->begin == (uintptr_t)addr)
        {
          list_delete(&region->node);
          spin_unlock_irqrestore_notrace(&g_lock, flags);
          return;
        }
    }

  spin_unlock_irqrestore_notrace(&g_lock, flags);
  kasan_unpoison(addr, size);
}
