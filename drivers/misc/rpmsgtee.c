/****************************************************************************
 * drivers/misc/rpmsgtee.c
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

#include <nuttx/tee.h>

#include <fcntl.h>
#include <netpacket/rpmsg.h>
#include <nuttx/drivers/rpmsgtee.h>
#include <nuttx/kmalloc.h>
#include <nuttx/mutex.h>
#include <sys/mman.h>
#include <sys/param.h>

#include "optee_msg.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Some GlobalPlatform error codes used in this driver */

#define TEEC_SUCCESS                    0x00000000
#define TEEC_ERROR_BAD_PARAMETERS       0xFFFF0006
#define TEEC_ERROR_NOT_SUPPORTED        0xFFFF000A
#define TEEC_ERROR_COMMUNICATION        0xFFFF000E
#define TEEC_ERROR_OUT_OF_MEMORY        0xFFFF000C
#define TEEC_ERROR_BUSY                 0xFFFF000D
#define TEEC_ERROR_SHORT_BUFFER         0xFFFF0010

#define TEEC_ORIGIN_COMMS               0x00000002

#define TEE_IOCTL_PARAM_SIZE(x)         (sizeof(struct tee_ioctl_param) * (x))

#define MAX_IOVEC_NUM                   7
#define MAX_PARAM_NUM                   6

#define RPMSGTEE_SERVER_PATH            "rpmsgtee"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/* The file operation functions */

static int rpmsgtee_open(FAR struct file *filep);
static int rpmsgtee_close(FAR struct file *filep);
static int rpmsgtee_ioctl(FAR struct file *filep, int cmd,
                          unsigned long arg);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* File operations */

static const struct file_operations g_rpmsgtee_ops =
{
  rpmsgtee_open,  /* open */
  rpmsgtee_close, /* close */
  NULL,           /* read */
  NULL,           /* write */
  NULL,           /* seek */
  rpmsgtee_ioctl, /* ioctl */
  NULL,           /* mmap */
  NULL,           /* truncate */
  NULL            /* poll */
};

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsgtee_open
 *
 * Description:
 *   Rpmsg-tee open operation
 *
 * Parameters:
 *   filep  - the file instance
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

static int rpmsgtee_open(FAR struct file *filep)
{
  FAR struct socket *psock;
  FAR char *remotecpu;
  struct sockaddr_rpmsg addr;
  int ret;

  remotecpu = filep->f_inode->i_private;
  psock = (FAR struct socket *)kmm_zalloc(sizeof(struct socket));
  if (psock == NULL)
    {
      return -ENOMEM;
    }

  ret = psock_socket(AF_RPMSG, SOCK_STREAM, 0, psock);
  if (ret < 0)
    {
      kmm_free(psock);
      return ret;
    }

  memset(&addr, 0, sizeof(addr));
  addr.rp_family = AF_RPMSG;
  strlcpy(addr.rp_name, RPMSGTEE_SERVER_PATH, sizeof(addr.rp_name));
  strlcpy(addr.rp_cpu, remotecpu, sizeof(addr.rp_cpu));
  ret = psock_connect(psock, (FAR const struct sockaddr *)&addr,
                      sizeof(addr));
  if (ret < 0)
    {
      psock_close(psock);
      kmm_free(psock);
      return ret;
    }

  filep->f_priv = psock;
  return 0;
}

