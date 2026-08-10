#!/usr/bin/env python3
# tools/traceconfig.py
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
# Trace where a CONFIG_* value came from.
#
# With layered Kconfig.defaults a symbol's value can come from a fragment, from
# the defconfig, from a 'select' on some other symbol, or from the base tree,
# and a minimal defconfig no longer tells you which. This answers that.
#
#   ./tools/traceconfig.py -o out/qemu_vela_bl CONFIG_RAM_START
#   ./tools/traceconfig.py -o out/qemu_vela_bl --why-not CONFIG_FEATURE
#   ./tools/traceconfig.py -o out/qemu_vela_bl --all
#
# Normally invoked through the 'tcc' shell function, which fills in -o from the
# current lunch target. See build/envsetup.sh.
#
# What it parses, and why it matters:
#
#   By default the *pristine minimal defconfig* is loaded, not .config. An
#   expanded .config holds a user value for every symbol, so tracing it reports
#   'assign' for everything and tells you nothing about where the value came
#   from. The defconfig has a user value only for what it actually assigns, so
#   the layered defaults show up as defaults. Use --current to trace .config
#   anyway.
#
#   The value in .config is cross-checked either way, so a .config edited by
#   menuconfig (or left over from before a Kconfig.defaults change) is reported
#   as drift rather than silently ignored.
#
# Attribution comes from Symbol.origin in kconfiglib, i.e. from the same code
# that computes the value. This tool never re-derives which mechanism won; it
# only enumerates the candidates in order to explain the outcome. Symbol.origin
# comes from the patch series in
# prebuilts/tools/python/dist-packages/kconfiglib/patches/.
#
############################################################################

import argparse
import difflib
import json
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from kconfig_env import KconfigEnv, KconfigEnvError  # noqa: E402
from kconfiglib import (  # noqa: E402
    AND,
    OR,
    TRI_TO_STR,
    TYPE_TO_STR,
    Symbol,
    expr_str,
    expr_value,
    split_expr,
)

# Symbols CMake reads straight out of the raw defconfig *text*, before the
# Kconfig tree is parsed at all. They decide the shape of the tree (which arch,
# chip and board Kconfigs get sourced), so they cannot be moved into a
# Kconfig.defaults layer -- the layer is only read once the tree already exists.
#
# Derived from nuttx/CMakeLists.txt: nuttx_export_kconfig_by_value() at the top
# for CONFIG_APPS_DIR, then nuttx_export_kconfig(${NUTTX_DEFCONFIG}) followed by
# the arch/chip/board resolution, all of which runs before
# nuttx_generate_kconfig_aggregate().
PREREAD_SYMBOLS = frozenset(
    {
        "APPS_DIR",
        "ARCH",
        "ARCH_BOARD_CUSTOM",
        "ARCH_BOARD_CUSTOM_DIR",
        "ARCH_CHIP",
        "ARCH_CHIP_CUSTOM",
        "ARCH_CHIP_CUSTOM_DIR",
    }
)


class Candidate:
    """One mechanism that could give the symbol its value."""

    __slots__ = (
        "kind",
        "loc",
        "source",
        "cond",
        "cond_value",
        "active",
        "winner",
        "layer",
        "detail",
        "reason",
    )

    def __init__(
        self,
        kind,
        loc=None,
        source=None,
        cond=None,
        cond_value=2,
        active=True,
        layer=None,
        detail=None,
    ):
        self.kind = kind
        self.loc = loc
        self.source = source  # verbatim source line, if we have one
        self.cond = cond
        self.cond_value = cond_value
        self.active = active  # would contribute if nothing outranks it
        self.winner = False
        self.layer = layer
        self.detail = detail
        self.reason = None  # why it did not win

    @property
    def where(self):
        return _where(self.loc)


class Trace:
    """Everything we worked out about one symbol."""

    def __init__(self, name, sym):
        self.name = name
        self.sym = sym
        self.kind = None  # winning mechanism, from Symbol.origin
        self.origin_loc = None
        self.value = None
        self.candidates = []
        self.notes = []
        self.choice = None
        self.current = None  # value found in .config
        self.drift = False


