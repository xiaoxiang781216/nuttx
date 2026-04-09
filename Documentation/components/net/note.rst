================
Note over NETDEV
================

Note over NETDEV streams kernel tracing or instrumentation data (e.g., scheduler
events, interrupts, context switches) to a remote host for analysis.

UDP
===

The UDP note parsing tool is integrated in ``tools/pynuttx/traceparse.py``::

  python traceparse.py --elf nuttx --port 50000 --dump-notes

CAN
===

The CAN note parsing tool is integrated in ``tools/pynuttx/traceparse.py``::

  python traceparse.py --elf nuttx --can can0 0x1f4 --dump-notes
