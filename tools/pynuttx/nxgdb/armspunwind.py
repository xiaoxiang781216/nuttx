############################################################################
# tools/pynuttx/nxgdb/armspunwind.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.  The
# ASF licenses this file to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance with the
# License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
# License for the specific language governing permissions and limitations
# under the License.
#
############################################################################

"""
ARM Stack-based Backtrace Unwinder for GDB

This module provides stack-based backtrace unwinding for ARM architecture
without relying on unwind tables. It analyzes branch instructions (BL/BLX)
in the code to reconstruct call stacks from stack memory.

Usage:
    from arm_sp_unwind import ArmSpUnwind

    # Initialize unwinder
    unwinder = ArmSpUnwind()

    # Set custom code regions if needed
    unwinder.set_code_regions(
        [(0x8000000, 0x8100000), (0x20000000, 0x20010000)]
    )

    # Unwind from TCB
    tcb = gdb.parse_and_eval("tcb_ptr")
    addresses = unwinder.unwind_from_tcb(tcb, max_frames=10)

    # Unwind from stack memory range
    addresses = unwinder.unwind_from_stack(
        sp=0x20001000, top=0x20002000, max_frames=10
    )

    # Use with Backtrace class for pretty printing
    from backtrace import Backtrace
    backtrace = Backtrace(addresses)
    print(backtrace)
"""

import argparse
import re
from typing import List, Optional, Tuple

import gdb

from . import autocompeletion, utils
from .backtrace import Backtrace

# ARM Thumb instruction masks and opcodes
# BLX instruction: 0b010001111x000000
IMASK_T_BLX = 0xFF80
IOP_T_BLX = 0x4780

# BL instruction (high halfword): 0b11110xxxxxxxxxxx
IMASK_T_BL = 0xF800
IOP_T_BL = 0xF000

# BL instruction (low halfword): 0b11x1xxxxxxxxxxxx
IMASK_T_BL_LOW = 0xD000
IOP_T_BL_LOW = 0xD000


def parse_code_regions() -> List[Tuple[int, int]]:
    """Parse code regions from GDB target sections

    Parse output of 'maint info target-sections' to find all CODE
    sections. Returns list of (start, end) tuples for code regions.
    """

    # Execute GDB command to get target sections
    output = gdb.execute("maint info target-sections", to_string=True)

    regions = []
    # Pattern to match lines with CODE flag
    # Look for hex addresses around -> on lines containing CODE
    pattern = r"0x([0-9a-fA-F]+)->0x([0-9a-fA-F]+).*\bCODE\b"

    for line in output.split("\n"):
        match = re.search(pattern, line)
        if match:
            start_addr = int(match.group(1), 16)
            end_addr = int(match.group(2), 16)

            # Only add valid regions (start < end)
            if start_addr < end_addr:
                regions.append((start_addr, end_addr))

    if regions:
        # Sort regions by start address and merge
        # overlapping/adjacent ones
        regions.sort(key=lambda x: x[0])
        merged = []
        for start, end in regions:
            if merged and start <= merged[-1][1]:
                # Overlapping or adjacent, merge them
                merged[-1] = (
                    merged[-1][0],
                    max(merged[-1][1], end),
                )
            else:
                merged.append((start, end))

        return merged

    return []