# --------------------------------------------------------------------- helpers


def _source_line(loc):
    """The verbatim source line at 'loc', for display."""
    if not loc:
        return None
    path, linenr = loc
    try:
        with open(path, encoding="utf-8", errors="replace") as f:
            for i, line in enumerate(f, 1):
                if i == linenr:
                    return line.rstrip()
    except OSError:
        pass
    return None


# Set once from the environment, in main(). _SHORTEN_ROOT is the workspace root
# (the directory holding nuttx/, apps/, vendor/, out/ ...); _NUTTX_DIR is what
# relative Kconfig paths are relative to.
_SHORTEN_ROOT = None
_NUTTX_DIR = None


def _short(path):
    """Rewrite a Kconfig path into something worth putting in front of a user.

    Three things happen:
      - relative paths (as they come out of the parse) are made absolute;
      - symlinks are resolved, because CMake bridges the board and chip Kconfig
        files into the build directory as symlinks. Reporting
        out/.../boards/dummy/Kconfig is technically correct but useless, whereas
        vendor/<x>/boards/<y>/Kconfig is the file you actually edit;
      - the workspace root prefix is stripped.
    """
    if path is None:
        return "?"
    text = str(path)
    if _NUTTX_DIR and not os.path.isabs(text):
        text = os.path.join(str(_NUTTX_DIR), text)
    if os.path.isabs(text):
        text = os.path.realpath(text)
    if _SHORTEN_ROOT:
        root = str(_SHORTEN_ROOT) + os.sep
        if text.startswith(root):
            return text[len(root) :]
    return text


def _where(cand_or_loc):
    """'file:line' for display, with the path shortened."""
    loc = getattr(cand_or_loc, "loc", cand_or_loc)
    if loc is None:
        return "(implicit)"
    return "%s:%s" % (_short(loc[0]), loc[1])


def _cond_terms(cond):
    """Split a condition into its top-level AND terms with their values."""
    if cond is None:
        return []
    return [
        (expr_str(term), TRI_TO_STR[expr_value(term)]) for term in split_expr(cond, AND)
    ]


def _dotconfig_value(path, prefix, name):
    """The value 'name' has in a .config-format file, or None if absent."""
    if not path.is_file():
        return None
    set_re = re.compile(r"^%s%s=(.*)$" % (re.escape(prefix), re.escape(name)))
    unset_re = re.compile(r"^# %s%s is not set$" % (re.escape(prefix), re.escape(name)))
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.rstrip()
            match = set_re.match(line)
            if match:
                value = match.group(1)
                if len(value) >= 2 and value[0] == '"' and value[-1] == '"':
                    value = value[1:-1]
                return value
            if unset_re.match(line):
                return "n"
    return None


def _selector_locs(sym, expr, prop):
    """Locate the 'select'/'imply' statements behind a reverse dependency.

    'expr' is one OR operand of sym.rev_dep / sym.weak_rev_dep. Its first AND
    operand is the selecting symbol; the statement itself is found by looking
    through that symbol's own 'selects'/'implies' for one targeting 'sym'.
    """
    selector = split_expr(expr, AND)[0]
    locs = []
    if isinstance(selector, Symbol):
        for target, _cond, loc in getattr(selector, prop, ()):
            if target is sym:
                locs.append((selector.name, loc))
    if not locs:
        locs.append((getattr(selector, "name", expr_str(expr)), None))
    return locs


# -------------------------------------------------------------------- analysis


