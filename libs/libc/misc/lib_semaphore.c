/****************************************************************************
 * libs/libc/misc/lib_semaphore.c
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
#include <nuttx/semaphore.h>

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxsem_trywait
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

int nxsem_trywait(FAR sem_t *sem)
{
  int ret = -EAGAIN;

  /* This API should not be called from the idleloop or interrupt */

#if defined(CONFIG_BUILD_FLAT) || defined(__KERNEL__)
  DEBUGASSERT(!up_interrupt_context());
  DEBUGASSERT(!OSINIT_IDLELOOP() || !sched_idletask());
#endif

  DEBUGASSERT(sem != NULL);
  DEBUGASSERT(!NXSEM_IS_MUTEX(sem));

  /* Disable fast path if priority protection is enabled on the semaphore */

#if defined(CONFIG_PRIORITY_INHERITANCE) || defined(CONFIG_PRIORITY_PROTECT)
  if ((sem->flags & SEM_PRIO_MASK) == SEM_PRIO_NONE)
    {
#endif
      for (; ; )
        {
          FAR atomic_t *val = NXSEM_COUNT(sem);
          int32_t old = atomic_read(val);

          if (old < 1)
            {
              break;
            }

          if (atomic_try_cmpxchg_acquire(val, &old, old - 1))
            {
              ret = OK;
              break;
            }
        }
#if defined(CONFIG_PRIORITY_INHERITANCE) || defined(CONFIG_PRIORITY_PROTECT)
    }
  else
    {
      ret = nxsem_trywait_slow(sem);
    }
#endif

  return ret;
}

/****************************************************************************
 * Name: nxsem_clockwait
 *
 * Description:
 *   This function will lock the semaphore referenced by sem as in the
 *   sem_wait() function. However, if the semaphore cannot be locked without
 *   waiting for another process or thread to unlock the semaphore by
 *   performing a sem_post() function, this wait will be terminated when the
 *   specified timeout expires.
 *
 *   The timeout will expire when the absolute time specified by abstime
 *   passes, as measured by the clock on which timeouts are based (that is,
 *   when the value of that clock equals or exceeds abstime), or if the
 *   absolute time specified by abstime has already been passed at the
 *   time of the call.
 *
 *   This is an internal OS interface.  It is functionally equivalent to
 *   sem_wait except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 * Input Parameters:
 *   sem     - Semaphore object
 *   clockid - The timing source to use in the conversion
 *   abstime - The absolute time to wait until a timeout is declared.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   That may be one of:
 *
 *   EINVAL    The sem argument does not refer to a valid semaphore.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The semaphore could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *   EINTR     A signal interrupted this function.
 *
 ****************************************************************************/

int nxsem_clockwait(FAR sem_t *sem, clockid_t clockid,
                    FAR const struct timespec *abstime)
{
  int ret = -EINVAL;

  /* Try to take the semaphore without waiting. */

  ret = nxsem_trywait(sem);
  if (ret != OK)
    {
      ret = nxsem_clockwait_slow(sem, clockid, abstime);
    }

  return ret;
}

/****************************************************************************
 * Name: nxsem_clockwait_uninterruptible
 *
 * Description:
 *   This function is wrapped version of nxsem_clockwait(), which is
 *   uninterruptible and convenient for use.
 *
 * Input Parameters:
 *   sem     - Semaphore object
 *   clockid - The timing source to use in the conversion
 *   abstime - The absolute time to wait until a timeout is declared.
 *
 * Returned Value:
 *   EINVAL    The sem argument does not refer to a valid semaphore.  Or the
 *             thread would have blocked, and the abstime parameter specified
 *             a nanoseconds field value less than zero or greater than or
 *             equal to 1000 million.
 *   ETIMEDOUT The semaphore could not be locked before the specified timeout
 *             expired.
 *   EDEADLK   A deadlock condition was detected.
 *   ECANCELED May be returned if the thread is canceled while waiting.
 *
 ****************************************************************************/

int nxsem_clockwait_uninterruptible(FAR sem_t *sem, clockid_t clockid,
                                    FAR const struct timespec *abstime)
{
  int ret;

  do
    {
      /* Take the semaphore (perhaps waiting) */

      ret = nxsem_clockwait(sem, clockid, abstime);
    }
  while (ret == -EINTR);

  return ret;
}

/****************************************************************************
 * Name: nxsem_tickwait
 *
 * Description:
 *   This function is a lighter weight version of sem_timedwait().  It is
 *   non-standard and intended only for use within the RTOS.
 *
 * Input Parameters:
 *   sem     - Semaphore object
 *   delay   - Ticks to wait from the start time until the semaphore is
 *             posted.  If ticks is zero, then this function is equivalent
 *             to nxsem_trywait().
 *
 * Returned Value:
 *   This is an internal OS interface, not available to applications, and
 *   hence follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   -ETIMEDOUT is returned on the timeout condition.
 *
 ****************************************************************************/

int nxsem_tickwait(FAR sem_t *sem, clock_t delay)
{
  int ret;

  DEBUGASSERT(sem != NULL && up_interrupt_context() == false);

  /* Try to take the semaphore without waiting. */

  ret = nxsem_trywait(sem);
  if (ret != OK)
    {
      if (delay == 0u)
        {
          /* Timed out already before waiting */

          ret = -ETIMEDOUT;
        }
      else
        {
          ret = nxsem_tickwait_slow(sem, delay);
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxsem_tickwait_uninterruptible
 *
 * Description:
 *   This function is wrapped version of nxsem_tickwait(), which is
 *   uninterruptible and convenient for use.
 *
 * Input Parameters:
 *   sem     - Semaphore object
 *   delay   - Ticks to wait from the start time until the semaphore is
 *             posted.  If ticks is zero, then this function is equivalent
 *             to sem_trywait().
 *
 * Returned Value:
 *   This is an internal OS interface, not available to applications, and
 *   hence follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure:
 *
 *     -ETIMEDOUT is returned on the timeout condition.
 *     -ECANCELED may be returned if the thread is canceled while waiting.
 *
 ****************************************************************************/

int nxsem_tickwait_uninterruptible(FAR sem_t *sem, clock_t delay)
{
  clock_t end = clock_delay2abstick(delay);
  int ret;

  for (; ; )
    {
      /* Take the semaphore (perhaps waiting) */

      ret = nxsem_tickwait(sem, delay);
      if (ret != -EINTR)
        {
          break;
        }

      delay = end - clock();
      if ((int32_t)delay < 0)
        {
          delay = 0;
        }
    }

  return ret;
}

/****************************************************************************
 * Name: nxsem_wait_uninterruptible
 *
 * Description:
 *   This function is wrapped version of nxsem_wait(), which is
 *   uninterruptible and convenient for use.
 *
 * Parameters:
 *   sem - Semaphore descriptor.
 *
 * Return Value:
 *   Zero(OK)  - On success
 *   EINVAL    - Invalid attempt to get the semaphore
 *   ECANCELED - May be returned if the thread is canceled while waiting.
 *
 * NOTE:  It is essential that callers of this function handle the
 * ECANCELED error.  Correct handling is that the function should return the
 * error and the error should propagate back up the calling tree to the
 * cancellation point interface function where the thread termination will
 * be handled gracefully
 *
 ****************************************************************************/

int nxsem_wait_uninterruptible(FAR sem_t *sem)
{
  int ret;

  do
    {
      /* Take the semaphore (perhaps waiting) */

      ret = nxsem_wait(sem);
    }
  while (ret == -EINTR);

  return ret;
}
