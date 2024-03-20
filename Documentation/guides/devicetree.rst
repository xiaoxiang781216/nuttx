====================
Device Tree
====================

Overview
--------

Currently, Nuttx supports to parse FDT(Flattened Device Tree) using libfdt, a
utility library for reading and manipulating the binary format:

https://github.com/dgibson/dtc/

Based on that, Nuttx has implemented some common functions to get properties.
And, Device tree's support in Nuttx is to reduce the configuration of chips/boards,
not used in nuttx kernel framework yet.

How to use
-----------

1. Enable Device tree and libfdt

Enable Kconfig

    .. code-block:: console

      CONFIG_DEVICE_TREE=y                        /* Enable Device Tree */
      CONFIG_LIBFDT=y                             /* Enable utility library */

2. Register DTB address

Use fdt_register to let Nuttx know the dtb Address

3. Parse DTB

Chip/board use fdt_get to get dtb address, and then use fdt_* APIs to prase dtb
properties.