/****************************************************************************
 * Name: rpmsgtee_close
 *
 * Description:
 *   Rpmsg-tee close operation
 *
 * Parameters:
 *   filep  - the file instance
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

static int rpmsgtee_close(FAR struct file *filep)
{
  FAR struct socket *psock = filep->f_priv;

  psock_close(psock);
  kmm_free(psock);
  return 0;
}

static int rpmsgtee_to_msg_param(FAR struct optee_msg_param *mparams,
                                 size_t num_params,
                                 FAR const struct tee_ioctl_param *params)
{
  size_t n;

  for (n = 0; n < num_params; n++)
    {
      FAR const struct tee_ioctl_param *p = params + n;
      FAR struct optee_msg_param *mp = mparams + n;

      if (p->attr & ~TEE_IOCTL_PARAM_ATTR_MASK)
        {
          return -EINVAL;
        }

      switch (p->attr & TEE_IOCTL_PARAM_ATTR_TYPE_MASK)
        {
          case TEE_IOCTL_PARAM_ATTR_TYPE_NONE:
            mp->attr = OPTEE_MSG_ATTR_TYPE_NONE;
            break;
          case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT:
          case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT:
          case TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INOUT:
            mp->attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT + p->attr -
                       TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT;
            mp->u.value.a = p->a;
            mp->u.value.b = p->b;
            mp->u.value.c = p->c;
            break;
          case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT:
          case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_OUTPUT:
          case TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT:
            mp->attr = OPTEE_MSG_ATTR_TYPE_RMEM_INPUT + p->attr -
                       TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT;
             if (p->c != TEE_MEMREF_NULL)
              {
                mp->u.rmem.shm_ref = p->c;
              }
            else
              {
                mp->u.rmem.shm_ref = 0;
              }

            mp->u.rmem.size = p->b;
            mp->u.rmem.offs = p->a;
            break;
          default:
            return -EINVAL;
        }
    }

  return 0;
}

static int rpmsgtee_from_msg_param(FAR struct tee_ioctl_param *params,
                                   size_t num_params,
                                   FAR const struct optee_msg_param *mparams)
{
  size_t n;

  for (n = 0; n < num_params; n++)
    {
      FAR const struct optee_msg_param *mp = mparams + n;
      FAR struct tee_ioctl_param *p = params + n;

      switch (mp->attr & OPTEE_MSG_ATTR_TYPE_MASK)
        {
          case OPTEE_MSG_ATTR_TYPE_NONE:
            p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_NONE;
            p->a = 0;
            p->b = 0;
            p->c = 0;
            break;
          case OPTEE_MSG_ATTR_TYPE_VALUE_INPUT:
          case OPTEE_MSG_ATTR_TYPE_VALUE_OUTPUT:
          case OPTEE_MSG_ATTR_TYPE_VALUE_INOUT:
            p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_INPUT +
                      mp->attr - OPTEE_MSG_ATTR_TYPE_VALUE_INPUT;
            p->a = mp->u.value.a;
            p->b = mp->u.value.b;
            p->c = mp->u.value.c;
            break;
          case OPTEE_MSG_ATTR_TYPE_RMEM_INPUT:
          case OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT:
          case OPTEE_MSG_ATTR_TYPE_RMEM_INOUT:
            p->attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INPUT +
                      mp->attr - OPTEE_MSG_ATTR_TYPE_RMEM_INPUT;
            p->b = mp->u.rmem.size;
            break;
          default:
            return -EINVAL;
        }
    }

  return 0;
}

static ssize_t rpmsgtee_recv(FAR struct socket *psock, FAR void *msg,
                             size_t size)
{
  size_t remain = size;

  while (remain)
    {
      ssize_t n = psock_recv(psock, msg, remain, 0);
      if (n <= 0)
        {
          return remain == size ? n : size - remain;
        }

      remain -= n;
      msg = (FAR char *)msg + n;
    }

  return size;
}

static int rpmsgtee_send_recv(FAR struct socket *psocket,
                              FAR struct optee_msg_arg *arg)
{
  /* iov[0]: struct opteee_msg_arg + struct optee_msg_param[n]
   * iov[1 - n+1]: shm_mem
   * 0 <= n <= 6
   */

  size_t arg_size = OPTEE_MSG_GET_ARG_SIZE(arg->num_params);
  size_t shm_size[MAX_PARAM_NUM];
  size_t shm_addr[MAX_PARAM_NUM];
  struct iovec iov[MAX_IOVEC_NUM];
  struct msghdr msghdr;
  unsigned long iovlen = 1;
  unsigned long i;
  int ret;

  memset(iov, 0, sizeof(iov));
  memset(shm_size, 0, sizeof(shm_size));

  iov[0].iov_base = arg;
  iov[0].iov_len = arg_size;

  for (i = 0; i < arg->num_params; i++)
    {
      if (arg->params[i].attr == OPTEE_MSG_ATTR_TYPE_RMEM_INPUT ||
          arg->params[i].attr == OPTEE_MSG_ATTR_TYPE_RMEM_INOUT)
        {
          iov[iovlen].iov_base =
            (FAR void *)(uintptr_t)arg->params[i].u.rmem.shm_ref;
          iov[iovlen].iov_len = arg->params[i].u.rmem.size;
          shm_size[i] = arg->params[i].u.rmem.size;
          shm_addr[i] = arg->params[i].u.rmem.shm_ref;
          iovlen++;
        }
      else if (arg->params[i].attr == OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT)
        {
          shm_size[i] = arg->params[i].u.rmem.size;
          shm_addr[i] = arg->params[i].u.rmem.shm_ref;
        }
    }

  memset(&msghdr, 0, sizeof(struct msghdr));
  msghdr.msg_iov = iov;
  msghdr.msg_iovlen = iovlen;

  ret = psock_sendmsg(psocket, &msghdr, 0);
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsgtee_recv(psocket, arg, arg_size);
  if (ret < 0)
    {
      return ret;
    }

  for (i = 0; i < arg->num_params; i++)
    {
      if (arg->params[i].attr == OPTEE_MSG_ATTR_TYPE_RMEM_OUTPUT ||
          arg->params[i].attr == OPTEE_MSG_ATTR_TYPE_RMEM_INOUT)
        {
          size_t size = MIN(arg->params[i].u.rmem.size, shm_size[i]);
          arg->params[i].u.rmem.shm_ref = shm_addr[i];
          ret = rpmsgtee_recv(psocket,
                              (FAR void *)(uintptr_t)
                              arg->params[i].u.rmem.shm_ref, size);
          if (ret < 0)
            {
              return ret;
            }
        }
    }

  return 0;
}

