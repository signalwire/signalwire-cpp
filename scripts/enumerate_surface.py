#!/usr/bin/env python3
"""enumerate_surface.py -- emit port_surface.json for the C++ SignalWire SDK.

This walks every ``include/signalwire/**/*.hpp`` and ``**/*.h`` header, parses
out the namespace/class/public-method structure with regex, and emits JSON
matching the shape of ``porting-sdk/python_surface.json``.

Output shape (identical to ``python_surface.json``)::

    {
      "version": "1",
      "generated_from": "signalwire-cpp @ <git sha>",
      "modules": {
        "signalwire.core.agent_base": {
          "classes": {
            "AgentBase": ["__init__", "set_prompt_text", ...]
          },
          "functions": [...]
        },
        ...
      }
    }

Symbol naming contract (from porting-sdk CHECKLIST_TEMPLATE.md
Phase 13 symbol-level surface parity):

  * Class names are kept as-is (``AgentBase``, ``FunctionResult``, ...).
  * Method names in this SDK are ALREADY snake_case (this is a C++ port
    that followed the Python naming) -- no translation needed.
  * Constructors (methods with the same name as their class, inside
    a public block) are emitted as ``__init__``.
  * C++ namespaces are translated to Python's canonical module path via
    ``CLASS_MODULE_MAP`` below. Port-only classes without a Python analog
    fall back to a native-namespace translation
    (``signalwire::rest::PhoneCallHandler`` -> ``signalwire.rest.phone_call_handler``).
  * Only ``public:`` members are emitted. Everything in ``private:`` or
    ``protected:`` is skipped.
  * Operator overloads are skipped (no Python analog in v1).
  * Destructors (``~Foo``) are skipped.
  * Friend declarations and forward declarations are skipped.

Regex-based parsing is pragmatic for this SDK size (~15 headers). libclang is
heavyweight overkill; the header surface is regular enough that a line-based
visibility scanner handles it.

Usage:
    python3 scripts/enumerate_surface.py                    # write port_surface.json
    python3 scripts/enumerate_surface.py --output FILE
    python3 scripts/enumerate_surface.py --check            # exit 1 on drift
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

# ---------------------------------------------------------------------------
# Class -> Python module mapping
# ---------------------------------------------------------------------------
# Every class in the C++ SDK has to be reported under a Python-reference dotted
# module name so the diff against ``python_surface.json`` lines up. The table
# below is the single source of truth for that mapping. When the Python
# reference renames a module, this table changes, not the parser.
#
# Classes NOT in this map fall back to the native-namespace translation
# (e.g. ``signalwire::rest::PhoneCallHandler`` -> ``signalwire.rest.phone_call_handler``).
CLASS_MODULE_MAP: dict[str, str] = {
    # -- agent ------------------------------------------------------------
    "AgentBase": "signalwire.core.agent_base",

    # -- contexts ---------------------------------------------------------
    "Context": "signalwire.core.contexts",
    "ContextBuilder": "signalwire.core.contexts",
    "GatherInfo": "signalwire.core.contexts",
    "GatherQuestion": "signalwire.core.contexts",
    "Step": "signalwire.core.contexts",

    # -- datamap ----------------------------------------------------------
    "DataMap": "signalwire.core.data_map",

    # -- swaig ------------------------------------------------------------
    "FunctionResult": "signalwire.core.function_result",
    "ToolDefinition": "signalwire.core.swaig_function",

    # -- skills -----------------------------------------------------------
    "SkillBase": "signalwire.core.skill_base",
    "SkillManager": "signalwire.core.skill_manager",
    "SkillRegistry": "signalwire.skills.registry",

    # -- server -----------------------------------------------------------
    "AgentServer": "signalwire.agent_server",

    # -- security ---------------------------------------------------------
    "SessionManager": "signalwire.core.security.session_manager",

    # -- swml -------------------------------------------------------------
    # Document/Schema have no exact Python analog, so treat as port-only
    # via the native translation.
    # ``Service`` in C++ == Python's ``SWMLService``; rename at emit time.
    # Handled via CLASS_RENAME_MAP below, not via module mapping.

    # -- rest -------------------------------------------------------------
    "HttpClient": "signalwire.rest._base",
    "CrudResource": "signalwire.rest._base",
    "SignalWireRestError": "signalwire.rest._base",
    "RestClient": "signalwire.rest.client",

    # -- relay ------------------------------------------------------------
    "RelayClient": "signalwire.relay.client",
    "Call": "signalwire.relay.call",
    "Message": "signalwire.relay.message",
    # Action / RelayEvent / CallEvent / MessageEvent / DialEvent /
    # ComponentEvent have no 1:1 Python analog -- port-only, use native
    # translation.

    # -- prefabs ----------------------------------------------------------
    "ConciergeAgent": "signalwire.prefabs.concierge",
    "FAQBotAgent": "signalwire.prefabs.faq_bot",
    "InfoGathererAgent": "signalwire.prefabs.info_gatherer",
    "ReceptionistAgent": "signalwire.prefabs.receptionist",
    "SurveyAgent": "signalwire.prefabs.survey",

    # -- logging ----------------------------------------------------------
    # ``Logger`` in signalwire::logging -> Python core.logging_config
    # has no matching class (Python uses module-level functions),
    # so let it fall through to native translation.
}


# -- Class rename map --------------------------------------------------------
# C++ names that should be emitted under a different class name to match the
# Python reference. These are cases where the C++ name is purely cosmetic (a
# legitimate simplification that still refers to the same concept).
CLASS_RENAME_MAP: dict[tuple[str, str], tuple[str, str]] = {
    # (source_ns, source_class) -> (target_module, target_class)
    ("signalwire::swml", "Service"): (
        "signalwire.core.swml_service", "SWMLService",
    ),
}


# Reserved identifiers that must never be emitted as methods.
SKIP_METHOD_NAMES: set[str] = {
    # C++ constructs that can superficially look like methods
    "operator",
    "using",
    "typedef",
    "friend",
    "template",
    "return",
    "if", "else", "for", "while", "do", "switch", "case",
}


# Classes whose surface we deliberately don't enumerate because they're not
# part of the SDK's public contract (internal helper types, event-parsing
# POD structs with only static from_X methods that are port-specific, etc.).
#
# We DO enumerate these but they will show up as port additions since the
# Python side doesn't have them. The diff catches them in PORT_ADDITIONS.md
# which is the correct treatment -- they're deliberate C++ additions.
# So this set is intentionally empty; nothing is silently dropped.
CLASSES_TO_SKIP: set[str] = set()


# -- Mixin projection -------------------------------------------------------
# Python's AgentBase collapses multiple inheritance across 9 mixin classes.
# The C++ port flattens all those methods onto AgentBase directly. To make
# the diff line up, we *project* each mixin class into port_surface.json by
# picking the matching methods off AgentBase.
#
# This table mirrors ``signalwire.core.mixins.*`` in ``python_surface.json``.
# If Python adds/removes/renames a mixin method, update this table and the
# nightly diff will tell you.
MIXIN_PROJECTIONS: dict[tuple[str, str], list[str]] = {
    # (module_path, class_name) -> list of method names to copy from
    # the C++ AgentBase class if present there.
    ("signalwire.core.mixins.ai_config_mixin", "AIConfigMixin"): [
        "add_function_include", "add_hint", "add_hints", "add_internal_filler",
        "add_language", "add_mcp_server", "add_pattern_hint", "add_pronunciation",
        "enable_debug_events", "enable_mcp_server",
        "set_function_includes", "set_global_data", "set_internal_fillers",
        "set_languages", "set_native_functions", "set_param", "set_params",
        "set_post_prompt_llm_params", "set_prompt_llm_params",
        "set_pronunciations", "update_global_data",
    ],
    ("signalwire.core.mixins.auth_mixin", "AuthMixin"): [
        # These two AuthMixin methods are implementation-detail protected
        # helpers in C++ (validate_auth) that aren't part of the public C++
        # surface. Tracked as a PORT_OMISSIONS exemption, not a projection.
    ],
    ("signalwire.core.mixins.mcp_server_mixin", "MCPServerMixin"): [
        # Empty in Python -- class exists as a marker only.
    ],
    ("signalwire.core.mixins.prompt_mixin", "PromptMixin"): [
        "define_contexts", "get_prompt",
        "prompt_add_section", "prompt_add_subsection", "prompt_add_to_section",
        "prompt_has_section", "reset_contexts", "set_post_prompt",
        "set_prompt_text",
    ],
    ("signalwire.core.mixins.serverless_mixin", "ServerlessMixin"): [
        # C++ has no Lambda/GCF/Azure runtime; tracked as PORT_OMISSIONS.
    ],
    ("signalwire.core.mixins.skill_mixin", "SkillMixin"): [
        "add_skill", "has_skill", "list_skills", "remove_skill",
    ],
    ("signalwire.core.mixins.state_mixin", "StateMixin"): [
        # validate_tool_token lives on SessionManager in C++, not AgentBase;
        # tracked as PORT_OMISSIONS.
    ],
    ("signalwire.core.mixins.tool_mixin", "ToolMixin"): [
        "define_tool", "on_function_call", "register_swaig_function",
    ],
    ("signalwire.core.mixins.web_mixin", "WebMixin"): [
        "enable_debug_routes", "manual_set_proxy_url", "run", "serve",
        "set_dynamic_config_callback",
    ],
}


# ---------------------------------------------------------------------------
# Parser
# ---------------------------------------------------------------------------

# Match "namespace foo {", "namespace foo::bar {", or "namespace foo { namespace bar {"
NAMESPACE_RE = re.compile(r"^\s*namespace\s+([A-Za-z_][\w:]*)\s*\{")
# Unnamed namespace
ANON_NAMESPACE_RE = re.compile(r"^\s*namespace\s*\{")
# class Foo { / class Foo : public Bar {  -- exclude forward declarations
CLASS_RE = re.compile(
    r"^\s*(?:class|struct)\s+([A-Z][A-Za-z0-9_]*)\b(?:\s*(?:final\s*)?:\s*[^{;]+)?\s*\{"
)
# Forward declarations like "class Foo;" or "class foo::Bar;" inside scopes
FORWARD_DECL_RE = re.compile(r"^\s*(?:class|struct)\s+[^{;]*;\s*$")
# Visibility specifiers: "public:", "private:", "protected:"
VISIBILITY_RE = re.compile(r"^\s*(public|private|protected)\s*:\s*$")
# A plausible method line. This is intentionally loose -- we post-filter.
# Captures: return/qualifiers, then name, then open-paren.
# Matches things like:
#   AgentBase& set_name(const std::string& n) { ... }
#   static const std::set<std::string>& supported_internal_filler_names();
#   explicit AgentBase(const std::string& name = "agent", ...);
#   ~AgentBase();
#   Action answer();
#   json to_json() const { ... }
#
# The key signal: an identifier followed by `(` before `;` or `{` on the
# logical line, and the line isn't obviously a local variable or type alias.
METHOD_RE = re.compile(
    # Leading whitespace, optional modifiers
    r"^\s*"
    # Optional specifiers / attributes. Non-greedy blob up to the name.
    # We allow any text here because the types get arbitrarily complex
    # (templates, nested types, qualifiers). We require the name+paren
    # at the end to anchor.
    r"(?P<prefix>(?:[A-Za-z_][\w:<>,\s*&\[\]]*?\s+)?)"
    # The method / ctor / dtor name
    r"(?P<tilde>~?)(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    # Opening paren
    r"\s*\("
)


def strip_line_comments(line: str) -> str:
    """Remove // comments; block comments handled at buffer level."""
    # Find // outside of strings. Simplified: assume source code is
    # well-behaved (no // inside strings in the header API sections).
    idx = line.find("//")
    if idx != -1:
        return line[:idx]
    return line


