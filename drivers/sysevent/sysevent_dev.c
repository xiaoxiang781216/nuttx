/****************************************************************************
 * drivers/sysevent/sysevent_dev.c
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
#include <nuttx/kmalloc.h>
#include <nuttx/fs/fs.h>
#include <sched.h>
#include <poll.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/param.h>
#include <debug.h>

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct kfifo_s
{
  unsigned int  in;
  unsigned int  out;
  unsigned int  size;
  FAR char      *data;
};

struct sysevent_dev_s
{
  struct kfifo_s    fifo;
  FAR mutex_t       lock;
  FAR struct pollfd *pfd;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static ssize_t sysevent_dev_read(FAR struct file *filep, FAR char *buffer,
                                 size_t len);
static ssize_t sysevent_dev_write(FAR struct file *filep,
                                  FAR const char *buffer, size_t len);
static int sysevent_dev_poll(FAR struct file *filep, FAR struct pollfd *fds,
                             bool setup);

/****************************************************************************
 * Private Data
 ****************************************************************************/

struct sysevent_dev_s *sysevent_dev;

static const struct file_operations g_sysevent_dev_fops =
{
  NULL,               /* open */
  NULL,               /* close */
  sysevent_dev_read,  /* read */
  sysevent_dev_write, /* write */
  NULL,               /* seek */
  NULL,               /* ioctl */
  NULL,               /* mmap */
  NULL,               /* truncate */
  sysevent_dev_poll   /* poll */
};

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline unsigned int roundup_pow_of_two(unsigned int n)
{
  return 1 << fls(n - 1);
}

static inline unsigned int kfifo_avail(FAR struct kfifo_s *fifo)
{
  return (fifo->size - (fifo->in - fifo->out));
}