def analyze(env, kconf, name, config_file):
    """Build a Trace for 'name'. Symbol.origin decides, we only explain."""
    sym = kconf.syms.get(name)
    trace = Trace(name, sym)

    if sym is None:
        trace.kind = "missing"
        close = difflib.get_close_matches(name, kconf.syms.keys(), 5, 0.6)
        trace.notes.append(
            "no such Kconfig symbol in this configuration's Kconfig tree"
        )
        if close:
            trace.notes.append(
                "did you mean: %s" % ", ".join(kconf.config_prefix + c for c in close)
            )
        return trace

    if not sym.nodes:
        trace.kind = "undefined"
        trace.notes.append(
            "referenced by other Kconfig files but never defined "
            "(no 'config %s' anywhere in the tree that got sourced)" % name
        )
        return trace

    trace.value = sym.str_value

    origin = sym.origin
    if origin is None:
        trace.kind = "not-written"
        trace.notes.append(
            "not written to the configuration (value %r, visibility %s)"
            % (sym.str_value, TRI_TO_STR[sym.visibility])
        )
    else:
        trace.kind, trace.origin_loc = origin

    # --- the user assignment, if any ---------------------------------------
    if sym.user_value is not None:
        value = (
            TRI_TO_STR[sym.user_value]
            if isinstance(sym.user_value, int)
            else sym.user_value
        )
        cand = Candidate(
            "assign",
            loc=sym.user_loc,
            source=_source_line(sym.user_loc),
            active=bool(sym.visibility),
            detail="assigned %r" % value,
        )
        if not sym.visibility:
            cand.reason = (
                "ignored: the symbol is not visible "
                "(visibility n), so a user value has no effect"
            )
        trace.candidates.append(cand)

    # --- 'default' properties, in priority order ---------------------------
    # sym.defaults is the concatenation of the per-location defaults in parse
    # order, which for the aggregate root is highest-priority layer first.
    for default, cond, loc in sym.defaults:
        cond_value = expr_value(cond)
        trace.candidates.append(
            Candidate(
                "default",
                loc=loc,
                source=_source_line(loc),
                cond=cond,
                cond_value=cond_value,
                active=bool(cond_value),
                layer=env.layer_of_kconfig(loc[0]) if loc else None,
                detail="default %s" % expr_str(default),
            )
        )

    # --- reverse dependencies ('select' / 'imply') -------------------------
    for prop, attr, kind in (
        ("selects", "rev_dep", "select"),
        ("implies", "weak_rev_dep", "imply"),
    ):
        expr = getattr(sym, attr)
        if expr is kconf.n:
            continue
        for operand in split_expr(expr, OR):
            value = expr_value(operand)
            for selector_name, loc in _selector_locs(sym, operand, prop):
                trace.candidates.append(
                    Candidate(
                        kind,
                        loc=loc,
                        source=_source_line(loc),
                        cond=operand,
                        cond_value=value,
                        active=bool(value),
                        layer=env.layer_of_kconfig(loc[0]) if loc else None,
                        detail="%s %s (from %s, value %s)"
                        % (kind, name, selector_name, TRI_TO_STR[value]),
                    )
                )

    # --- mark the winner, using Symbol.origin as the authority -------------
    _mark_winner(trace)

    # --- choice membership -------------------------------------------------
    if sym.choice is not None:
        choice = sym.choice
        trace.choice = {
            "name": choice.name,
            "prompt": next((n.prompt[0] for n in choice.nodes if n.prompt), None),
            "loc": _where(choice.nodes[0].loc) if choice.nodes else None,
            "mode": choice.str_value,
            "selection": (choice.selection.name if choice.selection else None),
            "user_selection": (
                choice.user_selection.name if choice.user_selection else None
            ),
        }
        if sym.defaults:
            trace.notes.append(
                "%s is a choice symbol: its own 'default' properties have no "
                "effect, the value follows the choice selection" % name
            )

    # --- build-system pre-read --------------------------------------------
    if name in PREREAD_SYMBOLS:
        trace.notes.append(
            "%s%s is read by CMake from the raw defconfig text before the "
            "Kconfig tree is parsed, so it must stay in the defconfig and "
            "cannot be moved into a Kconfig.defaults layer"
            % (kconf.config_prefix, name)
        )

    # --- cross-check against the expanded .config -------------------------
    trace.current = _dotconfig_value(env.dotconfig, kconf.config_prefix, name)
    if trace.current is not None and trace.value is not None:
        trace.drift = trace.current != trace.value and not (
            trace.current == "n" and trace.value == "n"
        )
    if trace.drift and config_file != env.dotconfig:
        trace.notes.append(
            "the value in %s is %r, which differs from what %s expands to. "
            "Either .config was edited (menuconfig/setconfig) or it predates "
            "the current Kconfig.defaults layers."
            % (env.dotconfig.name, trace.current, config_file.name)
        )

    return trace


