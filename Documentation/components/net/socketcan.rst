========================
SocketCAN Device Drivers
========================

- ``include/nuttx/net/netdev.h``. All structures and APIs
  needed to work with drivers are provided in this header file.
  The structure struct net_driver_s defines the interface and is
  passed to the network via ``netdev_register()``.

- ``include/nuttx/can.h``. CAN & CAN FD frame data structures and dlc to len size

  .. code-block:: c

     uint8_t can_bytes2dlc(uint8_t nbytes);
     uint8_t can_dlc2bytes(uint8_t dlc);

- ``int netdev_register(FAR struct net_driver_s *dev, enum net_lltype_e lltype)'``.
  Each driver registers itself by calling ``netdev_register()``.

- ``Include/nuttx/net/can.h``. contains lookup tables for CAN
  dlc to CAN FD len sizes named

- **Initialization sequence is as follows**.

  #. ``xxx_netinitialize(void)`` is called on startup of NuttX in this
     function you call your own init function to initialize your
     CAN driver
  #. In your own init function you create the net_driver_s
     structure set required init values and register the required
     callbacks for SocketCAN
  #. Then you ensure that the CAN interface is in down mode
     (usually done by calling the d_ifdown function)
  #. Register the net_driver_s using netdev_register

- **Receive sequence is as follows**.

  #. Device generates interrupt
  #. Process this interrupt in your interrupt handler
  #. When a new CAN frame has been received you process this frame
  #. When the CAN frame is a normal CAN frame you allocate the
     can_frame struct, when it's a CAN FD frame you allocate a
     canfd_frame struct (note you can of course preallocate and
     just use the pointer).
  #. Copy the frame from the driver to the struct you've
     allocated in the previous step.
  #. Point the net_driver_s d_buf pointer to the allocated can_frame
  #. Call the ``can_input(FAR struct net_driver_s *dev)``
     function ``include/nuttx/net/can.h``

- **Transmit sequence is as follows**.

  #. Socket layer executes d_txavail callback
  #. An example of the txavail function can be found in
     ``arch/arm/src/s32k1xx/s32k1xx_flexcan.c``
  #. An example of the txpoll function can be found in
     ``arch/arm/src/s32k1xx/s32k1xx_flexcan.c``
  #. In your ``transmit(struct driver_s *priv)`` function you
     check the length of ``net_driver_s.d_len`` whether it
     matches the size of a ``struct can_frame`` or
     ``struct canfd_frame`` then you cast the content of the
     ``net_driver_s.d_buf`` pointer to the correct CAN frame struct

ISO-TP
======

ISO-TP (``CAN_ISOTP``) is a socket protocol that provides message-oriented
communication over CAN. It is enabled with ``CONFIG_NET_CAN_ISOTP=y`` option.

By default a message may carry up to 4095 bytes, the maximum length the
classic 12-bit First Frame ``FF_DL`` field can encode. Selecting
``CONFIG_LIBC_CAN_ISOTP_LONG_PDU=y`` enables the 32-bit ``FF_DL`` escape
sequence (ISO 15765-2:2016), extending the maximum message length to
2^32-1 bytes at the cost of a larger First Frame header and wider internal
length fields.
Leave it disabled on classic-CAN-only configurations that never transfer long
messages so no extra code or memory is spent. A ``send`` of a message larger
than the configured limit fails with ``EMSGSIZE``.

A socket is created with ``AF_CAN`` / ``SOCK_DGRAM`` / ``CAN_ISOTP`` and
bound to a ``sockaddr_can`` specifying the interface and the TX/RX CAN IDs
via ``can_addr.tp.tx_id`` and ``can_addr.tp.rx_id``. Each ``send`` call
transmits one complete message; each ``recv`` call returns one complete
reassembled message.

Segmentation, flow control, and reassembly are handled transparently by the
kernel. The N_Cr timeout (1000 ms) applies on both TX (waiting for a Flow
Control frame after the First Frame) and RX (waiting for Consecutive Frames
after a First Frame).

RPMsg CAN
=========

Exposes a remote CAN controller as a local SocketCAN interface over RPMsg,
using the generic net rpmsg driver.

Enable ``CONFIG_NET_RPMSG_DRV`` on both sides: the client registers the
interface with ``net_rpmsg_drv_init(cpuname, name, NET_LL_CAN)``, and the
side that owns the bus bridges a local SocketCAN interface with
``net_rpmsg_bridge_init(cpuname, name, ifname)``.

The remote bus follows the client interface: bringing the client up or down
brings the far interface up or down with it.
