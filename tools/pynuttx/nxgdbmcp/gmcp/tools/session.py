############################################################################
# tools/pynuttx/gmcp/nxgdbmcp/gmcp/tools/session.py
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

import uuid
from typing import Dict, List, Optional

from mcp.server.fastmcp import Context

from ..context import get_session
from ..session.gdb_session import GdbSession
from ..utils import _exec_command, error_handler, get_available_port


@error_handler
async def _start(host: Optional[str] = None, port: Optional[int] = None) -> GdbSession:
    session_id = str(uuid.uuid4())

    session = GdbSession(session_id, host, port)

    return session


@error_handler
async def _load_program(
    ctx: Context,
    session_id: str,
    program: str,
    arguments: Optional[List[str]] = None,
) -> str:
    session = get_session(ctx, session_id)

    # Load executable
    output = await session.execute_command(f"file {program}")

    # Set program arguments if provided
    if arguments:
        args_str = " ".join(f"{arg}" for arg in arguments)
        args_output = await session.execute_command(f"set args {args_str}")
        output += f"\n{args_output}"

    return f"Program loaded: {program}\n\nOutput:\n{output}"


@error_handler
async def _load_program_with_parsed_core(
    ctx: Context,
    session_id: str,
    program: str,
    core_path: str,
    nxgdb_path: str = "nuttx/tools/pynuttx/gdbinit.py",
) -> str:
    session = get_session(ctx, session_id)

    # First load the program
    file_output = await session.execute_command(f"file {program}")

    await session.execute_command(f"source {nxgdb_path}")

    # Find an available port for the stub
    port = get_available_port()
    # Then load the core file with the port
    core_output = await session.execute_command(
        f"target nxstub --core {core_path} --port {port}"
    )

    return f"Core file loaded: {core_path} \n\nOutput:\n{file_output}\n{core_output}"


@error_handler
async def _load_program_with_parsed_memdump(
    ctx: Context,
    session_id: str,
    program: str,
    rawfiles: list[str],
    nxgdb_path: str = "nuttx/tools/pynuttx/gdbinit.py",
) -> str:
    session = get_session(ctx, session_id)

    # First load the program
    file_output = await session.execute_command(f"file {program}")

    await session.execute_command(f"source {nxgdb_path}")

    # Find an available port for the stub
    port = get_available_port()
    # Prepare rawfile arguments string
    rawfile_args = " ".join(rawfiles)
    # Then load the memory dump(s) with the port
    core_output = await session.execute_command(
        f"target nxstub --rawfile {rawfile_args} --port {port}"
    )

    return f"Memory dump(s) loaded: {rawfile_args} \n\nOutput:\n{file_output}\n{core_output}"