def _mark_winner(trace):
    """Flag the candidate(s) that Symbol.origin says produced the value."""
    kind = trace.kind
    loc = trace.origin_loc

    if kind in (None, "missing", "undefined"):
        return

    if kind in ("unset", "not-written"):
        # Nothing produced a value, but the candidates still deserve a reason.
        pass
    elif kind in ("select", "imply"):
        # For a reverse dependency, origin's loc is a list of contributing
        # expressions rather than a location, so every active candidate of that
        # kind contributed.
        for cand in trace.candidates:
            if cand.kind == kind and cand.active:
                cand.winner = True
    else:
        for cand in trace.candidates:
            if cand.kind != kind:
                continue
            if loc is None:
                # Implicit default (no location). Nothing concrete to point at.
                continue
            if cand.loc and tuple(cand.loc) == tuple(loc):
                cand.winner = True
                break
            # origin normalises Kconfig paths to absolute; candidate locs may
            # be relative to nuttx/
            if (
                cand.loc
                and Path(cand.loc[0]).name == Path(loc[0]).name
                and cand.loc[1] == loc[1]
            ):
                cand.winner = True
                break

    # Explain the losers
    winner_index = next((i for i, c in enumerate(trace.candidates) if c.winner), None)
    for i, cand in enumerate(trace.candidates):
        if cand.winner or cand.reason:
            continue
        if not cand.active:
            if cand.kind == "default":
                cand.reason = "condition is n, so this default is disabled"
            else:
                cand.reason = (
                    "the %sing symbol is not enabled, so this does "
                    "not contribute" % cand.kind
                )
        elif kind in ("select", "imply") and cand.kind != kind:
            cand.reason = (
                "overridden by the %s above, which raises the value "
                "regardless of defaults" % kind
            )
        elif winner_index is not None and i > winner_index:
            win = trace.candidates[winner_index]
            cand.reason = "outranked by the %s at %s%s" % (
                win.kind,
                win.where,
                " (%s)" % win.layer.label if win.layer else "",
            )
        elif kind == "assign":
            cand.reason = (
                "%s is assigned explicitly by the configuration, "
                "which takes precedence over defaults" % trace.name
            )
        elif kind in ("unset", "not-written") and cand.kind == "default":
            # kconfiglib only records a 'default' as the origin when it yields a
            # non-n value (see Symbol.tri_value). A satisfied 'default n' leaves
            # the symbol at its implicit n and reports 'unset'.
            cand.reason = (
                "its condition holds, but the default itself is n, "
                "so the symbol just stays at the implicit n and "
                "kconfiglib records no source"
            )
        elif winner_index is None:
            cand.reason = "did not determine the value"


# ------------------------------------------------------------------- rendering


def _layer_tag(cand):
    if cand.layer is None:
        return ""
    return "   <- %s" % cand.layer.label