static int rpmsgtee_ioctl_open_session(FAR struct socket *psocket,
                                       FAR struct tee_ioctl_buf_data *buf)
{
  char msg_buf[OPTEE_MSG_GET_ARG_SIZE(MAX_PARAM_NUM)];
  FAR struct tee_ioctl_open_session_arg *arg;
  FAR struct optee_msg_arg *msg;
  int ret;

  if (buf->buf_len > TEE_MAX_ARG_SIZE ||
      buf->buf_len < sizeof(struct tee_ioctl_open_session_arg))
    {
      return -EINVAL;
    }

  arg = (FAR struct tee_ioctl_open_session_arg *)(uintptr_t)buf->buf_ptr;

  if (sizeof(*arg) + TEE_IOCTL_PARAM_SIZE(arg->num_params) !=
      buf->buf_len)
    {
      return -EINVAL;
    }

  if (arg->num_params + 2 > MAX_PARAM_NUM)
    {
      return -EINVAL;
    }

  if (arg->clnt_login >= TEE_IOCTL_LOGIN_REE_KERNEL_MIN &&
      arg->clnt_login <= TEE_IOCTL_LOGIN_REE_KERNEL_MAX)
    {
      return -EPERM;
    }

  arg->ret = TEEC_ERROR_COMMUNICATION;
  arg->ret_origin = TEEC_ORIGIN_COMMS;

  memset(msg_buf, 0, sizeof(msg_buf));
  msg = (FAR struct optee_msg_arg *)&msg_buf[0];

  msg->cmd = OPTEE_MSG_CMD_OPEN_SESSION;
  msg->cancel_id = arg->cancel_id;
  msg->num_params = arg->num_params + 2;

  /* Initialize and add the meta parameters needed when opening a
   * session.
   */

  msg->params[0].attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT |
                        OPTEE_MSG_ATTR_META;
  msg->params[1].attr = OPTEE_MSG_ATTR_TYPE_VALUE_INPUT |
                        OPTEE_MSG_ATTR_META;
  memcpy(&msg->params[0].u.value, arg->uuid, sizeof(arg->uuid));
  msg->params[1].u.value.c = arg->clnt_login;

  ret = rpmsgtee_to_msg_param(msg->params + 2, arg->num_params, arg->params);
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsgtee_send_recv(psocket, msg);
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsgtee_from_msg_param(arg->params, arg->num_params,
                                msg->params + 2);
  if (ret < 0)
    {
      return ret;
    }

  arg->session = msg->session;
  arg->ret = msg->ret;
  arg->ret_origin = msg->ret_origin;

  return ret;
}

static int rpmsgtee_ioctl_invoke(FAR struct socket *psocket,
                                 FAR struct tee_ioctl_buf_data *buf)
{
  char msg_buf[OPTEE_MSG_GET_ARG_SIZE(MAX_PARAM_NUM)];
  FAR struct tee_ioctl_invoke_arg *arg;
  FAR struct optee_msg_arg *msg;
  int ret;

  if (buf->buf_len > TEE_MAX_ARG_SIZE ||
      buf->buf_len < sizeof(struct tee_ioctl_invoke_arg))
    {
      return -EINVAL;
    }

  arg = (FAR struct tee_ioctl_invoke_arg *)(uintptr_t)buf->buf_ptr;

  if (sizeof(*arg) + TEE_IOCTL_PARAM_SIZE(arg->num_params) !=
      buf->buf_len)
    {
      return -EINVAL;
    }

  if (arg->num_params > MAX_PARAM_NUM)
    {
      return -EINVAL;
    }

  arg->ret = TEEC_ERROR_COMMUNICATION;
  arg->ret_origin = TEEC_ORIGIN_COMMS;

  memset(msg_buf, 0, sizeof(msg_buf));
  msg = (FAR struct optee_msg_arg *)&msg_buf[0];

  msg->cmd = OPTEE_MSG_CMD_INVOKE_COMMAND;
  msg->func = arg->func;
  msg->session = arg->session;
  msg->cancel_id = arg->cancel_id;
  msg->num_params = arg->num_params;

  ret = rpmsgtee_to_msg_param(msg->params, arg->num_params, arg->params);
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsgtee_send_recv(psocket, msg);
  if (ret < 0)
    {
      return ret;
    }

  ret = rpmsgtee_from_msg_param(arg->params, arg->num_params, msg->params);
  if (ret < 0)
    {
      return ret;
    }

  arg->ret = msg->ret;
  arg->ret_origin = msg->ret_origin;

  return ret;
}