def strip_block_comments(text: str) -> str:
    """Remove /* ... */ comments (possibly multi-line)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text[i:i + 2] == "/*":
            end = text.find("*/", i + 2)
            if end == -1:
                break
            # Preserve newlines inside the comment to keep line numbers sane.
            block = text[i:end + 2]
            out.append("\n" * block.count("\n"))
            i = end + 2
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def strip_strings(line: str) -> str:
    """Mask string/char literals with spaces so their braces don't confuse us."""
    # Replace content inside double-quoted strings (respecting simple escapes).
    result = []
    i = 0
    n = len(line)
    while i < n:
        c = line[i]
        if c == '"':
            result.append('"')
            i += 1
            while i < n:
                c = line[i]
                if c == "\\" and i + 1 < n:
                    result.append("  ")
                    i += 2
                    continue
                if c == '"':
                    result.append('"')
                    i += 1
                    break
                result.append(" ")
                i += 1
        elif c == "'":
            result.append("'")
            i += 1
            while i < n:
                c = line[i]
                if c == "\\" and i + 1 < n:
                    result.append("  ")
                    i += 2
                    continue
                if c == "'":
                    result.append("'")
                    i += 1
                    break
                result.append(" ")
                i += 1
        else:
            result.append(c)
            i += 1
    return "".join(result)


