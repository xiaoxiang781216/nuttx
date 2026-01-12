/****************************************************************************
 * include/sys/socket.h
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

#ifndef __INCLUDE_SYS_SOCKET_H
#define __INCLUDE_SYS_SOCKET_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <sys/types.h>
#include <sys/uio.h>
#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/**
 * @cond
 * The socket()domain parameter specifies a communication domain; this
 * selects the protocol family which will be used for communication.
 */

/* Supported Protocol Families */

#define PF_UNSPEC      0         /* Protocol family unspecified */
#define PF_UNIX        1         /* Local communication */
#define PF_LOCAL       1         /* Local communication */
#define PF_INET        2         /* IPv4 Internet protocols */
#define PF_INET6      10         /* IPv6 Internet protocols */
#define PF_NETLINK    16         /* Netlink IPC socket */
#define PF_ROUTE      PF_NETLINK /* 4.4BSD Compatibility*/
#define PF_PACKET     17         /* Low level packet interface */
#define PF_CAN        29         /* Controller Area Network (SocketCAN) */
#define PF_BLUETOOTH  31         /* Bluetooth sockets */
#define PF_IEEE802154 36         /* Low level IEEE 802.15.4 radio frame interface */
#define PF_VSOCK      40         /* vSockets */
#define PF_PKTRADIO   64         /* Low level packet radio interface */
#define PF_RPMSG      65         /* Remote core communication */

/* Supported Address Families. Opengroup.org requires only AF_UNSPEC,
 * AF_UNIX, AF_INET and AF_INET6.
 */

#define AF_UNSPEC      PF_UNSPEC
#define AF_UNIX        PF_UNIX
#define AF_LOCAL       PF_LOCAL
#define AF_INET        PF_INET
#define AF_INET6       PF_INET6
#define AF_NETLINK     PF_NETLINK
#define AF_ROUTE       PF_ROUTE
#define AF_PACKET      PF_PACKET
#define AF_CAN         PF_CAN
#define AF_BLUETOOTH   PF_BLUETOOTH
#define AF_IEEE802154  PF_IEEE802154
#define AF_VSOCK       PF_VSOCK
#define AF_PKTRADIO    PF_PKTRADIO
#define AF_RPMSG       PF_RPMSG

/* The socket created by socket() has the indicated type, which specifies
 * the communication semantics.
 */

#define SOCK_UNSPEC    0        /* Unspecified socket type */
#define SOCK_STREAM    1        /* Provides sequenced, reliable, two-way,
                                 * connection-based byte streams. An out-of-band data
                                 * transmission mechanism may be supported.
                                 */
#define SOCK_DGRAM     2        /* Supports  datagrams (connectionless, unreliable
                                 * messages of a fixed maximum length).
                                 */
#define SOCK_RAW       3        /* Provides raw network protocol access. */
#define SOCK_RDM       4        /* Provides a reliable datagram layer that does not
                                 * guarantee ordering.
                                 */
#define SOCK_SEQPACKET 5        /* Provides a sequenced, reliable, two-way
                                 * connection-based data transmission path for
                                 * datagrams of fixed maximum length; a consumer is
                                 * required to read an entire packet with each read
                                 * system call.
                                 */
#define SOCK_CTRL      6        /* SOCK_CTRL is the preferred socket type to use
                                 * when we just want a socket for performing driver
                                 * ioctls. This definition is not POSIX compliant.
                                 */
#define SOCK_SMS       7        /* Support SMS(Short Message Service) socket.
                                 * This definition is not POSIX compliant.
                                 */
#define SOCK_PACKET   10        /* Obsolete and should not be used in new programs */

#define SOCK_CLOEXEC  02000000  /* Atomically set close-on-exec flag for the new
                                 * descriptor(s).
                                 */
#define SOCK_NONBLOCK 00004000  /* Atomically mark descriptor(s) as non-blocking. */

#define SOCK_MAX (SOCK_PACKET + 1)
#define SOCK_TYPE_MASK 0xf      /* Mask which covers at least up to SOCK_MASK-1.
                                 * The remaining bits are used as flags.
                                 */

/* Bits in the FLAGS argument to `send', `recv', et al. These are the bits
 * recognized by Linux, not all are supported by NuttX.
 */

#define MSG_OOB          0x000001 /* Process out-of-band data.  */
#define MSG_PEEK         0x000002 /* Peek at incoming messages.  */
#define MSG_DONTROUTE    0x000004 /* Don't use local routing.  */
#define MSG_CTRUNC       0x000008 /* Control data lost before delivery.  */
#define MSG_PROXY        0x000010 /* Supply or ask second address.  */
#define MSG_TRUNC        0x000020
#define MSG_DONTWAIT     0x000040 /* Enable nonblocking IO.  */
#define MSG_EOR          0x000080 /* End of record.  */
#define MSG_WAITALL      0x000100 /* Wait for a full request.  */
#define MSG_FIN          0x000200
#define MSG_SYN          0x000400
#define MSG_CONFIRM      0x000800 /* Confirm path validity.  */
#define MSG_RST          0x001000
#define MSG_ERRQUEUE     0x002000 /* Fetch message from error queue.  */
#define MSG_NOSIGNAL     0x004000 /* Do not generate SIGPIPE.  */
#define MSG_MORE         0x008000 /* Sender will send more.  */
#define MSG_CMSG_CLOEXEC 0x100000 /* Set close_on_exit for file
                                   * descriptor received through SCM_RIGHTS.
                                   */

/* Protocol levels supported by get/setsockopt(): */

#define SOL_SOCKET       1 /* Only socket-level options supported */

/* Socket-level options */

#define SO_REUSEADDR     0 /* Allow reuse of local addresses (get/set)
                            * arg: pointer to integer containing a boolean
                            * value
                            */
#define SO_BROADCAST     1 /* Permits sending of broadcast messages (get/set).
                            * arg: pointer to integer containing a boolean
                            * value
                            */
#define SO_DEBUG         2 /* Enables recording of debugging information
                            * (get/set).
                            * arg: pointer to integer containing a boolean
                            * value
                            */
