================================
``arping`` ARP "ping" command
================================

Overview
--------
The ``arping`` application sends ARP REQUEST packets to a target IPv4 address
on the local network and reports the responding MAC address with round-trip
time. It is useful for verifying Layer 2 connectivity, resolving IP-to-MAC
mappings, and detecting duplicate IP addresses.

Unlike ``ping`` which operates at the IP layer (ICMP), ``arping`` operates at
the Ethernet/ARP layer using AF_PACKET raw sockets.

Requirements
------------
- Networking enabled: ``CONFIG_NET=y``
- ARP support: ``CONFIG_NET_ARP=y``
- Packet socket support: ``CONFIG_NET_PKT=y``
- Build the library: ``CONFIG_NETUTILS_ARPING=y``
- Build the command: ``CONFIG_SYSTEM_ARPING=y``

Synopsis
--------
::

	arping [-w <timeout>] -i <device> <destination>

Options
-------
- ``-i <device>``: Network interface to use (required). e.g., ``eth0``, ``wlan0``.
- ``-w <timeout>``: How long to wait for a reply in seconds (default: 0.5s).
- ``-h``: Show help and exit.
- ``<destination>``: Target IPv4 address (required).

Output
------
On success, ``arping`` prints:

::

	ARPING 10.0.1.1 from 10.0.1.2 wlan0
	Unicast reply from 10.0.1.1 [52:1c:16:3d:b4:c8] 0.867ms

The output shows the target IP, the resolved MAC address in brackets, and the
round-trip time in milliseconds.

If no reply is received within the timeout, the command exits with an error.

Exit Status
-----------
- Positive (>0): ARP reply received successfully.
- Negative: Error occurred (e.g., ``-ETIMEDOUT`` for no reply, ``-EINVAL`` for
  invalid parameters).

Configuration
-------------
- ``CONFIG_NETUTILS_ARPING_RETRIES``: Number of ARP request retries before
  giving up (default: 1).
- ``CONFIG_SYSTEM_ARPING_PRIORITY``: Task priority (default: 100).
- ``CONFIG_SYSTEM_ARPING_STACKSIZE``: Task stack size (default:
  ``DEFAULT_TASK_STACKSIZE``).

Examples
--------
Resolve the MAC address of a gateway:

.. code-block:: bash

	arping -i wlan0 10.0.1.1

Set a longer timeout (2 seconds):

.. code-block:: bash

	arping -w 2 -i eth0 192.168.1.1

Notes
-----
- The ``-i`` option is mandatory because ARP operates at Layer 2 and requires
  a specific network interface to send and receive frames.
- ``arping`` creates a ``PF_PACKET`` / ``SOCK_RAW`` socket with protocol
  ``ETH_P_ARP``, so only ARP frames are received.
- Self-sent packets are filtered both at the kernel level (via
  ``pkt_loopback`` / ``pendiob`` mechanism) and at the application level
  (source MAC comparison), so the sender will not see its own requests.
- The retry count is configurable via Kconfig. Each retry sends a new ARP
  REQUEST and waits for the full timeout duration.

Troubleshooting
---------------
- ``ERROR: Create sockfd failed``: Packet socket not available. Check
  ``CONFIG_NET_PKT=y``.
- ``ERROR: Bind failed``: Invalid interface name or interface not up.
- ``ERROR: Arping request timed out``: No ARP reply received. Verify the
  target is on the same subnet and the interface has a valid IP address.

Implementation Details
----------------------
The command is split into two components:

- ``apps/netutils/arping/arping.c``: Core library providing the ``arping()``
  API. Constructs raw Ethernet frames (``ethhdr`` + ``ether_arp``), sends via
  ``write()`` on an AF_PACKET socket, and validates ARP replies.
- ``apps/system/arping/arping.c``: NSH command-line wrapper that parses
  arguments, calls ``arping()``, and formats the output with timing.
