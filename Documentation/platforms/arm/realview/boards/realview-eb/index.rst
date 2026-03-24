==========
realview-eb
==========

The `ARM RealView Emulation Baseboard
<https://developer.arm.com/documentation/dui0303/latest/>`_ (RealView-EB) is
an ARM development platform.  NuttX supports running on this board under
QEMU emulation (``qemu-system-arm -M realview-eb``) with the following
CPU cores:

* ARM1136 (default)
* ARM11MPCore

This board configuration provides support for these devices:

* GICv2 Generic Interrupt Controller
* SP804 Dual Timer
* PL011 UART controller
* SMC91C111 Ethernet controller

Getting Started
===============

NSH (ARM1136, default)
----------------------

Configuring NuttX and compile::

    $ ./tools/configure.sh -l realview-eb:nsh1136
    $ make

NSH (ARM11MPCore)
-----------------

Configuring NuttX and compile::

    $ ./tools/configure.sh -l realview-eb:nsh11mpcore
    $ make

Running with QEMU
=================

ARM1136::

    $ qemu-system-arm -M realview-eb -cpu arm1136 -nographic -kernel ./nuttx

ARM11MPCore::

    $ qemu-system-arm -M realview-eb -cpu arm11mpcore -nographic -kernel ./nuttx

With networking (SMC91C111, user-mode)::

    $ qemu-system-arm -M realview-eb -cpu arm1136 -nographic -kernel ./nuttx \
        -nic user,model=smc91c111

With networking (SMC91C111, TAP)::

    $ qemu-system-arm -M realview-eb -cpu arm1136 -nographic -kernel ./nuttx \
        -nic tap,model=smc91c111,ifname=nuttx0,script=no,downscript=no

Login to the NuttX shell (NSH) and verify the system is running::

    NuttShell (NSH) NuttX-13.x.x
    nsh> uname -a
    NuttX 13.x.x ... arm realview-eb
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

     $ qemu-system-arm -M realview-eb -nographic -kernel ./nuttx -S -s

3. Run GDB with TUI, connect to QEMU, load nuttx and continue
   (at shell terminal 2)::

     $ arm-none-eabi-gdb -tui --eval-command='target remote localhost:1234' nuttx
     (gdb) c
     Continuing.
     ^C
     Program received signal SIGINT, Interrupt.
     (gdb)
