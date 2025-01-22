#!/usr/bin/env bash
############################################################################
# tools/abi_replace.sh
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

usage() {
  echo "Usage: $0 -i input -w wrap.dat [-o output] [-d] [-p prefix]"
  echo "  -i input: input file"
  echo "  -w wrap.dat: replace symbol map"
  echo "  -o output: output file"
  echo "  -d: debug mode"
  echo "  -p prefix: prefix of tool"
  exit 1
}

while getopts "i:w:o:d:p:" opt; do
  case $opt in
    i) input="$OPTARG" ;;
    w) wrap="$OPTARG" ;;
    o) output="$OPTARG" ;;
    d) debug="$OPTARG" ;;
    p) prefix="$OPTARG" ;;
    *) usage ;;
  esac
done

debug() {
  if [ "$debug" == "debug" ]; then
    echo "$1"
  fi
}

if [ "$debug" == "verbose" ]; then
  set -x
fi

NM=${prefix}nm
OBJCOPY=${prefix}objcopy

if [ -z "$input" ] || [ -z "$wrap" ]; then
  echo "Error: input file and wrap.dat is required"
  usage
fi

# If output is not specified, use input as output
if [ -z "$output" ]; then
  output="$input"
fi

# Read symbol list from wrap.dat
old_symbol_list=$(grep -v '^$' "$wrap" | awk '{print $1}')
new_symbol_list=$(grep -v '^$' "$wrap" | awk '{print $2}')

# Check if there are any symbols to be replaced
sum=0
input_all_symbols=$(${NM} -j "$input")
for symbol in $old_symbol_list; do
  result=$(echo "$input_all_symbols" | grep -w "$symbol")
  num=$(echo "$result" | grep -v '^[[:space:]]*$' | wc -l)
  sum=$((sum + num))
  debug "$symbol: $num"
done

if [ $sum -eq 0 ]; then
  echo "Symbol replace failed, no symbol need to be replaced"
  exit 1
fi

# Execute symbol replace
${OBJCOPY} --redefine-syms="$wrap" "$input" "$output"

# Count the number of symbols replaced
sum=0
output_all_symbols=$(${NM} -j "$output")
for symbol in $new_symbol_list; do
  result=$(echo "$output_all_symbols" | grep -w "$symbol")
  num=$(echo "$result" | wc -l)
  sum=$((sum + num))
  debug "$symbol: $num"
done

# Check symbol replace result
if [ $sum -gt 0 ]; then
  echo "Symbol replace success, $sum symbols replaced"
else
  echo "Symbol replace failed"
  exit 1
fi
