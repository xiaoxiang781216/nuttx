==============================
FUSE (Filesystem in Userspace)
==============================

NuttX provides a transport-independent FUSE client core under ``fs/fuse``.
The core implements the NuttX ``mountpt_operations`` interface on top of the
standard FUSE wire protocol (kernel ABI 7.38, see ``include/nuttx/fuse.h``)
and dispatches each request through a pluggable transport.

Architecture
============

::

  Application (open/read/write/opendir ...)
        |
        v
      NuttX VFS
        |
        v
  fs/fuse core  (g_fuse_operations + FUSE encode/decode + node walk)
        |  conn->transport->request()
        v
  fuse_transport_ops  --------+------------------------+
        |                     |                        |
        v                     v                        v
  /dev/fuse transport    virtio-fs transport      (other transports)
  (CONFIG_FS_FUSE_DEV)   (CONFIG_FS_VIRTIOFS)

The core owns the filesystem semantics and the request life cycle.  A
transport only moves the FUSE request/reply bytes between the core and a
server.  ``fuse_request()`` computes the request ``unique`` and ``len``,
submits the request through ``conn->transport->request()`` and blocks on a
per-request semaphore until the completion path posts it.

Concurrency
===========

Each in-flight request owns its own ``sem_t``.  The connection lock only
protects request enqueue (transport queue or virtqueue); it never wraps the
full request round trip.  Multiple VFS calls may therefore be in flight
concurrently without serializing on a single global lock.

Transports
==========

The mount type selects the transport through the mount data options:

============  ==================  =================================
Mount type    Option              Transport
============  ==================  =================================
``fuse``      ``fd=<n>``          ``/dev/fuse`` (``CONFIG_FS_FUSE_DEV``)
``virtiofs``  ``tag=<name>``      virtio-fs (``CONFIG_FS_VIRTIOFS``)
============  ==================  =================================

The ``fuse`` mount type is registered by the core.  Enabling the core alone
(``CONFIG_FS_FUSE``) provides no transport, so a mount only succeeds once at
least one transport is configured.

Configuration
=============

``CONFIG_FS_FUSE``
  Enable the transport-independent FUSE core and register the ``fuse`` mount
  type.

See also ``CONFIG_FS_FUSE_DEV`` for the local ``/dev/fuse`` transport and
``CONFIG_FS_VIRTIOFS`` for the virtio-fs transport.

Testing
=======

The ``qemu-armv7a:fuse`` configuration exercises the core together with the
``/dev/fuse`` local transport, the virtio-fs transport and the official
libfuse userspace library (``CONFIG_LIB_FUSE``) on the qemu-armv7a target::

    tools/configure.sh qemu-armv7a:fuse
    make
    qemu-system-arm -M virt -kernel nuttx -nographic

At the NSH prompt run the passthrough test from ``apps/testing/fs/fuse``::

    nsh> fuse_test
    fuse_test: PASSED

The test mounts a libfuse passthrough server over ``/dev/fuse`` backed by a
tmpfs directory and exercises create/write/read/mkdir/readdir/stat/unlink/
rmdir before unmounting.

The same configuration also builds the ported upstream libfuse example
programs (``CONFIG_LIB_FUSE_EXAMPLES``):  ``fuse_hello`` and
``fuse_hello_ll`` mount a minimal read-only filesystem over ``/dev/fuse``,
and ``fuse_cuse`` plus ``fuse_cuse_client`` run a ram-backed userspace
character device over ``/dev/cuse``.
