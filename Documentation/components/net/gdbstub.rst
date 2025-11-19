===================
GDBSTUB over NETDEV
===================

GDBSTUB over NETDEV enables remote interactive kernel debugging via the GDB
Remote Serial Protocol. It use bidirectional communication over UDP.

Run the following command to connect to gdbstub from gdb::

  target remote udp:<IP>:<port>

.. note::
   The netpoll interface doen't support ARP requests, so when we connect
   to NuttX device with GDB during a crash, the host running GDB must already
   have the ARP entry for NuttX target, otherwise you won't be able to connect!

   You can add a static ARP entry with ``arp -s <ipaddr> <mac>``.