def register_session_tools(gdb_mcp):
    @gdb_mcp.tool()
    async def gdb_connect(
        ctx: Context, host: Optional[str] = None, port: Optional[int] = None
    ) -> str:
        """Establish a connection with GDB via socket with optional host and port.
        This action will not load coredump or do anything else.

        Args:
            ctx (Context): The context object containing runtime/session information.
            host (Optional[str]): Host IP for the GDB debugging session, default to localhost if None.
            port (Optional[int]): Port number for the GDB debugging session, default to 20819 if None.

        Returns:
            session (GdbSession): An instance of GdbSession representing the established connection.
        """
        session = await _start(host, port)

        ctx.request_context.lifespan_context.sessions[session.id] = session
        return f"GDB session connected, ID: {session.id}\n"

    @gdb_mcp.tool()
    async def gdb_target_remote(ctx: Context, session_id: str, remote: str) -> str:
        """Connect to a remote gdbserver via target remote ip:port"""
        return await _exec_command(ctx, session_id, f"target remote {remote}")

    @gdb_mcp.tool()
    async def gdb_start_with_remote(
        ctx: Context,
        program: str,
        remote: str,
        host: Optional[str] = None,
        port: Optional[int] = None,
    ) -> str:
        """Establish a connection with GDB via socket with optional host and port,
        load file, and connect to remote target in one step."""
        # Start GDB session first
        session = await _start(host, port)

        output1 = await session.execute_command(f"file {program}")
        output2 = await session.execute_command(f"target remote {remote}")

        ctx.request_context.lifespan_context.sessions[session.id] = session
        return f"GDB session connected, ID: {session.id}\n\nFile output:\n{output1}\n\nRemote output:\n{output2}"

    @gdb_mcp.tool()
    async def gdb_source_python(ctx: Context, session_id: str, script_path: str) -> str:
        """Source a python script in GDB (using 'source <script>')"""
        return await _exec_command(ctx, session_id, f"source {script_path}")

    @gdb_mcp.tool()
    async def gdb_load_program(
        ctx: Context,
        session_id: str,
        program: str,
        arguments: Optional[List[str]] = None,
    ) -> str:
        """Load a program into GDB without coredump"""
        return await _load_program(ctx, session_id, program, arguments)

    @gdb_mcp.tool()
    async def gdb_load_program_with_core(
        ctx: Context, session_id: str, program: str, core_path: str
    ) -> str:
        """Load program with a core dump file"""
        output = await _exec_command(ctx, session_id, f"file {program}")
        output += await _exec_command(ctx, session_id, f"core {core_path}")
        return output

    @gdb_mcp.tool()
    async def gdb_load_program_with_parsed_core(
        ctx: Context,
        session_id: str,
        program: str,
        core_path: str,
        nxgdb_path: str = "nuttx/tools/pynuttx/gdbinit.py",
    ) -> str:
        """
        Load a program and parse the coredump using nxgdb, so GDB can debug with the NuttX thread model.

        This function first loads the executable with the 'file' command,
        then sources nxgdb(e.g., nuttx/tools/pynuttx/gdbinit.py),
        and finally loads the coredump with 'target nxstub --core <core> --port <port>'.
        This allows GDB to correctly recognize NuttX threads and debugging context,
        greatly improving the debugging experience.
        Note: nxgdb must be loaded before loading the coredump. The port is dynamically selected.
        """
        return await _load_program_with_parsed_core(
            ctx, session_id, program, core_path, nxgdb_path
        )

    @gdb_mcp.tool()
    async def gdb_load_program_with_parsed_memdump(
        ctx: Context,
        session_id: str,
        program: str,
        rawfiles: List[str],
        nxgdb_path: str = "nuttx/tools/pynuttx/gdbinit.py",
    ) -> str:
        """
        Load a program and parse one or more memory dump files using nxgdb, so GDB can debug with the NuttX thread model.

        Args:
            ctx: Context object
            session_id: GDB session ID
            program: Path to the program executable
            rawfiles: List of memory dump file arguments, each in the format 'memdump.bin:address'
            nxgdb_path: Path to nxgdb Python script (default: nuttx/tools/pynuttx/gdbinit.py)

        This function first loads the executable with the 'file' command,
        then sources nxgdb (e.g., nuttx/tools/pynuttx/gdbinit.py),
        and finally loads the memory dump(s) with
        'target nxstub --rawfile <file1:addr1> <file2:addr2> ... --port <port>'.
        This allows GDB to correctly recognize NuttX threads and debugging context,
        greatly improving the debugging experience.
        Note: nxgdb must be loaded before loading the memory dump. The port is dynamically selected.
        """
        return await _load_program_with_parsed_memdump(
            ctx, session_id, program, rawfiles, nxgdb_path
        )

    @gdb_mcp.tool()
    async def gdb_attach(ctx: Context, session_id: str, pid: int) -> str:
        """Attach to a running process"""
        return await _exec_command(ctx, session_id, f"attach {pid}")

    @gdb_mcp.tool()
    async def gdb_terminate(ctx: Context, session_id: str) -> str:
        """Terminate a GDB session"""
        try:
            session = get_session(ctx, session_id)
            await session.cleanup()
            ctx.request_context.lifespan_context.sessions.pop(session_id, None)
            return f"GDB session terminated: {session_id}"

        except ValueError as e:
            return str(e)
        except Exception as e:
            return f"Failed to terminate GDB session: {str(e)}"

    @gdb_mcp.tool()
    def gdb_list_sessions(ctx: Context) -> str:
        """List all active GDB sessions"""
        sessions: Dict[str, GdbSession] = ctx.request_context.lifespan_context.sessions
        session_info = []

        for session_id, session in sessions.items():
            session_info.append(
                {
                    "id": session_id,
                    "host": session.host,
                    "port": session.port,
                }
            )

        return f"Active GDB Sessions ({len(sessions)}):\n\n{session_info}"

    @gdb_mcp.tool()
    async def gdb_run(ctx: Context, session_id: str) -> str:
        """Run the loaded program"""
        return await _exec_command(ctx, session_id, "run")

    @gdb_mcp.tool()
    async def gdb_kill(ctx: Context, session_id: str) -> str:
        """Kill the running process"""
        return await _exec_command(ctx, session_id, "kill")