def render_text(trace, kconf, env, config_file, verbose=False, out=sys.stdout):
    name = kconf.config_prefix + trace.name
    write = out.write

    if trace.kind in ("missing", "undefined"):
        write("%s\n" % name)
        for note in trace.notes:
            write("  ! %s\n" % note)
        return

    sym = trace.sym
    write("%s = %s\n" % (name, _fmt_value(sym, trace.value)))
    write(
        "    type %s, visibility %s%s\n"
        % (
            TYPE_TO_STR[sym.type],
            TRI_TO_STR[sym.visibility],
            ", choice symbol" if sym.choice is not None else "",
        )
    )
    write("    traced against %s\n" % _short(config_file))

    winners = [c for c in trace.candidates if c.winner]
    write("\neffective source: %s\n" % trace.kind)
    if not winners:
        if trace.kind == "unset":
            write(
                "  nothing assigned, defaulted, selected or implied it -- "
                "the symbol stays at the implicit %s\n" % _implicit_value(sym)
            )
        elif trace.kind == "default" and trace.origin_loc is None:
            write("  an implicit default (no 'default' statement matched)\n")
        elif trace.kind == "not-written":
            write("  not written to the configuration\n")
    for cand in winners:
        write("  %s%s\n" % (cand.where, _layer_tag(cand)))
        if cand.source:
            write("      %s\n" % cand.source.strip())
        # The source line already tells the whole story for an assignment, and
        # for a 'default' it is usually literally the same text.
        if cand.kind != "assign" and (
            not cand.source or cand.source.strip() != cand.detail
        ):
            write("      %s\n" % cand.detail)
        if cand.cond is not None and cand.cond_value != 2:
            write("      condition evaluates to %s\n" % TRI_TO_STR[cand.cond_value])
        elif cand.kind == "default" and cand.cond is not None:
            terms = _cond_terms(cand.cond)
            if len(terms) > 1 or (terms and terms[0][0] != "y"):
                write("      condition: %s\n" % ", ".join("%s=%s" % t for t in terms))

    losers = [c for c in trace.candidates if not c.winner]
    # Always show a candidate that is active, or that comes from one of the
    # Kconfig.defaults layers -- a fragment the user wrote is exactly what they
    # are looking for, even when its condition did not hold.
    shown = (
        losers
        if verbose
        else [
            c
            for c in losers
            if c.active or (c.layer is not None and not c.layer.is_base)
        ]
    )
    hidden = [c for c in losers if c not in shown]

    if shown:
        write("\nother candidates, in priority order:\n")
        for cand in shown:
            write("  %-7s %s%s\n" % (cand.kind, cand.where, _layer_tag(cand)))
            if cand.source:
                write("      %s\n" % cand.source.strip())
            if cand.reason:
                write("      -> %s\n" % cand.reason)

    if hidden:
        # Symbols like ARCH_HAVE_* are selected from every arch in the tree.
        # Listing 20 dead selects buries the one thing that mattered.
        by_kind = {}
        for cand in hidden:
            by_kind[cand.kind] = by_kind.get(cand.kind, 0) + 1
        write(
            "\n%s inactive candidate%s (%s) -- pass -v to list them\n"
            % (
                len(hidden),
                "" if len(hidden) == 1 else "s",
                ", ".join(
                    "%d %s%s" % (n, k, "" if n == 1 else "s")
                    for k, n in sorted(by_kind.items())
                ),
            )
        )

    if trace.choice:
        ch = trace.choice
        write("\nchoice:\n")
        write(
            "  %s%s at %s\n"
            % (
                "<choice %s>" % ch["name"] if ch["name"] else "<choice>",
                ' "%s"' % ch["prompt"] if ch["prompt"] else "",
                ch["loc"],
            )
        )
        write(
            "  mode %s, selection %s%s\n"
            % (
                ch["mode"],
                ch["selection"],
                (
                    ""
                    if ch["user_selection"] in (None, ch["selection"])
                    else " (user picked %s, overridden)" % ch["user_selection"]
                ),
            )
        )

    write("\ndefined at:\n")
    for node in sym.nodes:
        layer = env.layer_of(node.loc[0])
        write(
            "  %s%s%s\n"
            % (
                _where(node.loc),
                (
                    "   <- %s" % layer.label
                    if layer is not None and not layer.is_base
                    else ""
                ),
                "" if node.prompt else "   (no prompt)",
            )
        )

    if trace.current is not None:
        write(
            "\n%s in %s: %s%s\n"
            % (
                name,
                env.dotconfig.name,
                trace.current,
                "   DRIFT" if trace.drift else "   (consistent)",
            )
        )

    for note in trace.notes:
        write("\nnote: %s\n" % note)


