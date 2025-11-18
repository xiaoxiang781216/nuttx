############################################################################
# tools/pynuttx/nxgdb/venv.py
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

import argparse
import subprocess
import sys
import venv
from os import path
from typing import Optional

import gdb

from . import autocompeletion

venv_dir = ".gdbenv"
venv_install_flag = "not initialized"


def install_dependency(
    location: str = venv_dir,
    requirements: Optional[str] = None,
    package: Optional[str] = None,
) -> None:
    if requirements:
        if not path.exists(requirements):
            raise RuntimeError(f"requirements.txt not found at {requirements}")
    elif package:
        if not package.strip():
            raise RuntimeError("Package name is empty")
    else:
        raise RuntimeError("Either requirements or package must be specified")

    pip_path = path.join(location, "bin", "pip")
    if not path.exists(pip_path):
        raise RuntimeError(f"pip not found at {pip_path}, please create venv first")

    if requirements:
        pip_cmd = [pip_path, "install", "-r", requirements]
    else:
        pip_cmd = [pip_path, "install", package]

    try:
        subprocess.check_call(pip_cmd)
    except KeyboardInterrupt:
        print("pip install interrupted by user. You can re-run to resume.")
    except subprocess.CalledProcessError as e:
        print(f"pip install failed (exit {e.returncode}). re-run to retry.")
    else:
        with open(venv_install_flag, "w") as f:
            f.write("s")


def init_venv_and_install_dependency(location: str = venv_dir) -> None:
    requirements = path.join(
        path.dirname(path.abspath(__file__)), "../requirements.txt"
    )

    if path.exists(location):
        print(f"Virtual environment already exists at {location}")
    else:
        print(f"Creating virtual environment at {location}")
        venv.EnvBuilder(with_pip=True, symlinks=True).create(location)

    if not path.exists(venv_install_flag):
        install_dependency(location=location, requirements=requirements)


@autocompeletion.complete
class GDBVenv(gdb.Command):
    """Create NuttX GDB Plugin virtual environment"""

    def __init__(self):
        super().__init__("gdbvenv", gdb.COMMAND_USER)
        self.parser = self.get_argparser()
        self.use_venv()  # If venv already exists, use it immediately

    def get_argparser(self):
        parser = argparse.ArgumentParser(
            description="GDB Plugin Virtual Environment Manager",
        )
        parser.add_argument(
            "venvdir",
            type=str,
            default=venv_dir,
            nargs="?",
            help="Path to the virtual environment directory",
        )
        parser.add_argument(
            "--install",
            type=str,
            help="Path to requirements.txt or package, or package name to install into the virtual environment",
        )
        return parser

    def use_venv(self, location: str = venv_dir) -> None:
        py_version = f"python{sys.version_info.major}.{sys.version_info.minor}"
        site_packages = path.join(location, "lib", py_version, "site-packages")
        if site_packages in sys.path:
            return

        if path.exists(site_packages):
            print(f"Adding site-packages: {site_packages}")
            sys.path.insert(0, site_packages)
        else:
            print(f"site-packages not found at {site_packages}")

    def invoke(self, arg, from_tty):  # type: ignore
        try:
            args = self.parser.parse_args(gdb.string_to_argv(arg))
        except SystemExit:
            return

        global venv_install_flag
        venv_install_flag = path.join(args.venvdir, ".gdbenv_installed")

        if args.install:
            if args.install.endswith("requirements.txt"):
                install_dependency(location=args.venvdir, requirements=args.install)
            else:
                install_dependency(location=args.venvdir, package=args.install)
        else:
            init_venv_and_install_dependency(location=args.venvdir)

        self.use_venv(location=args.venvdir)
