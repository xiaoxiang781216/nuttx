############################################################################
# tools/pynuttx/gdbrpc/examples/gdbscript.py
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

import os
import queue

import gdbrpc


class ScriptLoader(gdbrpc.Request):
    def __init__(self, path):
        super().__init__()
        self.path = path

    def __call__(self, q: queue.Queue):
        # Executed on GDB, the server side
        import gdb

        try:
            result = gdb.execute(f"source {self.path}", to_string=True)
        except Exception as e:
            result = str(e)
        q.put(result)


if __name__ == "__main__":

    client = gdbrpc.Client()  # Use default localhost:20819, change if needed

    assert client.connect()

    script_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), "examplescript.py")
    )

    print(client.call(ScriptLoader(script_path), timeout=10))

    client.disconnect()
