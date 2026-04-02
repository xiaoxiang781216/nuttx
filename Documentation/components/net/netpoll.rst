=======
Netpoll
=======

Netpoll is a tool inspired by the Linux netpoll API. It allows network
interfaces to be used where the normal network stack is inaccessible,
such as interrupt handlers. It drives the ``netdev_lowerhalf_s`` driver
directly, bypassing the socket layer.

The NuttX implementation is based on the ``struct netdev_lowerhalf_s`` which
means that the network interface using netpoll must be based on
the :ref:`Netdev Lowerhalf <netdriver>` implementation.
The driver must implement ``transmit``, ``receive``, and ``txpoll``.
``txpoll`` is called when IOBs or TX quota are exhausted; it must retire
at least one pending TX descriptor and free its packet.

Configuration
=============

- ``CONFIG_NET_NETPOLL`` - enable the netpoll subsystem

UDP
===

The UDP transport sends and receives IPv4/UDP datagrams over an Ethernet
interface. It is the simplest way to expose a kernel debugging channel
over an existing Ethernet network - the peer only needs a standard UDP
socket.

CAN
===

The CAN transport sends raw payloads directly on a CAN bus. Large
payloads are transparently split across multiple CAN frames, each carried
in its own IOB. The CAN frame header is written into the IOB guard space
so the payload bytes require no copy. Netpoll transmits one prepared frame
per ``transmit`` call.

Receive is not supported. For bidirectional communication use the ISO-TP
transport.

The frame format and CAN ID width are selected at compile time:

- ``CONFIG_NET_CAN_CANFD`` - when enabled, all transmitted frames use
  CAN FD format with a 64-byte data field and the ``CANFD_FDF`` flag set.
  When disabled, classic CAN frames with an 8-byte data field are used.
- ``CONFIG_NET_CAN_EXTID`` - when enabled, all transmitted frames use the
  29-bit extended CAN ID (``CAN_EFF_FLAG``). When disabled, the 11-bit
  base CAN ID is used.

NOTE: for CAN FD, the data segment length can only take specific
predefinied values. Therefore, when the last data segment to be sent has
an invalid length, padding is required. Netpoll adds the necessary padding
and fills it with zeros.

======
ISO-TP
======

The ISO-TP transport adds bidirectional, message-oriented communication on
top of the CAN netpoll layer. Payloads up to 4095 bytes are supported.
Both send and receive block until the exchange completes or a timeout
occurs. On receive, a partial message is discarded and reception restarts
if no consecutive frame arrives within the N_Cr timeout.

The frame format and CAN ID width are selected at compile time:

- ``CONFIG_NET_CAN_CANFD`` - when enabled, all transmitted frames use
  CAN FD format with a 64-byte data field and the ``CANFD_FDF`` flag set.
  When disabled, classic CAN frames with an 8-byte data field are used.
- ``CONFIG_NET_CAN_EXTID`` - when enabled, all transmitted frames use the
  29-bit extended CAN ID (``CAN_EFF_FLAG``). When disabled, the 11-bit
  base CAN ID is used.
