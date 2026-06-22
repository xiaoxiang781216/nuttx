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

Testing
=======

The ``qemu-armv7a:virtiofs`` configuration runs the guest transport against a
host ``virtiofsd`` (vhost-user virtio-fs) server.  Start the host daemon,
sharing a directory::

    virtiofsd --socket-path=/tmp/vfsd.sock --shared-dir=/path/to/share \
        --sandbox none

Then launch QEMU with a virtio-fs device bound to that socket.  virtio-fs
requires a shared memory backend::

    qemu-system-arm -cpu cortex-a7 -nographic \
        -machine virt,highmem=off,virtualization=off,gic-version=2 \
        -m 256 -object memory-backend-memfd,id=mem,size=256M,share=on \
        -numa node,memdev=mem \
        -chardev socket,id=char0,path=/tmp/vfsd.sock \
        -device vhost-user-fs-device,chardev=char0,tag=myfs,bus=virtio-mmio-bus.0 \
        -global virtio-mmio.force-legacy=false \
        -kernel nuttx

At the NSH prompt, mount and access the host directory::

    nsh> mount -t virtiofs -o tag=myfs none /mnt
    nsh> ls /mnt
    nsh> cat /mnt/<file>

The configuration also enables ``CONFIG_DEVICE_TREE`` (to discover the
virtio-mmio devices from the QEMU device tree) and
``CONFIG_DEV_SIMPLE_ADDRENV`` (for the virtqueue DMA address translation).
