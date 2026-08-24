====
ext4
====

NuttX supports the ext2/ext3/ext4 family of file systems through an
integration of the `lwext4 <https://github.com/gkostka/lwext4>`_ library
(a portable, small ext2/3/4 implementation suitable for embedded systems).
The library lives under ``fs/lwext4/lwext4`` and is wrapped by the VFS
adapter ``fs/lwext4/lwext4_vfs.c``.

Features
========

- ext2/ext3/ext4 on-disk formats
- Journaling (JBD2) with automatic recovery on mount
- Large file support (files larger than 2 GiB)
- Extended attributes (xattr)
- Read-only mounting for read-only media

Enabling
========

Enable the file system in your configuration::

  CONFIG_FS_LWEXT4=y

The lwext4 sources are fetched automatically at build time when they are not
already present (``git clone`` for the Make build, ``FetchContent`` for the
CMake build), so no manual setup is required. To manage the sources by hand
instead::

  cd nuttx/fs/lwext4
  git clone https://github.com/gkostka/lwext4.git

Configuration options
=====================

All options live in ``fs/lwext4/Kconfig``:

``CONFIG_LWEXT4_BCACHE_BLOCKS``
  Number of cached blocks (default 64). This is the value actually handed to
  the library's dynamic block cache. The cache memory upper bound is this
  count times the filesystem block size. This cache is only valid for
  metadata (inode, extent, directory, bitmap and superblock blocks); file
  data bypasses the cache and is read/written directly to the user buffer.

``CONFIG_LWEXT4_ENABLE_JOURNAL``
  Enable the JBD2 journal for data consistency (default ``y``). Disabling it
  improves performance at the cost of crash safety.

``CONFIG_LWEXT4_MAX_MOUNTPOINTS``
  Maximum number of ext4 file systems mounted simultaneously (default 4).

How the library is configured
-----------------------------

The upstream lwext4 library is configured through preprocessor macros
(``CONFIG_DEBUG_PRINTF``, ``CONFIG_JOURNALING_ENABLE``,
``CONFIG_BLOCK_DEV_CACHE_SIZE`` and so on) that it reads from its own
``ext4_config.h``. That header, when built with ``CONFIG_USE_DEFAULT_CFG=0``,
includes ``fs/lwext4/generated/ext4_config.h`` (resolved from the
``fs/lwext4`` include path) before applying its own guarded defaults.

``generated/ext4_config.h`` maps the NuttX Kconfig options onto those
library macros in one place, so the mapping lives in a single header rather
than being duplicated across the Make and CMake build files.

Debug verbosity follows the standard NuttX file-system debug levels:
``CONFIG_DEBUG_FS_INFO`` / ``CONFIG_DEBUG_FS_WARN`` / ``CONFIG_DEBUG_FS_ERROR``
turn on ``CONFIG_DEBUG_PRINTF`` inside the library; the runtime verbosity
mask is controlled at run time through ``ext4_dmask_set()``. When no
file-system debug level is enabled, ``ext4_dbg()`` is compiled out entirely.

Usage
=====

Mounting
--------

The file-system type name is ``ext4``:

.. code-block:: c

  #include <sys/mount.h>

  /* Mount an ext4 file system from /dev/mmcsd0 at /mnt/sdcard */

  ret = mount("/dev/mmcsd0", "/mnt/sdcard", "ext4", 0, NULL);
  if (ret < 0)
    {
      printf("Mount failed: %d\n", errno);
    }

Mount options
-------------

The ``data`` argument of ``mount()`` accepts a comma-separated option string:

``ro``
  Mount the file system read-only.

``autoformat``
  If the initial mount fails because the on-device data is invalid or damaged,
  format the device as ext4 and retry the mount.

``forceformat``
  Always format the device as ext4 before mounting.

.. code-block:: c

  /* Read-only mount */

  mount("/dev/mmcsd0", "/mnt/sdcard", "ext4", 0, "ro");

  /* Auto-format invalid storage, then mount */

  mount("/dev/mmcsd0", "/mnt/sdcard", "ext4", 0, "autoformat");

  /* Force a reformat before mounting */

  mount("/dev/mmcsd0", "/mnt/sdcard", "ext4", 0, "forceformat");

