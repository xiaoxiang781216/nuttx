#!/usr/bin/env python3
# tools/devicetree/gen_dynamic_api.py
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

import argparse
import os
import pickle
from collections import defaultdict


def generate_macro_definitions(board_nums, output_file):
    # load all board data
    dict_list = []
    board_ids = list(range(1, board_nums + 1))
    for bid in board_ids:
        filename = f"select_macros_{bid}.pickle"
        if os.path.exists(filename):
            with open(filename, "rb") as f:
                dict_list.append(pickle.load(f))

    # collect all unique macro names
    merged_dict = defaultdict(list)
    for data in dict_list:
        for key, value in data.items():
            merged_dict[key].append(value)

    # generate macro definitions
    output = []
    output.append('#include "devicetree_generated_base.h"')
    for bid in board_ids:
        output.append(f'#include "devicetree_generated_{bid}.h"')
    output.append("\n#define DT_NODE_DEFAULT 1\n")

    for key, value in merged_dict.items():
        macro_name = f"#define {key} \\\n"
        macro_value = ""
        for item in value:
            macro_value += f"{item} : \\\n"
        macro_value += f"{key.replace('_DYNAMIC', '')}\n"

        output.append(f"{macro_name}{macro_value}")

    # write to file
    with open(output_file, "w") as f:
        f.write("\n".join(output))
        dynamic_foreach_macro_write(f, board_ids)


