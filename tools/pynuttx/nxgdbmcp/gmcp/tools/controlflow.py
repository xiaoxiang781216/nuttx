############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/tools/controlflow.py
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

import re
from typing import Optional

from mcp.server.fastmcp import Context

from ..context import get_session
from ..utils import _exec_command, error_handler


def _format_breakpoint_result(
    location: str, condition: Optional[str] = None, output: str = ""
) -> str:
    msg = f"Breakpoint set at: {location}"
    if condition:
        msg += f" with condition: {condition}"
    msg += "\n\nOutput:\n" + output
    return msg


@error_handler
async def _set_breakpoint(
    ctx: Context, session_id: str, location: str, condition: Optional[str] = None
):
    session = get_session(ctx, session_id)
    output = await session.execute_command(f"break {location}")
    if condition:
        match = re.search(r"Breakpoint (\d+)", output)
        if match:
            bp_num = match.group(1)
            condition_output = await session.execute_command(
                f"condition {bp_num} {condition}"
            )
            output += f"\n{condition_output}"
    return _format_breakpoint_result(location, condition, output)


@error_handler
async def _step(ctx: Context, session_id: str, instructions: bool = False) -> str:
    session = get_session(ctx, session_id)
    command = "stepi" if instructions else "step"
    output = await session.execute_command(command)
    return (
        f"Stepped over {'instruction' if instructions else 'function call'}\n\nOutput:\n"
        f"{output}"
    )


@error_handler
async def _next(ctx: Context, session_id: str, instructions: bool = False) -> str:
    session = get_session(ctx, session_id)
    command = "nexti" if instructions else "next"
    output = await session.execute_command(command)
    return f"Stepped over {'instruction' if instructions else 'function call'}\n\nOutput:\n{output}"


@error_handler
async def _backtrace(
    ctx: Context, session_id: str, full: bool = False, limit: Optional[int] = None
) -> str:
    session = get_session(ctx, session_id)

    # Build backtrace command with options
    command = "bt"
    if full:
        command += " full"
    if limit is not None:
        command += f" {limit}"

    output = await session.execute_command(command)
    limit_str = f" (limit: {limit})" if limit else ""
    return f"Backtrace{' (full)' if full else ''}{limit_str}:\n\n{output}"


@error_handler
async def _process_info(ctx: Context, session_id: str) -> str:
    session = get_session(ctx, session_id)
    output = await session.execute_command("info program")
    pid_output = await session.execute_command("info proc")
    return f"Process information:\n\n{output}\n\nDetails:\n{pid_output}"


def register_control_flow_tools(gdb_mcp):
    @gdb_mcp.tool()
    async def gdb_set_breakpoint(
        ctx: Context, session_id: str, location: str, condition: Optional[str] = None
    ) -> str:
        """Set a breakpoint"""
        return await _set_breakpoint(ctx, session_id, location, condition)

    @gdb_mcp.tool()
    async def gdb_continue(ctx: Context, session_id: str) -> str:
        """Continue program execution"""
        return await _exec_command(ctx, session_id, "continue")

    @gdb_mcp.tool()
    async def gdb_step(
        ctx: Context, session_id: str, instructions: bool = False
    ) -> str:
        """Step program execution"""
        return await _step(ctx, session_id, instructions)

    @gdb_mcp.tool()
    async def gdb_next(
        ctx: Context, session_id: str, instructions: bool = False
    ) -> str:
        """Step over function calls"""
        return await _next(ctx, session_id, instructions)

    @gdb_mcp.tool()
    async def gdb_finish(ctx: Context, session_id: str) -> str:
        """Execute until the current function returns"""
        return await _exec_command(ctx, session_id, "finish")

    @gdb_mcp.tool()
    async def gdb_backtrace(
        ctx: Context, session_id: str, full: bool = False, limit: Optional[int] = None
    ) -> str:
        return await _backtrace(ctx, session_id, full, limit)

    @gdb_mcp.tool()
    async def gdb_breakpoint_list(ctx: Context, session_id: str) -> str:
        """List all breakpoints"""
        return await _exec_command(ctx, session_id, "info breakpoints")

    @gdb_mcp.tool()
    async def gdb_breakpoint_delete(
        ctx: Context, session_id: str, breakpoint_id: int
    ) -> str:
        """Delete a breakpoint"""
        return await _exec_command(ctx, session_id, f"delete {breakpoint_id}")

    @gdb_mcp.tool()
    async def gdb_thread_list(ctx: Context, session_id: str) -> str:
        """List all threads in the current host process"""
        return await _exec_command(ctx, session_id, "info threads")

    @gdb_mcp.tool()
    async def gdb_thread_select(ctx: Context, session_id: str, thread_id: int) -> str:
        """Select a specific thread"""
        return await _exec_command(ctx, session_id, f"thread {thread_id}")

    @gdb_mcp.tool()
    async def gdb_process_info(ctx: Context, session_id: str) -> str:
        """Get information about the current process"""
        return await _process_info(ctx, session_id)

    @gdb_mcp.tool()
    async def nxgdb_list_thread(ctx: Context, session_id: str) -> str:
        """List all NuttX threads in the current system.

        Executes the 'info nxthread' command to retrieve thread information using the
        NuttX thread model, which provides more detailed thread data than standard GDB
        thread listings.

        Args:
            ctx (Context): The context object containing runtime/session information.
            session_id (str): Identifier for the GDB debugging session.

        Returns:
            str: Detailed thread information from the NuttX thread model including
                 thread IDs, states, priorities, and stack information.
        """
        return await _exec_command(ctx, session_id, "info nxthreads")

    @gdb_mcp.tool()
    async def nxgdb_thread_apply(
        ctx: Context, session_id: str, ids: str, cmd: str
    ) -> str:
        """Apply a GDB command to some NuttX threads using 'nthread apply'.

        Executes the 'nthread apply <all|id list> <cmd>' command, where:
        - 'all' applies the command to all NuttX threads
        - 'id list' applies the command to specific thread IDs (comma or space separated)
        - 'cmd' is the GDB command to execute (e.g., 'bt')

        Args:
            ctx (Context): The context object containing runtime/session information.
            session_id (str): Identifier for the GDB debugging session.
            ids (str): 'all' or a list of thread IDs.
            cmd (str): The GDB command to execute for each thread.

        Returns:
            str: The output from the 'nthread apply' command for the specified threads.
        """
        return await _exec_command(ctx, session_id, f"nthread apply {ids} {cmd}")

    @gdb_mcp.tool()
    async def nxgdb_thread_select(ctx: Context, session_id: str, thread_id: int) -> str:
        """Select a specific NuttX thread by pid.

        The 'nxthread <pid>' command switches to the thread with the given NuttX thread model pid
        by restoring the registers for that thread context.
        """
        return await _exec_command(ctx, session_id, f"nxthread {thread_id}")
