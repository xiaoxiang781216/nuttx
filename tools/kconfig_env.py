#!/usr/bin/env python3
# tools/kconfig_env.py
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
#
# Shared plumbing for tools that need to parse the Kconfig tree of a CMake
# build directory the same way the build itself does.
#
# There are two things that are easy to get wrong and that every such tool
# needs to get right:
#
#   1. The top-level Kconfig file. With layered Kconfig.defaults the build
#      parses a generated aggregate root ($BUILD/Kconfig.aggregate), not
#      nuttx/Kconfig. Parsing the wrong root does not fail -- it silently
#      recomputes every invisible symbol from the base tree alone and drops all
#      layer-provided values. This mirrors nuttx_kconfig_top() in
#      cmake/nuttx_aggregate_kconfig.cmake.
#
#   2. The parse environment. The Kconfig tree references $APPSDIR, $APPSBINDIR,
#      $EXTERNALDIR, $DRIVERS_PLATFORM_DIR and $BINDIR, and the build runs the
#      Kconfig tools with nuttx/ as the working directory and $srctree unset.
#      Relative filenames in the parsed tree (MenuNode.loc) therefore come out
#      relative to nuttx/, and reproducing that exactly is what makes reported
#      locations match what the build would report.
#
# CMake-only: a configured build directory is required. The Makefile flow
# (tools/configure.sh + make olddefconfig) does not build an aggregate root and
# is not supported here.
#
# Usage:
#
#   from kconfig_env import KconfigEnv
#
#   env = KconfigEnv.from_build_dir("/path/to/out/board_config")
#   kconf = env.load(env.base_defconfig)   # or env.dotconfig
#   sym = kconf.syms["RAM_START"]
#
# Run directly to dump what it resolved:
#
#   ./tools/kconfig_env.py /path/to/out/board_config
#
############################################################################

import os
import re
import sys
from pathlib import Path

try:
    import kconfiglib
    from kconfiglib import Kconfig
except ImportError:
    sys.exit(
        "error: kconfiglib not importable.\n"
        "       Source build/envsetup.sh, which puts\n"
        "       prebuilts/tools/python/dist-packages/kconfiglib on PYTHONPATH."
    )


# Where the vendored, patched kconfiglib lives, for error messages
_PREBUILT_HINT = "prebuilts/tools/python/dist-packages/kconfiglib"

# 'osource "<path>"' in the generated aggregate root
_OSOURCE_RE = re.compile(r'^\s*osource\s+"(?P<path>[^"]+)"\s*$')

# 'source "Kconfig"' -- the base tree, always last in the aggregate root
_SOURCE_RE = re.compile(r'^\s*source\s+"(?P<path>[^"]+)"\s*$')


class KconfigEnvError(Exception):
    """Raised when the build directory cannot be used for Kconfig parsing."""


class Layer:
    """One entry of the layered Kconfig.defaults chain.

    Layers are ordered by decreasing priority, matching the order the aggregate
    root osources them. 'index' is 1-based for display; the base tree gets the
    highest index and is_base set.
    """

    __slots__ = ("index", "path", "is_base")

    def __init__(self, index, path, is_base=False):
        self.index = index
        self.path = Path(path)
        self.is_base = is_base

    @property
    def label(self):
        if self.is_base:
            return "base tree"
        return "layer %d" % self.index

    def __repr__(self):
        return "<Layer %s %s>" % (self.label, self.path)


