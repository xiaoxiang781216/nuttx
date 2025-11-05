================
MPS3 AN547 Board
================

The MPS3 AN547 board configuration uses QEMU to emulate a generic ARM v8-M
series hardware platform and provides support for the following devices:

 - ARM Generic Timer
 - CMSDK UART controller

Getting Started
===============

Configuring and Running (Single Core)
-------------------------------------

1. Configuring NuttX and Compiling::

     ./tools/configure.sh -l mps3-an547:nsh
     make

2. Running with QEMU::

     $ qemu-system-arm -M mps3-an547 -nographic -kernel nuttx.bin

3. Pic ostest::

     ./tools/configure.sh mps3-an547:picostest
     make -j20
     genromfs -f romfs.img -d ../apps/bin/
     qemu-system-arm -M mps3-an547 -m 2G -nographic \
     -kernel nuttx.bin -gdb tcp::1127 \
     -device loader,file=romfs.img,addr=0x60000000
     nsh> /pic/hello
     nsh> /pic/ostest

4. Pic bootloader boot to ap, and run ostest::

     ./tools/configure.sh mps3-an547:ap
     make -j20
     mkdir -p pic
     arm-none-eabi-strip --remove-section=.rel.text --remove-section=.comment --strip-unneeded nuttx -o pic/boot
     genromfs -a -f 128 ../romfs.img -d pic
     make distclean -j20
     ./tools/configure.sh mps3-an547:bl
     make -j20
     qemu-system-arm -M mps3-an547 -m 2G -nographic \
     -kernel nuttx.bin -gdb tcp::1127 \
     -device loader,file=../romfs.img,addr=0x60000000
     bl> boot /pic/boot
     ap> ostest

5. Gdbstub demo::

     ./tools/configure.sh mps3-an547:gdbstub
     make -j20
     qemu-system-arm -M mps3-an547 -m 2G -nographic -kernel nuttx  -serial pty -serial pty
     QEMU 8.2.0 monitor - type 'help' for more information
     char device redirected to /dev/pts/14 (label serial0)
     char device redirected to /dev/pts/20 (label serial1)
     arm-none-eabi-gdb nuttx -ex "target remot /dev/pts/20"
     (gdb) c
     sudo minicom -b 115200 -D /dev/pts/14

6. elffixup::

     cmake -B ../build -DBOARD_CONFIG=boards/arm/mps/mps3-an547/configs/elffixup
     cmake --build ../build
     qemu-system-arm -M mps3-an547 -m 2G -device loader,file=../build/nuttx.hex -gdb tcp::1128 -nographic
     nsh> hello
     nsh> ostest

7. elffixup_protect::

     cmake -B ../build -DBOARD_CONFIG=boards/arm/mps/mps3-an547/configs/elffixup_protect
     cmake --build ../build
     qemu-system-arm -M mps3-an547 -m 2G -device loader,file=../build/nuttx -device loader,file=../build/nuttx_user.hex -gdb tcp::1128 -nographic
     nsh> hello
     nsh> ostest

7. flat_addrenv::

     cmake -B ../build -DBOARD_CONFIG=boards/arm/mps/mps3-an547/configs/flat_addrenv
     cmake --build ../build
     qemu-system-arm -M mps3-an547 -m 2G -device loader,file=../build/nuttx.hex -gdb tcp::1128 -nographic
     nsh> hello
     nsh> ostest

Debugging with QEMU
===================

The NuttX ELF image can be debugged using QEMU.

1. Enable Debug Symbols.
   Ensure the following change is applied to ``defconfig``::

     CONFIG_DEBUG_SYMBOLS=y

2. Run QEMU::

     qemu-system-arm -M mps3-an547 -nographic -kernel nuttx.bin -S -s

3. Run GDB with TUI::

     arm-none-eabi-gdb -tui --eval-command='target remote localhost:1234' nuttx
