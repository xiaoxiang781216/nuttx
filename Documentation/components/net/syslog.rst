==================
Syslog over NETDEV
==================

Syslog over NETDEV sends kernel or system log messages over the network.

Limitations:

* This component is functional after the network device is initialized, therefore
  the initial log from the system boot before this moment is lost.

* NETWORK debug messages must be disabled.

UDP
===

The UDP syslog can be captured with simple ``socat`` (dafault port 6666)::

  socat - UDP-RECV:6666

CAN
===

The CAN syslog PTY bridge is available in ``tools/can_pty.py``::

  python tools/can_pty.py can0 0x3e8
  minicom -D /tmp/can-pty
