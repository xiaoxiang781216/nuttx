================
Note over NETDEV
================

Note over NETDEV streams kernel tracing or instrumentation data (e.g., scheduler
events, interrupts, context switches) to a remote host for analysis.
It is one-way communication over UDP.

The UDP note parsing tool is integrated in located in ``tools/pynuttx/traceparse.py``::

  python traceparse.py --elf ../../nuttx --hex --port 50000
