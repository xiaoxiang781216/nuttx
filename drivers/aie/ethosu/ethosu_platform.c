/****************************************************************************
 * drivers/aie/ethosu/ethosu_platform.c
 * Platform adaptation layer for ethos-u-core-driver
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
 * Platform adaptation layer for ethos-u-core-driver
 *
 * This file implements the weak functions required by the external
 * ethos-u-core-driver library to adapt it to the NuttX environment.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <debug.h>

#include <nuttx/semaphore.h>
#include <nuttx/kmalloc.h>
#include <nuttx/clock.h>
#include <nuttx/mutex.h>
#include <nuttx/arch.h>

/* Include external ethos-u-core-driver headers */

#include <ethosu_driver.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Cache Management Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ethosu_flush_dcache
 *
 * Description:
 *   Flush/clean the data cache for the specified memory regions.
 *   This ensures that any cached data is written back to memory before
 *   the NPU accesses it.
 *
 * Input Parameters:
 *   base_addr      - Array of base addresses for memory regions to flush.
 *                    Can be NULL to flush entire cache.
 *   base_addr_size - Array of sizes for corresponding memory regions.
 *                    Must not be NULL if base_addr is not NULL.
 *   num_base_addr  - Number of memory regions to flush. If 0, flushes
 *                    entire cache.
 *
 * Returned Value:
 *   None
 *
 * Assumptions/Limitations:
 *   - Memory regions are automatically aligned to cache line boundaries
 *   - If base_addr is NULL, the entire cache is flushed
 *
 ****************************************************************************/

void ethosu_flush_dcache(FAR const uint64_t *base_addr,
                         FAR const size_t *base_addr_size, int num_base_addr)
{
  uintptr_t start;
  uintptr_t end;
  int i;

  if (base_addr == NULL || num_base_addr == 0)
    {
      /* Flush entire cache if no specific regions provided */

      up_clean_dcache_all();
    }
  else
    {
      /* Validate base_addr_size parameter */

      if (base_addr_size == NULL)
        {
          _err("ERROR: base_addr_size is NULL but base_addr is not NULL\n");
          return;
        }

      /* Flush each specified memory region */

      for (i = 0; i < num_base_addr; i++)
        {
          /* Validate individual parameters */

          if (base_addr_size[i] == 0)
            {
              continue; /* Skip zero-size regions */
            }

          start = (uintptr_t)base_addr[i];
          end = start + base_addr_size[i];

          /* Check for address overflow */

          if (end < start)
            {
              _err("ERROR: Address overflow in cache flush region %d\n", i);
              continue;
            }

          up_clean_dcache(start, end);
        }
    }
}

/****************************************************************************
 * Name: ethosu_invalidate_dcache
 *
 * Description:
 *   Invalidate the data cache for the specified memory regions.
 *   This ensures that any cached data is discarded and fresh data
 *   is read from memory after NPU processing.
 *
 * Input Parameters:
 *   base_addr      - Array of base addresses for memory regions to
 *                    invalidate. Can be NULL to invalidate entire cache.
 *   base_addr_size - Array of sizes for corresponding memory regions.
 *                    Must not be NULL if base_addr is not NULL.
 *   num_base_addr  - Number of memory regions to invalidate. If 0,
 *                    invalidates entire cache.
 *
 * Returned Value:
 *   None
 *
 * Assumptions/Limitations:
 *   - Memory regions are automatically aligned to cache line boundaries
 *   - If base_addr is NULL, the entire cache is invalidated
 *
 ****************************************************************************/

void ethosu_invalidate_dcache(FAR const uint64_t *base_addr,
                               FAR const size_t *base_addr_size,
                               int num_base_addr)
{
  uintptr_t start;
  uintptr_t end;
  int i;

  if (base_addr == NULL || num_base_addr == 0)
    {
      /* Invalidate entire cache if no specific regions provided */

      up_invalidate_dcache_all();
    }
  else
    {
      /* Validate base_addr_size parameter */

      if (base_addr_size == NULL)
        {
          _err("ERROR: base_addr_size is NULL but base_addr is not NULL\n");
          return;
        }

      /* Invalidate each specified memory region */

      for (i = 0; i < num_base_addr; i++)
        {
          /* Validate individual parameters */

          if (base_addr_size[i] == 0)
            {
              continue; /* Skip zero-size regions */
            }

          start = (uintptr_t)base_addr[i];
          end = start + base_addr_size[i];

          /* Check for address overflow */

          if (end < start)
            {
              _err("ERROR: Address overflow in cache invalidate region %d\n",
                   i);
              continue;
            }

          up_invalidate_dcache(start, end);
        }
    }
}

