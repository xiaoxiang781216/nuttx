README.txt
==========

This board configuration will use QEMU to emulate generic ARM v8-M series
hardware platform and provides support for these devices:

 - ARM Generic Timer
 - CMSDK UART controller

Contents
========
  - Getting Started
  - Status
  - Platform Features
  - Debugging with QEMU
  - FPU Support and Performance
  - SMP Support
  - References

Getting Started
===============

1. Configuring and running
  1.1 Single Core
   Configuring NuttX and compile:
   $ ./tools/configure.sh -l mps3-an547:nsh
   $ make
   Running with qemu
   $ qemu-system-arm -M mps3-an547 -nographic -kernel nuttx.bin
  1.2 Pic ostest
   $ ./tools/configure.sh mps3-an547:picostest
   $ make -j20
   $ genromfs -f romfs.img -d ../apps/bin/
   $ qemu-system-arm -M mps3-an547 -m 2G -nographic \
     -kernel nuttx.bin -gdb tcp::1127 \
     -device loader,file=romfs.img,addr=0x60000000
   $ nsh> /pic/hello
   $ nsh> /pic/ostest

   1.3 bootloader boot to Pic ap
   $ ./tools/configure.sh mps3-an547:ap
   $ make -j20
   $ mkdir -p pic
   $ cp boot pic/.
   $ genromfs -f -a 128 romfs.img -d pic
   $ make distclean -j20
   $ ./tools/configure.sh mps3-an547:bl
   $ make -j20
   $ qemu-system-arm -M mps3-an547 -m 2G -nographic \
     -kernel nuttx.bin -gdb tcp::1127 \
     -device loader,file=romfs.img,addr=0x60000000
   $ bl> boot /pic/boot
   $ ap> ostest

Debugging with QEMU
===================

The nuttx ELF image can be debugged with QEMU.

1. To debug the nuttx (ELF) with symbols, make sure the following change have
   applied to defconfig.

+CONFIG_DEBUG_SYMBOLS=y

2. Run QEMU(at shell terminal 1)

   $ qemu-system-arm -M mps3-an547 -nographic -kernel nuttx.bin -S -s

3. Run gdb with TUI, connect to QEMU, load nuttx and continue (at shell terminal 2)

   $ arm-none-eabi-gdb -tui --eval-command='target remote localhost:1234' nuttx