class Scope:
    """A nested scope (namespace or class) stacked during parsing."""

    def __init__(self, kind: str, name: str, brace_depth: int,
                 visibility: str | None = None):
        self.kind = kind              # "namespace" | "class" | "struct" | "anon"
        self.name = name
        self.brace_depth = brace_depth
        # Visibility applies to class/struct scopes. struct defaults to public.
        self.visibility = visibility


def parse_header(path: Path) -> list[tuple[str, str, list[str]]]:
    """Return list of (namespace_path, class_name, public_methods).

    namespace_path is a "::"-joined string like "signalwire::agent".
    Methods are already sorted.
    """
    raw = path.read_text(encoding="utf-8", errors="replace")
    text = strip_block_comments(raw)

    findings: list[tuple[str, str, list[str]]] = []

    scopes: list[Scope] = []
    brace_depth = 0

    # Per (namespace, class) collect methods
    # Key: (ns_path, class_name) -> ordered list of methods (dedup at end)
    collected: dict[tuple[str, str], list[str]] = {}

    lines = text.split("\n")
    for raw_line in lines:
        line = strip_line_comments(raw_line)
        code_line = strip_strings(line)

        # --- Namespace opener
        m = NAMESPACE_RE.match(code_line)
        if m:
            ns = m.group(1)
            # Support "namespace a::b {"
            parts = ns.split("::")
            # Count opening braces on this line
            # We expect exactly one for "namespace X {"
            opens = code_line.count("{")
            closes = code_line.count("}")
            # Push one Scope per part, all sharing the same brace_depth
            for i, p in enumerate(parts):
                # For nested "a::b", only the last part actually opens a brace.
                # C++ allows "namespace a::b { ... }" with a single pair.
                # So the first n-1 parts are logical; only the last increments
                # brace_depth. But standard parsers treat a::b as one scope,
                # which is fine: push a single multi-part scope.
                pass
            # Push as one combined scope using "::" name.
            # Only the last "{" actually opens; count opens-closes adjusts.
            scopes.append(Scope("namespace", ns, brace_depth))
            brace_depth += opens - closes
            continue

        m = ANON_NAMESPACE_RE.match(code_line)
        if m:
            opens = code_line.count("{")
            closes = code_line.count("}")
            scopes.append(Scope("anon", "", brace_depth))
            brace_depth += opens - closes
            continue

        # --- Class / struct opener (NOT a forward decl)
        if not FORWARD_DECL_RE.match(code_line):
            m = CLASS_RE.match(code_line)
            if m:
                class_name = m.group(1)
                is_struct = code_line.lstrip().startswith("struct")
                opens = code_line.count("{")
                closes = code_line.count("}")
                scopes.append(Scope(
                    "struct" if is_struct else "class",
                    class_name,
                    brace_depth,
                    visibility="public" if is_struct else "private",
                ))
                brace_depth += opens - closes
                continue

        # --- Visibility specifier
        m = VISIBILITY_RE.match(code_line)
        if m and scopes and scopes[-1].kind in ("class", "struct"):
            scopes[-1].visibility = m.group(1)
            # Visibility lines usually don't include braces, but be safe.
            brace_depth += code_line.count("{") - code_line.count("}")
            continue

        # --- Method detection inside a class scope with public visibility
        # Only at the immediate class-body depth: class_brace_depth + 1.
        # Anything deeper is inside a function body (local variables like
        # ``std::lock_guard<std::mutex> lock(mutex_);`` mustn't be misread as
        # methods).
        if scopes and scopes[-1].kind in ("class", "struct") and \
                scopes[-1].visibility == "public" and \
                brace_depth == scopes[-1].brace_depth + 1:
            method_name = extract_method_name(code_line, scopes[-1].name)
            if method_name is not None:
                ns_path = "::".join(
                    s.name for s in scopes if s.kind == "namespace"
                )
                # Nested classes: include the outer class name chain,
                # but for this SDK that's rare; we only emit the immediate
                # class's methods under its own name.
                class_name = scopes[-1].name
                collected.setdefault((ns_path, class_name), []).append(method_name)

        # --- Update brace depth for any other line with braces
        # (skip string braces already via strip_strings)
        brace_depth += code_line.count("{") - code_line.count("}")

        # Pop scopes whose brace_depth matches current depth-1
        while scopes and brace_depth <= scopes[-1].brace_depth:
            scopes.pop()

    # Dedup and sort methods per class
    for (ns, cls), methods in collected.items():
        seen = []
        seen_set = set()
        for m in methods:
            if m not in seen_set:
                seen.append(m)
                seen_set.add(m)
        findings.append((ns, cls, sorted(seen)))

    return findings


