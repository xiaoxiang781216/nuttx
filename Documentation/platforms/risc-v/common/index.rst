===========================================================================
RISC-V Specific Features
===========================================================================

RISC-V CLIC Interrupt Threshold Configuration
==============================================

Overview
--------

The RISC-V Core-Level Interrupt Controller (CLIC) provides more flexible interrupt
control compared to the legacy CLINT. One key feature is the interrupt threshold
mechanism (INTTHRESH), which allows fine-grained control over which interrupts
are processed based on their priority levels.

CLIC Interrupt Threshold Basics
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CLIC interrupt threshold works by:

1. **Threshold Register**: Each privilege mode has its own interrupt threshold register:
   - Machine mode: ``CSR_MINTTHRESH`` (0x347)
   - Supervisor mode: ``CSR_SINTTHRESH`` (0x147)

2. **Interrupt Filtering**: Only interrupts with priority levels above the current
   threshold are delivered to the processor.

3. **Context Preservation**: The threshold value must be saved and restored during
   context switches to maintain proper interrupt priority handling.

Configuration in Custom Chip Layer
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Kconfig Configuration
"""""""""""""""""""""

To enable CLIC interrupt threshold support in your custom chip, add the following
to your chip's Kconfig file:

.. code-block:: kconfig

   config ARCH_CHIP_MYCUSTOM_CHIP
       bool "My Custom RISC-V Chip"
       select ARCH_RV32  # or ARCH_RV64
       select ARCH_RV_HAVE_CLIC
       ---help---
         My custom RISC-V chip with CLIC support

Required Definitions
""""""""""""""""""""

In your chip-specific header file (e.g., ``arch/risc-v/include/mycustom/irq.h``):

.. code-block:: c

   /* Define maximum interrupt threshold value for your CLIC implementation */
   #define RISCV_MAX_INTTHRESH    255  /* Adjust based on your CLIC design */

   /* Optional: Define interrupt priority levels */
   #define CLIC_PRIO_CRITICAL          200
   #define CLIC_PRIO_HIGH              150
   #define CLIC_PRIO_NORMAL            100
   #define CLIC_PRIO_LOW               50

.. note::
   If ``RISCV_MAX_INTTHRESH`` is not defined by the chip, NuttX will use a default
   value of ``0xff`` (255). This default should work for most CLIC implementations
   that support 8-bit interrupt threshold values. Chips with different threshold
   widths should define their own maximum value accordingly.

Implementation Details
^^^^^^^^^^^^^^^^^^^^^^

Interrupt Save/Restore Mechanism
"""""""""""""""""""""""""""""""""

When ``ARCH_RV_HAVE_CLIC`` is enabled, NuttX automatically uses the
interrupt threshold register instead of the standard status register for
interrupt control. The CLIC implementation uses the ``SWAP_CSR()`` macro
for atomic read-modify-write operations on the threshold register:

.. code-block:: c

   /* Standard RISC-V interrupt disable (without CLIC) */
   noinstrument_function static inline_function irqstate_t up_irq_save(void)
   {
     irqstate_t flags;

     /* Read mstatus & clear machine interrupt enable (MIE) in mstatus */

     __asm__ __volatile__
       (
         "csrrc %0, " __XSTR(CSR_STATUS) ", %1\n"
         : "=r" (flags)
         : "r"(STATUS_IE)
         : "memory"
       );

     return flags;
   }

   /* CLIC interrupt threshold disable (with ARCH_RV_HAVE_CLIC) */
   noinstrument_function static inline_function irqstate_t up_irq_save(void)
   {
     /* Read current interrupt threshold and set to maximum to mask all */

     return SWAP_CSR(CSR_INTTHRESH, RISCV_MAX_INTTHRESH);
   }

Context Switch Handling
"""""""""""""""""""""""

The interrupt context register (``REG_INT_CTX``) stores different values
depending on CLIC configuration:

.. code-block:: c

   /*
    * Without CLIC (standard RISC-V): REG_INT_CTX stores CSR_MSTATUS or CSR_SSTATUS
    * With CLIC (ARCH_RV_HAVE_CLIC): REG_INT_CTX stores CSR_MINTTHRESH or CSR_SINTTHRESH
    *
    * The interrupt context is automatically determined by the CONFIG_ARCH_RV_HAVE_CLIC
    * configuration, eliminating the need for a separate threshold enable option.
    */

CLIC Driver Implementation
""""""""""""""""""""""""""

To implement the CLIC driver with interrupt threshold support, you may need to configure
CLIC properly to handle priority management and threshold levels in your chip's
driver code.

Integration Checklist
^^^^^^^^^^^^^^^^^^^^^^

When integrating CLIC interrupt threshold support:

☐ Enable ``ARCH_RV_HAVE_CLIC`` in Kconfig
☐ Define ``RISCV_MAX_INTTHRESH`` for your chip (optional - defaults to 0xff/255)
☐ Implement CLIC driver with priority management
☐ Verify interrupt threshold save/restore in context switches
☐ Test interrupt priority levels and threshold functionality
☐ Add debug support for troubleshooting

References
^^^^^^^^^^

* RISC-V CLIC Specification
* NuttX RISC-V Architecture Documentation
* ``arch/risc-v/include/irq.h`` - Core interrupt handling definitions
* ``arch/risc-v/src/common/riscv_exception_common.S`` - Assembly interrupt handling

RISC-V Zicfilp Control Flow Integrity Extension
===============================================

Overview
--------

The RISC-V Zicfilp (Control Flow Integrity for Indirect Branches) extension provides
hardware support for control flow integrity by implementing branch protection mechanisms.
This extension helps protect against Return-Oriented Programming (ROP) and Jump-Oriented
Programming (JOP) attacks by adding hardware checks for indirect branch targets.

Zicfilp Extension Basics
^^^^^^^^^^^^^^^^^^^^^^^

The Zicfilp extension provides:

1. **Hardware CFI Support**: Hardware-based control flow integrity checks for indirect branches
2. **Branch Protection**: Protection against code-reuse attacks (ROP/JOP)
3. **Compiler Integration**: Works with compiler-generated CFI instrumentation
4. **Performance**: Hardware implementation provides better performance than software-only solutions

Configuration
^^^^^^^^^^^^^

Chip Configuration Requirement
""""""""""""""""""""""""""""""

**IMPORTANT**: The ``ARCH_RV_ISA_ZICFILP`` option must be selected by the chip
configuration and should not be manually enabled by users. This ensures that
the extension is only activated on hardware that actually supports it.

To enable Zicfilp extension support in your custom RISC-V chip:

.. code-block:: kconfig

   config ARCH_CHIP_MYCUSTOM_CHIP
       bool "My Custom RISC-V Chip with Zicfilp"
       select ARCH_RV32  # or ARCH_RV64
       select ARCH_RV_ISA_M
       select ARCH_RV_ISA_A
       select ARCH_RV_ISA_C
       select ARCH_RV_ISA_ZICFILP  # Select this only if hardware supports it
       ---help---
         My custom RISC-V chip with Zicfilp CFI support

Kconfig Configuration
"""""""""""""""""""""

