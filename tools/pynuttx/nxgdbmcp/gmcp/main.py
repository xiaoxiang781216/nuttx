############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/main.py
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

import argparse
import asyncio

from mcp.server.fastmcp import FastMCP

from . import tools
from .session import SessionManager, app_lifespan

description = "nuttx-gdb is used to debug the NuttX kernel. It can start gdb with nuttx and crash coredump. "
description += "It can also load nxgdb gdbinit.py to use nxgdb commands, which include `memdump`, `memfind`, etc. "
description += (
    "However, nxgdb commands maybe are not high quality, so the output may be poor."
)


def main():

    parser = argparse.ArgumentParser(description="GDB mcp server for NuttX")
    parser.add_argument("--stdio", action="store_true", help="enable stdio transport")
    parser.add_argument("--port", type=int, default=20819, help="server port")

    args = parser.parse_args()

    if args.stdio:
        gdb_mcp = FastMCP(
            "nuttx-gdb",
            lifespan=app_lifespan,
            instructions=description,
        )
    else:
        gdb_mcp = FastMCP(
            "nuttx-gdb",
            lifespan=app_lifespan,
            instructions=description,
            stateless_http=True,
            host="0.0.0.0",
            port=args.port,
        )

    tools.register_command_tools(gdb_mcp)
    tools.register_control_flow_tools(gdb_mcp)
    tools.register_memory_tools(gdb_mcp)
    tools.register_session_tools(gdb_mcp)
    tools.register_util_tools(gdb_mcp)
    tools.register_value_tools(gdb_mcp)

    try:
        if args.stdio:
            gdb_mcp.run(transport="stdio")
        else:
            import uvicorn

            uvicorn.run(gdb_mcp.streamable_http_app(), host="0.0.0.0", port=args.port)
    except KeyboardInterrupt:
        print("Cleaning up GDB sessions...")
        session_manager = SessionManager.get_instance()
        asyncio.run(session_manager.cleanup_all())
        print("GDB-MCP server stopped")


if __name__ == "__main__":
    main()