def extract_method_name(code_line: str, class_name: str) -> str | None:
    """If this line looks like a public method declaration, return its name.

    Returns None otherwise. Applies a bunch of filters to weed out:
      - destructors (we don't map them)
      - operators
      - data members (``int foo_ = 0;`` has no paren before ``;``)
      - typedefs and usings
      - control-flow keywords
      - return statements
    """
    stripped = code_line.strip()
    if not stripped:
        return None
    # Skip lines that are plainly not declarations
    if stripped.startswith(("//", "#", "return ", "return;", "return(")):
        return None
    if stripped.startswith(("using ", "typedef ", "friend ", "template ", "enum ")):
        return None
    # A method declaration must contain '(' before ';' or '{'.
    # If there's no '(' at all, it's not a method.
    if "(" not in stripped:
        return None

    # Try the regex
    m = METHOD_RE.match(code_line)
    if not m:
        return None

    name = m.group("name")
    tilde = m.group("tilde")
    prefix = m.group("prefix") or ""

    # Skip destructors (~Foo())
    if tilde:
        return None

    # Skip control-flow / reserved words matched as "name"
    if name in {"if", "else", "for", "while", "do", "switch", "case",
                "return", "sizeof", "throw", "new", "delete",
                "typedef", "using", "template", "friend",
                "enum", "union", "struct", "class", "namespace"}:
        return None

    # Skip operator overloads
    if name == "operator":
        return None

    # Skip when the line is a member declaration that happens to have '(',
    # e.g. "std::function<void()> cb_;". Heuristic: if the "name" is followed
    # immediately by an identifier that itself opens a paren, or if '(' in the
    # prefix (indicating a type expression like std::function<...>(...)).
    # Safer approach: require that the '(' right after the name isn't
    # preceded by '>' without space.
    # In practice the METHOD_RE already handles this since it requires
    # "name\s*\(" at the end. Still, drop if the prefix ends with '>'.
    if prefix.rstrip().endswith(">"):
        # e.g. "std::function<void()> cb;" -- here name is the member,
        # but prefix doesn't end with '>' because the match anchors at the
        # last identifier before '('. This branch is defensive.
        pass

    # Skip if this line is actually a typedef/using for a callable type:
    # "using DynamicConfigCallback = std::function<void(...)>;"
    # Already filtered by stripped.startswith("using ").

    # Skip macro invocations (all-caps names) like REGISTER_SKILL(Foo)
    if name.isupper() and len(name) > 2:
        # Heuristic: all-caps 3+ chars is a macro call, not a method.
        return None

    # Constructor detection: name matches class name
    if name == class_name:
        return "__init__"

    # Skip names starting with underscore (C++ "private-ish" convention)
    if name.startswith("_"):
        return None

    # Skip operator-like names that the regex somehow captured
    if name in SKIP_METHOD_NAMES:
        return None

    return name