class KconfigEnv:
    """Resolved Kconfig parse environment for one CMake build directory."""

    __slots__ = (
        "build_dir",
        "nuttx_dir",
        "apps_dir",
        "apps_bindir",
        "aggregate",
        "top_kconfig",
        "layers",
    )

    # ------------------------------------------------------------------ setup

    def __init__(self, build_dir, nuttx_dir, apps_dir, apps_bindir):
        self.build_dir = Path(build_dir)
        self.nuttx_dir = Path(nuttx_dir)
        self.apps_dir = Path(apps_dir)
        self.apps_bindir = Path(apps_bindir)

        aggregate = self.build_dir / "Kconfig.aggregate"
        self.aggregate = aggregate if aggregate.exists() else None
        # Mirrors nuttx_kconfig_top()
        self.top_kconfig = self.aggregate or (self.nuttx_dir / "Kconfig")
        self.layers = self._parse_layers()

    @classmethod
    def from_build_dir(cls, build_dir):
        """Resolve the environment from a configured CMake build directory."""
        build_dir = Path(build_dir).resolve()
        if not build_dir.is_dir():
            raise KconfigEnvError("no such build directory: %s" % build_dir)

        cache = build_dir / "CMakeCache.txt"
        if not cache.is_file():
            raise KconfigEnvError(
                "%s is not a configured CMake build directory (no "
                "CMakeCache.txt).\nBuild the target once, or run 'lunch' and "
                "then 'm'." % build_dir
            )

        nuttx_dir = _cache_value(cache, "CMAKE_HOME_DIRECTORY")
        if not nuttx_dir:
            raise KconfigEnvError("CMAKE_HOME_DIRECTORY missing from %s" % cache)
        nuttx_dir = Path(nuttx_dir).resolve()

        apps_dir = cls._resolve_apps_dir(build_dir, nuttx_dir)
        # Mirrors CMakeLists.txt: NUTTX_APPS_BINDIR =
        #   ${CMAKE_BINARY_DIR}/${basename of NUTTX_APPS_DIR}
        apps_bindir = build_dir / apps_dir.name

        return cls(build_dir, nuttx_dir, apps_dir, apps_bindir)

    @staticmethod
    def _resolve_apps_dir(build_dir, nuttx_dir):
        # CONFIG_APPS_DIR wins if the configuration sets it, exactly like
        # CMakeLists.txt does via nuttx_export_kconfig_by_value(). Prefer the
        # expanded .config, fall back to the copied defconfig.
        for name in (".config", "defconfig"):
            path = build_dir / name
            if not path.is_file():
                continue
            value = _config_value(path, "CONFIG_APPS_DIR")
            if value:
                # Relative CONFIG_APPS_DIR is relative to nuttx/
                return (nuttx_dir / value).resolve()

        for candidate in ("../apps", "../nuttx-apps", "dummy"):
            path = (nuttx_dir / candidate).resolve()
            if path.is_dir():
                return path

        raise KconfigEnvError("cannot locate the apps directory next to %s" % nuttx_dir)

    # ------------------------------------------------------------------ layers

    def _parse_layers(self):
        """Ordered layer list, derived from the aggregate root itself.

        The aggregate root *is* the layer metadata: the order its 'osource'
        lines appear in is the priority order (highest first), and the trailing
        'source "Kconfig"' is the base tree. Nothing else needs to be kept in
        sync.
        """
        if self.aggregate is None:
            return [Layer(1, self.nuttx_dir / "Kconfig", is_base=True)]

        layers = []
        base = None
        with open(self.aggregate, encoding="utf-8") as f:
            for line in f:
                match = _OSOURCE_RE.match(line)
                if match:
                    layers.append(Layer(len(layers) + 1, match.group("path")))
                    continue
                match = _SOURCE_RE.match(line)
                if match:
                    # Relative to $srctree, which is nuttx/ for us
                    base = (self.nuttx_dir / match.group("path")).resolve()

        if base is None:
            raise KconfigEnvError(
                "%s has no 'source' line for the base tree -- is it really the "
                "generated aggregate root?" % self.aggregate
            )
        layers.append(Layer(len(layers) + 1, base, is_base=True))
        return layers

    @property
    def base_layer(self):
        """The base-tree layer (nuttx/Kconfig), always the lowest priority."""
        return self.layers[-1]

    def layer_of(self, path):
        """The Layer whose root is exactly 'path', or None.

        Only layer *roots* match. A Kconfig file that is not a layer root was
        reached through the base tree -- callers that know 'path' is a Kconfig
        location can fall back to base_layer. Do not call this for 'assign'
        origins: those point at a .config/defconfig, which is not a layer.
        """
        if path is None:
            return None
        path = Path(path)
        if not path.is_absolute():
            path = (self.nuttx_dir / path).resolve()
        for layer in self.layers:
            if layer.path == path:
                return layer
        return None

    def layer_of_kconfig(self, path):
        """Like layer_of(), but assumes 'path' is a parsed Kconfig file.

        Falls back to the base-tree layer, since every Kconfig file that is not
        a layer root is sourced from the base tree.
        """
        return self.layer_of(path) or self.base_layer

    # ------------------------------------------------- well-known input files

    @property
    def base_defconfig(self):
        """The pristine (minimal) defconfig CMake copied into the build dir.

        This is the right input for "where does this value come from": it still
        has a user value only for the symbols the defconfig actually assigns,
        so defaults contributed by the layers are visible as defaults. Tracing
        the expanded .config instead reports every symbol as 'assign', because
        an expanded .config holds a user value for everything.
        """
        return self.build_dir / "defconfig"

    @property
    def dotconfig(self):
        """The expanded .config the build actually compiles against."""
        return self.build_dir / ".config"

    # ------------------------------------------------------------------ parsing

    def apply(self, config_file=None):
        """Export the Kconfig environment variables and chdir to nuttx/.

        Mirrors KCONFIG_ENV plus WORKING_DIRECTORY from cmake/menuconfig.cmake.
        $srctree is deliberately left unset, so that relative paths in the
        parsed tree come out exactly as the build sees them.
        """
        os.environ["APPSDIR"] = str(self.apps_dir)
        os.environ["APPSBINDIR"] = str(self.apps_bindir)
        os.environ["EXTERNALDIR"] = "dummy"
        os.environ["DRIVERS_PLATFORM_DIR"] = "dummy"
        os.environ["BINDIR"] = str(self.build_dir)
        os.environ.pop("srctree", None)
        if config_file is not None:
            os.environ["KCONFIG_CONFIG"] = str(config_file)
        os.chdir(self.nuttx_dir)

    def load(self, config_file, warn=False):
        """Parse the top-level Kconfig and load 'config_file' into it."""
        config_file = Path(config_file)
        if not config_file.is_file():
            raise KconfigEnvError("no such configuration file: %s" % config_file)

        self.apply(config_file)
        kconf = Kconfig(
            str(self.top_kconfig),
            warn=warn,
            warn_to_stderr=warn,
            suppress_traceback=True,
        )
        kconf.load_config(str(config_file))
        return kconf

    # ------------------------------------------------------------- capability

    @staticmethod
    def require_origin():
        """Fail loudly unless kconfiglib can report value origins.

        Symbol.origin comes from the patch series in
        prebuilts/tools/.../kconfiglib/patches/. Without it a tool could still
        produce output, but it would be guesswork -- and once 'configdefault'
        is in use the node-level fallback is not even sound, because
        _finalize_sym() inserts into sym.defaults. So refuse instead.
        """
        if hasattr(kconfiglib.Symbol, "origin"):
            return
        sys.exit(
            "error: this kconfiglib does not support value-origin tracking\n"
            "       (kconfiglib.Symbol has no 'origin' attribute).\n"
            "\n"
            "       imported from: %s\n"
            "       version:       %s\n"
            "\n"
            "       Expected the patched copy in %s.\n"
            "       Sync prebuilts/tools and re-source build/envsetup.sh, or\n"
            "       see %s/README.vela.md."
            % (
                getattr(kconfiglib, "__file__", "<unknown>"),
                getattr(kconfiglib, "VERSION", "<unknown>"),
                _PREBUILT_HINT,
                _PREBUILT_HINT,
            )
        )


