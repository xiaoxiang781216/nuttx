===============
nrf51-microbit
===============

The `BBC micro:bit <https://microbit.org>`_ is a pocket-sized computer based on the
Nordic nRF51822 SoC (ARM Cortex-M0 at 16 MHz, 256K flash, 16K RAM) designed for
educational purposes.

QEMU provides built-in support for the BBC micro:bit with the ``microbit`` machine type,
making it possible to run and test NuttX without physical hardware.

Serial Console
==============

The default console uses UART0, which QEMU connects to the terminal via ``-serial``.

Getting Started
===============

Prerequisites
-------------

You need ``qemu-system-arm`` with micro:bit support. Most distributions include this
by default::

  # Ubuntu / Debian
  sudo apt-get install qemu-system-arm

  # macOS
  brew install qemu

Configuring and Building
------------------------

Configure NuttX and compile::

  $ cd nuttx
  $ ./tools/configure.sh -l nrf51-microbit:nsh
  $ make -j$(nproc)

Running with QEMU
-----------------

Run the NuttX image with QEMU::

  $ qemu-system-arm -M microbit -nographic -serial mon:stdio -kernel ./nuttx

You should see the NSH shell prompt::

  NuttShell (NSH) NuttX-13.x.x
  v6m>

To exit QEMU, press ``Ctrl-A`` then ``x``.

Debugging with QEMU
====================

1. Make sure ``CONFIG_DEBUG_SYMBOLS=y`` is enabled in the defconfig.

2. Start QEMU with GDB server (terminal 1)::

     $ qemu-system-arm -M microbit -nographic -serial mon:stdio \
       -kernel ./nuttx -S -s

3. Connect GDB (terminal 2)::

     $ arm-none-eabi-gdb -tui --eval-command='target remote localhost:1234' nuttx
     (gdb) c
     Continuing.
     ^C
     Program received signal SIGINT, Interrupt.
     (gdb)

Configurations
==============

Each configuration is maintained in a sub-directory and can be selected as
follow::

  tools/configure.sh nrf51-microbit:<subdir>

Where <subdir> is one of the following:

nsh
---

Basic NuttShell (NSH) configuration with serial console on UART0.