def dynamic_foreach_macro_write(file, board_list):
    # DT_FOREACH_DTNAMIC(g_board_id, node_id, fn_foreach, fn)
    foreach_macro = (
        "\n#define DT_FOREACH_DYNAMIC(g_board_id, node_id, fn_foreach, fn) \\\n"
    )
    foreach_macro += "  do { \\\n"
    for bid in board_list:
        if bid == 1:
            foreach_macro += f"    if (g_board_id == {bid}) \\\n"
        else:
            foreach_macro += f"    else if (g_board_id == {bid}) \\\n"
        foreach_macro += "      { \\\n"
        foreach_macro += f"        fn_foreach(DT_CAT(B{bid}_, node_id), fn); \\\n"
        foreach_macro += "      } \\\n"
    foreach_macro += "    else \\\n"
    foreach_macro += "      { \\\n"
    foreach_macro += "        fn_foreach(node_id, fn); \\\n"
    foreach_macro += "      } \\\n"
    foreach_macro += "  } while(0)"

    # DT_FOREACH_VARGS_DYNAMIC(g_board_id, node_id, fn_foreach, fn, ...)
    foreach_vargs_macro = "\n#define DT_FOREACH_VARGS_DYNAMIC(g_board_id, node_id, fn_foreach, fn, ...) \\\n"
    foreach_vargs_macro += "  do { \\\n"
    for bid in board_list:
        if bid == 1:
            foreach_vargs_macro += f"    if (g_board_id == {bid}) \\\n"
        else:
            foreach_vargs_macro += f"    else if (g_board_id == {bid}) \\\n"
        foreach_vargs_macro += "      { \\\n"
        foreach_vargs_macro += (
            f"        fn_foreach(DT_CAT(B{bid}_, node_id), fn, __VA_ARGS__); \\\n"
        )
        foreach_vargs_macro += "      } \\\n"
    foreach_vargs_macro += "    else \\\n"
    foreach_vargs_macro += "      { \\\n"
    foreach_vargs_macro += "        fn_foreach(node_id, fn, __VA_ARGS__); \\\n"
    foreach_vargs_macro += "      } \\\n"
    foreach_vargs_macro += "  } while(0)"

    # DT_FOREACH_STATUS_OKAY_DYNAMIC(g_board_id, compat, fn)
    foreach_status_okay_macro = (
        "\n#define DT_FOREACH_STATUS_OKAY_DYNAMIC(g_board_id, compat, fn) \\\n"
    )
    foreach_status_okay_macro += "  do { \\\n"
    for bid in board_list:
        if bid == 1:
            foreach_status_okay_macro += f"    if (g_board_id == {bid}) \\\n"
        else:
            foreach_status_okay_macro += f"    else if (g_board_id == {bid}) \\\n"
        foreach_status_okay_macro += "      { \\\n"
        foreach_status_okay_macro += (
            f"        COND_CODE_1(DT_CAT3(B{bid}_, DT_COMPAT_HAS_OKAY_, compat), \\\n"
            f"        (DT_CAT3(B{bid}_, DT_FOREACH_OKAY_, compat)(fn)), \\\n"
            f"        ()) \\\n"
        )
        foreach_status_okay_macro += "      } \\\n"
    foreach_status_okay_macro += "    else \\\n"
    foreach_status_okay_macro += "      { \\\n"
    foreach_status_okay_macro += (
        "        COND_CODE_1(DT_CAT(DT_COMPAT_HAS_OKAY_, compat), \\\n"
    )
    foreach_status_okay_macro += (
        "        (DT_CAT(DT_FOREACH_OKAY_, compat)(fn)), \\\n"
    )
    foreach_status_okay_macro += "        ()) \\\n"
    foreach_status_okay_macro += "      } \\\n"
    foreach_status_okay_macro += "  } while(0)"

    # DT_FOREACH_STATUS_OKAY_VARGS_DYNAMIC(g_board_id, compat, fn, ...)
    foreach_status_okay_vagrs_macro = "\n#define DT_FOREACH_STATUS_OKAY_VARGS_DYNAMIC(g_board_id, compat, fn, ...) \\\n"
    foreach_status_okay_vagrs_macro += "  do { \\\n"
    for bid in board_list:
        if bid == 1:
            foreach_status_okay_vagrs_macro += f"    if (g_board_id == {bid}) \\\n"
        else:
            foreach_status_okay_vagrs_macro += f"    else if (g_board_id == {bid}) \\\n"
        foreach_status_okay_vagrs_macro += "      { \\\n"
        foreach_status_okay_vagrs_macro += (
            f"        COND_CODE_1(DT_CAT3(B{bid}_, DT_COMPAT_HAS_OKAY_, compat), \\\n"
            f"        (DT_CAT3(B{bid}_, DT_FOREACH_OKAY_VARGS_, compat)(fn, __VA_ARGS__)), \\\n"
            f"        ()) \\\n"
        )
        foreach_status_okay_vagrs_macro += "      } \\\n"
    foreach_status_okay_vagrs_macro += "    else \\\n"
    foreach_status_okay_vagrs_macro += "      { \\\n"
    foreach_status_okay_vagrs_macro += (
        "        COND_CODE_1(DT_CAT(DT_COMPAT_HAS_OKAY_, compat), \\\n"
    )
    foreach_status_okay_vagrs_macro += (
        "        (DT_CAT(DT_FOREACH_OKAY_VARGS_, compat)(fn, __VA_ARGS__)), \\\n"
    )
    foreach_status_okay_vagrs_macro += "        ()) \\\n"
    foreach_status_okay_vagrs_macro += "      } \\\n"
    foreach_status_okay_vagrs_macro += "  } while(0)"

    print(foreach_macro, file=file)
    print(foreach_vargs_macro, file=file)
    print(foreach_status_okay_macro, file=file)
    print(foreach_status_okay_vagrs_macro, file=file)


def parse_args() -> argparse.Namespace:
    # Returns parsed command-line arguments

    parser = argparse.ArgumentParser(allow_abbrev=False)
    parser.add_argument("--header-out", required=True, help="path to write header to")
    parser.add_argument(
        "--edt-pickle", help="path to read pickled edtlib.EDT object from"
    )
    parser.add_argument(
        "--board-nums",
        required=True,
        type=int,
        help="board identifier to use in generated header",
    )

    return parser.parse_args()


def main():
    args = parse_args()

    print(args.board_nums)

    generate_macro_definitions(args.board_nums, args.header_out)


if __name__ == "__main__":
    main()
