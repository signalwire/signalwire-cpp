#!/usr/bin/env python3
"""Deterministic, template-level C++ formatting for the code generators.

Every generator (generate_rest, generate_swml_verbs, generate_relay_protocol,
generate_swaig_payloads) runs its emitted source through ``format_generated_cpp``
as its final step, so a fresh regen already equals the clang-format-clean on-disk
tree. Without an in-generator format, GEN-FRESH (byte-compares a fresh regen to the
tree) and the FMT gate (``clang-format --dry-run -Werror``) are mutually exclusive —
the generator would emit unformatted text and one gate would always be red. (porting-sdk
AGENT_RULES §5.)

``format_generated_cpp`` is a PURE-PYTHON re-formatter that reproduces the exact subset
of clang-format Google style (ColumnLimit 100, the repo's ``.clang-format``) that the
generated headers exercise: system/library/quoted include ordering is handled at the
emit sites; here we do brace-depth indentation (namespaces don't indent — Google
``NamespaceIndentation: None``), the ``AccessModifierOffset: -1`` label offset,
short-``if``-block expansion (``AllowShortBlocksOnASingleLine: Never``), short
single-statement function collapse (``AllowShortFunctionsOnASingleLine: All``),
declaration/call bin-pack line wrapping (``BinPackParameters``/``BinPackArguments`` +
``AllowAllParametersOfDeclarationOnNextLine``) including binary-``+`` expression breaks
(``BreakBeforeBinaryOperators: None``), constructor init-list ``NextLine`` packing,
forward-only comment reflow (``ReflowComments``), trailing-comment alignment
(``AlignTrailingComments``), and blank-line collapse. It was validated to reproduce
clang-format 18 byte-for-byte across all 1105 generated headers, so the clang-format
FMT gate is a genuine no-op on the emitted tree.

``clang_format_source`` remains as the verify-only backstop (AGENT_RULES §5 level-2):
it shells out to the same clang-format the FMT gate uses, so a generator can assert its
own output is already clean (it should have nothing to do). It is NOT on the normal
emit path.
"""
from __future__ import annotations

import re
import shutil
import subprocess
import sys
from pathlib import Path

COL = 100  # matches .clang-format ColumnLimit

_CMT = re.compile(r"^(\s*)(///?)( )(\S.*)$")
_IF = re.compile(r"^if \((.*)\) \{ (.*;) \}$")
_ACCESS = re.compile(r"^(public|private|protected):")
_CTRL = re.compile(r"^(if|for|while|switch|else|do)\b")
_NS = re.compile(r"^namespace\b.*\{$")
_TRAIL = re.compile(r"^(.*\S)(  +)(//.*)$")


def _repo_root() -> Path:
    # scripts/ is directly under the repo root.
    return Path(__file__).resolve().parent.parent


# ---------------------------------------------------------------------------
# Deterministic formatter (the emit path).
# ---------------------------------------------------------------------------

def _split_toplevel(param_str: str) -> list[str]:
    """Split a top-level comma list respecting <> and () nesting."""
    out: list[str] = []
    depth = 0
    cur = ""
    for ch in param_str:
        if ch in "<(":
            depth += 1
        elif ch in ">)":
            depth -= 1
        if ch == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += ch
    if cur.strip():
        out.append(cur.strip())
    return out