def render_why_not(trace, kconf, env, want, verbose=False, out=sys.stdout):
    """Explain why the symbol is not what the caller expected."""
    name = kconf.config_prefix + trace.name
    write = out.write

    if trace.kind == "missing":
        write("%s does not exist in this configuration.\n" % name)
        for note in trace.notes:
            write("  %s\n" % note)
        write(
            "\n  Common causes:\n"
            "    - a typo in the symbol name\n"
            "    - the Kconfig file that defines it is not sourced for this\n"
            "      board (wrong arch/chip/board, or a missing 'source')\n"
        )
        return
    if trace.kind == "undefined":
        write("%s is referenced but never defined.\n" % name)
        for note in trace.notes:
            write("  %s\n" % note)
        return

    write("%s = %s" % (name, _fmt_value(trace.sym, trace.value)))
    if want is not None:
        write("   (you expected %s)" % want)
    write("\n")
    if isinstance(trace.origin_loc, tuple):
        write("  effective source: %s at %s\n" % (trace.kind, _where(trace.origin_loc)))
    elif isinstance(trace.origin_loc, list):
        write(
            "  effective source: %s, contributed by %s\n"
            % (trace.kind, " || ".join(trace.origin_loc))
        )
    elif trace.kind == "unset":
        write(
            "  effective source: none -- nothing assigned, defaulted, "
            "selected or implied it, so it stays at the implicit %s\n"
            % _implicit_value(trace.sym)
        )
    elif trace.kind == "not-written":
        write(
            "  effective source: none, and the symbol is not written to the "
            "configuration at all (visibility %s)\n" % TRI_TO_STR[trace.sym.visibility]
        )
    else:
        write("  effective source: %s\n" % trace.kind)

    if want is not None and str(trace.value) == str(want):
        write("\n  It already has that value.\n")
        return

    findings = why_not_findings(trace, kconf, env, verbose)

    if not findings:
        write("\n  Nothing looks wrong: no losing candidates and no override.\n")
        if want is not None:
            write(
                "  Nothing in the Kconfig tree offers %s the value %s.\n" % (name, want)
            )
        return

    write("\nfindings:\n")
    for i, finding in enumerate(findings, 1):
        write("  %d. %s\n" % (i, finding))

    if trace.drift:
        write(
            "\n  Also: %s in %s is %r while the defconfig expands to %r. "
            "Re-run cmake, or check whether .config was edited.\n"
            % (name, env.dotconfig.name, trace.current, trace.value)
        )


def why_not_findings(trace, kconf, env, verbose=False):
    """The reasons a symbol is not the value the caller expected.

    Kept separate from rendering so that the diagnosis can grow without the
    renderer growing with it, and so the same list can be reported as JSON.
    """
    name = kconf.config_prefix + trace.name
    findings = []

    # 1. choice member with defaults
    if trace.sym.choice is not None and trace.sym.defaults:
        findings.append(
            "%s is a member of a choice, and 'default' has no effect on choice "
            "members. Kconfig picks the choice selection instead. Assign it in "
            "the defconfig (%s=y), or give the choice a 'default'." % (name, name)
        )

    # 2. user assignment ignored because the symbol is invisible
    if trace.sym.user_value is not None and not trace.sym.visibility:
        deps = _cond_terms(trace.sym.direct_dep)
        unmet = [t for t in deps if t[1] == "n"]
        findings.append(
            "the configuration assigns %s, but the symbol's visibility is n, "
            "so the assignment is ignored.%s"
            % (
                name,
                (
                    " Unsatisfied dependencies: %s."
                    % ", ".join("%s=%s" % t for t in unmet)
                    if unmet
                    else ""
                ),
            )
        )

    # 3/4. per-candidate reasons.
    #
    # Only the ones that can plausibly be the user's problem. A symbol like
    # CONFIG_ARCH has a 'default' per architecture, all but one disabled; listing
    # seventeen of those buries everything else. So: always report a candidate
    # that is active (a real priority conflict) or that lives in one of the
    # Kconfig.defaults layers (the user's own fragment), and collapse the rest.
    quiet = 0
    for cand in trace.candidates:
        if cand.winner or not cand.reason:
            continue
        if cand.kind == "assign" and not trace.sym.visibility:
            continue  # already reported as finding 2, in more detail
        from_layer = cand.layer is not None and not cand.layer.is_base
        if not (verbose or cand.active or from_layer):
            quiet += 1
            continue
        extra = ""
        if cand.kind == "default" and not cand.active and cand.cond is not None:
            false_terms = [t for t in _cond_terms(cand.cond) if t[1] == "n"]
            if false_terms:
                extra = "  Unsatisfied: %s." % ", ".join(
                    "%s=%s" % t for t in false_terms
                )
        findings.append(
            "the %s at %s%s did not take effect: %s%s"
            % (
                cand.kind,
                cand.where,
                " (%s)" % cand.layer.label if cand.layer else "",
                cand.reason,
                extra,
            )
        )

    if quiet:
        findings.append(
            "%d further candidate%s in the base tree never applied (disabled "
            "conditions or selectors that are off) -- pass -v to list them"
            % (quiet, "" if quiet == 1 else "s")
        )

    # 5. forced by select/imply
    for cand in trace.candidates:
        if cand.winner and cand.kind in ("select", "imply"):
            findings.append(
                "the value is forced by a '%s', and a 'default' cannot lower "
                "it -- to change it, turn off the %sing symbol instead"
                % (cand.kind, cand.kind)
            )
            break

    # 6. build-system pre-read
    if trace.name in PREREAD_SYMBOLS:
        findings.append(
            "%s is read by CMake from the raw defconfig before the Kconfig "
            "tree is parsed, so it cannot be provided by a Kconfig.defaults "
            "layer -- keep it in the defconfig." % name
        )

    return findings


