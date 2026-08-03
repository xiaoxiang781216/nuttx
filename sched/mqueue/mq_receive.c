/****************************************************************************
 * sched/mqueue/mq_receive.c
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
#include <nuttx/nuttx.h>

#include <sys/types.h>
#include <stdbool.h>
#include <assert.h>
#include <errno.h>
#include <mqueue.h>
#include <debug.h>
#include <fcntl.h>

#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/mqueue.h>
#include <nuttx/cancelpt.h>

#include "sched/sched.h"
#include "mqueue/mqueue.h"

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: nxmq_verify_receive
 *
 * Description:
 *   This is internal, common logic shared by both [nx]mq_receive and
 *   [nx]mq_timedreceive.  This function verifies the input parameters that
 *   are common to both functions.
 *
 * Input Parameters:
 *   msgq   - Message queue descriptor
 *   msg    - Buffer to receive the message
 *   msglen - Size of the buffer in bytes
 *
 * Returned Value:
 *   On success, zero (OK) is returned.  A negated errno value is returned
 *   on any failure:
 *
 *   EBADF    Message queue opened not opened for reading.
 *   EMSGSIZE 'msglen' was less than the maxmsgsize attribute of the message
 *            queue.
 *   EINVAL   Invalid 'msg' or 'msgq'
 *
 ****************************************************************************/

#ifdef CONFIG_DEBUG_FEATURES
static int nxmq_verify_receive(FAR struct file *mq,
                               FAR char *msg, size_t msglen)
{
  FAR struct inode *inode = mq->f_inode;
  FAR struct mqueue_inode_s *msgq;

  if (inode == NULL)
    {
      return -EBADF;
    }

  msgq = inode->i_private;

  /* Verify the input parameters */

  if (!msg || !msgq)
    {
      return -EINVAL;
    }

  if ((mq->f_oflags & O_RDOK) == 0)
    {
      return -EBADF;
    }

  if (msglen < (size_t)msgq->maxmsgsize)
    {
      return -EMSGSIZE;
    }

  return OK;
}
#endif

/****************************************************************************
 * Name: nxmq_rcvtimeout
 *
 * Description:
 *   This function is called if the timeout elapses before the message queue
 *   becomes non-empty.
 *
 * Input Parameters:
 *   timer   - The hrtimer that expired.
 *   expired - The expiration time snapshot captured at dispatch time.
 *
 * Returned Value:
 *   0 (no periodic re-arm).
 *
 * Assumptions:
 *
 ****************************************************************************/

static uint64_t nxmq_rcvtimeout(FAR const hrtimer_t *timer, uint64_t expired)
{
  FAR struct tcb_s *wtcb  = container_of(timer, struct tcb_s, waittimer);
  irqstate_t        flags = enter_critical_section();

  /* It is also possible that an interrupt/context switch beat us to the
   * punch and already changed the task's state.
   */

  if (expired == timer->expired &&
      wtcb->task_state == TSTATE_WAIT_MQNOTEMPTY)
    {
      /* Restart with task with a timeout error */

      nxmq_wait_irq(wtcb, ETIMEDOUT);
    }

  leave_critical_section(flags);
  return 0;
}

/****************************************************************************
 * Name: nxmq_wait_receive
 *
 * Description:
 *   This is internal, common logic shared by both [nx]mq_receive and
 *   [nx]mq_timedreceive.  This function waits for a message to be received
 *   on the specified message queue, removes the message from the queue, and
 *   returns it.
 *
 * Input Parameters:
 *   msgq   - Message queue descriptor
 *   rcvmsg - The caller-provided location in which to return the newly
 *            received message.
 *   abstime - If non-NULL, this is the absolute time to wait until a
 *             message is received.
 *
 * Returned Value:
 *   On success, zero (OK) is returned.  A negated errno value is returned
 *   on any failure.
 *
 * Assumptions:
 * - The caller has provided all validity checking of the input parameters
 *   using nxmq_verify_receive.
 * - Interrupts should be disabled throughout this call.  This is necessary
 *   because messages can be sent from interrupt level processing.
 * - The caller is responsible for starting and cancelling the receive
 *   timeout timer.
 *
 ****************************************************************************/