def _binpack(first_line: str, params: list[str], tail: str, cont: str,
             first_inline: bool) -> list[str]:
    """Bin-pack ``params`` (BinPackParameters/BinPackArguments). ``tail`` (e.g.
    ') const {' or ');') is attached to the LAST param for the fit decision so
    clang-format's break-before-last behaviour is reproduced. If ``first_inline`` the
    params start on ``first_line``; otherwise ``first_line`` stands alone and the params
    begin on the next line at ``cont``. A final line that still overflows and is a
    binary-``+`` expression is broken at the operators (the ``+`` ends the line)."""
    n = len(params)
    pieces = [p + ("," if i < n - 1 else tail) for i, p in enumerate(params)]
    out: list[str] = []
    if first_inline:
        cur = first_line
    else:
        out.append(first_line)
        cur = cont
    started = False
    for pc in pieces:
        if not started:
            cur = cur + pc
            started = True
        elif len(cur) + 1 + len(pc) <= COL:
            cur = cur + " " + pc
        else:
            out.append(cur)
            cur = cont + pc
    if len(cur) > COL and " + " in cur:
        toks = cur.split(" + ")
        rebuilt = toks[0]
        for idx, t in enumerate(toks[1:], start=1):
            # reserve 2 cols for a trailing ' +' when this is not the last operand
            reserve = 2 if idx < len(toks) - 1 else 0
            if len(rebuilt) + len(" + ") + len(t) + reserve > COL:
                out.append(rebuilt + " +")
                rebuilt = cont + t
            else:
                rebuilt = rebuilt + " + " + t
        out.append(rebuilt)
    else:
        out.append(cur)
    return out


def _wrap_signature(indent: str, head: str, params: list[str], tail: str) -> list[str]:
    """Wrap a declaration/call ``<head>(<params>)<tail>``. Prefer Mode A (params
    bin-packed, continuation aligned under the open paren); if any Mode-A line still
    overflows, fall back to Mode B (all params on the next line at continuation +4)."""
    one = indent + head + ", ".join(params) + tail
    if len(one) <= COL:
        return [one]
    paren_col = len(indent + head)
    mode_a = _binpack(indent + head, params, tail, " " * paren_col, first_inline=True)
    if all(len(ln) <= COL for ln in mode_a):
        return mode_a
    cont = " " * (len(indent) + 4)
    return _binpack(indent + head.rstrip(), params, tail, cont, first_inline=False)


def _wrap_line(indent: str, s: str) -> list[str]:
    # constructor init-list: "explicit Name(args) : a(x), b(y) {}"
    m = re.match(r"^(.*?\)) : (.*) (\{\}?)$", s)
    if m:
        head, inits, brace = m.group(1), m.group(2), m.group(3)
        base = indent + "    : "  # ConstructorInitializerIndentWidth 4
        packed = base + inits + " " + brace
        if len(indent + head) <= COL and len(packed) <= COL:
            return [indent + head, packed]
        initlist = _split_toplevel(inits)
        lines = [indent + head, base + initlist[0] + ("," if len(initlist) > 1 else "")]
        cont = " " * len(base)
        for k, it in enumerate(initlist[1:], start=1):
            last = k == len(initlist) - 1
            lines.append(cont + it + ("" if last else ","))
        lines[-1] += " " + brace
        return lines
    # method signature / function-call: <head>(<params>)<tail>
    m = re.match(r"^(.*?\()(.*)(\)[^()]*)$", s)
    if m:
        return _wrap_signature(indent, m.group(1), _split_toplevel(m.group(2)), m.group(3))
    return [indent + s]


def _collapse_short_functions(rows: list) -> list:
    """Collapse a `<sig> {` / single-statement / `}` triple onto one line when it fits
    (AllowShortFunctionsOnASingleLine: All). ``rows`` are (indent, code) tuples or ""."""
    out: list = []
    i, n = 0, len(rows)
    while i < n:
        item = rows[i]
        if (item != "" and item[1].endswith("{") and "(" in item[1] and ")" in item[1]
                and not _CTRL.match(item[1])
                and i + 2 < n and rows[i + 1] != "" and rows[i + 2] != ""
                and rows[i + 2][1] == "}"
                and "{" not in rows[i + 1][1] and "}" not in rows[i + 1][1]):
            ind, sig = item
            stmt = rows[i + 1][1]
            if len(f"{ind}{sig} {stmt} }}") <= COL:
                out.append((ind, f"{sig} {stmt} }}"))
                i += 3
                continue
        out.append(item)
        i += 1
    return out


