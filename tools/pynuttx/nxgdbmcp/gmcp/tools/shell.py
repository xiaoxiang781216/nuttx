############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/tools/shell.py
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


def register_shell_tools(gdb_mcp):
    @gdb_mcp.tool()
    async def nxgdb_dmesg(ctx: Context, session_id: str) -> str:
        """View device RAM logs, including ramlog and (if available) rpmsg syslog.

        This tool retrieves RAM log information from the device to facilitate debugging and analysis
        of system runtime status. The log content includes ramlog and rpmsg syslog (if supported by the device).

        Args:
            ctx (Context): The context object containing runtime/session information.
            session_id (str): Identifier for the GDB debugging session.

        Returns:
            str: String containing the RAM log content.
        """
        return await _exec_command(ctx, session_id, "dmesg")

    @gdb_mcp.tool()
    async def nxgdb_ps(ctx: Context, session_id: str) -> str:
        """Display a snapshot of current processes and threads.

        Executes the `ps` command to retrieve process and thread information using the
        NuttX thread model, providing a Linux ps-like view of the system state.

        Args:
            ctx (Context): The context object containing runtime/session information.
            session_id (str): Identifier for the GDB debugging session.

        Returns:
            str: Process snapshot showing thread information from the NuttX thread model
                 rather than standard GDB thread listings.
        """
        return await _exec_command(ctx, session_id, "ps")
