/****************************************************************************
 * libs/libc/misc/lib_mutex.c
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
#include <nuttx/mutex.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxmutex_trywait
 *
 * Description:
 *   This function locks the specified semaphore only if the semaphore is
 *   currently not locked.  In either case, the call returns without
 *   blocking.
 *
 * Input Parameters:
 *   sem - the semaphore descriptor
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 *     - EINVAL - Invalid attempt to get the semaphore
 *     - EAGAIN - The semaphore is not available.
 *
 ****************************************************************************/

int nxmutex_trywait(FAR sem_t *sem)
{
  int ret = -EAGAIN;

  /* This API should not be called from the idleloop or interrupt */

#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  DEBUGASSERT(!up_interrupt_context());
  DEBUGASSERT(!OSINIT_IDLELOOP() || !sched_idletask());
#endif

  DEBUGASSERT(sem != NULL);
  DEBUGASSERT(NXSEM_IS_MUTEX(sem));

  /* Disable fast path if priority protection is enabled on the semaphore */

#ifdef CONFIG_PRIORITY_PROTECT
  if ((sem->flags & SEM_PRIO_MASK) != SEM_PRIO_PROTECT)
    {
#endif
      for (; ; )
        {
          FAR atomic_t *val = NXSEM_MHOLDER(sem);
          int32_t old = atomic_read(val);

          if (old != NXSEM_NO_MHOLDER)
            {
              break;
            }

          if (atomic_try_cmpxchg_acquire(val, &old, gettid()))
            {
              ret = OK;
              break;
            }
        }
#ifdef CONFIG_PRIORITY_PROTECT
    }
  else
    {
      ret = nxsem_trywait_slow(sem);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: nxmutex_init
 *
 * Description:
 *   This function initializes the UNNAMED mutex. Following a
 *   successful call to nxmutex_init(), the mutex may be used in subsequent
 *   calls to nxmutex_lock(), nxmutex_unlock(), and nxmutex_trylock().  The
 *   mutex remains usable until it is destroyed.
 *
 * Parameters:
 *   mutex - Semaphore to be initialized
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *
 ****************************************************************************/

int nxmutex_init(FAR mutex_t *mutex)
{
  int ret = nxsem_init(&mutex->sem, 0, NXSEM_NO_MHOLDER);

  if (ret >= 0)
    {
#ifdef CONFIG_PRIORITY_INHERITANCE
      nxsem_set_protocol(&mutex->sem, SEM_TYPE_MUTEX | SEM_PRIO_INHERIT);
#else
      nxsem_set_protocol(&mutex->sem, SEM_TYPE_MUTEX);
#endif
    }

  return ret;
}

/****************************************************************************
 * Name: nxmutex_ticklock
 *
 * Description:
 *   This function attempts to lock the mutex referenced by 'mutex'.  If the
 *   mutex value is (<=) zero, then the calling task will not return until it
 *   successfully acquires the lock or timed out
 *
 * Input Parameters:
 *   mutex   - Mutex object
 *   delay   - Ticks to wait from the start time until the semaphore is
 *             posted.  If ticks is zero, then this function is equivalent
 *             to nxmutex_trylock().
 *
 * Returned Value:
 *   OK        The mutex successfully acquires
 *   EINVAL    The mutex argument does not refer to a valid mutex.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The mutex could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *
 ****************************************************************************/

int nxmutex_ticklock(FAR mutex_t *mutex, clock_t delay)
{
  clock_t end;
  int ret;

  ret = nxmutex_trywait(&mutex->sem);
  if (ret >= 0)
    {
      nxmutex_add_backtrace(mutex);
    }
  else
    {
      /* Wait until we get the lock or until the timeout expires */

      end = clock_delay2abstick(delay);

      for (; ; )
        {
          ret = nxsem_tickwait_slow(&mutex->sem, delay);
          if (ret >= 0)
            {
              nxmutex_add_backtrace(mutex);
              break;
            }
          else if (ret != -EINTR && ret != -ECANCELED)
            {
              break;
            }

          delay = end - clock();
          if ((int32_t)delay <= 0)
            {
              ret = -ETIMEDOUT;
              break;
            }
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxmutex_clocklock
 *
 * Description:
 *   This function attempts to lock the mutex referenced by 'mutex'.  If the
 *   mutex value is (<=) zero, then the calling task will not return until it
 *   successfully acquires the lock or timed out
 *
 * Input Parameters:
 *   mutex   - Mutex object
 *   clockid - The clock to be used as the time base
 *   abstime - The absolute time when the mutex lock timed out
 *
 * Returned Value:
 *   OK        The mutex successfully acquires
 *   EINVAL    The mutex argument does not refer to a valid mutex.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The mutex could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *
 ****************************************************************************/

int nxmutex_clocklock(FAR mutex_t *mutex, clockid_t clockid,
                      FAR const struct timespec *abstime)
{
  int ret;

  /* Try to take the semaphore without waiting. */

  ret = nxmutex_trywait(&mutex->sem);
  if (ret >= 0)
    {
      nxmutex_add_backtrace(mutex);
    }
  else
    {
      /* Wait until we get the lock or until the timeout expires */

      for (; ; )
        {
          if (abstime)
            {
              ret = nxsem_clockwait_slow(&mutex->sem, clockid, abstime);
            }
          else
            {
              ret = nxmutex_wait(&mutex->sem);
            }

          if (ret >= 0)
            {
              nxmutex_add_backtrace(mutex);
              break;
            }
          else if (ret != -EINTR && ret != -ECANCELED)
            {
              break;
            }
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxmutex_timedlock
 *
 * Description:
 *   This function attempts to lock the mutex .  If the mutex value
 *   is (<=) zero,then the calling task will not return until it
 *   successfully acquires the lock or timed out
 *
 * Input Parameters:
 *   mutex   - Mutex object
 *   timeout - The time when mutex lock timed out
 *
 * Returned Value:
 *   OK        The mutex successfully acquires
 *   EINVAL    The mutex argument does not refer to a valid mutex.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The mutex could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *
 ****************************************************************************/

int nxmutex_timedlock(FAR mutex_t *mutex, unsigned int timeout)
{
  struct timespec now;
  struct timespec delay;
  struct timespec rqtp;

  clock_gettime(CLOCK_MONOTONIC, &now);
  clock_ticks2time(&delay, MSEC2TICK(timeout));
  clock_timespec_add(&now, &delay, &rqtp);

  /* Wait until we get the lock or until the timeout expires */

  return nxmutex_clocklock(mutex, CLOCK_MONOTONIC, &rqtp);
}

/****************************************************************************
 * Name: nrxmutex_lock
 *
 * Description:
 *   This function attempts to lock the recursive mutex referenced by
 *   'rmutex'.The recursive mutex can be locked multiple times in the same
 *   thread.
 *
 * Parameters:
 *   rmutex - Recursive mutex descriptor.
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 ****************************************************************************/

int nxrmutex_lock(FAR rmutex_t *rmutex)
{
  int ret = OK;

  if (!nxrmutex_is_hold(rmutex))
    {
      for (; ; )
        {
          /* Take the semaphore (perhaps waiting) */

          ret = nxmutex_wait(&rmutex->mutex.sem);
          if (ret >= 0 || (ret != -EINTR && ret != -ECANCELED))
            {
              break;
            }
        }
    }

  if (ret >= 0)
    {
      DEBUGASSERT(rmutex->count < UINT_MAX);
      ++rmutex->count;

      nxmutex_add_backtrace(&rmutex->mutex);
    }

  return ret;
}

/****************************************************************************
 * Name: nxmutex_trylock
 *
 * Description:
 *   This function locks the mutex only if the mutex is currently not locked.
 *   If the mutex has been locked already, the call returns without blocking.
 *
 * Parameters:
 *   mutex - mutex descriptor.
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 *     -EINVAL - Invalid attempt to lock the mutex
 *     -EAGAIN - The mutex is not available.
 *
 ****************************************************************************/

int nxmutex_trylock(FAR mutex_t *mutex)
{
  int ret;

  ret = nxmutex_trywait(&mutex->sem);
  if (ret >= 0)
    {
      nxmutex_add_backtrace(mutex);
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_trylock
 *
 * Description:
 *   This function locks the recursive mutex if the recursive mutex is
 *   currently not locked or the same thread call.
 *   If the recursive mutex is locked and other thread call it,
 *   the call returns without blocking.
 *
 * Parameters:
 *   rmutex - Recursive mutex descriptor.
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 *     -EINVAL - Invalid attempt to lock the recursive mutex
 *     -EAGAIN - The recursive mutex is not available.
 *
 ****************************************************************************/

int nxrmutex_trylock(FAR rmutex_t *rmutex)
{
  int ret = OK;

  if (!nxrmutex_is_hold(rmutex))
    {
      ret = nxmutex_trylock(&rmutex->mutex);
    }

  if (ret >= 0)
    {
      DEBUGASSERT(rmutex->count < UINT_MAX);
      ++rmutex->count;
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_ticklock
 *
 * Description:
 *   This function attempts to lock the mutex referenced by 'mutex'.  If the
 *   mutex value is (<=) zero, then the calling task will not return until it
 *   successfully acquires the lock or timed out
 *
 * Input Parameters:
 *   rmutex  - Rmutex object
 *   delay   - Ticks to wait from the start time until the semaphore is
 *             posted.  If ticks is zero, then this function is equivalent
 *             to nxrmutex_trylock().
 *
 * Returned Value:
 *   OK        The mutex successfully acquires
 *   EINVAL    The mutex argument does not refer to a valid mutex.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The mutex could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *
 ****************************************************************************/

int nxrmutex_ticklock(FAR rmutex_t *rmutex, uint32_t delay)
{
  int ret = OK;

  if (!nxrmutex_is_hold(rmutex))
    {
      ret = nxmutex_ticklock(&rmutex->mutex, delay);
    }

  if (ret >= 0)
    {
      DEBUGASSERT(rmutex->count < UINT_MAX);
      ++rmutex->count;
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_clocklock
 *
 * Description:
 *   This function attempts to lock the mutex referenced by 'mutex'.  If the
 *   mutex value is (<=) zero, then the calling task will not return until it
 *   successfully acquires the lock or timed out
 *
 * Input Parameters:
 *   rmutex  - Rmutex object
 *   clockid - The clock to be used as the time base
 *   abstime - The absolute time when the mutex lock timed out
 *
 * Returned Value:
 *   OK        The mutex successfully acquires
 *   EINVAL    The mutex argument does not refer to a valid mutex.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The mutex could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *
 ****************************************************************************/

int nxrmutex_clocklock(FAR rmutex_t *rmutex, clockid_t clockid,
                       FAR const struct timespec *abstime)
{
  int ret = OK;

  if (!nxrmutex_is_hold(rmutex))
    {
      ret = nxmutex_clocklock(&rmutex->mutex, clockid, abstime);
    }

  if (ret >= 0)
    {
      DEBUGASSERT(rmutex->count < UINT_MAX);
      ++rmutex->count;
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_timedlock
 *
 * Description:
 *   This function attempts to lock the mutex .  If the mutex value
 *   is (<=) zero,then the calling task will not return until it
 *   successfully acquires the lock or timed out
 *
 * Input Parameters:
 *   rmutex  - Rmutex object
 *   timeout - The time when mutex lock timed out
 *
 * Returned Value:
 *   OK        The mutex successfully acquires
 *   EINVAL    The mutex argument does not refer to a valid mutex.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The mutex could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *   ECANCELED May be returned if the thread is canceled while waiting.
 *
 ****************************************************************************/

int nxrmutex_timedlock(FAR rmutex_t *rmutex, unsigned int timeout)
{
  int ret = OK;

  if (!nxrmutex_is_hold(rmutex))
    {
      ret = nxmutex_timedlock(&rmutex->mutex, timeout);
    }

  if (ret >= 0)
    {
      DEBUGASSERT(rmutex->count < UINT_MAX);
      ++rmutex->count;
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_unlock
 *
 * Description:
 *   This function attempts to unlock the recursive mutex
 *   referenced by 'rmutex'.
 *
 * Parameters:
 *   rmutex - Recursive mutex descriptor.
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 * Assumptions:
 *   This function may be called from an interrupt handler.
 *
 ****************************************************************************/

int nxrmutex_unlock(FAR rmutex_t *rmutex)
{
  int ret = OK;

  DEBUGASSERT(nxrmutex_is_hold(rmutex));
  DEBUGASSERT(rmutex->count > 0u);

  if (--rmutex->count == 0u)
    {
      ret = nxmutex_unlock(&rmutex->mutex);
      if (ret < 0)
        {
          ++rmutex->count;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nrxmutex_breaklock
 *
 * Description:
 *   This function attempts to break the recursive mutex
 *
 * Parameters:
 *   rmutex - Recursive mutex descriptor.
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 ****************************************************************************/

int nxrmutex_breaklock(FAR rmutex_t *rmutex, FAR unsigned int *count)
{
  int ret = OK;

  *count = 0u;
  if (nxrmutex_is_hold(rmutex))
    {
      *count = rmutex->count;
      rmutex->count = 0u;
      ret = nxmutex_unlock(&rmutex->mutex);
      if (ret < 0)
        {
          rmutex->count = *count;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_restorelock
 *
 * Description:
 *   This function attempts to restore the recursive mutex.
 *
 * Parameters:
 *   rmutex - Recursive mutex descriptor.
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 ****************************************************************************/

int nxrmutex_restorelock(FAR rmutex_t *rmutex, unsigned int count)
{
  int ret = OK;

  if (count != 0u)
    {
      ret = nxmutex_lock(&rmutex->mutex);
      if (ret >= 0)
        {
          rmutex->count = count;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxmutex_breaklock
 *
 * Description:
 *   This function attempts to break the mutex
 *
 * Parameters:
 *   mutex   - Mutex descriptor.
 *   locked  - Is the mutex break success
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   Possible returned errors:
 *
 ****************************************************************************/

int nxmutex_breaklock(FAR mutex_t *mutex, FAR unsigned int *locked)
{
  int ret;

  *locked = false;
  ret = nxmutex_unlock(mutex);
  if (ret >= 0)
    {
      *locked = true;
    }

  return ret;
}

/****************************************************************************
 * Name: nxrmutex_destroy
 *
 * Description:
 *   This function destroy the UNNAMED recursive mutex.
 *
 * Parameters:
 *   rmutex - Recursive mutex to be destroyed
 *
 * Return Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *
 ****************************************************************************/

int nxrmutex_destroy(FAR rmutex_t *rmutex)
{
  int ret = nxmutex_destroy(&rmutex->mutex);

  if (ret >= 0)
    {
      rmutex->count = 0;
    }

  return ret;
}
