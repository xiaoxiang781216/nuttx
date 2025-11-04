############################################################################
# tools/pynuttx/nxgdb/gdbrpc.py
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

from gdbrpc import commands

from . import autocompeletion

# Override the commands with autocompletion decorators


@autocompeletion.complete
class StartSocketServer(commands.StartSocketServer):
    """Start GDB socket server command"""

    pass


class StopSocketServer(commands.StopSocketServer):
    """Stop GDB socket server command"""

    pass


class SocketServerStatus(commands.SocketServerStatus):
    """Get socket server status command"""

    pass


# !NOTE: This command is under development and may not work as expected
@autocompeletion.complete
class StartSocketClient(commands.StartSocketClient):
    """Connect to GDB socket server"""

    pass