Once mounted, the file system is used through the standard POSIX APIs
(``open``, ``read``, ``write``, ``lseek``, ``mkdir``, ``opendir``,
``readdir``, ``unlink``, ``rename``, ``stat``, ``statfs``, ``umount`` ...).

Architecture
============

::

  Application (POSIX API: open/read/write/mkdir ...)
          |
          v
  NuttX VFS layer (fs/vfs/)
          |
          v
  lwext4_vfs.c (adapter layer)
          |
          v
  lwext4 library (fs/lwext4/lwext4/src/ext4*.c)
          |
          v
  Block device interface (lwext4 bdev read/write)
          |
          v
  NuttX block device driver

Supported operations
====================

- File: ``open``, ``close``, ``read``, ``write``, ``lseek`` (SEEK_SET /
  SEEK_CUR / SEEK_END), ``ftruncate``, ``fsync``, ``dup``, ``fstat``, basic
  ``ioctl``
- Directory: ``opendir``, ``readdir``, ``rewinddir``, ``closedir``, ``mkdir``,
  ``rmdir``
- File system: ``mount``, ``umount``, ``statfs``, ``unlink``, ``rename``,
  ``stat``, basic ``chstat``
- Block device interface: block read/write, lock/unlock, geometry query and
  sector-ratio handling
- Journal (JBD2) with write-back caching and automatic recovery on mount

Implementation notes
====================

The adapter layer (``lwext4_vfs.c``) maps the lwext4 library onto the NuttX
VFS following the same pattern as the FAT integration.

Path management
  lwext4 addresses files by a registered mount name followed by the path.
  The adapter derives a unique mount name per mount point from its object
  address (``/nx<addr>/``) and rewrites the NuttX relative path into that
  form on every call.

Error handling
  lwext4 error codes are translated to POSIX ``errno`` values at every entry
  point so applications see standard errors.

Memory management
  File and directory state is allocated dynamically through the file-system
  heap (``fs_heap_malloc``/``fs_heap_free``). The metadata block cache holds
  a configurable number of blocks (``CONFIG_LWEXT4_BCACHE_BLOCKS``); file
  data bypasses the cache.

Locking
  Each mount point owns a recursive mutex, installed into lwext4 through
  ``ext4_mount_setup_locks()`` with the mount point itself as the callback
  context. The library serializes all operations on that mount through its
  own lock, so different mount points never block each other.

NuttX integration
=================

The type is registered like any other block-device file system:

- ``include/sys/statfs.h`` defines ``EXT4_SUPER_MAGIC`` (``0xef53``), returned
  by ``lwext4_statfs()`` in ``f_type``.
- ``fs/mount/fs_mount.c`` adds ``CONFIG_FS_LWEXT4`` to ``BDFS_SUPPORT`` and
  registers ``{ "ext4", &g_lwext4_operations }`` in ``g_bdfsmap[]``, so
  ``mount()`` accepts the ``"ext4"`` type string.
- ``fs/mount/fs_gettype.c`` maps ``EXT4_SUPER_MAGIC`` back to the ``"ext4"``
  string for ``/proc/mounts``, ``df`` and ``stat -f``.

Comparison with other file systems
==================================

============= ============ ========= ==========
Feature       ext4         FAT       LittleFS
============= ============ ========= ==========
Journaling    yes          no        yes
Permissions   full         basic     none
Large files   yes (>2 GiB) up to 4 GiB limited
============= ============ ========= ==========

Limitations
===========

- Some advanced ext4 features are not supported.
- Performance is generally lower than a native Linux ext4 implementation.

License
=======

The lwext4 library is distributed under the MIT license. The NuttX
integration code (``lwext4_vfs.c``) is licensed under Apache 2.0.

References
==========

- lwext4 project: https://github.com/gkostka/lwext4
- ext4 specification: https://ext4.wiki.kernel.org/
