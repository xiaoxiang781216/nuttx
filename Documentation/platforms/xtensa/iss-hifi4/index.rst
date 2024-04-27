======================
hifi4 on ISS Simulator
======================

This port supports running NuttX on Cadence Xtensa Instruction Set Simulator (ISS).

The mandatory features are:

* System timer provided by timer interrupt option
* Use hostfs read/write on stdin/stdout as uart console

Toolchains
==========

Currently, only the Cadence xt-clang toolchain has been tested.