def _cache_value(cache_path, name):
    """Read one entry out of a CMakeCache.txt."""
    prefix = name + ":"
    with open(cache_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            if line.startswith(prefix):
                return line.split("=", 1)[1].strip() if "=" in line else None
    return None


def _config_value(config_path, name):
    """Read one CONFIG_* value out of a .config/defconfig, unquoted."""
    prefix = name + "="
    with open(config_path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith(prefix):
                value = line[len(prefix) :]
                if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                    value = value[1:-1]
                return value
    return None


def _main():
    if len(sys.argv) != 2:
        sys.exit("usage: %s <cmake-build-dir>" % Path(sys.argv[0]).name)

    env = KconfigEnv.from_build_dir(sys.argv[1])
    print("build dir      : %s" % env.build_dir)
    print("nuttx dir      : %s" % env.nuttx_dir)
    print("apps dir       : %s" % env.apps_dir)
    print("apps bindir    : %s" % env.apps_bindir)
    print("aggregate root : %s" % (env.aggregate or "<none>"))
    print("top-level      : %s" % env.top_kconfig)
    print(
        "base defconfig : %s%s"
        % (env.base_defconfig, "" if env.base_defconfig.is_file() else "   (missing)")
    )
    print(
        ".config        : %s%s"
        % (env.dotconfig, "" if env.dotconfig.is_file() else "   (missing)")
    )
    print(
        "kconfiglib     : %s %s (origin: %s)"
        % (
            getattr(kconfiglib, "__file__", "?"),
            getattr(kconfiglib, "VERSION", "?"),
            "yes" if hasattr(kconfiglib.Symbol, "origin") else "NO",
        )
    )
    print("layers (highest priority first):")
    for layer in env.layers:
        print("  %-10s %s" % (layer.label, layer.path))


if __name__ == "__main__":
    try:
        _main()
    except KconfigEnvError as exc:
        sys.exit("error: %s" % exc)
