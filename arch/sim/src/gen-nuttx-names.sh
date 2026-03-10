#!/usr/bin/env bash
############################################################################
# arch/sim/src/gen-nuttx-names.sh
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
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
# implied.  See the License for the specific language governing
# permissions and limitations under the License.
#
############################################################################
#
# Auto-generate nuttx-names.dat for sim two-stage linking.
#
# Instead of maintaining a manual whitelist (nuttx-names.in), this
# script uses nm to discover all globally defined symbols in nuttx.rel
# and automatically generates rename rules (symbol -> NXsymbol).
#
# Cross-domain symbols (defined in REQUIREDOBJS AND referenced by
# HOSTOBJS) are excluded from renaming so the host OS code can call
# them by their original names.
#
# Usage:
#   gen-nuttx-names.sh NM nuttx.rel output.dat \
#       [REQUIREDOBJS...] -- [HOSTOBJS... HEADOBJ...]
#

set -e

# Parse arguments

NM="$1"; shift
NUTTX_REL="$1"; shift
OUTPUT="$1"; shift

# Collect REQUIREDOBJS (before --) and HOSTOBJS (after --)

REQUIREDOBJS=()
HOSTOBJS=()
separator_seen=false

for arg in "$@"; do
  if [ "$arg" = "--" ]; then
    separator_seen=true
    continue
  fi
  if $separator_seen; then
    HOSTOBJS+=("$arg")
  else
    REQUIREDOBJS+=("$arg")
  fi
done

TMPDIR_WORK=$(mktemp -d)
trap 'rm -rf "$TMPDIR_WORK"' EXIT

# Step 1: All globally defined symbols in nuttx.rel (includes weak)

"$NM" --defined-only -g "$NUTTX_REL" | sed 's/.* //' | \
  sort -u > "$TMPDIR_WORK/all_defined.tmp"

# Step 2: Symbols defined in arch/sim local objects (REQUIREDOBJS)
#         Only these cross-domain bridge symbols are excluded.

if [ ${#REQUIREDOBJS[@]} -gt 0 ]; then
  "$NM" --defined-only -g "${REQUIREDOBJS[@]}" | sed 's/.* //' | \
    sort -u > "$TMPDIR_WORK/sim_defined.tmp"
else
  touch "$TMPDIR_WORK/sim_defined.tmp"
fi

# Step 3: Undefined symbols referenced by HOSTOBJS + HEADOBJ
#         Subtract symbols that HOSTOBJS themselves define, since
#         those are resolved internally and don't cross the domain.

if [ ${#HOSTOBJS[@]} -gt 0 ]; then
  "$NM" --undefined-only "${HOSTOBJS[@]}" | sed 's/.* //' | \
    sort -u > "$TMPDIR_WORK/host_undef_raw.tmp"
  "$NM" --defined-only -g "${HOSTOBJS[@]}" | sed 's/.* //' | \
    sort -u > "$TMPDIR_WORK/host_defined.tmp"
  comm -23 "$TMPDIR_WORK/host_undef_raw.tmp" \
           "$TMPDIR_WORK/host_defined.tmp" \
    > "$TMPDIR_WORK/host_undef.tmp"
else
  touch "$TMPDIR_WORK/host_undef.tmp"
fi

# Step 4: Cross-domain symbols = sim_defined ∩ host_undef
#         These must keep their original names.

comm -12 "$TMPDIR_WORK/sim_defined.tmp" \
         "$TMPDIR_WORK/host_undef.tmp" \
  > "$TMPDIR_WORK/cross_domain.tmp"

# Step 5: Rename set = all_defined - cross_domain - {main}
#         Output format: "symbol NXsymbol"
#         Skip address sanitizer symbols.

comm -23 "$TMPDIR_WORK/all_defined.tmp" \
         "$TMPDIR_WORK/cross_domain.tmp" | \
  grep -v '^__asan_.*$' | \
  sed 's/.*/& NX&/' > "$OUTPUT"
