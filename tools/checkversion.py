#!/usr/bin/env python3
# tools/checkversion.py
#
# SPDX-License-Identifier: Apache-2.0
#
# Licensed to the Apache Software Foundation (ASF) under one or more
# contributor license agreements.  See the NOTICE file distributed with
# this work for additional information regarding copyright ownership.
# The ASF licenses this file to you under the Apache License, Version 2.0
# (the "License"); you may not use this file except in compliance with
# the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

import os
import re
import shutil
import subprocess
import sys


def check_ripgrep_available() -> bool:
    return shutil.which("rg") is not None


def search_with_ripgrep(file_path: str) -> list[tuple[int, str]]:
    results = []
    try:
        cmd = [
            "rg",
            "-a",
            "-b",
            "-o",
            r"[a-f0-9]+(-dirty)? [A-Z][a-z]{2} {1,2}\d{1,2} \d{4} \d{2}:\d{2}:\d{2}",
            file_path,
        ]

        result = subprocess.run(cmd, capture_output=True, text=True)

        if result.returncode == 0 and result.stdout.strip():
            for line in result.stdout.strip().splitlines():
                if ":" in line:
                    offset_str, matched_text = line.split(":", 1)
                    results.append((int(offset_str), matched_text))
        return results

    except Exception as e:
        print(f"Error running ripgrep: {e}", file=sys.stderr)
        return results


def search_with_python(file_path: str) -> list[tuple[int, str]]:
    results = []
    try:
        pattern = re.compile(
            rb"[a-f0-9]+(-dirty)? [A-Z][a-z]{2} {1,2}\d{1,2} \d{4} \d{2}:\d{2}:\d{2}"
        )

        with open(file_path, "rb") as f:
            content = f.read()

        for match in pattern.finditer(content):
            offset = match.start()
            matched_text = match.group().decode("utf-8", errors="replace")
            results.append((offset, matched_text))
        return results

    except Exception as e:
        print(f"Error searching file: {e}", file=sys.stderr)
        return results


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <file_path>", file=sys.stderr)
        sys.exit(1)

    file_path = sys.argv[1]

    if not os.path.exists(file_path):
        print(f"Error: File '{file_path}' not found", file=sys.stderr)
        sys.exit(1)

    if check_ripgrep_available():
        print("Using ripgrep for search...", file=sys.stderr)
        results = search_with_ripgrep(file_path)
    else:
        print("ripgrep not found, using Python implementation...", file=sys.stderr)
        results = search_with_python(file_path)

    if results:
        for offset, matched_text in results:
            print(f"0x{offset:08x}:{matched_text}")
    else:
        print("No match found", file=sys.stderr)
        sys.exit(1)