#define SO_DONTROUTE     3 /* Requests that outgoing messages bypass standard
                            * routing (get/set)
                            * arg: pointer to integer containing a boolean
                            * value
                            */
#define SO_TIMESTAMP     4 /* Generates a timestamp in us for each incoming packet
                            * arg: integer value
                            */
#define SO_KEEPALIVE     5 /* Keeps connections active by enabling the periodic
                            * transmission of messages (get/set).
                            * arg:  pointer to integer containing a boolean int
                            * value
                            */
#define SO_LINGER        6 /* Lingers on a close() if data is present (get/set)
                            * arg: struct linger
                            */
#define SO_OOBINLINE     7 /* Leaves received out-of-band data (data marked
                            * urgent) inline
                            * (get/set) arg: pointer to integer containing a
                            * boolean value
                            */
#define SO_TIMESTAMPNS   8 /* Generates a timestamp in ns for each incoming packet
                            * arg: integer value
                            */
#define SO_TIMESTAMPING  9 /* Generates timestamp for each output packet
                            */
#define SO_RCVTIMEO     10 /* Sets the timeout value that specifies the maximum
                            * amount of time an input function waits until it
                            * completes (get/set).
                            * arg: struct timeval
                            */
#define SO_SNDTIMEO     11 /* Sets the timeout value specifying the amount of
                            * time that an output function blocks because flow
                            * control prevents data from being sent(get/set).
                            * arg: struct timeval
                            */

#define __SO_PROTOCOL   11 /* Protocol-level socket options may begin with this
                            * value. The current value is determined by the option
                            * in SOL_SOCKET that uses the highest bit, which
                            * is SO_TIMESTAMPNS
                            */

/* Start from 64 to avoid redefinition with other protocol options. */

#define SO_RCVBUF       64 /* Sets receive buffer size.
                            * arg: integer value (get/set).
                            */
#define SO_RCVLOWAT     65 /* Sets the minimum number of bytes to process for
                            * socket input (get/set).
                            * arg: integer value
                            */
#define SO_SNDBUF       66 /* Sets send buffer size (get/set).
                            * arg: integer value
                            */
#define SO_SNDLOWAT     67 /* Sets the minimum number of bytes to process for
                            * socket output (get/set).
                            * arg: integer value
                            */
#define SO_TYPE         68 /* Reports the socket type (get only).
                            * return: int
                            */
#define SO_ERROR        69 /* Reports and clears error status (get only).
                            * arg: returns an integer value
                            */
#define SO_BINDTODEVICE 70 /* Bind this socket to a specific network device.
                            */
#define SO_PEERCRED     71 /* Return the credentials of the peer process
                            * connected to this socket.
                            */
#define SO_PRIORITY     72 /* Set the priority for all packets to be sent on this
                            * socket.  Nuttx uses this value to order the
                            * networking queues: packets with a higher priority
                            * may be processed first depending on the selected
                            * device queueing discipline.
                            */
#define SO_ACCEPTCONN   73 /* Reports whether socket listening is enabled
                            * (get only).
                            * arg: pointer to integer containing a boolean
                            * value
                            */
#define SO_SNDBUFFORCE  74
#define SO_RCVBUFFORCE  75
#define SO_RXQ_OVFL     76

/* Timestamp generation */

#define SOF_TIMESTAMPING_TX_HARDWARE  (1 << SO_TIMESTAMPING)
#define SOF_TIMESTAMPING_TX_SOFTWARE  SOF_TIMESTAMPING_TX_HARDWARE

/* Timestamp reporting */

#define SOF_TIMESTAMPING_SOFTWARE     SOF_TIMESTAMPING_TX_SOFTWARE
#define SOF_TIMESTAMPING_RAW_HARDWARE SOF_TIMESTAMPING_TX_HARDWARE

/* Protocol-level socket operations. */

#define SOL_IP          IPPROTO_IP     /* See options in include/netinet/in.h */
#define SOL_IPV6        IPPROTO_IPV6   /* See options in include/netinet/in.h */
#define SOL_TCP         IPPROTO_TCP    /* See options in include/netinet/in.h */
#define SOL_UDP         IPPROTO_UDP    /* See options in include/netinet/in.h */
#define SOL_RAW         IPPROTO_RAW    /* See options in include/netinet/in.h */
#define SOL_ICMPV6      IPPROTO_ICMPV6 /* See options in include/netinet/in.h */

/* Bluetooth-level operations. */

#define SOL_HCI         0  /* See options in include/netpacket/bluetooth.h */
#define SOL_L2CAP       6  /* See options in include/netpacket/bluetooth.h */
#define SOL_SCO         17 /* See options in include/netpacket/bluetooth.h */
#define SOL_RFCOMM      18 /* See options in include/netpacket/bluetooth.h */

#define SOL_PACKET      19

/* Values for the 'how' argument of shutdown() */

#define SHUT_RD         1 /* Bit 0: Disables further receive operations */
#define SHUT_WR         2 /* Bit 1: Disables further send operations */
#define SHUT_RDWR       3 /* Bits 0+1: Disables further send and receive
                           * operations
                           */

/* The maximum backlog queue length */

#ifdef CONFIG_NET_TCPBACKLOG_CONNS
#  define SOMAXCONN CONFIG_NET_TCPBACKLOG_CONNS
#else
#  define SOMAXCONN 8
#endif

/* Definitions associated with sendmsg/recvmsg */

#define CMSG_NXTHDR(mhdr, cmsg) cmsg_nxthdr((mhdr), (cmsg))

#define CMSG_ALIGN(len) \
  (((len) + sizeof(long) - 1) & ~(sizeof(long) - 1))
#define CMSG_DATA(cmsg) \
  ((FAR void *)((FAR char *)(cmsg) + CMSG_ALIGN(sizeof(struct cmsghdr))))
#define CMSG_SPACE(len) \
  (CMSG_ALIGN(sizeof(struct cmsghdr)) + CMSG_ALIGN(len))
#define CMSG_LEN(len)   \
  (CMSG_ALIGN(sizeof(struct cmsghdr)) + (len))

