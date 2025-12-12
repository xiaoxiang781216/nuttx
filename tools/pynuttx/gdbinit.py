############################################################################
# tools/pynuttx/gdbinit.py
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

import sys
from os import path
from pathlib import Path

here = path.dirname(path.abspath(__file__))

nuttx_path = Path(__file__).resolve().parents[2]
libcxx_path = path.join(
    nuttx_path, "libs", "libxx", "libcxx", "libcxx", "utils", "gdb", "libcxx"
)

if __name__ == "__main__":
    if here not in sys.path:
        sys.path.insert(0, here)

    if path.exists(libcxx_path):
        sys.path.insert(0, libcxx_path)
        import gdb
        import printers

        printer = printers.LibcxxPrettyPrinter("libcxx")
        gdb.pretty_printers.append(printer)
        printers.register_libcxx_printer_loader()

    modules = ("gdbrpc", "nxelf", "nxgdb", "nxreg", "nxstub", "nxtrace")
    for key in list(sys.modules):
        if key.startswith(modules):
            del sys.modules[key]

    import nxgdb  # noqa: F401

    print("GDB Plugin Loaded Successfully")
