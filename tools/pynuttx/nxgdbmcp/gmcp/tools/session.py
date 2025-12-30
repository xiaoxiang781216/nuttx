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
from typing import Dict, Optional

from mcp.server.fastmcp import Context

from ..context import get_session
from ..session.gdb_session import GdbSession
from ..utils import error_handler


@error_handler
async def _start(host: Optional[str] = None, port: Optional[int] = None) -> GdbSession:
    session_id = str(uuid.uuid4())

    session = GdbSession(session_id, host, port)

    return session


def register_session_tools(gdb_mcp):
    @gdb_mcp.tool()
    async def gdb_connect(
        ctx: Context, host: Optional[str] = None, port: Optional[int] = None
    ) -> str:
        """Establish a connection with GDB via socket with optional host and port"""
        session = await _start(host, port)

        ctx.request_context.lifespan_context.sessions[session.id] = session
        return f"GDB session connected, ID: {session.id}\n"

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