#define __CMSG_FIRSTHDR(ctl, len) \
  ((len) >= sizeof(struct cmsghdr) ? (FAR struct cmsghdr *)(ctl) : NULL)
#define CMSG_FIRSTHDR(msg) \
  __CMSG_FIRSTHDR((msg)->msg_control, (msg)->msg_controllen)
#define CMSG_OK(mhdr, cmsg) ((cmsg)->cmsg_len >= sizeof(struct cmsghdr) && \
                            (cmsg)->cmsg_len <= (unsigned long) \
                            ((mhdr)->msg_controllen - \
                             ((FAR char *)(cmsg) - (FAR char *)(mhdr)->msg_control)))
#define for_each_cmsghdr(cmsg, msg) \
       for (cmsg = CMSG_FIRSTHDR(msg); \
            cmsg; \
            cmsg = CMSG_NXTHDR(msg, cmsg))

/* "Socket"-level control message types: */

#define SCM_RIGHTS      0x01    /* rw: access rights (array of int) */
#define SCM_CREDENTIALS 0x02    /* rw: struct ucred */
#define SCM_SECURITY    0x03    /* rw: security label */
#define SCM_TIMESTAMP   SO_TIMESTAMP

/* Desired design of maximum size and alignment (see RFC2553) */

#define SS_MAXSIZE   128                 /* Implementation-defined maximum size. */
#define SS_ALIGNSIZE (sizeof(uintptr_t)) /* Implementation-defined desired alignment. */

/* Definitions used for sockaddr_storage structure paddings design */
#define SS_PAD1SIZE (SS_ALIGNSIZE - sizeof(sa_family_t))
#define SS_PAD2SIZE (SS_MAXSIZE - (sizeof(sa_family_t) + \
                     SS_PAD1SIZE + SS_ALIGNSIZE))

/* Network socket control */

#define DENY_INET_SOCK_ENABLE  0x01   /* Deny to create INET socket */
#define DENY_INET_SOCK_DISABLE 0x02   /* Not deny to create INET socket */

/****************************************************************************
 * Type Definitions
 ****************************************************************************/

/* sockaddr_storage structure. This structure must be (1) large enough to
 * accommodate all supported protocol-specific address structures, and (2)
 * aligned at an appropriate boundary so that pointers to it can be cast
 * as pointers to protocol-specific address structures and used to access
 * the fields of those structures without alignment problems.
 */

struct aligned_data(SS_ALIGNSIZE) sockaddr_storage
{
  sa_family_t ss_family;       /* Address family */

  /* Following fields are implementation-defined */

  begin_packed_struct struct
  {
    char ss_pad1[SS_PAD1SIZE]; /* 6-byte pad; this is to make implementation-defined
                                * pad up to alignment field that follows explicit in
                                * the data structure */
    int64_t ss_align;          /* Field to force desired structure storage alignment */
    char ss_pad2[SS_PAD2SIZE]; /* 112-byte pad to achieve desired size, SS_MAXSIZE
                                * value minus size of ss_family ss_pad1, ss_align
                                * fields is 112. */
  }
  end_packed_struct ss_data[1];
};

/* The sockaddr structure is used to define a socket address which is used
 * in the bind(), connect(), getpeername(), getsockname(), recvfrom(), and
 * sendto() functions.
 */

struct sockaddr
{
  sa_family_t sa_family;       /* Address family: See AF_* definitions */
  char        sa_data[14];     /* 14-bytes data (actually variable length) */
};

/* Used with the SO_LINGER socket option */

struct linger
{
  int l_onoff;                  /* Indicates whether linger option is enabled. */
  int l_linger;                 /* Linger time, in seconds. */
};

struct msghdr
{
  FAR void *msg_name;           /* Socket name */
  socklen_t msg_namelen;        /* Length of name */
  FAR struct iovec *msg_iov;    /* Data blocks */
  unsigned long msg_iovlen;     /* Number of blocks */
  FAR void *msg_control;        /* Per protocol magic (eg BSD file descriptor passing) */
  unsigned long msg_controllen; /* Length of cmsg list */
  unsigned int msg_flags;
};

struct cmsghdr
{
  unsigned long cmsg_len;       /* Data byte count, including hdr */
  int cmsg_level;               /* Originating protocol */
  int cmsg_type;                /* Protocol-specific type */
};

struct ucred
{
  pid_t pid;
  uid_t uid;
  gid_t gid;
};

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

static inline FAR struct cmsghdr *__cmsg_nxthdr(FAR void *__ctl,
                                                unsigned long __size,
                                                FAR struct cmsghdr *__cmsg)
{
  size_t len = CMSG_ALIGN(__cmsg->cmsg_len);
  FAR struct cmsghdr *__ptr =
               (FAR struct cmsghdr *)(((FAR char *)__cmsg) + len);

  if (len < sizeof(*__cmsg) ||
      (unsigned long)((FAR char *)(__ptr + 1) - (FAR char *)__ctl) > __size)
    {
      return NULL;
    }

  return __ptr;
}

static inline FAR struct cmsghdr *cmsg_nxthdr(FAR const struct msghdr *__msg,
                                              FAR struct cmsghdr *__cmsg)
{
  return __cmsg_nxthdr(__msg->msg_control, __msg->msg_controllen, __cmsg);
}

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#undef EXTERN
#if defined(__cplusplus)
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/**
 * @endcond
 * @brief
 *   socket() creates an endpoint for communication and returns a
 *   descriptor.
 *
 * @param  domain   The domain argument specifies a communication domain;
 *     this selects the protocol family which will be used for communication.
 * @param  type     The socket has the indicated type, which specifies
 *     the communication semantics.
 * @param  protocol The protocol specifies a particular protocol to be used
 *     with the socket.
 *
 * @return  A non-negative socket descriptor on success; -1 on error with
 *     errno set appropriately.
 *
 * @retval  EACCES
 *     Permission to create a socket of the specified type and/or protocol
 *     is denied.
 * @retval  EAFNOSUPPORT
 *     The implementation does not support the specified address family.
 * @retval  EINVAL
 *     Unknown protocol, or protocol family not available.
 * @retval  EMFILE
 *     Process file table overflow.
 * @retval  ENFILE
 *     The system limit on the total number of open files has been reached.
 * @retval  ENOBUFS
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 * @retval  ENOMEM
 *     Insufficient memory is available. The socket cannot be created until
 *     sufficient resources are freed.
 * @retval  EPROTONOSUPPORT
 *     The protocol type or the specified protocol is not supported within
 *     this domain.
 */

