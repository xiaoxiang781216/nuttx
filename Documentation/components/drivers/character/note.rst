=====================
Note Driver Interface
=====================

Note driver is the interface to access the instrumentation data.
Each note driver registers its own device node at ``/dev/note/<driver_name>``.
For example:

- ``/dev/note/ram`` - RAM note driver
- ``/dev/note/log`` - Log note driver
- ``/dev/note/rpmsg`` - RPMSG note driver

Each device node supports ioctl commands to control its own instrumentation filter.

.. _noteram:

Note Device (``/dev/note/<driver_name>``)
=========================================

  ``/dev/note/<driver_name>`` is the device to access instrumentation data and control filters.
  Each note driver registers its own device node with its driver name.

Common Header Files
-------------------

  The header file ``include/nuttx/note/note_driver.h`` provides the common interface definitions.

Common Data Structures
----------------------

.. c:struct:: note_filter_mode_s

  .. code-block:: c

    struct note_filter_mode_s
    {
      unsigned int type_mask;     /* Filter mode type mask */
    #ifdef CONFIG_SMP
      cpu_set_t cpuset;           /* The set of monitored CPUs */
    #endif
    };

  - ``type_mask`` : Filter mode type mask. Controls which types of notes are recorded.

  - ``cpuset`` : (SMP only) Monitor only CPUs in the bitset. Bit 0=CPU0, Bit1=CPU1, etc.

.. c:struct:: note_filter_syscall_s

  .. code-block:: c

    struct note_filter_syscall_s
    {
      uint8_t syscall_mask[];
    };

  - ``syscall_mask`` : A bitmap array of the syscall filter. If a bit is set, the corresponding syscall is not recorded.
    The following helper macros are available:

    .. c:macro:: NOTE_FILTER_SYSCALLMASK_SET(nr, s)

      Set syscall number `nr` as masked. `s` specifies the variable of `struct note_filter_syscall_s`

    .. c:macro:: NOTE_FILTER_SYSCALLMASK_CLR(nr, s)

      Set syscall number `nr` as unmasked.

    .. c:macro:: NOTE_FILTER_SYSCALLMASK_ISSET(nr, s)

      Check whether syscall number `nr` is masked or not. True if masked.

    .. c:macro:: NOTE_FILTER_SYSCALLMASK_ZERO(s)

      Clear all masks.

.. c:struct:: note_filter_irq_s

  .. code-block:: c

    struct note_filter_irq_s
    {
      uint8_t irq_mask[];
    };

  - ``irq_mask`` : A bitmap array of the IRQ filter. If a bit is set, the corresponding IRQ is not recorded.
    The following helper macros are available:

    .. c:macro:: NOTE_FILTER_IRQMASK_SET(nr, s)

      Set IRQ number `nr` as masked. `s` specifies the variable of `struct note_filter_irq_s`

    .. c:macro:: NOTE_FILTER_IRQMASK_CLR(nr, s)

      Set IRQ number `nr` as unmasked.

    .. c:macro:: NOTE_FILTER_IRQMASK_ISSET(nr, s)

      Check whether IRQ number `nr` is masked or not. True if masked.

    .. c:macro:: NOTE_FILTER_IRQMASK_ZERO(s)

      Clear all masks.

.. c:struct:: note_filter_tag_s

  .. code-block:: c

    struct note_filter_tag_s
    {
      uint8_t tag_mask[];
    };

  - ``tag_mask`` : A bitmap array of the tag filter. If a bit is set, the corresponding tag is not recorded.
    The following helper macros are available:

    .. c:macro:: NOTE_FILTER_TAGMASK_SET(nr, s)

      Set tag number `nr` as masked.

    .. c:macro:: NOTE_FILTER_TAGMASK_CLR(nr, s)

      Set tag number `nr` as unmasked.

    .. c:macro:: NOTE_FILTER_TAGMASK_ISSET(nr, s)

      Check whether tag number `nr` is masked or not. True if masked.

    .. c:macro:: NOTE_FILTER_TAGMASK_ZERO(s)

      Clear all masks.

