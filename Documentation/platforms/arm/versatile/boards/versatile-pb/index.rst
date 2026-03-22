============
versatile-pb
============

The `ARM Versatile Platform Baseboard
<https://developer.arm.com/documentation/dui0225/latest/>`_ (VersatilePB) is
an ARM926EJ-S based development platform.  NuttX supports running on this
board both on real hardware and under QEMU emulation
(``qemu-system-arm -M versatilepb``).

This board configuration provides support for these devices:

* PL190 Vectored Interrupt Controller (VIC)
* SP804 Dual Timer
* PL011 UART controller

Getting Started
===============

NSH
---

Configuring NuttX and compile::

    $ ./tools/configure.sh -l versatile-pb:nsh
    $ make

Running with QEMU
=================

The NuttX raw binary can be booted directly with QEMU::

    $ qemu-system-arm -M versatilepb -nographic -kernel ./nuttx

Login to the NuttX shell (NSH) and verify the system is running::

    NuttShell (NSH) NuttX-13.x.x
    nsh> uname -a
    NuttX 13.x.x ... arm versatile-pb
    nsh> ps
      PID GROUP PRI POLICY   TYPE    NPX STATE    EVENT     SIGMASK           STACK   USED  FILLED COMMAND
        0     0   0 FIFO     Kthread N-- Ready              0000000000000000 004088 000896  21.9%  Idle_Task
        ...
    nsh> free
    ...

Testing
-------

Basic verification steps after booting into NSH:

1. Check platform identification::

    nsh> uname -a

2. Verify the scheduler is running::

    nsh> ps

3. Verify timer tick is working (should take approximately 1 second)::

    nsh> sleep 1

4. Verify memory management::

    nsh> free

To exit QEMU, press ``Ctrl-A`` then ``X``.

Debugging with QEMU
====================

1. To debug the nuttx ELF with symbols, make sure the following change has
   been applied to defconfig::

     +CONFIG_DEBUG_SYMBOLS=y

2. Run QEMU at shell terminal 1 with GDB server enabled::

     $ qemu-system-arm -M versatilepb -nographic -kernel ./nuttx -S -s

3. Run GDB with TUI, connect to QEMU, load nuttx and continue
   (at shell terminal 2)::

     $ arm-none-eabi-gdb -tui --eval-command='target remote localhost:1234' nuttx
     (gdb) c
     Continuing.
     ^C
     Program received signal SIGINT, Interrupt.
     (gdb)