int socket(int domain, int type, int protocol);

/**
 * @brief
 *   Create an unbound pair of connected sockets in a specified domain, of a
 *   specified type, under the protocol optionally specified by the protocol
 *   argument. The two sockets shall be identical. The file descriptors used
 *   in referencing the created sockets shall be returned in
 *   sv[0] and sv[1].
 *
 * @param  domain   The domain argument specifies a communication domain;
 *     this selects the protocol family which will be used for communication.
 * @param  type     The socket has the indicated type, which specifies
 *     the communication semantics.
 * @param  protocol The protocol specifies a particular protocol to be used
 *     with the socket.
 * @param  sv[2]    The user provided array in which to catch the pair
 *     descriptors
 *
 * @return
 *   On success, zero is returned.  On error, -1 is returned, errno is
 *   set to indicate the error, and sv is left unchanged
 *
 * @retval EAFNOSUPPORT
 *     The specified address family is not supported on this machine.
 * @retval EFAULT
 *     The address sv does not specify a valid part of the process
 *     address space.
 * @retval EMFILE
 *     The per-process limit on the number of open file
 *     descriptors has been reached.
 * @retval ENFILE
 *     The system-wide limit on the total number of open files has
 *     been reached.
 * @retval EOPNOTSUPP
 *     The specified protocol does not support creation of socket
 *     pairs.
 * @retval EPROTONOSUPPORT
 *     The specified protocol is not supported on this machine.
 */

int socketpair(int domain, int type, int protocol, int sv[2]);

/**
 * @brief
 *   bind() Gives the socket sockfd the local address addr.addr is addrlen
 *   bytes long. Traditionally, this is called “assigning a name to a
 *   socket.” When a socket is created with socket(), it exists in a
 *   name space (address family) but has no name assigned.
 *
 * @param sockfd   Socket descriptor from socket.
 * @param addr     Socket local address.
 * @param addrlen  Length of addr.
 *
 * @return  0 on success; -1 on error with errno set appropriately.
 *
 * @retval  EACCES
 *     The address is protected, and the user is not the superuser.
 * @retval  EADDRINUSE
 *     The given address is already in use.
 * @retval  EBADF
 *     sockfd is not a valid descriptor.
 * @retval  EINVAL
 *     The socket is already bound to an address.
 * @retval  ENOTSOCK
 *     sockfd is a descriptor for a file, not a socket.
 */