Common Ioctls
-------------

  The following ioctl commands are supported by all note driver device nodes.

.. c:macro:: NOTE_GETFILTER

  Get note filter mode for this driver

  :argument: A writable pointer to :c:struct:`note_filter_mode_s`

  :return: If success, 0 (``OK``) is returned and current note filter mode is stored into the given pointer.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_SETFILTER

  Set note filter mode for this driver

  :argument: A read-only pointer to :c:struct:`note_filter_mode_s`

  :return: If success, 0 (``OK``) is returned and the given filter mode is set as the current settings.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_GETSYSCALLFILTER

  Get syscall filter setting for this driver

  :argument: A writable pointer to :c:struct:`note_filter_syscall_s`

  :return: If success, 0 (``OK``) is returned and current syscall filter mode is stored into the given pointer.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_SETSYSCALLFILTER

  Set syscall filter setting for this driver

  :argument: A read-only pointer to :c:struct:`note_filter_syscall_s`

  :return: If success, 0 (``OK``) is returned and the given syscall filter mode is set as the current settings.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_GETIRQFILTER

  Get IRQ filter setting for this driver

  :argument: A writable pointer to :c:struct:`note_filter_irq_s`

  :return: If success, 0 (``OK``) is returned and current IRQ filter mode is stored into the given pointer.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_SETIRQFILTER

  Set IRQ filter setting for this driver

  :argument: A read-only pointer to :c:struct:`note_filter_irq_s`

  :return: If success, 0 (``OK``) is returned and the given IRQ filter mode is set as the current settings.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_GETTAGFILTER

  Get tag filter setting for this driver

  :argument: A writable pointer to :c:struct:`note_filter_tag_s`

  :return: If success, 0 (``OK``) is returned and current tag filter mode is stored into the given pointer.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_SETTAGFILTER

  Set tag filter setting for this driver

  :argument: A read-only pointer to :c:struct:`note_filter_tag_s`

  :return: If success, 0 (``OK``) is returned and the given tag filter mode is set as the current settings.
    If failed, a negated ``errno`` is returned.

Noteram Device (``/dev/note/ram``)
==================================

  ``/dev/note/ram`` is the RAM-based note driver device.
  In addition to common ioctls, it provides read function to get the trace data
  and additional ioctls to control the buffer mode.

``/dev/note/ram`` Header Files
------------------------------

  The header file ``include/nuttx/note/noteram_driver.h`` provides the interface definitions of the device.

``/dev/note/ram`` Additional Ioctls
-----------------------------------

.. c:macro:: NOTE_CLEAR

  Clear all contents of the circular buffer

  :argument: Ignored

  :return: Always returns 0.

.. c:macro:: NOTE_GETMODE

  Get overwrite mode

  :argument: A writable pointer to ``unsigned int``.
    The overwrite mode takes one of the following values.

    .. c:macro:: NOTE_MODE_OVERWRITE_DISABLE

      Overwrite mode is disabled. When the buffer is full, accepting the data will be stopped.

    .. c:macro:: NOTE_MODE_OVERWRITE_ENABLE

      Overwrite mode is enabled.

    .. c:macro:: NOTE_MODE_OVERWRITE_OVERFLOW

      Overwrite mode is disabled and the buffer is already full.

  :return: If success, 0 (``OK``) is returned and current overwrite mode is stored into the given pointer.
           If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_SETMODE

  Set overwrite mode

  :argument: A read-only pointer to ``unsigned int``.

  :return: If success, 0 (``OK``) is returned and the given overwriter mode is set as the current settings.
    If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_GETREADMODE

  Get read mode (binary or text)

  :argument: A writable pointer to ``unsigned int``.

  :return: If success, 0 (``OK``) is returned and current read mode is stored into the given pointer.
           If failed, a negated ``errno`` is returned.

.. c:macro:: NOTE_SETREADMODE

  Set read mode (binary or text)

  :argument: A read-only pointer to ``unsigned int``.

  :return: If success, 0 (``OK``) is returned and the given read mode is set as the current settings.
    If failed, a negated ``errno`` is returned.