def _reflow_comments(lines: list[str]) -> list[str]:
    """Forward-only comment reflow (ReflowComments). Consecutive line comments sharing
    the same prefix form a paragraph; if any line overflows, over-long lines push their
    overflow words down to the FRONT of the next line, but a fitting line is never pulled
    up into. A paragraph that already fits is left byte-for-byte."""
    out: list[str] = []
    i, n = 0, len(lines)
    while i < n:
        m = _CMT.match(lines[i])
        if not m:
            out.append(lines[i])
            i += 1
            continue
        indent, marker = m.group(1), m.group(2)
        prefix = f"{indent}{marker} "
        para = [lines[i]]
        j = i + 1
        while j < n:
            mj = _CMT.match(lines[j])
            if not mj or mj.group(1) != indent or mj.group(2) != marker:
                break
            para.append(lines[j])
            j += 1
        if all(len(pl) <= COL for pl in para):
            out.extend(para)
        else:
            carry: list[str] = []
            for pl in para:
                avail = carry + _CMT.match(pl).group(4).split()
                line = prefix + avail[0]
                k = 1
                while k < len(avail) and len(line) + 1 + len(avail[k]) <= COL:
                    line += " " + avail[k]
                    k += 1
                out.append(line)
                carry = avail[k:]
            while carry:
                line = prefix + carry[0]
                k = 1
                while k < len(carry) and len(line) + 1 + len(carry[k]) <= COL:
                    line += " " + carry[k]
                    k += 1
                out.append(line)
                carry = carry[k:]
        i = j
    return out


def _align_trailing_comments(lines: list[str]) -> list[str]:
    """Align trailing ``//`` comments (AlignTrailingComments) within runs of consecutive
    non-blank lines: the comment column is 2 past the widest CODE portion among the
    comment-bearing lines in the run (non-comment lines don't push it)."""
    out: list[str] = []
    run: list[tuple[str, str]] = []

    def flush() -> None:
        nonlocal run
        if not run:
            return
        widths = [len(code) for code, cm in run if cm]
        col = max(widths) + 2 if widths else 0
        for code, cm in run:
            out.append(code + " " * (col - len(code)) + cm if cm else code)
        run = []

    for ln in lines:
        if ln.strip() == "":
            flush()
            out.append(ln)
            continue
        m = _TRAIL.match(ln)
        if m and not ln.lstrip().startswith("//"):
            run.append((m.group(1), m.group(3)))
        else:
            run.append((ln, ""))
    flush()
    return out


