############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/tools/value.py
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

from typing import Optional, Union

from mcp.server.fastmcp import Context

from ..context import get_session
from ..utils import _exec_command, error_handler


@error_handler
async def _examine(
    ctx: Context,
    session_id: str,
    expression: Union[str, int],
    format: str = "x",
    count: int = 1,
) -> str:
    session = get_session(ctx, session_id)

    # Convert int to string for GDB
    expr_str = str(expression)

    # Map format codes to GDB format specifiers
    format_map = {
        "x": "x",  # hex
        "d": "d",  # decimal
        "u": "u",  # unsigned decimal
        "o": "o",  # octal
        "t": "t",  # binary
        "i": "i",  # instruction
        "c": "c",  # char
        "f": "f",  # float
        "s": "s",  # string
    }

    gdb_format = format_map.get(format, "x")
    command = f"x/{count}{gdb_format} {expr_str}"
    output = await session.execute_command(command)
    return f"Examine {expr_str} (format: {format}, count: {count}):\n\n{output}"


@error_handler
async def _watchpoint(
    ctx: Context,
    session_id: str,
    expression: Union[str, int],
    watch_type: str = "write",
) -> str:
    session = get_session(ctx, session_id)

    # Convert int to string for GDB
    expr_str = str(expression)

    # Map watch types to GDB options
    watch_options = {"read": "r", "write": "w", "read_write": "aw"}
    option = watch_options.get(watch_type, "w")

    if option == "r":
        output = await session.execute_command(f"rwatch {expr_str}")
    elif option == "aw":
        output = await session.execute_command(f"awatch {expr_str}")
    else:
        output = await session.execute_command(f"watch {expr_str}")
    return f"Watchpoint set on {expr_str} (type: {watch_type})\n\nOutput:\n{output}"


def register_value_tools(gdb_mcp):
    @gdb_mcp.tool()
    async def gdb_print(
        ctx: Context, session_id: str, expression: Union[str, int]
    ) -> str:
        """Print value of expression.

        Args:
            session_id: The GDB session identifier
            expression: A string containing the GDB expression to evaluate
                (e.g., "variable_name", "*ptr", "array[0]"),
                or an integer representing a decimal memory address
        """
        expr_str = str(expression)
        return await _exec_command(ctx, session_id, f"print {expr_str}")

    @gdb_mcp.tool()
    async def gdb_examine(
        ctx: Context,
        session_id: str,
        expression: Union[str, int],
        format: str = "x",
        count: int = 1,
    ) -> str:
        """Examine memory at the specified address or expression.

        Args:
            session_id: The GDB session identifier
            expression: A string containing the memory address or expression
                (e.g., "0x12345678", "&variable", "ptr"),
                or an integer representing a decimal memory address
            format: Display format:
                "x" (hex)
                "d" (decimal)
                "u" (unsigned)
                "o" (octal)
                "t" (binary)
                "i" (instruction)
                "c" (char)
                "f" (float)
                "s" (string)
            count: Number of items to display
        """
        return await _examine(ctx, session_id, expression, format, count)

    @gdb_mcp.tool()
    async def gdb_info_registers(
        ctx: Context, session_id: str, register: Optional[str] = None
    ) -> str:
        """Display CPU register values.

        Args:
            session_id: The GDB session identifier
            register: Optional register name as a string
                (e.g., "rax", "rsp", "pc"). If None, displays all registers.
        """
        command = "info registers"
        command += f" {register}" if register is not None else ""
        return await _exec_command(ctx, session_id, command)

    @gdb_mcp.tool()
    async def gdb_watchpoint(
        ctx: Context,
        session_id: str,
        expression: Union[str, int],
        watch_type: str = "write",
    ) -> str:
        """Set a watchpoint on a variable or memory address.

        Args:
            session_id: The GDB session identifier
            expression: A string containing the variable name or memory address to watch
                (e.g., "my_variable", "*0x12345678"),
                or an integer representing a decimal memory address
            watch_type: Type of watchpoint - "write" (default), "read", or "read_write"
        """
        return await _watchpoint(ctx, session_id, expression, watch_type)

    @gdb_mcp.tool()
    async def gdb_expression(
        ctx: Context, session_id: str, expression: Union[str, int]
    ) -> str:
        """Evaluate an expression in the current frame.

        Args:
            session_id: The GDB session identifier
            expression: A string containing the expression to evaluate
                (e.g., "x + y", "func(arg)", "sizeof(struct)"),
                or an integer representing a decimal memory address
        """
        expr_str = str(expression)
        return await _exec_command(ctx, session_id, f"print {expr_str}")
