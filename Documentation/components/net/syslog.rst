==================
Syslog over NETDEV
==================

Syslog over NETDEV sends kernel or system log messages over the network.
It is one-way communication over UDP.

The UDP syslog can be captured with simple ``socat`` (dafault port 6666)::

  socat - UDP-RECV:6666

Limitations:

* This component is functional after the network device is initialized, therefore
  the initial log from the system boot before this moment is lost.

* NETWORK debug messages must be disabled.