The actual Kconfig definition is:

.. code-block:: kconfig

   config ARCH_RV_ISA_ZICFILP
       bool
       default n
       ---help---
         Enable support for the RISC-V Zicfilp (Control Flow Integrity for Indirect Branches)
         extension. This extension provides hardware support for control flow integrity
         by implementing branch protection mechanisms. When enabled, the compiler will
         generate code with control flow protection for indirect branches.

         NOTE: This option should not be manually enabled. It must be selected by
         the chip configuration to ensure proper hardware support.

.. warning::
   **Do not manually enable this option** through menuconfig or defconfig unless you are
   creating a custom chip configuration. The chip configuration should select this option
   based on actual hardware capabilities.

Valid Configuration Methods
"""""""""""""""""""""""""""

1. **Chip Configuration** (Recommended): Add ``select ARCH_RV_ISA_ZICFILP`` to your chip's Kconfig
2. **Custom Board** (Advanced): Only if creating a custom chip configuration that supports Zicfilp

Invalid Configuration Methods
"""""""""""""""""""""""""""""

These methods should be avoided:

1. **menuconfig**: Manual selection through the menu system
2. **defconfig**: Direct addition of ``CONFIG_ARCH_RV_ISA_ZICFILP=y``

The above methods bypass hardware capability checks and may result in CFI instructions
being generated for hardware that doesn't support them.

Compiler Support
^^^^^^^^^^^^^^^^

When ``ARCH_RV_ISA_ZICFILP`` is enabled, the build system automatically:

1. **ISA String**: Adds ``_zicfilp`` to the ``-march`` compiler flag

   .. code-block:: bash

      # Example: rv64imac becomes rv64imac_zicfilp
      -march=rv64imac_zicfilp

2. **CFI Protection**: Adds ``-fcf-protection=branch`` compiler flag for branch protection

   .. code-block:: bash

      # Compiler flags
      -fcf-protection=branch

Implementation Details
^^^^^^^^^^^^^^^^^^^^^^

Hardware Requirements
"""""""""""""""""""""

To use this feature, your RISC-V implementation must:

- Support the Zicfilp extension in hardware
- Implement the required CFI instructions and checks
- Support the necessary CSRs for CFI configuration

Toolchain Requirements
""""""""""""""""""""""

- **GCC**: Version that supports Zicfilp extension and ``-fcf-protection=branch``
- **Clang**: Version that supports Zicfilp extension
- **Binutils**: Version that can assemble Zicfilp instructions

Runtime Behavior
"""""""""""""""""

When CFI is not enabled or supported at runtime, CFI-related instructions will be
treated as NOPs (no operation):

.. note::
   **CFI Instruction Behavior**: If the Zicfilp extension is not enabled in hardware
   or runtime configuration, CFI-related instructions inserted by the compiler will
   execute as NOP instructions. This provides backward compatibility but offers no
   protection.

Integration Example
^^^^^^^^^^^^^^^^^^^

For a custom RISC-V chip with Zicfilp support:

.. code-block:: kconfig

   config ARCH_CHIP_MYCUSTOM_CHIP
       bool "My Custom RISC-V Chip with CFI"
       select ARCH_RV64
       select ARCH_RV_ISA_M
       select ARCH_RV_ISA_A
       select ARCH_RV_ISA_C
       select ARCH_RV_ISA_ZICFILP  # Select only if hardware supports Zicfilp
       ---help---
         My custom RISC-V chip with Zicfilp CFI support

.. note::
   **Hardware Verification Required**: Only select ``ARCH_RV_ISA_ZICFILP`` if your
   chip actually implements the Zicfilp extension in hardware. Selecting this option
   for chips without hardware support will generate CFI instructions that execute
   as NOPs, providing no security benefit.

Debugging and Verification
^^^^^^^^^^^^^^^^^^^^^^^^^^

To verify Zicfilp is properly enabled:

1. **Check compiler flags**: Verify ``-march`` includes ``_zicfilp`` and ``-fcf-protection=branch`` is present
2. **Disassemble code**: Look for CFI-related instructions in generated code
3. **Runtime testing**: Test with CFI violations to ensure protection is active

References
^^^^^^^^^^

* RISC-V Zicfilp Extension Specification
* GCC Control Flow Protection Documentation
* RISC-V ISA Manual - Chapter on Code Integrity Extensions