/****************************************************************************
 * Mutex Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ethosu_mutex_create
 *
 * Description:
 *   Create and initialize a mutex object for synchronization. This function
 *   allocates memory for a new mutex and initializes it using NuttX mutex
 *   APIs. The created mutex can be used to protect critical sections in
 *   the Ethos-U driver.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   On success, returns a pointer to the newly created mutex object.
 *   Returns NULL if:
 *   - Memory allocation fails
 *   - Mutex initialization fails
 *
 * Assumptions:
 *   - The returned mutex must be destroyed using ethosu_mutex_destroy()
 *   - Should be called from thread context (not from interrupt context)
 *   - Memory is allocated from kernel heap using kmm_malloc()
 *
 ****************************************************************************/

FAR void *ethosu_mutex_create(void)
{
  FAR mutex_t *mutex;
  int ret;

  mutex = kmm_malloc(sizeof(mutex_t));
  if (mutex != NULL)
    {
      ret = nxmutex_init(mutex);
      if (ret < 0)
        {
          _err("ERROR: Failed to initialize mutex: %d\n", ret);
          kmm_free(mutex);
          return NULL;
        }
    }

  return mutex;
}

/****************************************************************************
 * Name: ethosu_mutex_destroy
 *
 * Description:
 *   Destroy a mutex object and release associated resources. This function
 *   properly deinitializes the mutex using NuttX APIs and frees the
 *   allocated memory.
 *
 * Input Parameters:
 *   mutex - Pointer to the mutex object to destroy. Can be NULL, in which
 *           case the function returns without performing any operation.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   - The mutex should not be locked when this function is called
 *   - The mutex pointer becomes invalid after this call
 *   - Must not be called from interrupt context
 *     (uses nxmutex_destroy/kmm_free)
 *   - Safe to call with NULL pointer
 *
 ****************************************************************************/

void ethosu_mutex_destroy(FAR void *mutex)
{
  if (mutex != NULL)
    {
      nxmutex_destroy((FAR mutex_t *)mutex);
      kmm_free(mutex);
    }
}

/****************************************************************************
 * Name: ethosu_mutex_lock
 *
 * Description:
 *   Acquire a mutex lock. This function blocks the calling thread until
 *   the mutex becomes available. Once acquired, the mutex provides
 *   exclusive access to the protected resource.
 *
 * Input Parameters:
 *   mutex - Pointer to the mutex object to lock. Must not be NULL.
 *
 * Returned Value:
 *   On success, returns 0.
 *   On failure, returns a negated errno value:
 *   -EINVAL - Invalid mutex pointer (NULL)
 *   Other negative values may be returned by nxmutex_lock()
 *
 * Assumptions:
 *   - Function may block the calling thread
 *   - Mutex must be unlocked using ethosu_mutex_unlock()
 *   - Recursive locking behavior depends on mutex type
 *   - Should not be called from interrupt context
 *
 ****************************************************************************/

int ethosu_mutex_lock(FAR void *mutex)
{
  if (mutex == NULL)
    {
      return -EINVAL;
    }

  return nxmutex_lock((FAR mutex_t *)mutex);
}

/****************************************************************************
 * Name: ethosu_mutex_unlock
 *
 * Description:
 *   Release a previously acquired mutex lock. This function makes the
 *   mutex available for other threads to acquire. If other threads are
 *   waiting for this mutex, one of them will be awakened.
 *
 * Input Parameters:
 *   mutex - Pointer to the mutex object to unlock. Must not be NULL and
 *           must be currently locked by the calling thread.
 *
 * Returned Value:
 *   On success, returns 0.
 *   On failure, returns a negated errno value:
 *   -EINVAL - Invalid mutex pointer (NULL)
 *   Other negative values may be returned by nxmutex_unlock()
 *
 * Assumptions:
 *   - Mutex must have been previously locked by the calling thread
 *   - Function should not be called from interrupt context
 *   - Calling thread must own the mutex lock
 *   - Function does not block
 *
 ****************************************************************************/

int ethosu_mutex_unlock(FAR void *mutex)
{
  if (mutex == NULL)
    {
      return -EINVAL;
    }

  return nxmutex_unlock((FAR mutex_t *)mutex);
}