static inline unsigned int kfifo_used(FAR struct kfifo_s *fifo)
{
  return (fifo->in - fifo->out);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static int kfifo_alloc(FAR struct kfifo_s *fifo, unsigned int size)
{
  fifo->in = 0;
  fifo->out = 0;

  if (size < 2)
    {
      fifo->data = NULL;
      fifo->size = 0;
      return -EINVAL;
    }

  size = roundup_pow_of_two(size);
  fifo->data = kmm_zalloc(size);
  if (fifo->data == NULL)
    {
      fifo->size = 0;
      return -ENOMEM;
    }

  fifo->size = size;
  return 0;
}

static unsigned int kfifo_in(FAR struct kfifo_s *fifo, FAR const void *buf,
                             unsigned int len)
{
  unsigned int offs;
  unsigned int copy;
  unsigned int l;

  offs = fifo->in & (fifo->size - 1);
  copy = MIN(len, kfifo_avail(fifo));
  l = MIN(len, fifo->size - offs);

  memcpy(fifo->data + offs, buf, l);
  memcpy(fifo->data, buf + l, copy - l);

  fifo->in += copy;
  return copy;
}

static unsigned int kfifo_out(FAR struct kfifo_s *fifo, FAR void *buf,
                              unsigned int len)
{
  unsigned int offs;
  unsigned int copy;
  unsigned int l;

  offs = fifo->out & (fifo->size - 1);
  copy = MIN(len, kfifo_used(fifo));
  l = MIN(len, fifo->size - offs);

  memcpy(buf, fifo->data + offs, l);
  memcpy(buf + l, fifo->data, copy - l);

  fifo->out += copy;
  return copy;
}

static void kfifo_free(FAR struct kfifo_s *fifo)
{
  kmm_free(fifo->data);
  fifo->in = 0;
  fifo->out = 0;
  fifo->data = NULL;
  fifo->size = 0;
}

/****************************************************************************
 * Name: sysevent_dev_read
 *
 * Description:
 *   Read a sysevent string from sysevent device.
 *
 ****************************************************************************/

static ssize_t sysevent_dev_read(FAR struct file *filep, FAR char *buffer,
                                 size_t len)
{
  int ret = 0;
  int fifo_len;
  int tmp_out_size;

  DEBUGASSERT(filep != NULL && buffer != NULL && len > 0);
  nxmutex_lock(&sysevent_dev->lock);

  fifo_len = kfifo_used(&sysevent_dev->fifo);
  len = MIN(len, fifo_len);
  if (len > 0)
    {
      if (kfifo_out(&sysevent_dev->fifo, &tmp_out_size, sizeof(int)) > 0)
        {
          ret = kfifo_out(&sysevent_dev->fifo, buffer, tmp_out_size);
          if (ret == 0)
            {
              nxmutex_unlock(&sysevent_dev->lock);
              _err("copy_to_user failed tmp_out_size is %d", tmp_out_size);
              return -EFAULT;
            }
        }
    }

  nxmutex_unlock(&sysevent_dev->lock);
  return ret;
}

/****************************************************************************
 * Name: sysevent_dev_write
 *
 * Description:
 *   Write a sysevent string to sysevent device.
 *
 ****************************************************************************/

static ssize_t sysevent_dev_write(FAR struct file *filep,
                                  FAR const char *buffer, size_t len)
{
  int ret;

  DEBUGASSERT(filep != NULL && buffer != NULL && len > 0);
  ret = 0;
  nxmutex_lock(&sysevent_dev->lock);

  if (kfifo_avail(&sysevent_dev->fifo) < len + sizeof(int))
    {
      _err("kfifo avail space is not enough");
      goto out;
    }

  /* Write event len first, then write the event string */

  if (kfifo_in(&sysevent_dev->fifo, &len, sizeof(int)) > 0)
    {
      ret = kfifo_in(&sysevent_dev->fifo, buffer, len);
      if (ret > 0)
        {
          poll_notify(&sysevent_dev->pfd, 1, POLLIN);
        }
    }

out:
  nxmutex_unlock(&sysevent_dev->lock);
  return ret;
}

/****************************************************************************
 * Name: sysevent_dev_poll
 *
 * Description:
 *   Poll sysevent device.
 *
 ****************************************************************************/

static int sysevent_dev_poll(FAR struct file *filep, FAR struct pollfd *fds,
                             bool setup)
{
  FAR struct inode *inode;
  FAR struct sysevent_dev_s *dev;
  int ret;

  _info("setup: %d\n", (int)setup);

  DEBUGASSERT(filep != NULL && fds != NULL);
  inode = filep->f_inode;

  DEBUGASSERT(inode != NULL && inode->i_private != NULL);
  dev = inode->i_private;

  /* Exclusive access */

  ret = nxmutex_lock(&dev->lock);
  if (ret < 0)
    {
      return ret;
    }

  /* Ignore waits that do not include POLLIN */

  if ((fds->events & POLLIN) == 0)
    {
      ret = -EDEADLK;
      goto errout;
    }

  /* Are we setting up the poll?  Or tearing it down? */

  if (setup)
    {
      /* Check if we can accept this poll.
       * For now, only one thread can poll the device at any time
       * (shorter / simpler code)
       */

      if (dev->pfd)
        {
          ret = -EBUSY;
          goto errout;
        }

      dev->pfd = fds;

      /* Is there is already data in the fifo? then trigger POLLIN now -
       * don't wait for RX.
       */

      if (kfifo_used(&dev->fifo) > 0)
        {
          poll_notify(&dev->pfd, 1, POLLIN);
        }
    }
  else /* Tear it down */
    {
      dev->pfd = NULL;
    }

errout:
  nxmutex_unlock(&dev->lock);
  return ret;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: write_sysevent_kfifo
 *
 * Description:
 *   Used for kernel modules to write sysevent directly to sysevent kfifo.
 *
 ****************************************************************************/

int write_sysevent_kfifo(char *kbuf, int size)
{
  int ret;

  ret = 0;
  nxmutex_lock(&sysevent_dev->lock);

  if (kfifo_avail(&sysevent_dev->fifo) < size + sizeof(int))
    {
      _err("kfifo avail space is not enough");
      goto out;
    }

  if (kfifo_in(&sysevent_dev->fifo, &size, sizeof(int)) > 0)
    {
      ret = kfifo_in(&sysevent_dev->fifo, kbuf, size);
      if (ret > 0)
        {
          poll_notify(&sysevent_dev->pfd, 1, POLLIN);
        }
    }

out:
  nxmutex_unlock(&sysevent_dev->lock);
  return ret;
}

/****************************************************************************
 * Name: sysevent_dev_init
 *
 * Description:
 *   Initialize sysevent device.
 *
 ****************************************************************************/

int sysevent_dev_init(void)
{
  int ret;

  sysevent_dev = kmm_zalloc(sizeof(struct sysevent_dev_s));
  if (sysevent_dev == NULL)
    {
      _err("Fail to create sysevent_device");
      ret = -ENOMEM;
      goto err;
    }

  if (kfifo_alloc(&sysevent_dev->fifo, CONFIG_SYSEVENT_DEV_FIFO_SIZE) != 0)
    {
      ret = -ENOMEM;
      goto err_dev;
    }

  ret = register_driver(CONFIG_SYSEVENT_DEV_PATH, &g_sysevent_dev_fops, 0666,
                        sysevent_dev);
  if (ret < 0)
    {
      goto err_kfifo;
    }

  nxmutex_init(&sysevent_dev->lock);
  sysevent_dev->pfd = NULL;
  return 0;

err_kfifo:
  kfifo_free(&sysevent_dev->fifo);
err_dev:
  kmm_free(sysevent_dev);
  sysevent_dev = NULL;
err:
  return ret;
}

