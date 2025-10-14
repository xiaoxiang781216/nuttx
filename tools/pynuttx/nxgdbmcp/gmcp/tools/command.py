############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/tools/command.py
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

from mcp.server.fastmcp import Context

from ..utils import _exec_command


def register_command_tools(gdb_mcp):

    @gdb_mcp.tool()
    async def gdb_command(ctx: Context, session_id: str, command: str) -> str:
        """Execute a GDB command"""
        return await _exec_command(ctx, session_id, command)

    @gdb_mcp.tool()
    async def gdb_get_nxgdb_cmd_list(ctx: Context, session_id: str) -> str:
        """Get nxgdb command help info, nxgdb is a set of gdb scripts for debugging NuttX.
        Note that nxgdb may have issues. When analyzing NuttX crashes,
        you need to determine whether the problem is with nxgdb or with NuttX itself.
        """
        return await _exec_command(ctx, session_id, "help user-defined")