/****************************************************************************
 * Semaphore Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ethosu_semaphore_create
 *
 * Description:
 *   Create and initialize a semaphore object for thread synchronization.
 *   This function allocates memory for a new semaphore and initializes
 *   it with an initial count of 0, making it suitable for signaling
 *   between threads.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   On success, returns a pointer to the newly created semaphore object.
 *   Returns NULL if:
 *   - Memory allocation fails
 *   - Semaphore initialization fails
 *
 * Assumptions:
 *   - The returned semaphore must be destroyed using
 *     ethosu_semaphore_destroy()
 *   - Semaphore is initialized with count 0 (signaling semaphore)
 *   - Should be called from thread context (not from interrupt context)
 *   - Memory is allocated from kernel heap using kmm_malloc()
 *
 ****************************************************************************/

void *ethosu_semaphore_create(void)
{
  FAR sem_t *sem = kmm_malloc(sizeof(sem_t));
  if (sem != NULL)
    {
      if (nxsem_init(sem, 0, 0) < 0)
        {
          kmm_free(sem);
          _err("ERROR: Failed to initialize semaphore\n");
          return NULL;
        }
    }

  return sem;
}

/****************************************************************************
 * Name: ethosu_semaphore_destroy
 *
 * Description:
 *   Destroy a semaphore object and release associated resources. This
 *   function properly deinitializes the semaphore using POSIX APIs and
 *   frees the allocated memory.
 *
 * Input Parameters:
 *   sem - Pointer to the semaphore object to destroy. Can be NULL, in which
 *         case the function returns without performing any operation.
 *
 * Returned Value:
 *   None
 *
 * Assumptions:
 *   - No threads should be waiting on the semaphore when destroyed
 *   - The semaphore pointer becomes invalid after this call
 *   - Must not be called from interrupt context
 *     (uses nxsem_destroy/kmm_free)
 *   - Safe to call with NULL pointer
 *
 ****************************************************************************/

void ethosu_semaphore_destroy(FAR void *sem)
{
  if (sem != NULL)
    {
      nxsem_destroy((FAR sem_t *)sem);
      kmm_free(sem);
    }
}

/****************************************************************************
 * Name: ethosu_semaphore_take
 *
 * Description:
 *   Wait for a semaphore to become available (take/acquire operation).
 *   This function blocks the calling thread until either the semaphore
 *   becomes available or the specified timeout expires. If the timeout
 *   is ETHOSU_SEMAPHORE_WAIT_FOREVER, the function will wait indefinitely.
 *
 * Input Parameters:
 *   sem     - Pointer to the semaphore object. Must not be NULL.
 *   timeout - Timeout value in nanoseconds. Use
 *             ETHOSU_SEMAPHORE_WAIT_FOREVER for infinite timeout.
 *
 * Returned Value:
 *   On success, returns 0.
 *   On failure, returns a negated errno value:
 *   -EINVAL   - Invalid semaphore pointer (NULL)
 *   -ETIMEDOUT - Timeout expired
 *   Other negative errno values from sem_timedwait()
 *
 * Assumptions:
 *   - Function may block the calling thread
 *   - Timeout is specified in nanoseconds
 *   - Function should not be called from interrupt context for blocking
 *     calls
 *   - Uses CLOCK_MONOTONIC for timeout calculations
 *
 ****************************************************************************/

int ethosu_semaphore_take(FAR void *sem, uint64_t timeout)
{
  if (sem == NULL)
    {
      return -EINVAL;
    }

  if (timeout == ETHOSU_SEMAPHORE_WAIT_FOREVER)
    {
      return nxsem_wait((FAR sem_t *)sem);
    }
  else
    {
      /* Convert nanoseconds to milliseconds then to ticks */

      uint32_t msec = timeout / NSEC_PER_MSEC;
      clock_t ticks = MSEC2TICK(msec);
      return nxsem_tickwait((FAR sem_t *)sem, ticks);
    }
}

/****************************************************************************
 * Name: ethosu_semaphore_give
 *
 * Description:
 *   Signal a semaphore (give/post operation). This function increments
 *   the semaphore count and wakes up one waiting thread if any are blocked
 *   on the semaphore. This is typically used to signal completion of an
 *   operation or availability of a resource.
 *
 * Input Parameters:
 *   sem - Pointer to the semaphore object. Must not be NULL.
 *
 * Returned Value:
 *   On success, returns 0.
 *   On failure, returns a negated errno value:
 *   -EINVAL - Invalid semaphore pointer (NULL)
 *   Other negative errno values from sem_post()
 *
 * Assumptions:
 *   - Function does not block
 *   - Wakes up one waiting thread (if any)
 *   - Increments semaphore count atomically
 *
 ****************************************************************************/

int ethosu_semaphore_give(FAR void *sem)
{
  if (sem == NULL)
    {
      return -EINVAL;
    }

  return nxsem_post((FAR sem_t *)sem);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/