class ArmSpUnwind:
    """
    ARM Stack-based Backtrace Unwinder

    This class provides functionality to unwind ARM call stacks by analyzing
    branch instructions (BL/BLX) stored on the stack, without requiring
    unwind tables or frame pointers.
    """

    def __init__(self):
        """Initialize the ARM stack unwinder"""
        self.code_regions = parse_code_regions()

    def set_code_regions(self, regions: List[Tuple[int, int]]):
        """
        Set custom code regions for backtrace validation

        Args:
            regions: List of (start, end) tuples defining valid code
                     address ranges.
                     Example: [(0x8000000, 0x8100000),
                               (0x20000000, 0x20010000)]
        """
        self.code_regions = regions

    def in_code_region(self, pc: int) -> bool:
        """
        Check if program counter is within valid code regions

        Args:
            pc: Program counter address to check

        Returns:
            True if PC is within a valid code region, False otherwise
        """
        if not self.code_regions:
            # If no regions defined, try to validate using symbol table
            try:
                # Try to find symbol at this address
                block = gdb.block_for_pc(pc)
                return block is not None
            except (gdb.error, Exception):
                return False

        for start, end in self.code_regions:
            if start <= pc < end:
                return True

        return False

    def is_thumb_blx_instruction(self, ins16: int) -> bool:
        """
        Check if 16-bit value is a Thumb BLX instruction

        Args:
            ins16: 16-bit instruction value

        Returns:
            True if instruction is BLX, False otherwise
        """
        return (ins16 & IMASK_T_BLX) == IOP_T_BLX

    def is_thumb_bl_instruction(self, ins16_high: int, ins16_low: int) -> bool:
        """
        Check if two 16-bit values form a Thumb BL instruction

        Thumb BL is a 32-bit instruction split into two 16-bit halfwords:
        - High halfword: 0b11110xxxxxxxxxxx (0xF000-0xF7FF or 0xF800-0xFFFF)
        - Low halfword:  0b11x1xxxxxxxxxxxx (0xD000-0xDFFF or 0xF000-0xFFFF)

        Args:
            ins16_high: High 16-bit halfword of instruction
            ins16_low: Low 16-bit halfword of instruction

        Returns:
            True if instruction is BL, False otherwise
        """
        return (ins16_high & IMASK_T_BL) == IOP_T_BL and (
            ins16_low & IMASK_T_BL_LOW
        ) == IOP_T_BL_LOW

    def read_word(self, addr: int, halfword: bool = False) -> Optional[int]:
        """
        Read a word from memory

        Args:
            addr: Memory address to read from
            halfword: If True, read 16-bit halfword; if False, read 32-bit word

        Returns:
            Word value or None if read fails
        """
        try:
            inferior = gdb.selected_inferior()
            size = 2 if halfword else 4
            mem = inferior.read_memory(addr, size)
            # ARM is typically little-endian
            return int.from_bytes(mem, byteorder="little")
        except (gdb.MemoryError, Exception):
            return None

    def unwind_from_stack(self, sp: int, top: int, max_frames: int = 100) -> List[int]:
        """
        Unwind call stack by analyzing branch instructions on the stack

        This function scans the stack memory from sp to top, looking for
        return addresses that point to BL or BLX instructions. It
        reconstructs the call stack without relying on frame pointers or
        unwind tables.

        Args:
            sp: Stack pointer (bottom of stack to scan)
            top: Top of stack region
            max_frames: Maximum number of frames to collect

        Returns:
            List of return addresses (instruction addresses before the call)
        """
        addresses = []

        while len(addresses) < max_frames and sp < top:
            # Read potential return address from stack
            addr = self.read_word(sp)
            sp += 4
            if addr is None or not self.in_code_region(addr):
                continue

            # ARM Thumb mode: LSB is 1, clear it to get actual address
            # Subtract 2 to get the address of the branch instruction
            addr = (addr & ~1) - 2

            # Verify the address is still in code region
            if not self.in_code_region(addr):
                continue

            # Read the instruction at this address
            ins16_low = self.read_word(addr, halfword=True)
            if ins16_low is None:
                continue

            # Check if it's a BLX instruction (16-bit)
            if self.is_thumb_blx_instruction(ins16_low):
                addresses.append(addr)
                continue

            # Check if it might be a BL instruction (32-bit)
            if (ins16_low & IMASK_T_BL_LOW) != IOP_T_BL_LOW:
                continue

            # Need to check high halfword (2 bytes before)
            addr_high = addr - 2
            if not self.in_code_region(addr_high):
                continue

            ins16_high = self.read_word(addr_high, halfword=True)
            if ins16_high is None:
                continue

            # Check if high and low halfwords form a BL instruction
            if self.is_thumb_bl_instruction(ins16_high, ins16_low):
                addresses.append(addr_high)

        return addresses

    def unwind_from_tcb(self, tcb, max_frames: int = 100) -> List[int]:
        """
        Unwind call stack from a TCB (Task Control Block)

        This function extracts stack information from the TCB and performs
        stack-based unwinding. It handles both running and non-running tasks,
        as well as interrupt contexts.

        Args:
            tcb: TCB structure (gdb.Value) or None for current task
            max_frames: Maximum number of frames to collect

        Returns:
            List of return addresses
        """

        if not tcb or int(tcb) == 0:
            return []

        # Get TCB fields
        try:
            # Get stack bounds
            stack_base = int(tcb["stack_base_ptr"])
            stack_size = int(tcb["adj_stack_size"])
            stack_top = stack_base + stack_size
            # Unwind from the saved stack pointer
            if stack_base and stack_top:
                return self.unwind_from_stack(stack_base, stack_top, max_frames)

        except (gdb.error, Exception):
            # Failed to access TCB fields
            pass

        return []


