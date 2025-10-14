############################################################################
# tools/pynuttx/nxgdbmcp/src/gmcp/session/gdb_session.py
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

import gdbrpc


class GdbSession:
    def __init__(
        self,
        session_id: str,
        host: Optional[str] = None,
        port: Optional[int] = None,
    ):

        self.id = session_id
        self.host = host
        self.port = port
        self.session = (
            gdbrpc.Client(self.host, self.port)
            if self.host and self.port
            else gdbrpc.Client()
        )

        assert self.session.connect()

    async def execute_command(self, command: str) -> str:
        if not self.session:
            raise RuntimeError("GDB socket connection not established")

        try:
            response = self.session.call(gdbrpc.ShellExec(command))
            return response

        except Exception as e:
            raise RuntimeError(f"Error executing command '{command}': {str(e)}")

    async def cleanup(self):
        try:
            if self.session:
                self.session.disconnect()
        except Exception as e:
            print(f"Error during cleanup: {e}")
        finally:
            self.session = None
            print("GDB session cleanup completed")