# ---------------------------------------------------------------------------
# Module-path translation
# ---------------------------------------------------------------------------

def native_ns_to_module(ns_path: str) -> str:
    """Translate ``signalwire::rest`` -> ``signalwire.rest``.

    Used as a fallback for classes not in CLASS_MODULE_MAP.
    """
    return ns_path.replace("::", ".")


def module_for_class(class_name: str, ns_path: str) -> str | None:
    """Return the Python module path for ``class_name`` in ``ns_path``.

    None means "skip this class entirely" (not used in this SDK right now).
    """
    if class_name in CLASSES_TO_SKIP:
        return None
    if class_name in CLASS_MODULE_MAP:
        return CLASS_MODULE_MAP[class_name]
    # Port-only class: use native translation, with class name snake_cased
    # as the module leaf. Example: signalwire::rest::PhoneCallHandler ->
    # signalwire.rest.phone_call_handler
    leaf = camel_to_snake(class_name)
    base = native_ns_to_module(ns_path)
    if not base:
        return f"signalwire.{leaf}"
    return f"{base}.{leaf}"


def camel_to_snake(name: str) -> str:
    """CamelCase -> snake_case. Handles acronym runs."""
    # Insert an underscore before each capital letter that follows a lowercase
    # or is followed by a lowercase.
    s1 = re.sub(r"(.)([A-Z][a-z]+)", r"\1_\2", name)
    s2 = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", s1)
    return s2.lower()


# ---------------------------------------------------------------------------
# Top-level
# ---------------------------------------------------------------------------

def git_sha(repo: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"],
            stderr=subprocess.DEVNULL,
        ).decode().strip()
    except Exception:
        return "N/A"


