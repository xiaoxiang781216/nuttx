############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/session/context.py
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

from contextlib import asynccontextmanager
from dataclasses import dataclass
from typing import Dict, Optional

from mcp.server.fastmcp import Context, FastMCP

from .gdb_session import GdbSession


class SessionManager:
    _instance: Optional["SessionManager"] = None

    def __init__(self):
        if SessionManager._instance is not None:
            raise RuntimeError("SessionManager is a singleton")
        self.sessions: Dict[str, GdbSession] = {}

    @classmethod
    def get_instance(cls) -> "SessionManager":
        if cls._instance is None:
            cls._instance = SessionManager()
        return cls._instance

    async def cleanup_all(self):
        for session_id, session in list(self.sessions.items()):
            await session.cleanup()
            self.sessions.pop(session_id, None)


@dataclass
class AppContext:
    """Application context for storing active sessions"""

    @property
    def sessions(self) -> Dict[str, GdbSession]:
        return SessionManager.get_instance().sessions


@asynccontextmanager
async def app_lifespan(server: FastMCP):
    session_manager = SessionManager.get_instance()  # noqa: F841
    try:
        yield AppContext()
    finally:
        pass


def get_session(ctx: Context, session_id: str) -> GdbSession:
    """Get GDB session by ID"""
    sessions = ctx.request_context.lifespan_context.sessions
    if session_id not in sessions:
        raise ValueError(f"No active GDB session with ID: {session_id}")
    return sessions[session_id]
