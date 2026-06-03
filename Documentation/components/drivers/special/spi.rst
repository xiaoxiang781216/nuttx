==================
SPI Device Drivers
==================

-  ``include/nuttx/spi/spi.h``. All structures and APIs needed
   to work with SPI drivers are provided in this header file.

-  ``struct spi_ops_s``. Each SPI device driver must implement
   an instance of ``struct spi_ops_s``. That structure defines a
   call table with the following methods:

-  **Binding SPI Drivers**. SPI drivers are not normally directly
   accessed by user code, but are usually bound to another, higher
   level device driver. See for example,
   ``int mmcsd_spislotinitialize(int minor, int slotno, FAR struct spi_dev_s *spi)``
   in ``drivers/mmcsd/mmcsd_spi.c``. In general, the binding
   sequence is:

   #. Get an instance of ``struct spi_dev_s`` from the
      hardware-specific SPI device driver, and
   #. Provide that instance to the initialization method of the
      higher level device driver.

-  **Examples**: ``drivers/loop.c``,
   ``drivers/mmcsd/mmcsd_spi.c``, ``drivers/ramdisk.c``, etc.

RPMsg SPI
=========

NuttX can expose an SPI controller across a RPMsg link. Enable
``CONFIG_SPI_RPMSG`` on the client side and ``CONFIG_SPI_RPMSG_SERVER`` on
the side that owns the real SPI bus. ``CONFIG_SPI_EXCHANGE`` is also required.

The client creates a normal ``struct spi_dev_s`` instance with
``spi_rpmsg_initialize(cpuname, name)``. The server exports a local SPI
controller with ``spi_rpmsg_server_initialize(name, spi)``. Board code may
then pass the client instance to device drivers, or register it with
``spi_register()`` so NSH tools can access it as ``/dev/spiN``.

RPMsg is only the transport. SPI transfers use a private RPMsg payload
format carrying the selected chip, mode, bits per word,
frequency, delay, transmit data, receive data, and transfer result.

Bus locking never crosses the link: every message executes atomically on
the server, and an ``spi_transfer()`` sequence that fits into one RPMsg
buffer is forwarded as a single message.

Native VirtIO SPI
=================

The same SPI lower-half can be exposed over a native VirtIO transport instead
of RPMsg. ``CONFIG_DRIVERS_VIRTIO_SPI`` enables the frontend (client), which
probes a VirtIO SPI device (device id 45), reads its configuration space and
exposes a ``struct spi_dev_s`` retrievable with ``virtio_spi_get_master(index)``.
As for the RPMsg client, board code may then pass it to device drivers or
register it with ``spi_register()`` as ``/dev/spiN``.
``CONFIG_DRIVERS_VHOST_SPI``
enables the device-side backend (server), which serves the request virtqueue
and drives a local SPI controller registered with ``vhost_spi_register()``.

The frontend and backend share the Linux-compatible VirtIO SPI wire-format
definitions in ``include/nuttx/virtio/virtio-spi.h``. An ``spi_transfer()``
sequence executes as one bus-locked run of requests, with CS held between
transfers that do not de-select. The frontend can be
validated against a host vhost-user backend, or against a second NuttX
instance running the vhost backend over rptun/IVSHMEM.
