############################################################################
# tools/pynuttx/nxgdb/autocompeletion.py
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

from __future__ import annotations

import gdb

metavar_map = {"file": gdb.COMPLETE_FILENAME, "symbol": gdb.COMPLETE_SYMBOL}


# The parser._actions type is `class _HelpAction`
# you can refer https://github.com/python/cpython/blob/3.10/Lib/argparse.py#L1091
def get_options(parser, filter=None, member="option_strings"):
    rs = []
    for action in parser._actions:
        if filter is None or filter(action):
            # option_strings: A list of command-line option strings which
            #   should be associated with this action. ['-a', '--arch']
            # dest: The name of the attribute to hold the created object(s). 'arch'
            value = getattr(action, member)
            if value and isinstance(value, list):
                rs.extend(value)
            elif value:
                rs.append(value)
    return rs


def get_option_choices(parser, name):
    for action in parser._actions:
        if action.dest == name:
            return action.choices


def get_option_metavar(parser, name):
    for action in parser._actions:
        if action.dest == name:
            return action.metavar


def get_options_metavar_filter(name):
    return lambda action: action.metavar == name


def get_options_choices_filter():
    return lambda action: action.choices is not None


def get_options_positional_filter():
    return lambda action: not action.option_strings


def complete_for_options(parser, word):
    argv = gdb.string_to_argv(word)
    if word == "" or word.endswith(" "):
        argv.append("")
    # gcc -march=native, the `march` is option, `native` is argument
    latest_option = argv[-2] if len(argv) > 1 else argv[0]
    latest_argument = argv[-1]

    options = get_options(parser)
    # `--help` -> long_option, `help` -> long_option_without_dash
    long_options_without_dash = [
        item[2:] for item in options if len(item) > 2 and item[:2] == "--"
    ]

    positional_options = get_options(
        parser, get_options_positional_filter(), member="dest"
    )
    pos_opt = (
        metavar_map.get(get_option_metavar(parser, positional_options[0]))
        if len(positional_options) != 0
        else None
    )

    return (
        options,
        long_options_without_dash,
        pos_opt,
        latest_option,
        latest_argument,
    )


def complete_specific_argument(parser, option):
    for meta_var, completion_list in metavar_map.items():
        options = get_options(parser, get_options_metavar_filter(meta_var))
        if option in options:
            return completion_list
    return None


def common_argument_completion(
    parser,
    word,
):
    """
    Common command line argument completion for custom gdb commands.
    """

    options, long_options_without_dash, pos_opt, latest_option, latest_argument = (
        complete_for_options(parser, word)
    )

    choice_options = get_options(parser, get_options_choices_filter(), member="dest")
    choices = [get_option_choices(parser, choice) for choice in choice_options]
    option_without_dash = ""
    if latest_option[:2] == "--":
        option_without_dash = latest_option[2:]
    elif latest_option[:1] == "-":
        option_without_dash = latest_option[1:]
    for index, element in enumerate(choice_options):
        if option_without_dash == element:
            return [item for item in choices[index] if item.startswith(latest_argument)]

    # to complete arguments
    rs = complete_specific_argument(parser, latest_option)
    if rs:
        return rs

    # to complete options
    if latest_argument == "-":
        return [item[1:] for item in options]
    if latest_argument.startswith("--"):
        return [
            item
            for item in long_options_without_dash
            if item.startswith(latest_argument[2:])
        ]
    if latest_argument.startswith("-"):
        return [latest_argument[1:]]

    # If the option is completed, but no arguments is provided.
    if latest_option and latest_option[0] == "-":
        return []
    # First, we can try to complete positional options
    if pos_opt:
        return pos_opt
    return options


def complete(self):
    def complete(self, word, text):
        return common_argument_completion(self.parser, word)

    self.complete = complete
    return self