@autocompeletion.complete
class ArmSpBacktrace(gdb.Command):
    """
    Unwind ARM call stack using stack analysis (no unwind tables)

    Usage:
        armspbt                    - Unwind current task
        armspbt <tcb_address>      - Unwind specific task
        armspbt -s <sp> -t <top>   - Unwind stack range
        armspbt -f <max_frames>    - Limit number of frames

    Examples:
        armspbt
        armspbt 0x20001234
        armspbt -s 0x20001000 -t 0x20002000
        armspbt 0x20001234 -f 20
    """

    def __init__(self):
        # Check if arch is arm
        arch = gdb.execute("show architecture", to_string=True)
        if "arm" not in arch.lower():
            return

        super().__init__("armspbt", gdb.COMMAND_USER)

        parser = argparse.ArgumentParser(
            description="ARM stack-based backtrace unwinder"
        )
        parser.add_argument(
            "tcb", nargs="?", type=str, help="TCB address or expression"
        )
        parser.add_argument("-s", "--sp", type=str, help="Stack pointer address")
        parser.add_argument("-t", "--top", type=str, help="Top of stack address")
        parser.add_argument(
            "-f",
            "--frames",
            type=int,
            default=100,
            help="Maximum frames",
        )
        self.parser = parser

    @utils.dont_repeat_decorator
    def invoke(self, args, from_tty):
        try:
            pargs = self.parser.parse_args(gdb.string_to_argv(args))
        except SystemExit:
            return

        unwinder = ArmSpUnwind()

        # Unwind from explicit stack range
        if pargs.sp and pargs.top:
            try:
                sp = int(utils.parse_arg(pargs.sp))
                top = int(utils.parse_arg(pargs.top))
                addresses = unwinder.unwind_from_stack(sp, top, pargs.frames)
            except (gdb.error, Exception) as e:
                gdb.write(f"Error parsing stack range: {e}\n")
                return
        # Unwind from TCB
        elif pargs.tcb:
            try:
                tcb = utils.parse_arg(pargs.tcb)
                addresses = unwinder.unwind_from_tcb(tcb, pargs.frames)
            except (gdb.error, Exception) as e:
                gdb.write(f"Error parsing TCB: {e}\n")
                return
        # Unwind current task
        else:
            thread = gdb.selected_thread()
            if thread is None:
                gdb.write("No thread selected\n")
                return

            pid = utils.get_gdb_thread_pid(thread)
            tcb = utils.get_tcb(pid)
            print(f"Unwinding current task TCB: {hex(tcb)}, {utils.get_task_name(tcb)}")
            print(
                f"Stack: {tcb.stack_base_ptr} - {tcb.stack_base_ptr + tcb.adj_stack_size}"
            )

            addresses = unwinder.unwind_from_tcb(tcb, pargs.frames)

        if not addresses:
            gdb.write("No backtrace found\n")
            return

        # Format and display backtrace
        backtrace = Backtrace(addresses, break_null=False)
        gdb.write(str(backtrace))
        gdb.write(f"\nTotal frames: {len(addresses)}\n")