int bind(int sockfd, FAR const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief
 *   connect() connects the socket referred to by the file descriptor
 *   sockfd to the address specified by addr. The addrlen argument
 *   specifies the size of addr. The format of the address in addr is
 *   determined by the address space of the socket sockfd. If the socket
 *   sockfd is of type SOCK_DGRAM then addr is the address to which
 *   datagrams are sent by default, and the only address from which
 *   datagrams are received. If the socket is of type SOCK_STREAM or
 *   SOCK_SEQPACKET, this call attempts to make a connection to the socket
 *   that is bound to the address specified by addr. Generally,
 *   connection-based protocol sockets may successfully connect() only once;
 *   connectionless protocol sockets may use connect() multiple times to
 *   change their association. Connectionless sockets may dissolve the
 *   association by connecting to an address with the sa_family member of
 *   sockaddr set to AF_UNSPEC.
 *
 * @param sockfd  Socket descriptor returned by socket().
 * @param addr    Server address (form depends on type of socket).
 * @param addrlen Length of actual addr.
 *
 * @return  0 on success; -1 on error with `errno <#ErrnoAccess>`
 *     __ set appropriately.
 *
 * @retval  EACCES or EPERM
 *     The user tried to connect to a broadcast address without having
 *     the socket broadcast flag enabled or the connection request
 *     failed because of a local firewall rule.
 * @retval  EADDRINUSE
 *     Local address is already in use.
 * @retval  EAFNOSUPPORT
 *     The passed address didn’t have the correct address family in its
 *     sa_family field.
 * @retval  EAGAIN
 *     No more free local ports or insufficient entries in the routing
 *     cache. For PF_INET.
 * @retval  EALREADY
 *     The socket is non-blocking and a previous connection attempt has
 *     not yet been completed.
 * @retval  EBADF
 *     The file descriptor is not a valid index in the descriptor table.
 * @retval ECONNREFUSED
 *     No one listening on the remote address.
 * @retval  EFAULT
 *     The socket structure address is outside the user’s address space.
 * @retval  EINPROGRESS
 *     The socket is non-blocking and the connection cannot be completed
 *     immediately.
 * @retval  EINTR
 *     The system call was interrupted by a signal that was caught.
 * @retval  EISCONN
 *     The socket is already connected.
 * @retval  ENETUNREACH
 *     Network is unreachable.
 * @retval ENOTSOCK
 *     The file descriptor is not associated with a socket.
 * @retval  ETIMEDOUT
 *     Timeout while attempting connection. The server may be too busy
 *     to accept new connections.
 */

int connect(int sockfd, FAR const struct sockaddr *addr, socklen_t addrlen);

/**
 * @brief
 *   To accept connections, a socket is first created with socket(), a
 *   willingness to accept incoming connections and a queue limit for
 *   incoming connections are specified with listen(), and then the
 *   connections are accepted with accept(). The listen() call applies only
 *   to sockets of type SOCK_STREAM or SOCK_SEQPACKET.
 *
 * @param sockfd   Socket descriptor of the bound socket.
 * @param backlog  The maximum length the queue of pending connections may
 *     grow. If a connection request arrives with the queue full, the client
 *     may receive an error with an indication of ECONNREFUSED or, if the
 *     underlying protocol supports retransmission, the request may be
 *     ignored so that retries succeed.
 *
 * @return  On success, zero is returned. On error, -1 is returned,
 *     and `errno <#ErrnoAccess>`__ is set appropriately.
 *
 * @retval  EADDRINUSE
 *     The address is protected, and the user is not the superuser.
 * @retval  EBADF
 *     The argument sockfd is not a valid descriptor.
 * @retval  ENOTSOCK
 *     The argument sockfd is not a socket.
 * @retval  EOPNOTSUPP
 *     The socket is not of a type that supports the listen operation.
 */

int listen(int sockfd, int backlog);

/**
 * @brief
 *   The accept() function is used with connection-based socket
 *   types (SOCK_STREAM, SOCK_SEQPACKET and SOCK_RDM). It extracts the
 *   first connection request on the queue of pending connections,
 *   creates a new connected socket with most of the same properties
 *   as sockfd, and allocates a new socket descriptor for the socket,
 *   which is returned. The newly created socket is no longer in the
 *   listening state. The original socket sockfd is unaffected by this
 *   call. Per file descriptor flags are not inherited across an accept.
 *
 *   The sockfd argument is a socket descriptor that has been created
 *   with socket(), bound to a local address with bind(), and is
 *   listening for connections after a call to listen().
 *
 *   On return, the addr structure is filled in with the address of
 *   the connecting entity. The addrlen argument initially contains the
 *   size of the structure pointed to by addr; on return it will contain
 *   the actual length of the address returned.
 *
 *   If no pending connections are present on the queue, and the socket
 *   is not marked as non-blocking, accept blocks the caller until a
 *   connection is present. If the socket is marked non-blocking and no
 *   pending connections are present on the queue, accept returns EAGAIN.
 *
 * @param sockfd  Socket descriptor of the listening socket.
 * @param addr    Receives the address of the connecting client.
 * @param addrlen Input: allocated size of addr, Return: returned size
 *     of addr.
 *
 * @return  Returns -1 on error. If it succeeds, it returns a
 *     non-negative integer that is a descriptor for the accepted socket.
 *
 * @retval  EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and no connections are present
 *     to be accepted.
 * @retval  EBADF
 *     The descriptor is invalid.
 * @retval  ENOTSOCK
 *     The descriptor references a file, not a socket.
 * @retval  EOPNOTSUPP
 *     The referenced socket is not of type SOCK_STREAM.
 * @retval  EINTR
 *     The system call was interrupted by a signal that was caught
 *     before a valid connection arrived.
 * @retval  ECONNABORTED
 *     A connection has been aborted.
 * @retval EINVAL
 *     Socket is not listening for connections.
 * @retval  EMFILE
 *     The per-process limit of open file descriptors has been reached.
 * @retval  ENFILE
 *     The system maximum for file descriptors has been reached.
 * @retval  EFAULT
 *     The addr parameter is not in a writable part of the user
 *     address space.
 * @retval  ENOBUFS or ENOMEM
 *     Not enough free memory.
 * @retval  EPROTO
 *     Protocol error.
 * @retval  EPERM
 *     Firewall rules forbid connection.
 */

int accept(int sockfd, FAR struct sockaddr *addr, FAR socklen_t *addrlen);

/**
 * @cond
 * @brief
 *   The accept4 function is used with connection-based socket types
 *   (SOCK_STREAM, SOCK_SEQPACKET and SOCK_RDM). It extracts the first
 *   connection request on the queue of pending connections, creates a new
 *   connected socket with mostly the same properties as 'sockfd', and
 *   allocates a new socket descriptor for the socket, which is returned.
 *   The newly created socket is no longer in the listening state. The
 *   original socket 'sockfd' is unaffected by this call.  Per file
 *   descriptor flags are not inherited across an accept.
 *
 *   The 'sockfd' argument is a socket descriptor that has been created with
 *   socket(), bound to a local address with bind(), and is listening for
 *   connections after a call to listen().
 *
 *   On return, the 'addr' structure is filled in with the address of the
 *   connecting entity. The 'addrlen' argument initially contains the size
 *   of the structure pointed to by 'addr'; on return it will contain the
 *   actual length of the address returned.
 *
 *   If no pending connections are present on the queue, and the socket is
 *   not marked as non-blocking, accept blocks the caller until a connection
 *   is present. If the socket is marked non-blocking and no pending
 *   connections are present on the queue, accept returns EAGAIN.
 *
 * @param sockfd   The listening socket descriptor
 * @param addr     Receives the address of the connecting client
 * @param addrlen  Input: allocated size of 'addr',
 *     Return: returned size of 'addr'
 * @param flags    The flags used for initialization
 *
 * @return Returns -1 on error. If it succeeds, it returns a non-negative
 *    integer that is a descriptor for the accepted socket.
 *
 * @retval EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and no connections are present to
 *     be accepted.
 * @retval EBADF
 *     The descriptor is invalid.
 * @retval ENOTSOCK
 *     The descriptor references a file, not a socket.
 * @retval EOPNOTSUPP
 *     The referenced socket is not of type SOCK_STREAM.
 * @retval EINTR
 *     The system call was interrupted by a signal that was caught before
 *     a valid connection arrived.
 * @retval ECONNABORTED
 *     A connection has been aborted.
 * @retval EINVAL
 *     Socket is not listening for connections.
 * @retval EMFILE
 *     The per-process limit of open file descriptors has been reached.
 * @retval ENFILE
 *     The system maximum for file descriptors has been reached.
 * @retval EFAULT
 *     The addr parameter is not in a writable part of the user address
 *     space.
 * @retval ENOBUFS or ENOMEM
 *     Not enough free memory.
 * @retval EPROTO
 *     Protocol error.
 * @retval EPERM
 *     Firewall rules forbid connection.
 */

int accept4(int sockfd, FAR struct sockaddr *addr, FAR socklen_t *addrlen,
            int flags);

/**
 * @endcond
 * @brief
 *   The send() call may be used only when the socket is in a connected
 *   state (so that the intended recipient is known). The only difference
 *   between send() and write() is the presence of flags. With zero flags
 *   parameter, send() is equivalent to write(). Also, send(sockfd,buf,len,
 *   flags) is equivalent to sendto(sockfd,buf,len,flags,NULL,0).
 *
 * @param  sockfd   Socket descriptor of the socket
 * @param  buf      Data to send
 * @param  len      Length of data to send
 * @param  flags    Send flags
 *
 * @return On success, returns the number of characters sent. On error,
 *     -1 is returned, and errno is set appropriately.
 *
 * @retval  EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and the requested operation
 *     would block.
 * @retval  EBADF
 *     An invalid descriptor was specified.
 * @retval  ECONNRESET
 *     Connection reset by peer.
 * @retval  EDESTADDRREQ
 *     The socket is not connection-mode, and no peer address is set.
 * @retval  EFAULT
 *      An invalid user space address was specified for a parameter.
 * @retval  EINTR
 *      A signal occurred before any data was transmitted.
 * @retval  EINVAL
 *      Invalid argument passed.
 * @retval  EISCONN
 *     The connection-mode socket was connected already but a recipient
 *     was specified. (Now either this error is returned, or the recipient
 *     specification is ignored.)
 * @retval  EMSGSIZE
 *     The socket type requires that message be sent atomically, and the
 *     size of the message to be sent made this impossible.
 * @retval  ENOBUFS
 *     The output queue for a network interface was full. This generally
 *     indicates that the interface has stopped sending, but may be
 *     caused by transient congestion.
 * @retval  ENOMEM
 *     No memory available.
 * @retval  ENOTCONN
 *     The socket is not connected, and no target has been given.
 * @retval  ENOTSOCK
 *     The argument s is not a socket.
 * @retval  EOPNOTSUPP
 *     Some bit in the flags argument is inappropriate for the socket
 *     type.
 * @retval  EPIPE
 *     The local end has been shut down on a connection oriented socket.
 *     In this case the process will also receive a SIGPIPE unless
 *     MSG_NOSIGNAL is set.
 */

ssize_t send(int sockfd, FAR const void *buf, size_t len, int flags);

/**
 * @brief
 *   If sendto() is used on a connection-mode (SOCK_STREAM, SOCK_SEQPACKET)
 *   socket, the parameters to and 'tolen' are ignored (and the error
 *   EISCONN may be returned when they are not NULL and 0), and the error
 *   ENOTCONN is returned when the socket was not actually connected.
 *
 * @param  sockfd   Socket descriptor of socket
 * @param  buf      Data to send
 * @param  len      Length of data to send
 * @param  flags    Send flags
 * @param  to       Address of recipient
 * @param  tolen    The length of the address structure
 *
 * @return On success, returns the number of characters sent.  On error,
 *     -1 is returned, and errno is set appropriately.
 *
 * @retval  EAGAIN or EWOULDBLOCK
 *     The socket is marked non-blocking and the requested operation
 *     would block.
 * @retval  EBADF
 *     An invalid descriptor was specified.
 * @retval  ECONNRESET
 *     Connection reset by peer.
 * @retval  EDESTADDRREQ
 *     The socket is not connection-mode, and no peer address is set.
 * @retval  EFAULT
 *      An invalid user space address was specified for a parameter.
 * @retval  EINTR
 *      A signal occurred before any data was transmitted.
 * @retval  EINVAL
 *      Invalid argument passed.
 * @retval  EISCONN
 *     The connection-mode socket was connected already but a recipient
 *     was specified. (Now either this error is returned, or the recipient
 *     specification is ignored.)
 * @retval  EMSGSIZE
 *     The socket type requires that message be sent atomically, and the
 *     size of the message to be sent made this impossible.
 * @retval  ENOBUFS
 *     The output queue for a network interface was full. This generally
 *     indicates that the interface has stopped sending, but may be
 *     caused by transient congestion.
 * @retval  ENOMEM
 *     No memory available.
 * @retval  ENOTCONN
 *     The socket is not connected, and no target has been given.
 * @retval  ENOTSOCK
 *     The argument s is not a socket.
 * @retval  EOPNOTSUPP
 *     Some bit in the flags argument is inappropriate for the socket
 *     type.
 * @retval  EPIPE
 *     The local end has been shut down on a connection oriented socket.
 *     In this case the process will also receive a SIGPIPE unless
 *     MSG_NOSIGNAL is set.
 */

ssize_t sendto(int sockfd, FAR const void *buf, size_t len, int flags,
               FAR const struct sockaddr *to, socklen_t tolen);

/**
 * @brief
 *   The recv() call is identical to recvfrom() with a NULL from parameter.
 *
 * @param  sockfd   Socket descriptor of socket
 * @param  buf      Buffer to receive data
 * @param  len      Length of buffer
 * @param  flags    Receive flags
 *
 * @return On success, returns the number of characters received. On error,
 *     -1 is returned, and errno is set appropriately.
 */

ssize_t recv(int sockfd, FAR void *buf, size_t len, int flags);

/**
 * @brief
 *   recvfrom() receives messages from a socket, and may be used to receive
 *   data on a socket whether or not it is connection-oriented.
 *
 *   If from is not NULL, and the underlying protocol provides the source
 *   address, this source address is filled in. The argument fromlen
 *   initialized to the size of the buffer associated with from, and
 *   modified on return to indicate the actual size of the address stored
 *   there.
 *
 * @param  sockfd    Socket descriptor of socket
 * @param  buf       Buffer to receive data
 * @param  len       Length of buffer
 * @param  flags     Receive flags
 * @param  from      Address of source (may be NULL)
 * @param  fromlen   The length of the address structure
 *
 * @return On success, returns the number of characters received. On error,
 *     -1 is returned, and errno is set appropriately.
 *
 * @retval  EAGAIN
 *     The socket is marked non-blocking and the receive operation would
 *     block, or a receive timeout had been set and the timeout expired
 *     before data was received.
 * @retval  EBADF
 *     The argument sockfd is an invalid descriptor.
 * @retval  ECONNREFUSED
 *     A remote host refused to allow the network connection (typically
 *     because it is not running the requested service).
 * @retval  EFAULT
 *     The receive buffer pointer(s) point outside the process's address
 *     space.
 * @retval  EINTR
 *     The receive was interrupted by delivery of a signal before any data
 *     were available.
 * @retval  EINVAL
 *     Invalid argument passed.
 * @retval  ENOMEM
 *     Could not allocate memory.
 * @retval  ENOTCONN
 *     The socket is associated with a connection-oriented protocol and has
 *     not been connected.
 * @retval  ENOTSOCK
 *     The argument sockfd does not refer to a socket.
 */

ssize_t recvfrom(int sockfd, FAR void *buf, size_t len, int flags,
                 FAR struct sockaddr *from, FAR socklen_t *fromlen);

/**
 * @brief
 *   The shutdown() function will cause all or part of a full-duplex
 *   connection on the socket associated with the file descriptor socket to
 *   be shut down.
 *
 *   The shutdown() function disables subsequent send and/or receive
 *   operations on a socket, depending on the value of the how argument.
 *
 * @param  sockfd - Specifies the file descriptor of the socket.
 * @param  how    - Specifies the type of shutdown. The values are as
 *   follows:
 *     SHUT_RD    - Disables further receive operations.
 *     SHUT_WR    - Disables further send operations.
 *     SHUT_RDWR  - Disables further send and receive operations.
 *
 * @return Upon successful completion, shutdown() will return 0; otherwise,
 *     -1 will be returned and errno set to indicate the error.
 *
 * @retval    EBADF      - The socket argument is not a valid file
 *     descriptor.
 * @retval    EINVAL     - The how argument is invalid.
 * @retval    ENOTCONN   - The socket is not connected.
 * @retval    ENOTSOCK   - The socket argument does not refer to a socket.
 * @retval    ENOBUFS    - Insufficient resources were available in the
 *     system to perform the operation.
 * @retval    EOPNOTSUPP - The operation is not supported for this
 *     socket's protocol
 */

int shutdown(int sockfd, int how);

/**
 * @brief
 *   setsockopt() sets the option specified by the 'option' argument,
 *   at the protocol level specified by the 'level' argument, to the value
 *   pointed to by the 'value' argument for the socket associated with the
 *   file descriptor specified by the 'sockfd' argument.
 *
 *   The 'level' argument specifies the protocol level of the option.  To
 *   set options at the socket level, specify the level argument as
 *   SOL_SOCKET.
 *
 *   See <sys/socket.h> a complete list of values for the socket level
 *   'option' argument.
 *
 *   Protocol level options, such as SOL_TCP, are defined in
 *   protocol-specific header files, for example include/netinet/tcp.h
 *
 * @param  sockfd    Socket descriptor of socket
 * @param  level     Protocol level to set the option
 * @param  option    identifies the option to set
 * @param  value     Points to the argument value
 * @param  value_len The length of the argument value
 *
 * @return 0 on success; -1 on failure.
 *
 * @retval  EBADF
 *     The 'sockfd' argument is not a valid socket descriptor.
 * @retval  EDOM
 *     The send and receive timeout values are too big to fit into the
 *     timeout fields in the socket structure.
 * @retval  EINVAL
 *     The specified option is invalid at the specified socket 'level' or
 *     the socket has been shut down.
 * @retval  EISCONN
 *     The socket is already connected, and a specified option cannot be set
 *     while the socket is connected.
 * @retval  ENOPROTOOPT
 *     The 'option' is not supported by the protocol.
 * @retval  ENOTSOCK
 *     The 'sockfd' argument does not refer to a socket.
 * @retval  ENOMEM
 *     There was insufficient memory available for the operation to
 *     complete.
 * @retval  ENOBUFS
 *     Insufficient resources are available in the system to complete the
 *     call.
 */

int setsockopt(int sockfd, int level, int option,
               FAR const void *value, socklen_t value_len);

/**
 * @brief
 *   getsockopt() retrieve the value for the option specified by the
 *   'option' argument for the socket specified by the 'sockfd' argument. If
 *   the size of the option value is greater than 'value_len', the value
 *   stored in the object pointed to by the 'value' argument will be
 *   silently truncated.
 *   Otherwise, the length pointed to by the 'value_len' argument
 *   will be modified to indicate the actual length of the 'value'.
 *
 *   The 'level' argument specifies the protocol level of the option. To
 *   retrieve options at the socket level, specify the level argument as
 *   SOL_SOCKET; to retrieve options at the TCP-protocol level, the level
 *   argument is SOL_TCP.
 *
 *   See <sys/socket.h> a complete list of values for the socket-level
 *   'option' argument.  Protocol-specific options are are protocol specific
 *   header files (such as netinet/tcp.h for the case of the TCP protocol).
 *
 * @param  sockfd    Socket descriptor of socket
 * @param  level     Protocol level to set the option
 * @param  option    identifies the option to get
 * @param  value     Points to the argument value
 * @param  value_len The length of the argument value
 *
 * @return Returns zero (OK) on success.  On failure, -1 (ERROR) is
 *     returned and the errno variable is set appropriately
 *
 * @retval  EBADF
 *     The 'sockfd' argument is not a valid socket descriptor.
 * @retval  EINVAL
 *     The specified option is invalid at the specified socket 'level' or
 *     the socket has been shutdown.
 * @retval  ENOPROTOOPT
 *     The 'option' is not supported by the protocol.
 * @retval  ENOTSOCK
 *     The 'sockfd' argument does not refer to a socket.
 * @retval  ENOBUFS
 *     Insufficient resources are available in the system to complete the
 *     call.
 */

int getsockopt(int sockfd, int level, int option,
               FAR void *value, FAR socklen_t *value_len);

/**
 * @brief
 *   The getsockname() function retrieves the locally-bound name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * @param  sockfd   Socket descriptor of socket [in]
 * @param  addr     sockaddr structure to receive data [out]
 * @param  addrlen  Length of sockaddr structure [in/out]
 *
 * @return On success, 0 is returned, the 'addr' argument points to the
 *     address of the socket, and the 'addrlen' argument points to the
 *     length of the address. Otherwise, -1 is returned and errno is set
 *     to indicate the error. Possible errno values that may be returned
 *     include:
 *
 * @retval  EBADF      - The socket argument is not a valid file descriptor.
 * @retval  ENOTSOCK   - The socket argument does not refer to a socket.
 * @retval  EOPNOTSUPP - The operation is not supported for this socket's
 *     protocol.
 * @retval  EINVAL     - The socket has been shut down.
 * @retval  ENOBUFS    - Insufficient resources were available in the system
 *     to complete the function.
 */

int getsockname(int sockfd, FAR struct sockaddr *addr,
                FAR socklen_t *addrlen);

/**
 * @brief
 *   The getpeername() function retrieves the remote-connected name of the
 *   specified socket, stores this address in the sockaddr structure pointed
 *   to by the 'addr' argument, and stores the length of this address in the
 *   object pointed to by the 'addrlen' argument.
 *
 *   If the actual length of the address is greater than the length of the
 *   supplied sockaddr structure, the stored address will be truncated.
 *
 *   If the socket has not been bound to a local name, the value stored in
 *   the object pointed to by address is unspecified.
 *
 * @param  sockfd   Socket descriptor of socket [in]
 * @param  addr     sockaddr structure to receive data [out]
 * @param  addrlen  Length of sockaddr structure [in/out]
 *
 * @return On success, 0 is returned, the 'addr' argument points to the
 *     address of the socket, and the 'addrlen' argument points to the
 *     length of the address. Otherwise, -1 is returned and errno is set
 *     to indicate the error. Possible errno values that may be returned
 *     include:
 *
 * @retval  EBADF      - The socket argument is not a valid file descriptor.
 * @retval  ENOTSOCK   - The socket argument does not refer to a socket.
 * @retval  EOPNOTSUPP - The operation is not supported for this socket's
 *     protocol.
 * @retval  EINVAL     - The socket has been shut down.
 * @retval  ENOBUFS    - Insufficient resources were available in the system
 *     to complete the function.
 */

int getpeername(int sockfd, FAR struct sockaddr *addr,
                FAR socklen_t *addrlen);

/**
 * @brief
 *   recvmsg() receives messages from a socket, and may be used to
 *   receive data on a socket whether or not it is connection-oriented.
 *
 * @param  sockfd   Socket descriptor of socket
 * @param  msg      Buffer to receive the message
 * @param  flags    Receive flags
 *
 * @return On success, returns the number of characters received. On error,
 *     -1 is returned, and errno is set appropriately.
 *
 * @retval  EAGAIN
 *     The socket is marked non-blocking and the receive operation would
 *     block, or a receive timeout had been set and the timeout expired
 *     before data was received.
 * @retval  EBADF
 *     The argument sockfd is an invalid descriptor.
 * @retval  ECONNREFUSED
 *     A remote host refused to allow the network connection (typically
 *     because it is not running the requested service).
 * @retval  EFAULT
 *     The receive buffer pointer(s) point outside the process's address
 *     space.
 * @retval  EINTR
 *     The receive was interrupted by delivery of a signal before any data
 *     were available.
 * @retval  EINVAL
 *     Invalid argument passed.
 * @retval  ENOMEM
 *     Could not allocate memory.
 * @retval  ENOTCONN
 *     The socket is associated with a connection-oriented protocol and has
 *     not been connected.
 * @retval  ENOTSOCK
 *     The argument sockfd does not refer to a socket.
 */

ssize_t recvmsg(int sockfd, FAR struct msghdr *msg, int flags);

/**
 * @brief
 *   The sendmsg() call is identical to sendfrom() with a NULL from
 *   parameter.
 *
 * @param  sockfd   Socket descriptor of socket
 * @param  msg      Buffer to receive the message
 * @param  flags    Receive flags
 *
 * @return On success, returns the number of characters received. On error,
 *     -1 is returned, and errno is set appropriately.
 *
 * @retval  EAGAIN
 *     The socket is marked non-blocking and the receive operation would
 *     block, or a receive timeout had been set and the timeout expired
 *     before data was received.
 * @retval  EBADF
 *     The argument sockfd is an invalid descriptor.
 * @retval  ECONNREFUSED
 *     A remote host refused to allow the network connection (typically
 *     because it is not running the requested service).
 * @retval  EFAULT
 *     The receive buffer pointer(s) point outside the process's address
 *     space.
 * @retval  EINTR
 *     The receive was interrupted by delivery of a signal before any data
 *     were available.
 * @retval  EINVAL
 *     Invalid argument passed.
 * @retval  ENOMEM
 *     Could not allocate memory.
 * @retval  ENOTCONN
 *     The socket is associated with a connection-oriented protocol and has
 *     not been connected.
 * @retval  ENOTSOCK
 *     The argument sockfd does not refer to a socket.
 */

ssize_t sendmsg(int sockfd, FAR const struct msghdr *msg, int flags);

#if CONFIG_FORTIFY_SOURCE > 0
fortify_function(send) ssize_t send(int sockfd, FAR const void *buf,
                                    size_t len, int flags)
{
  fortify_assert(len <= fortify_size(buf, 0));
  return __real_send(sockfd, buf, len, flags);
}

fortify_function(sendto) ssize_t sendto(int sockfd, FAR const void *buf,
                                        size_t len, int flags,
                                        FAR const struct sockaddr *to,
                                        socklen_t tolen)
{
  fortify_assert(len <= fortify_size(buf, 0));
  return __real_sendto(sockfd, buf, len, flags, to, tolen);
}

fortify_function(recv) ssize_t recv(int sockfd, FAR void *buf,
                                    size_t len, int flags)
{
  fortify_assert(len <= fortify_size(buf, 0));
  return __real_recv(sockfd, buf, len, flags);
}

fortify_function(recvfrom) ssize_t recvfrom(int sockfd, FAR void *buf,
                                            size_t len, int flags,
                                            FAR struct sockaddr *from,
                                            FAR socklen_t *fromlen)
{
  fortify_assert(len <= fortify_size(buf, 0));
  return __real_recvfrom(sockfd, buf, len, flags, from, fromlen);
}
#endif

#undef EXTERN
#if defined(__cplusplus)
}
#endif

#endif /* __INCLUDE_SYS_SOCKET_H */
