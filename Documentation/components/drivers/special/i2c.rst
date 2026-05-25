==================
I2C Device Drivers
==================

-  ``include/nuttx/i2c/i2c_master.h`` and ``include/nuttx/i2c/i2c_slave.h``.
   All structures and APIs needed to work with I2C drivers are provided in
   this header file.

-  ``struct i2c_ops_s``. Each I2C device driver must implement
   an instance of ``struct i2c_ops_s``. That structure defines a
   call table with the following methods:

-  **Binding I2C Drivers**. I2C drivers are not normally directly
   accessed by user code, but are usually bound to another, higher
   level device driver. In general, the binding sequence is:

   #. Get an instance of ``struct i2c_master_s`` from the
      hardware-specific I2C device driver, and
   #. Provide that instance to the initialization method of the
      higher level device driver.

-  **Examples**: ``arch/z80/src/ez80/ez80_i2c.c``,
   ``arch/z80/src/z8/z8_i2c.c``, etc.

-  ``struct i2c_slaveops_s``. Each I2C slave device driver must implement
   an instance of ``struct i2c_slaveops_s``. That structure defines a call
   table with the following methods:

-  **Binding I2C Slave Drivers**. I2C slave drivers are normally directly
   accessed by user code, We can read and write to device nodes using posix
   interfaces.

RPMsg I2C
=========

NuttX can expose an I2C controller across a RPMsg link. Enable
``CONFIG_I2C_RPMSG`` on the client side and ``CONFIG_I2C_RPMSG_SERVER`` on
the side that owns the real I2C bus.

The client creates a normal ``struct i2c_master_s`` instance with
``i2c_rpmsg_initialize(cpuname, name)``. The server exports a local I2C
controller with ``i2c_rpmsg_server_initialize(name, i2c)``. Board code may
then pass the client instance to device drivers, or register it with
``i2c_register()`` so NSH tools can access it as ``/dev/i2cN``.

RPMsg is only the transport. I2C transfers use a private RPMsg payload
format carrying each NuttX ``struct i2c_msg_s`` with its address, flags, and
frequency.
