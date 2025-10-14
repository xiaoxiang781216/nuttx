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

from typing import Optional

from mcp.server.fastmcp import Context

from ..context import get_session
from ..utils import _exec_command, error_handler


@error_handler
async def _examine(
    ctx: Context,
    session_id: str,
    expression: str,
    format: str = "x",
    count: int = 1,
) -> str:
    session = get_session(ctx, session_id)

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
    command = f"x/{count}{gdb_format} {expression}"
    output = await session.execute_command(command)
    return f"Examine {expression} (format: {format}, count: {count}):\n\n{output}"


@error_handler
async def _watchpoint(
    ctx: Context, session_id: str, expression: str, watch_type: str = "write"
) -> str:
    session = get_session(ctx, session_id)
    # Map watch types to GDB options
    watch_options = {"read": "r", "write": "w", "read_write": "aw"}
    option = watch_options.get(watch_type, "w")

    if option == "r":
        output = await session.execute_command(f"rwatch {expression}")
    elif option == "aw":
        output = await session.execute_command(f"awatch {expression}")
    else:
        output = await session.execute_command(f"watch {expression}")
    return f"Watchpoint set on {expression} (type: {watch_type})\n\nOutput:\n{output}"


def register_value_tools(gdb_mcp):
    @gdb_mcp.tool()
    async def gdb_print(ctx: Context, session_id: str, expression: str) -> str:
        """Print value of expression"""
        return await _exec_command(ctx, session_id, f"print {expression}")

    @gdb_mcp.tool()
    async def gdb_examine(
        ctx: Context,
        session_id: str,
        expression: str,
        format: str = "x",
        count: int = 1,
    ) -> str:
        """Examine memory"""
        return await _examine(ctx, session_id, expression, format, count)

    @gdb_mcp.tool()
    async def gdb_info_registers(
        ctx: Context, session_id: str, register: Optional[str] = None
    ) -> str:
        """Display registers"""
        command = "info registers"
        command += f" {register}" if register is not None else ""
        return await _exec_command(ctx, session_id, command)

    @gdb_mcp.tool()
    async def gdb_watchpoint(
        ctx: Context, session_id: str, expression: str, watch_type: str = "write"
    ) -> str:
        """Set a watchpoint on a variable or memory address"""
        return await _watchpoint(ctx, session_id, expression, watch_type)

    @gdb_mcp.tool()
    async def gdb_expression(ctx: Context, session_id: str, expression: str) -> str:
        """Evaluate an expression in the current frame"""
        return await _exec_command(ctx, session_id, f"print {expression}")