def _fmt_value(sym, value):
    if value is None:
        return "<none>"
    if TYPE_TO_STR[sym.type] == "string":
        return '"%s"' % value
    return value


def _implicit_value(sym):
    """What a symbol falls back to with no default, select or assignment."""
    return "n" if TYPE_TO_STR[sym.type] in ("bool", "tristate") else "empty string"


def trace_to_dict(trace, kconf, env):
    sym = trace.sym
    data = {
        "name": kconf.config_prefix + trace.name,
        "kind": trace.kind,
        "value": trace.value,
        "notes": trace.notes,
    }
    if sym is not None and sym.nodes:
        data.update(
            {
                "type": TYPE_TO_STR[sym.type],
                "visibility": TRI_TO_STR[sym.visibility],
                "is_choice_symbol": sym.choice is not None,
                "origin_loc": (
                    list(trace.origin_loc)
                    if isinstance(trace.origin_loc, tuple)
                    else trace.origin_loc
                ),
                "defined_at": [_where(n.loc) for n in sym.nodes],
                "candidates": [
                    {
                        "kind": c.kind,
                        "where": c.where,
                        "layer": c.layer.label if c.layer else None,
                        "detail": c.detail,
                        "source": c.source.strip() if c.source else None,
                        "condition_value": TRI_TO_STR[c.cond_value],
                        "active": c.active,
                        "winner": c.winner,
                        "reason": c.reason,
                    }
                    for c in trace.candidates
                ],
                "choice": trace.choice,
                "dotconfig_value": trace.current,
                "drift": trace.drift,
            }
        )
    return data


def collect_all(env, kconf):
    """Every configured symbol, grouped like Zephyr's traceconfig."""
    groups = {"visible": [], "invisible": [], "unset": []}
    for sym in kconf.unique_defined_syms:
        origin = sym.origin
        if origin is None:
            continue
        kind, loc = origin
        if kind == "unset":
            group = "unset"
        elif sym.visibility:
            group = "visible"
        else:
            group = "invisible"
        if isinstance(loc, tuple):
            layer = env.layer_of(loc[0])
            where = _where(loc)
            layer_label = (
                layer.label if layer is not None and not layer.is_base else None
            )
        elif isinstance(loc, list):
            where = " || ".join(loc)
            layer_label = None
        else:
            where = ""
            layer_label = None
        groups[group].append(
            {
                "name": kconf.config_prefix + sym.name,
                "type": TYPE_TO_STR[sym.type],
                "value": sym.str_value,
                "kind": kind,
                "where": where,
                "layer": layer_label,
            }
        )
    for rows in groups.values():
        rows.sort(key=lambda row: row["name"])
    return groups


