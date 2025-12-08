############################################################################
# tools/pynuttx/gdbrpc/examples/breakpoint.py
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

import logging
import queue

import gdbrpc

# Set a breakpoint and notify client when hit
# Note: temporary breakpoint is used here
# Note: backtrace is printed when breakpoint is hit


class SetBreakpoint(gdbrpc.Request):
    def __init__(self, location: str):
        super().__init__()
        self.location = location

    def __call__(self, q: queue.Queue):
        import gdb

        class Bp(gdb.Breakpoint):
            import queue

            def __init__(self, spec: str, q: queue.Queue):
                # Note that we use temporary breakpoint, so that it is automatically removed after being hit
                super().__init__(spec, internal=False, temporary=True)

                # The q is used to notify GDBRPC that the __call__ is finally finished.
                self._q = q

            def stop(self) -> bool:
                # Since we are in GDB's context, we can use gdb module directly
                bt = gdb.execute("bt", to_string=True)

                # Put any message to the queue to notify breakpoint hit
                self._q.put(bt)
                return True

        bp = Bp(self.location, q)
        assert bp.is_valid()
        return self.location


class BreakpointHit(gdbrpc.PostRequest):
    def __init__(self):
        super().__init__()

    def __call__(self, result):
        print(f"Breakpoint hit!:\n{result}")


if __name__ == "__main__":

    client = gdbrpc.Client(
        log_level=logging.ERROR
    )  # Use default localhost:20819, change if needed

    assert client.connect()

    bp = SetBreakpoint("up_idle")
    notifier = BreakpointHit()
    print(client.call(bp, post_request=notifier, timeout=10))

    # Now you can do other things, or wait for breakpoint hit notification
    print("Waiting for breakpoint to be hit...")
    notifier.finish.wait()
    print("Breakpoint handling completed.")
    client.disconnect()
