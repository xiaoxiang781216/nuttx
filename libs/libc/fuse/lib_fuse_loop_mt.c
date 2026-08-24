/****************************************************************************
 * libs/libc/fuse/lib_fuse_loop_mt.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
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

#include <poll.h>
#include <pthread.h>
#include <string.h>
#include <sys/eventfd.h>
#include <fuse_lowlevel.h>

#include <nuttx/lib/lib.h>

/****************************************************************************
 * Private Functions
 ****************************************************************************/

 #if !defined(CONFIG_DISABLE_PTHREAD) && defined(CONFIG_EVENT_FD_POLL)
static FAR void *fuse_do_work(FAR void *data)
{
  FAR struct fuse_session *se = data;
  struct fuse_buf buf;
  struct pollfd pfd[2];
  int ret;

  memset(&buf, 0, sizeof(buf));
  memset(pfd, 0, sizeof(pfd));

  pfd[0].fd     = fuse_session_fd(se);
  pfd[0].events = POLLIN;
  pfd[1].fd     = fuse_session_eventfd(se);
  pfd[1].events = POLLIN;

  while (!fuse_session_exited(se))
    {
      /* Indefinite block: wakeups are event driven, no timer and no
       * idle power draw.
       */

      if (poll(pfd, 2, -1) < 0)
        {
          if (get_errno() == EINTR)
            {
              continue;
            }

          break;
        }

      if ((pfd[1].revents & POLLIN) != 0)
        {
          break;
        }

      if ((pfd[0].revents & POLLIN) != 0)
        {
          ret = fuse_session_receive_buf(se, &buf);

          if (ret == -EINTR)
            {
              continue;
            }

          if (ret <= 0)
            {
              break;
            }

          fuse_session_process_buf(se, &buf);
        }
    }

  fuse_buf_free(&buf);
  return NULL;
}

static inline unsigned int
fuse_max_threads(FAR struct fuse_loop_config *config)
{
  return config != NULL && config->max_threads > 0 ?
         config->max_threads : 1;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int fuse_session_loop_mt(FAR struct fuse_session *se,
                         FAR struct fuse_loop_config *config)
{
  unsigned int max_threads = fuse_max_threads(config);
  pthread_t workers[max_threads];
  pthread_attr_t attr;
  unsigned int i;
  int eventfd;

  if (max_threads <= 1)
    {
      return fuse_session_loop(se);
    }

  eventfd = eventfd(0, EFD_CLOEXEC);
  if (eventfd < 0)
    {
      return -get_errno();
    }

  fuse_session_set_eventfd(se, eventfd);

  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, CONFIG_LIBC_FS_FUSE_STACKSIZE);

  /* The calling thread becomes a worker itself, only nthreads - 1
   * extra threads are created.
   */

  for (i = 0; i < max_threads - 1; i++)
    {
      if (pthread_create(&workers[i], &attr, fuse_do_work, se) != 0)
        {
          break;
        }
    }

  pthread_attr_destroy(&attr);

  if (i == 0)
    {
      /* No worker could be started: fall back to the single loop */

      fuse_session_set_eventfd(se, -1);
      close(eventfd);

      return fuse_session_loop(se);
    }

  fuse_do_work(se);

  for (i = 0; i < max_threads - 1; i++)
    {
      pthread_join(workers[i], NULL);
    }

  fuse_session_set_eventfd(-1);
  close(eventfd);

  return 0;
}
#else
int fuse_session_loop_mt(FAR struct fuse_session *se,
                         FAR struct fuse_loop_config *config)
{
  return fuse_session_loop(se);
}
#endif /* !DISABLE_PTHREAD && EVENT_FD_POLL */

FAR struct fuse_loop_config *fuse_loop_cfg_create(void)
{
  FAR struct fuse_loop_config *config;

  config = lib_calloc(1, sizeof(*config));
  if (config == NULL)
    {
      return NULL;
    }

  return config;
}

void fuse_loop_cfg_destroy(FAR struct fuse_loop_config *config)
{
  lib_free(config);
}

void fuse_loop_cfg_set_idle_threads(FAR struct fuse_loop_config *config,
                                    unsigned int value)
{
  config->max_idle_threads = value;
}

void fuse_loop_cfg_set_max_threads(FAR struct fuse_loop_config *config,
                                   unsigned int value)
{
  config->max_threads = value;
}

void fuse_loop_cfg_set_clone_fd(FAR struct fuse_loop_config *config,
                                unsigned int value)
{
  config->clone_fd = value;
}