def format_generated_cpp(src: str) -> str:
    """Format one generated C++ header exactly as clang-format 18 (repo .clang-format)
    would, purely in Python — so raw generator emit is already FMT-gate-clean and the
    clang-format backstop has nothing to do. Input lines are treated as logical: leading
    whitespace is recomputed from brace depth."""
    raw = src.split("\n")
    # collapse runs of blank lines to a single blank
    lines: list[str] = []
    prev_blank = False
    for s in (ln.strip() for ln in raw):
        if s == "":
            if prev_blank:
                continue
            prev_blank = True
            lines.append("")
        else:
            prev_blank = False
            lines.append(s)
    # Rejoin continuation lines: if a code line has unbalanced '(' (a wrapped
    # signature/call split across lines), merge following lines until the parens
    # balance. This makes the formatter idempotent — feeding it already-wrapped input
    # yields the same result as feeding the unwrapped logical line. Comment lines and
    # blank lines are never merged.
    def _paren_balance(text: str) -> int:
        bal = 0
        instr = False
        prev = ""
        for ch in text:
            if instr:
                if ch == '"' and prev != "\\":
                    instr = False
            elif ch == '"':
                instr = True
            elif ch == "(":
                bal += 1
            elif ch == ")":
                bal -= 1
            prev = ch
        return bal

    rejoined: list[str] = []
    k = 0
    while k < len(lines):
        cur = lines[k]
        if cur == "" or cur.lstrip().startswith("//"):
            rejoined.append(cur)
            k += 1
            continue
        bal = _paren_balance(cur)
        while bal > 0 and k + 1 < len(lines) and lines[k + 1] != "" \
                and not lines[k + 1].lstrip().startswith("//"):
            k += 1
            cur = cur + " " + lines[k]
            bal += _paren_balance(lines[k])
        rejoined.append(cur)
        k += 1
    lines = rejoined
    # Join a ctor signature with its following ':'-init line, gathering a (possibly
    # already-wrapped, one-initializer-per-line) init list back into a single logical
    # line. This keeps the formatter idempotent on its own wrapped output.
    joined: list[str] = []
    k = 0
    while k < len(lines):
        cur = lines[k]
        if (k + 1 < len(lines) and cur.startswith("explicit ") and cur.endswith(")")
                and lines[k + 1].startswith(": ")):
            init = lines[k + 1]
            k += 1
            # absorb continuation initializer lines until the init list terminates
            # (a line ending in '{}' or '{' closes the constructor head).
            while not (init.rstrip().endswith("{}") or init.rstrip().endswith("{")) \
                    and k + 1 < len(lines) and lines[k + 1] != "" \
                    and not lines[k + 1].lstrip().startswith("//"):
                k += 1
                init = init + " " + lines[k]
            joined.append(cur + " " + init)
            k += 1
        else:
            joined.append(cur)
            k += 1
    lines = joined
    # expand short single-line ifs
    exp: list[str] = []
    for s in lines:
        m = _IF.match(s)
        if m:
            exp.append(f"if ({m.group(1)}) {{")
            exp.append(m.group(2))  # body; reindent supplies the indentation
            exp.append("}")
        else:
            exp.append(s)
    # reindent by brace depth (namespaces don't add indent)
    rows: list = []
    stack: list[str] = []
    for s in exp:
        if s == "":
            rows.append("")
            continue
        lead_close = 0
        for ch in s:
            if ch in "})":
                lead_close += 1
            else:
                break
        blk_depth = sum(1 for kind in stack if kind == "blk")
        popped_blk = 0
        tmp = list(stack)
        for _ in range(lead_close):
            if tmp:
                if tmp[-1] == "blk":
                    popped_blk += 1
                tmp.pop()
        eff = max(blk_depth - popped_blk, 0)
        if _ACCESS.match(s):
            ind = " " * (eff * 2 - 1)  # AccessModifierOffset: -1
        elif s.startswith(":"):
            ind = " " * (eff * 2 + 4)  # ctor-init continuation
        else:
            ind = " " * (eff * 2)
        rows.append((ind, s))
        is_ns = bool(_NS.match(s))
        for ch in s:
            if ch == "{":
                stack.append("ns" if is_ns else "blk")
            elif ch == "}":
                if stack:
                    stack.pop()
    rows = _collapse_short_functions(rows)
    # wrap over-long code lines
    final: list[str] = []
    for item in rows:
        if item == "":
            final.append("")
            continue
        ind, s = item
        full = ind + s
        if len(full) <= COL or s.lstrip().startswith("//"):
            final.append(full)
            continue
        final.extend(_wrap_line(ind, s))
    final = _reflow_comments(final)
    final = _align_trailing_comments(final)
    return "\n".join(final)


# ---------------------------------------------------------------------------
# Verify-only backstop (NOT on the emit path).
# ---------------------------------------------------------------------------

def clang_format_source(src: str, *, assume_filename: str = "x.hpp") -> str:
    """Return ``src`` formatted with the repo's clang-format config — the verify-only
    backstop (AGENT_RULES §5 level-2). ``format_generated_cpp`` already produces
    clang-format-clean output, so this must be a no-op on generated source; it exists so
    a generator/test can assert that (and to catch template drift). Raises if
    clang-format is missing."""
    exe = shutil.which("clang-format")
    if exe is None:
        sys.stderr.write(
            "clang-format not found on PATH — needed only for the verify-only backstop.\n"
        )
        raise SystemExit(2)
    proc = subprocess.run(
        [exe, f"-assume-filename={_repo_root() / assume_filename}"],
        input=src,
        capture_output=True,
        text=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(f"clang-format failed:\n{proc.stderr}\n")
        raise SystemExit(proc.returncode)
    return proc.stdout