static int
rpmsgtee_ioctl_close_session(FAR struct socket *psocket,
                             FAR struct tee_ioctl_close_session_arg *arg)
{
  struct optee_msg_arg msg;

  memset(&msg, 0, sizeof(struct optee_msg_arg));
  msg.cmd = OPTEE_MSG_CMD_CLOSE_SESSION;
  msg.session = arg->session;
  msg.num_params = 0;

  return rpmsgtee_send_recv(psocket, &msg);
}

static int rpmsgtee_ioctl_version(FAR struct tee_ioctl_version_data *vers)
{
  vers->impl_id = TEE_IMPL_ID_OPTEE;
  vers->impl_caps = TEE_OPTEE_CAP_TZ;
  vers->gen_caps = TEE_GEN_CAP_GP | TEE_GEN_CAP_MEMREF_NULL;
  return 0;
}

static int rpmsgtee_ioctl_cancel(FAR struct socket *psocket,
                                 FAR struct tee_ioctl_cancel_arg *arg)
{
  struct optee_msg_arg msg;

  memset(&msg, 0, sizeof(struct optee_msg_arg));
  msg.cmd = OPTEE_MSG_CMD_CANCEL;
  msg.session = arg->session;
  msg.cancel_id = arg->cancel_id;
  return rpmsgtee_send_recv(psocket, &msg);
}

static int
rpmsgtee_ioctl_shm_alloc(FAR struct tee_ioctl_shm_alloc_data *data)
{
  int memfd = memfd_create(RPMSGTEE_SERVER_PATH, O_CREAT);

  if (memfd < 0)
    {
      return -errno;
    }

  if (ftruncate(memfd, data->size) < 0)
    {
      close(memfd);
      return -errno;
    }

  data->id = (uintptr_t)mmap(NULL, data->size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, memfd, 0);
  if (data->id == (uintptr_t)MAP_FAILED)
    {
      return -errno;
    }

  return memfd;
}

/****************************************************************************
 * Name: rpmsgtee_ioctl
 *
 * Description:
 *   Rpmsg-tee ioctl operation
 *
 * Parameters:
 *   filep  - the file instance
 *   cmd    - the ioctl command
 *   arg    - the ioctl arguments
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

static int rpmsgtee_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  FAR struct socket *psock = filep->f_priv;
  FAR void *parg = (FAR void *)arg;

  switch (cmd)
    {
      case TEE_IOC_VERSION:
        return rpmsgtee_ioctl_version(parg);
      case TEE_IOC_OPEN_SESSION:
        return rpmsgtee_ioctl_open_session(psock, parg);
      case TEE_IOC_INVOKE:
        return rpmsgtee_ioctl_invoke(psock, parg);
      case TEE_IOC_CLOSE_SESSION:
        return rpmsgtee_ioctl_close_session(psock, parg);
      case TEE_IOC_CANCEL:
        return rpmsgtee_ioctl_cancel(psock, parg);
      case TEE_IOC_SHM_ALLOC:
        return rpmsgtee_ioctl_shm_alloc(parg);
      default:
        return -ENOTTY;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: rpmsgtee_register
 *
 * Description:
 *   Rpmsg-tee client initialize function, the client cpu should call
 *   this function in the board initialize process.
 *
 * Parameters:
 *   remotecpu  - the server cpu name
 *   localpath  - the tee path in local cpu,
 *
 * Returned Values:
 *   OK on success; A negated errno value is returned on any failure.
 *
 ****************************************************************************/

int rpmsgtee_register(FAR const char *remotecpu, FAR const char *devpath)
{
  /* Arguments check */

  if (remotecpu == NULL || devpath == NULL)
    {
      return -EINVAL;
    }

  return register_driver(devpath, &g_rpmsgtee_ops, 0666,
                         (FAR void *)remotecpu);
}