static inline_function int
nxmq_wait_receive(FAR struct mqueue_inode_s *msgq,
                  FAR struct mqueue_msg_s **rcvmsg)
{
  FAR struct mqueue_msg_s *newmsg;
  FAR struct tcb_s *rtcb = this_task();

#ifdef CONFIG_CANCELLATION_POINTS
  /* nxmq_wait_receive() is not a cancellation point, but it may be called
   * from mq_receive() or mq_timedreceive() which are cancellation point.
   */

  if (check_cancellation_point())
    {
      /* If there is a pending cancellation, then do not perform
       * the wait.  Exit now with ECANCELED.
       */

      return -ECANCELED;
    }
#endif

  /* Get the message from the head of the queue */

  while ((newmsg = (FAR struct mqueue_msg_s *)
                   list_remove_head(&msgq->msglist)) == NULL)
    {
      msgq->cmn.nwaitnotempty++;

      /* Initialize the 'errcode" used to communication wake-up error
       * conditions.
       */

      rtcb->waitobj = msgq;
      rtcb->errcode = OK;

      /* Remove the tcb task from the running list. */

      nxsched_remove_self(rtcb);

      /* Add the task to the specified blocked task list */

      rtcb->task_state = TSTATE_WAIT_MQNOTEMPTY;
      nxsched_add_prioritized(rtcb, MQ_WNELIST(msgq->cmn));

      /* Now, perform the context switch */

      nxsched_switch(this_task(), rtcb);

      /* When we resume at this point, either (1) the message queue
       * is no longer empty, or (2) the wait has been interrupted by
       * a signal.  We can detect the latter case be examining the
       * errno value (should be either EINTR or ETIMEDOUT).
       */

      if (rtcb->errcode != OK)
        {
          break;
        }
    }

  *rcvmsg = newmsg;
  return -rtcb->errcode;
}

/****************************************************************************
 * Name: nxmq_notify_receive
 *
 * Description:
 *   This is internal, common logic shared by both [nx]mq_receive and
 *   [nx]mq_timedreceive.
 *   This function notifies any tasks that are waiting for the message queue
 *   to become non-empty. This function is called after a message is
 *   received from the message queue.
 *
 * Input Parameters:
 *   msgq    - Message queue descriptor
 *
 * Returned Value:
 *   Returns the length of the received message.  This function does not
 *   fail.
 *
 * Assumptions:
 * - Pre-emption should be disabled throughout this call.
 *
 ****************************************************************************/

static inline_function void
nxmq_notify_receive(FAR struct mqueue_inode_s *msgq)
{
  FAR struct tcb_s *btcb;

  /* Check if any tasks are waiting for the MQ not full event. */

  if (msgq->cmn.nwaitnotfull > 0)
    {
      FAR struct tcb_s *rtcb = this_task();

      /* Find the highest priority task that is waiting for
       * this queue to be not-full in waitfornotfull list.
       * This must be performed in a critical section because
       * messages can be sent from interrupt handlers.
       */

      btcb = (FAR struct tcb_s *)dq_remfirst(MQ_WNFLIST(msgq->cmn));

      /* If one was found, unblock it.  NOTE:  There is a race
       * condition here:  the queue might be full again by the
       * time the task is unblocked
       */

      DEBUGASSERT(btcb != NULL);

      tcb_timer_cancel(btcb);

      msgq->cmn.nwaitnotfull--;

      /* Indicate that the wait is over. */

      btcb->waitobj = NULL;

      /* Add the task to ready-to-run task list and
       * perform the context switch if one is needed
       */

      if (nxsched_add_readytorun(btcb))
        {
          nxsched_switch(this_task(), rtcb);
        }
    }
}