def render_all(env, kconf, config_file, out=sys.stdout):
    groups = collect_all(env, kconf)
    out.write("traced against %s\n" % _short(config_file))
    for group in ("visible", "invisible", "unset"):
        rows = groups[group]
        out.write("\n== %s symbols (%d) ==\n" % (group, len(rows)))
        for row in rows:
            if group == "unset":
                out.write("  %s\n" % row["name"])
            else:
                out.write(
                    "  %-44s %-6s %-14s %-8s %s%s\n"
                    % (
                        row["name"],
                        row["type"],
                        row["value"],
                        row["kind"],
                        row["where"],
                        "  [%s]" % row["layer"] if row["layer"] else "",
                    )
                )


# ----------------------------------------------------------------------- main


def main():
    parser = argparse.ArgumentParser(
        description="Trace where a CONFIG_* value came from", allow_abbrev=False
    )
    parser.add_argument(
        "-o",
        "--out_dir",
        required=True,
        help="CMake build directory (e.g. out/qemu_vela_qemu-armeabi-v7a-bl)",
    )
    parser.add_argument(
        "symbol",
        nargs="*",
        help="symbol(s) to trace, with or without the CONFIG_ prefix. "
        "For --why-not, NAME or NAME=VALUE",
    )
    parser.add_argument(
        "-n",
        "--why-not",
        action="store_true",
        help="explain why the symbol is not the value you expected",
    )
    parser.add_argument(
        "-c",
        "--current",
        action="store_true",
        help="trace the expanded .config instead of the pristine defconfig "
        "(reports every symbol as 'assign'; rarely what you want)",
    )
    parser.add_argument(
        "-f", "--file", help="trace an explicit configuration file instead"
    )
    parser.add_argument(
        "--all",
        action="store_true",
        help="report every configured symbol instead of named ones",
    )
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="list inactive candidates too (dead selects, disabled defaults)",
    )
    parser.add_argument(
        "-w",
        "--warn",
        action="store_true",
        help="let kconfiglib print its parse warnings",
    )
    args = parser.parse_args()

    if not args.all and not args.symbol:
        parser.error("give at least one symbol, or --all")

    KconfigEnv.require_origin()

    try:
        env = KconfigEnv.from_build_dir(args.out_dir)
    except KconfigEnvError as exc:
        sys.exit("error: %s" % exc)

    # Display paths relative to the workspace root (the directory holding
    # nuttx/, apps/, vendor/, out/ ...). Absolute paths drown out the output.
    global _SHORTEN_ROOT, _NUTTX_DIR
    _SHORTEN_ROOT = env.nuttx_dir.parent
    _NUTTX_DIR = env.nuttx_dir

    if args.file:
        config_file = Path(args.file)
        if not config_file.is_absolute():
            config_file = (Path.cwd() / config_file).resolve()
    elif args.current:
        config_file = env.dotconfig
    else:
        config_file = env.base_defconfig

    try:
        kconf = env.load(config_file, warn=args.warn)
    except KconfigEnvError as exc:
        sys.exit("error: %s" % exc)

    if args.all:
        if args.json:
            json.dump(
                {"config": _short(config_file), "groups": collect_all(env, kconf)},
                sys.stdout,
                indent=2,
            )
            sys.stdout.write("\n")
        else:
            render_all(env, kconf, config_file)
        return 0

    prefix = kconf.config_prefix
    results = []
    exit_code = 0

    for spec in args.symbol:
        want = None
        if "=" in spec:
            spec, want = spec.split("=", 1)
        name = spec[len(prefix) :] if spec.startswith(prefix) else spec
        name = name.upper()

        trace = analyze(env, kconf, name, config_file)
        if trace.kind in ("missing", "undefined"):
            exit_code = 1

        if args.json:
            data = trace_to_dict(trace, kconf, env)
            if want is not None:
                data["expected"] = want
            results.append(data)
        else:
            if results:
                print()
            results.append(trace)
            if args.why_not:
                render_why_not(trace, kconf, env, want, verbose=args.verbose)
            else:
                render_text(trace, kconf, env, config_file, verbose=args.verbose)

    if args.json:
        json.dump(results, sys.stdout, indent=2)
        sys.stdout.write("\n")

    return exit_code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except BrokenPipeError:
        os.close(sys.stdout.fileno())
        sys.exit(0)
