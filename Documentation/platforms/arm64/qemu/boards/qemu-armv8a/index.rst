===========
qemu-armv8a
===========

.. include:: README.txt
   :literal:


5. Gdbstub demo

The Qemu version must be above 9.2 to support two serial ports.

One window::

   ./tools/configure.sh qemu-armv8a:gdbstub; make -j25
   qemu-system-aarch64 -cpu cortex-a53 -nographic -machine virt,virtualization=on,gic-version=3 -net none -kernel ./nuttx -serial mon:stdio -serial pty
   char device redirected to /dev/pts/27 (label serial1)
   - Ready to Boot Primary CPU
   - Boot from EL2
   - Boot from EL1
   - Boot to C runtime for OS Initialize

Another window::
  
   gdb-multiarch nuttx -ex "target remot /dev/pts/27"
   GNU gdb (Ubuntu 15.0.50.20240403-0ubuntu1) 15.0.50.20240403-git
   Copyright (C) 2024 Free Software Foundation, Inc.
   License GPLv3+: GNU GPL version 3 or later <http://gnu.org/licenses/gpl.html>
   This is free software: you are free to change and redistribute it.
   There is NO WARRANTY, to the extent permitted by law.
   Type "show copying" and "show warranty" for details.
   This GDB was configured as "x86_64-linux-gnu".
   Type "show configuration" for configuration details.
   For bug reporting instructions, please see:
   <https://www.gnu.org/software/gdb/bugs/>.
   Find the GDB manual and other documentation resources online at:
   <http://www.gnu.org/software/gdb/documentation/>.

   For help, type "help".
   Type "apropos word" to search for commands related to "word"...
   Reading symbols from nuttx...
   Remote debugging using /dev/pts/26
   gdb_get_registers (state=0x403e1590) at gdbstub/lib_gdbstub.c:1020
   1020              reg = state->running_regs;
   (gdb) c
