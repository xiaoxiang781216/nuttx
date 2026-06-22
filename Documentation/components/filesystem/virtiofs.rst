=========
virtio-fs
=========

virtio-fs is a guest transport for the :doc:`FUSE <fuse>` core.  Instead of
talking to a local ``/dev/fuse`` server, the guest carries the FUSE protocol
over a virtio-fs device's request virtqueue to a host server (for example
QEMU ``virtiofsd`` or the in-kernel vhost-fs host service).

The guest reuses the same FUSE core (``g_fuse_operations``); only the
transport changes.  ``fs/fuse/fuse_virtio.c`` implements
``fuse_transport_ops`` on top of the virtio request virtqueue.

Architecture
============

::

  guest application (open/read/write ...)
        |
        v
  guest VFS  ->  fs/fuse core  ->  fuse_virtio transport
                                        |  request virtqueue
                                        v
                                host virtio-fs server (virtiofsd / vhost-fs)

The request header and arguments are placed in the out part of a virtqueue
buffer; the reply header and buffers form the in part.  Each request is
submitted with its ``fuse_req_s`` as the virtqueue cookie, and the request
virtqueue completion callback wakes the corresponding waiter.  Submission
takes the connection lock only around the virtqueue enqueue/kick, so multiple
requests stay in flight concurrently.

Mounting
========

::

  mount -t virtiofs -o tag=<name> none /mnt/host

The ``tag=`` option selects the virtio-fs device whose configuration ``tag``
matches.  A hiprio and a request virtqueue are created at probe time.

Configuration
=============

``CONFIG_FS_VIRTIOFS``
  Enable the virtio-fs guest transport and register the ``virtiofs`` mount
  type.  Selects ``CONFIG_DRIVERS_VIRTIO``.  Requires ``CONFIG_FS_FUSE``.