def build_snapshot(repo: Path, include_dir: Path) -> dict:
    modules: dict[str, dict] = {}

    # Walk every .hpp/.h under include/
    patterns = ("**/*.hpp", "**/*.h")
    header_files: list[Path] = []
    for p in patterns:
        header_files.extend(sorted(include_dir.glob(p)))

    for path in header_files:
        try:
            findings = parse_header(path)
        except Exception as e:  # pragma: no cover
            print(f"warning: failed to parse {path}: {e}", file=sys.stderr)
            continue

        for ns_path, class_name, methods in findings:
            # Apply class rename (e.g. swml::Service -> SWMLService)
            emit_class = class_name
            emit_mod = None
            if (ns_path, class_name) in CLASS_RENAME_MAP:
                emit_mod, emit_class = CLASS_RENAME_MAP[(ns_path, class_name)]
            if emit_mod is None:
                emit_mod = module_for_class(emit_class, ns_path)
            if emit_mod is None:
                continue
            mod_entry = modules.setdefault(emit_mod, {"classes": {}, "functions": []})
            # If the class was already seen in another header (unlikely but
            # possible for split public/impl headers), merge the method list.
            existing = mod_entry["classes"].get(emit_class, [])
            merged = sorted(set(existing) | set(methods))
            mod_entry["classes"][emit_class] = merged

    # Apply mixin projections: the C++ AgentBase flattens Python's 9 mixin
    # classes. Emit the same method list under each mixin module path so
    # the diff against python_surface.json recognises the symbols as
    # implemented.
    agent_base_methods: set[str] = set()
    ab_entry = modules.get("signalwire.core.agent_base", {})
    ab_methods = ab_entry.get("classes", {}).get("AgentBase", [])
    agent_base_methods = set(ab_methods)

    for (mod, cls), expected_methods in MIXIN_PROJECTIONS.items():
        present = [m for m in expected_methods if m in agent_base_methods]
        mod_entry = modules.setdefault(mod, {"classes": {}, "functions": []})
        # Mixin class always exists (even if empty) so the class symbol
        # itself isn't flagged missing.
        mod_entry["classes"][cls] = sorted(present)

    # Remove empty modules (shouldn't happen in practice but be tidy)
    modules = {k: v for k, v in modules.items() if v["classes"] or v["functions"]}

    return {
        "version": "1",
        "generated_from": f"signalwire-cpp @ {git_sha(repo)}",
        "modules": modules,
    }


def main(argv: list[str]) -> int:
    repo = Path(__file__).resolve().parent.parent
    default_include = repo / "include" / "signalwire"
    default_output = repo / "port_surface.json"

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--include-dir", type=Path, default=default_include,
        help=f"Header root to walk (default: {default_include})",
    )
    parser.add_argument(
        "--output", type=Path, default=default_output,
        help=f"Where to write JSON (default: {default_output})",
    )
    parser.add_argument(
        "--stdout", action="store_true",
        help="Print JSON to stdout instead of writing --output",
    )
    parser.add_argument(
        "--check", action="store_true",
        help="Compare against the file at --output; exit 1 on drift",
    )
    args = parser.parse_args(argv)

    if not args.include_dir.is_dir():
        print(f"error: include dir not found: {args.include_dir}", file=sys.stderr)
        return 1

    snapshot = build_snapshot(repo, args.include_dir)
    rendered = json.dumps(snapshot, indent=2, sort_keys=True) + "\n"

    if args.check:
        if not args.output.is_file():
            print(f"error: {args.output} does not exist", file=sys.stderr)
            return 1
        existing = args.output.read_text(encoding="utf-8")

        def strip_meta(s: str) -> str:
            obj = json.loads(s)
            obj.pop("generated_from", None)
            return json.dumps(obj, indent=2, sort_keys=True) + "\n"

        if strip_meta(rendered) != strip_meta(existing):
            print(
                "DRIFT: port_surface.json is stale relative to headers.\n"
                "  Regenerate:\n"
                "    python3 scripts/enumerate_surface.py",
                file=sys.stderr,
            )
            return 1
        return 0

    if args.stdout:
        sys.stdout.write(rendered)
    else:
        args.output.write_text(rendered, encoding="utf-8")
        print(f"wrote {args.output} "
              f"({len(snapshot['modules'])} modules, "
              f"{sum(len(m['classes']) for m in snapshot['modules'].values())} classes, "
              f"{sum(sum(len(ms) for ms in m['classes'].values()) for m in snapshot['modules'].values())} methods)",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
