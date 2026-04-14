/****************************************************************************
 * libs/libc/tls/tls_emutls.c
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
 * Override GCC's __emutls_get_address for bare-metal toolchains compiled
 * with --disable-tls.  The default libgcc implementation uses a single
 * global pointer (no per-thread isolation).
 *
 * This version uses a cascaded task_tls + pthread_key approach:
 *   - task_tls index stored in obj->loc.offset (system-level, permanent)
 *   - task_tls value stores pthread_key+1 per task group (0 = not created)
 *   - pthread_getspecific/pthread_setspecific for per-thread data
 *
 * This avoids the problem of pthread keys being per-task-group while
 * __emutls_object is global.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <nuttx/lib/lib.h>
#include <nuttx/tls_task.h>

#include <assert.h>
#include <pthread.h>
#include <string.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

typedef unsigned int word __attribute__((mode(word)));

struct __emutls_object
{
  word size;
  word align;
  union
  {
    uintptr_t offset;  /* task_tls index + 1 (0 = not allocated) */
    FAR void *ptr;
  } loc;
  FAR void *templ;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static pthread_mutex_t g_emutls_lock = PTHREAD_MUTEX_INITIALIZER;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void emutls_destroy(FAR void *ptr)
{
  lib_free(ptr);
}

static FAR void *emutls_alloc(FAR struct __emutls_object *obj)
{
  FAR void *ret;

  if (obj->align > sizeof(FAR void *))
    {
      ret = lib_memalign(obj->align, obj->size);
    }
  else
    {
      ret = lib_malloc(obj->size);
    }

  if (ret != NULL)
    {
      if (obj->templ)
        {
          memcpy(ret, obj->templ, obj->size);
        }
      else
        {
          memset(ret, 0, obj->size);
        }
    }

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: __wrap___emutls_get_address
 *
 * Description:
 *   Return a per-thread pointer for the TLS variable described by obj.
 *   Overrides libgcc's single-threaded __emutls_get_address via --wrap
 *   linker option. Uses cascaded task_tls + pthread_key for per-thread
 *   isolation across task groups.
 *
 * Input Parameters:
 *   arg - Pointer to the __emutls_object descriptor (compiler-generated)
 *
 * Returned Value:
 *   Pointer to the per-thread TLS variable storage. Calls PANIC() on
 *   allocation failure since the compiler-generated caller cannot handle
 *   NULL.
 *
 ****************************************************************************/

FAR void *__wrap___emutls_get_address(FAR void *arg)
{
  FAR struct __emutls_object *obj = (FAR struct __emutls_object *)arg;
  pthread_key_t key;
  FAR void *ret;

  /* Ensure this object has a task_tls index allocated */

  if (obj->loc.offset == 0)
    {
      pthread_mutex_lock(&g_emutls_lock);
      if (obj->loc.offset == 0)
        {
          int idx = task_tls_alloc(NULL);
          if (idx < 0)
            {
              pthread_mutex_unlock(&g_emutls_lock);
              PANIC();
            }

          obj->loc.offset = idx + 1;
        }

      pthread_mutex_unlock(&g_emutls_lock);
    }

  /* Ensure this task group has a pthread key */

  key = task_tls_get_value(obj->loc.offset - 1) - 1;
  if (key < 0)
    {
      pthread_mutex_lock(&g_emutls_lock);
      key = task_tls_get_value(obj->loc.offset - 1) - 1;
      if (key < 0)
        {
          if (pthread_key_create(&key, emutls_destroy) != 0)
            {
              pthread_mutex_unlock(&g_emutls_lock);
              PANIC();
            }

          task_tls_set_value(obj->loc.offset - 1, key + 1);
        }

      pthread_mutex_unlock(&g_emutls_lock);
    }

  /* Get or allocate per-thread storage */

  ret = pthread_getspecific(key);
  if (ret == NULL)
    {
      ret = emutls_alloc(obj);
      if (ret == NULL)
        {
          PANIC();
        }

      if (pthread_setspecific(key, ret) != 0)
        {
          lib_free(ret);
          PANIC();
        }
    }

  return ret;
}

/****************************************************************************
 * Name: __wrap___emutls_register_common
 *
 * Description:
 *   Register a TLS common symbol. Called by GCC during startup
 *   (.init_array, single-threaded context) for thread_local variables
 *   with common linkage. Updates the object's size, alignment, and
 *   initialization template using largest-wins semantics.
 *
 * Input Parameters:
 *   arg   - Pointer to the __emutls_object descriptor
 *   size  - Size of the TLS variable
 *   align - Alignment requirement of the TLS variable
 *   templ - Pointer to initialization template in .tdata (may be NULL)
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

void __wrap___emutls_register_common(FAR void *arg,
                                     word size,
                                     word align,
                                     FAR void *templ)
{
  FAR struct __emutls_object *obj = (FAR struct __emutls_object *)arg;

  if (obj->size < size)
    {
      obj->size = size;
      obj->templ = NULL;
    }

  if (obj->align < align)
    {
      obj->align = align;
    }

  if (templ && size == obj->size)
    {
      obj->templ = templ;
    }
}