/****************************************************************************
 * Name: file_mq_timedreceive_internal
 *
 * Description:
 *   This is an internal function of file_mq_timedreceive()/
 *   file_mq_tickreceive(), please refer to the detailed description for
 *   more information.
 *
 * Input Parameters:
 *   mq      - Message Queue Descriptor
 *   msg     - Buffer to receive the message
 *   msglen  - Size of the buffer in bytes
 *   prio    - If not NULL, the location to store message priority.
 *   abstime - the absolute time to wait until a timeout is declared.
 *
 * Returned Value:
 *   On success, the length of the selected message in bytes is returned.
 *   On failure, -1 (ERROR) is returned and the errno is set appropriately:
 *
 *   EAGAIN    The queue was empty, and the O_NONBLOCK flag was set
 *             for the message queue description referred to by 'mqdes'.
 *   EPERM     Message queue opened not opened for reading.
 *   EMSGSIZE  'msglen' was less than the maxmsgsize attribute of the
 *             message queue.
 *   EINTR     The call was interrupted by a signal handler.
 *   EINVAL    Invalid 'msg' or 'mqdes' or 'abstime'
 *   ETIMEDOUT The call timed out before a message could be transferred.
 *
 ****************************************************************************/

static
ssize_t file_mq_timedreceive_internal(FAR struct file *mq, FAR char *msg,
                                      size_t msglen, FAR unsigned int *prio,
                                      FAR const struct timespec *abstime,
                                      sclock_t ticks)
{
  FAR struct mqueue_inode_s *msgq;
  FAR struct mqueue_msg_s *mqmsg;
  FAR struct tcb_s *rtcb = this_task();
  irqstate_t flags;
  ssize_t ret = 0;

  DEBUGASSERT(up_interrupt_context() == false);

  /* Verify the input parameters */

  if (abstime && !clock_timespec_validate(abstime))
    {
      return -EINVAL;
    }

  if (mq == NULL)
    {
      return -EINVAL;
    }

#ifdef CONFIG_DEBUG_FEATURES
  /* Verify the input parameters and, in case of an error, set
   * errno appropriately.
   */

  ret = nxmq_verify_receive(mq, msg, msglen);
  if (ret < 0)
    {
      return ret;
    }
#endif

  msgq = mq->f_inode->i_private;

  /* Furthermore, nxmq_wait_receive() expects to have interrupts disabled
   * because messages can be sent from interrupt level.
   */

  flags = enter_critical_section();

  /* Get the message from the message queue */

  mqmsg = (FAR struct mqueue_msg_s *)list_remove_head(&msgq->msglist);
  if (mqmsg == NULL)
    {
      if ((mq->f_oflags & O_NONBLOCK) != 0)
        {
          leave_critical_section(flags);
          return -EAGAIN;
        }

      /* Wait & get the message from the message queue */

      if (abstime)
        {
          tcb_timer_start_abstime(rtcb, CLOCK_REALTIME, abstime,
                                  nxmq_rcvtimeout);
        }
      else if (ticks >= 0)
        {
          tcb_timer_start_ticks(rtcb, ticks, nxmq_rcvtimeout);
        }

      ret = nxmq_wait_receive(msgq, &mqmsg);

      tcb_timer_cancel(rtcb);

      if (ret < 0)
        {
          leave_critical_section(flags);
          return ret;
        }
    }

  /* If we got message, then decrement the number of messages in
   * the queue while we are still in the critical section
   */

  if (msgq->nmsgs-- == msgq->maxmsgs)
    {
      nxmq_pollnotify(msgq, POLLOUT);
    }

  /* Notify all threads waiting for a message in the message queue */

  nxmq_notify_receive(msgq);

  leave_critical_section(flags);

  /* Return the message to the caller */

  if (prio)
    {
      *prio = mqmsg->priority;
    }

  memcpy(msg, mqmsg->mail, mqmsg->msglen);
  ret = mqmsg->msglen;

  /* Free the message structure */

  nxmq_free_msg(mqmsg);

  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: file_mq_timedreceive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages from
 *   the message queue specified by "mq."  If the message queue is empty
 *   and O_NONBLOCK was not set, file_mq_timedreceive() will block until a
 *   message is added to the message queue (or until a timeout occurs).
 *
 *   file_mq_timedreceive() is an internal OS interface.  It is functionally
 *   equivalent to mq_timedreceive() except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 *  See comments with mq_timedreceive() for a more complete description of
 *  the behavior of this function
 *
 * Input Parameters:
 *   mq      - Message Queue Descriptor
 *   msg     - Buffer to receive the message
 *   msglen  - Size of the buffer in bytes
 *   prio    - If not NULL, the location to store message priority.
 *   abstime - the absolute time to wait until a timeout is declared.
 *
 * Returned Value:
 *   On success, the length of the selected message in bytes is returned.
 *   On failure, -1 (ERROR) is returned and the errno is set appropriately:
 *
 *   EAGAIN    The queue was empty, and the O_NONBLOCK flag was set
 *             for the message queue description referred to by 'mqdes'.
 *   EPERM     Message queue opened not opened for reading.
 *   EMSGSIZE  'msglen' was less than the maxmsgsize attribute of the
 *             message queue.
 *   EINTR     The call was interrupted by a signal handler.
 *   EINVAL    Invalid 'msg' or 'mqdes' or 'abstime'
 *   ETIMEDOUT The call timed out before a message could be transferred.
 *
 ****************************************************************************/

ssize_t file_mq_timedreceive(FAR struct file *mq, FAR char *msg,
                             size_t msglen, FAR unsigned int *prio,
                             FAR const struct timespec *abstime)
{
  return file_mq_timedreceive_internal(mq, msg, msglen, prio, abstime, -1);
}

/****************************************************************************
 * Name: file_mq_tickreceive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages from
 *   the message queue specified by "mq."  If the message queue is empty
 *   and O_NONBLOCK was not set, file_mq_tickreceive() will block until a
 *   message is added to the message queue (or until a timeout occurs).
 *
 *   file_mq_tickreceive() is an internal OS interface.  It is functionally
 *   equivalent to mq_timedreceive() except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 *  See comments with mq_timedreceive() for a more complete description of
 *  the behavior of this function
 *
 * Input Parameters:
 *   mq      - Message Queue Descriptor
 *   msg     - Buffer to receive the message
 *   msglen  - Size of the buffer in bytes
 *   prio    - If not NULL, the location to store message priority.
 *   ticks   - Ticks to wait from the start time until the semaphore is
 *             posted.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   (see mq_timedreceive() for the list list valid return values).
 *
 ****************************************************************************/

ssize_t file_mq_tickreceive(FAR struct file *mq, FAR char *msg,
                            size_t msglen, FAR unsigned int *prio,
                            sclock_t ticks)
{
  return file_mq_timedreceive_internal(mq, msg, msglen, prio, NULL, ticks);
}

/****************************************************************************
 * Name: nxmq_timedreceive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages from
 *   the message queue specified by "mqdes."  If the message queue is empty
 *   and O_NONBLOCK was not set, nxmq_timedreceive() will block until a
 *   message is added to the message queue (or until a timeout occurs).
 *
 *   nxmq_timedreceive() is an internal OS interface.  It is functionally
 *   equivalent to mq_timedreceive() except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 *  See comments with mq_timedreceive() for a more complete description of
 *  the behavior of this function
 *
 * Input Parameters:
 *   mqdes   - Message Queue Descriptor
 *   msg     - Buffer to receive the message
 *   msglen  - Size of the buffer in bytes
 *   prio    - If not NULL, the location to store message priority.
 *   abstime - the absolute time to wait until a timeout is declared.
 *
 * Returned Value:
 *   On success, the length of the selected message in bytes is returned.
 *   On failure, -1 (ERROR) is returned and the errno is set appropriately:
 *
 *   EAGAIN    The queue was empty, and the O_NONBLOCK flag was set
 *             for the message queue description referred to by 'mqdes'.
 *   EPERM     Message queue opened not opened for reading.
 *   EMSGSIZE  'msglen' was less than the maxmsgsize attribute of the
 *             message queue.
 *   EINTR     The call was interrupted by a signal handler.
 *   EINVAL    Invalid 'msg' or 'mqdes' or 'abstime'
 *   ETIMEDOUT The call timed out before a message could be transferred.
 *
 ****************************************************************************/

ssize_t nxmq_timedreceive(mqd_t mqdes, FAR char *msg, size_t msglen,
                          FAR unsigned int *prio,
                          FAR const struct timespec *abstime)
{
  FAR struct file *filep;
  ssize_t ret;

  ret = file_get(mqdes, &filep);
  if (ret < 0)
    {
      return ret;
    }

  ret = file_mq_timedreceive_internal(filep, msg, msglen, prio, abstime, -1);
  file_put(filep);
  return ret;
}

/****************************************************************************
 * Name: mq_timedreceive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages from
 *   the message queue specified by "mqdes."  If the size of the buffer in
 *   bytes (msglen) is less than the "mq_msgsize" attribute of the message
 *   queue, mq_timedreceive will return an error.  Otherwise, the selected
 *   message is removed from the queue and copied to "msg."
 *
 *   If the message queue is empty and O_NONBLOCK was not set,
 *   mq_timedreceive() will block until a message is added to the message
 *   queue (or until a timeout occurs).  If more than one task is waiting
 *   to receive a message, only the task with the highest priority that has
 *   waited the longest will be unblocked.
 *
 *   mq_timedreceive() behaves just like mq_receive(), except that if the
 *   queue is empty and the O_NONBLOCK flag is not enabled for the message
 *   queue description, then abstime points to a structure which specifies a
 *   ceiling on the time for which the call will block.  This ceiling is an
 *   absolute timeout in seconds and nanoseconds since the Epoch (midnight
 *   on the morning of 1 January 1970).
 *
 *   If no message is available, and the timeout has already expired by the
 *   time of the call, mq_timedreceive() returns immediately.
 *
 * Input Parameters:
 *   mqdes   - Message Queue Descriptor
 *   msg     - Buffer to receive the message
 *   msglen  - Size of the buffer in bytes
 *   prio    - If not NULL, the location to store message priority.
 *   abstime - the absolute time to wait until a timeout is declared.
 *
 * Returned Value:
 *   On success, the length of the selected message in bytes is returned.
 *   On failure, -1 (ERROR) is returned and the errno is set appropriately:
 *
 *   EAGAIN    The queue was empty, and the O_NONBLOCK flag was set
 *             for the message queue description referred to by 'mqdes'.
 *   EPERM     Message queue opened not opened for reading.
 *   EMSGSIZE  'msglen' was less than the maxmsgsize attribute of the
 *             message queue.
 *   EINTR     The call was interrupted by a signal handler.
 *   EINVAL    Invalid 'msg' or 'mqdes' or 'abstime'
 *   ETIMEDOUT The call timed out before a message could be transferred.
 *
 ****************************************************************************/

ssize_t mq_timedreceive(mqd_t mqdes, FAR char *msg, size_t msglen,
                        FAR unsigned int *prio,
                        FAR const struct timespec *abstime)
{
  int ret;

  /* mq_timedreceive() is a cancellation point */

  enter_cancellation_point();

  /* Let nxmq_timedreceive do all of the work */

  ret = nxmq_timedreceive(mqdes, msg, msglen, prio, abstime);
  if (ret < 0)
    {
      set_errno(-ret);
      ret = ERROR;
    }

  leave_cancellation_point();
  return ret;
}

/****************************************************************************
 * Name: file_mq_receive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages
 *   from the message queue specified by "mq."  This is an internal OS
 *   interface.  It is functionally equivalent to mq_receive except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 *  See comments with mq_receive() for a more complete description of the
 *  behavior of this function
 *
 * Input Parameters:
 *   mq     - Message Queue Descriptor
 *   msg    - Buffer to receive the message
 *   msglen - Size of the buffer in bytes
 *   prio   - If not NULL, the location to store message priority.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   (see mq_receive() for the list list valid return values).
 *
 ****************************************************************************/

ssize_t file_mq_receive(FAR struct file *mq, FAR char *msg, size_t msglen,
                        FAR unsigned int *prio)
{
  return file_mq_timedreceive_internal(mq, msg, msglen, prio, NULL, -1);
}

/****************************************************************************
 * Name: nxmq_receive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages
 *   from the message queue specified by "mqdes."  This is an internal OS
 *   interface.  It is functionally equivalent to mq_receive except that:
 *
 *   - It is not a cancellation point, and
 *   - It does not modify the errno value.
 *
 *  See comments with mq_receive() for a more complete description of the
 *  behavior of this function
 *
 * Input Parameters:
 *   mqdes  - Message Queue Descriptor
 *   msg    - Buffer to receive the message
 *   msglen - Size of the buffer in bytes
 *   prio   - If not NULL, the location to store message priority.
 *
 * Returned Value:
 *   This is an internal OS interface and should not be used by applications.
 *   It follows the NuttX internal error return policy:  Zero (OK) is
 *   returned on success.  A negated errno value is returned on failure.
 *   (see mq_receive() for the list list valid return values).
 *
 ****************************************************************************/

ssize_t nxmq_receive(mqd_t mqdes, FAR char *msg, size_t msglen,
                     FAR unsigned int *prio)
{
  FAR struct file *filep;
  ssize_t ret;

  ret = file_get(mqdes, &filep);
  if (ret < 0)
    {
      return ret;
    }

  ret = file_mq_receive(filep, msg, msglen, prio);
  file_put(filep);
  return ret;
}

/****************************************************************************
 * Name: mq_receive
 *
 * Description:
 *   This function receives the oldest of the highest priority messages
 *   from the message queue specified by "mqdes."  If the size of the
 *   buffer in bytes (msglen) is less than the "mq_msgsize" attribute of
 *   the message queue, mq_receive will return an error.  Otherwise, the
 *   selected message is removed from the queue and copied to "msg."
 *
 *   If the message queue is empty and O_NONBLOCK was not set,
 *   mq_receive() will block until a message is added to the message
 *   queue.  If more than one task is waiting to receive a message, only
 *   the task with the highest priority that has waited the longest will
 *   be unblocked.
 *
 *   If the queue is empty and O_NONBLOCK is set, ERROR will be returned.
 *
 * Input Parameters:
 *   mqdes  - Message Queue Descriptor
 *   msg    - Buffer to receive the message
 *   msglen - Size of the buffer in bytes
 *   prio   - If not NULL, the location to store message priority.
 *
 * Returned Value:
 *   On success, the length of the selected message in bytes is returned.
 *   On failure, -1 (ERROR) is returned and the errno is set appropriately:
 *
 *   EAGAIN   The queue was empty, and the O_NONBLOCK flag was set
 *            for the message queue description referred to by 'mqdes'.
 *   EBADF    Message queue opened not opened for reading.
 *   EMSGSIZE 'msglen' was less than the maxmsgsize attribute of the
 *            message queue.
 *   EINTR    The call was interrupted by a signal handler.
 *   EINVAL   Invalid 'msg' or 'mqdes'
 *
 ****************************************************************************/

ssize_t mq_receive(mqd_t mqdes, FAR char *msg, size_t msglen,
                   FAR unsigned int *prio)
{
  int ret;

  /* mq_receive() is a cancellation point */

  enter_cancellation_point();

  /* Let nxmq_receive do all of the work */

  ret = nxmq_receive(mqdes, msg, msglen, prio);
  if (ret < 0)
    {
      set_errno(-ret);
      ret = ERROR;
    }

  leave_cancellation_point();
  return ret;
}
