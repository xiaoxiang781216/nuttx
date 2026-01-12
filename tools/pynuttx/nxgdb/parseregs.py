############################################################################
# tools/pynuttx/nxgdb/parseregs.py
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

import re


def parse_registers(argument):
    """Parse register values from a loose format string.

    Args:
        argument: String containing register patterns like "REG: value", "REG:value",
                  or "REG value"

    Returns:
        List of tuples (reg_name, value) where:
        - reg_name: register name in lowercase (for GDB)
        - value: integer value parsed from hex string

    Returns empty list if no valid patterns found or if argument is empty.

    The string may contain patterns like "REG: value", "REG:value", or "REG value" where:
    - REG: register name (case-insensitive, will be converted to lowercase)
    - value: hex value (with or without 0x prefix)
    - separator: optional colon or one or more spaces

    Example: "PC: 0x1234 SP: 5678 LR: 0xABC"
    Example: "PC:0x1234 SP:5678 LR:0xABC"
    Example: "PC 0x1234 SP 5678 LR 0xABC"
    """
    if not argument:
        return []

    # Clean the input string - remove non-printable characters
    cleaned_string = "".join(
        char for char in argument if char.isprintable() or char.isspace()
    )

    # Pattern to match "REG: value", "REG:value", or "REG value" where:
    # - REG: one or more word characters (letters, digits, underscore)
    # - separator: optional colon or one or more whitespace characters
    # - value: hex number with optional 0x prefix
    # - \s* allows optional spaces around the separator
    pattern = r"(\w+)\s*(?::|\s+)\s*(?:0x)?([0-9a-fA-F]+)"

    matches = re.findall(pattern, cleaned_string)

    if not matches:
        return []

    registers = []

    for reg_name, hex_value in matches:
        # Convert register name to lowercase for GDB
        reg_name_lower = reg_name.lower()

        # Parse hex value
        try:
            value = int(hex_value, 16)
            registers.append((reg_name_lower, value))
        except ValueError:
            # Skip invalid hex values
            continue

    return registers
