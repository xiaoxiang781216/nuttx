############################################################################
# tools/gdb/tests/test_runtime_memory.py
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

import unittest

import gdb

# The following test cases require running the program as
# we need to access the memory of the program


class TestMemory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        pass

    def check_output(self, out, expect="Total Blks  Total Size"):
        if expect not in out:
            self.fail(f"Got: {out}")

    def test_memdump(self):
        out = gdb.execute("memdump", to_string=True)
        self.check_output(out)

    def test_memdump_pid(self):
        out = gdb.execute("memdump -p 1", to_string=True)
        self.check_output(out)

    def test_memdump_addr(self):
        heap_start = gdb.parse_and_eval("g_mmheap")["mm_heapstart"][0]
        out = gdb.execute(f"memdump -a {hex(heap_start)}", to_string=True)
        self.check_output(out, expect="found belongs to")

    def test_memdump_used(self):
        out = gdb.execute("memdump --used", to_string=True)
        self.check_output(out)

    def test_memleak(self):
        out = gdb.execute("memleak", to_string=True)
        self.check_output(out, expect="total leak memory is")

    # memmap may stuck because of huge 2GB memory qemu provides.
    # def test_memmap(self):
    #     out = gdb.execute("memmap", to_string=True)

    def test_memfrag(self):
        out = gdb.execute("memfrag", to_string=True)
        self.check_output(out, expect="memory fragmentation rate")

    def test_mempool(self):
        out = gdb.execute("mempool", to_string=True)
        self.check_output(
            out, expect="total    bsize    nused    nfree   nifree  nwaiter"
        )
