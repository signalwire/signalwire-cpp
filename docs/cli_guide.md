# CLI Guide

This guide covers `bin/swaig-test`, the command-line tool included with the
SignalWire C++ SDK.

## Overview

`swaig-test` exercises a running agent from the outside, over HTTP — no
SignalWire platform connection needed. It can:

- **List SWAIG tools** registered on a running agent (`--list-tools`)
- **Execute a SWAIG tool** with parameters, simulating the platform's
  tool-call POST (`--exec`)
- **Dump the SWML document** the agent serves (`--dump-swml`)
- **Introspect a compiled example binary** without running a server
  (`--example`, tool listing only — see the AOT caveat below)

SWAIG (SignalWire AI Gateway) is the platform's AI tool-calling system; SWML
(SignalWire Markup Language) is the JSON document format that defines agent
behavior during calls.

## How it differs from the Python SDK's swaig-test

The Python SDK's `swaig-test` loads your agent **in-process** (agent discovery
inside a source file, serverless-environment simulation, mock request objects,
fake post_data generation). C++ is AOT-compiled — there is no source file to
load — so this SDK's `swaig-test` is a thin HTTP client instead:

- **URL mode** talks to your agent while it runs locally (start it first).
- **`--example` mode** is the only serverless path, and it is
  **tool-listing only**: it spawns a compiled example binary with
  `SWAIG_LIST_TOOLS=1`, captures the tool registry the SDK prints from
  `Service::serve()`, and exits without binding a port. `--exec` and
  `--dump-swml` are not available in `--example` mode — use URL mode against
  the running binary for those.

Python-CLI features like `--simulate-serverless`, `--agent-class`, or
`--fake-full-data` do not exist here; any invocation using them is rejected
with `Unknown option`.

## Requirements

The tool ships in the repo at [`bin/swaig-test`](../bin/swaig-test) — a
self-contained bash script. It needs:

- `bash` and `curl` (URL mode)
- `python3` (optional — used only to pretty-print JSON output; without it the
  raw JSON is printed)
- a built example binary under `build/` for `--example` mode

## Quick Start

Start an agent in one terminal:

```bash
./build/example_simple_agent
```

Then, from a second terminal:

```bash
# List the SWAIG tools registered on the agent
bin/swaig-test http://localhost:3000/agent --list-tools

# Execute a tool with parameters
bin/swaig-test http://localhost:3000/agent --exec get_weather --param location=NYC

# Dump the SWML document the agent serves
bin/swaig-test http://localhost:3000/agent --dump-swml
```

## URL Mode

URL mode drives the agent's real HTTP endpoints:

| Action | Request sent |
|--------|--------------|
| `--list-tools` | `POST <url>/swaig` with `{"action":"get_signature"}` (falls back to `POST <url>` and reads the SWML's function list) |
| `--exec <tool>` | `POST <url>/swaig` with `{"action":"execute","function":"<tool>","argument":{"parsed":[{...params}]},...}` — the same shape the platform sends for a tool call |
| `--dump-swml` | `POST <url>` with `{}` and prints the returned SWML JSON |

`--param key=value` may be repeated; each pair becomes a string argument in
the executed tool's `argument.parsed` payload:

```bash
bin/swaig-test http://localhost:3000/agent --exec lookup_order \
    --param order_id=12345 --param customer=jane
```

### Authentication

Agents auto-generate basic-auth credentials (printed at startup) unless
`SWML_BASIC_AUTH_USER` / `SWML_BASIC_AUTH_PASSWORD` are set. `swaig-test`
sends credentials from either source:

```bash
# Embedded in the URL
bin/swaig-test http://user:pass@localhost:3000/agent --dump-swml

# Or from the same env vars the agent reads
export SWML_BASIC_AUTH_USER=dev
export SWML_BASIC_AUTH_PASSWORD=secret
bin/swaig-test http://localhost:3000/agent --list-tools
```

### Verbose output

`--verbose` (or `-v`) includes the HTTP response headers and extra
diagnostics:

```bash
bin/swaig-test http://localhost:3000/agent --dump-swml --verbose
```

## Example Introspection Mode (`--example`)

For SWMLService-only examples (no HTTP server needed), `--example <name>`
locates the compiled binary CMake emits as `build/example_<name>`, runs it
with `SWAIG_LIST_TOOLS=1`, and prints the registered tools from the
`__SWAIG_TOOLS_BEGIN__` / `__SWAIG_TOOLS_END__` sentinel block the SDK emits
— all without binding any port or making a network call.

```bash
# Build the example first (examples are EXCLUDE_FROM_ALL)
cmake --build build --target example_swmlservice_swaig_standalone

# Introspect its tool registry
bin/swaig-test --example swmlservice_swaig_standalone --list-tools

# Raw JSON (no formatting), e.g. for piping into jq
bin/swaig-test --example swmlservice_swaig_standalone --list-tools --raw

# Binaries in a non-default build directory
bin/swaig-test --example swmlservice_swaig_standalone --list-tools --build-dir build-release
```

> **AOT caveat:** `--example` mode is **list-tools only**. Because C++ is
> AOT-compiled, there is no in-process agent loading; executing a tool or
> rendering SWML requires the agent's HTTP surface. To test `--exec` /
> `--dump-swml` against an example, run the binary (it serves HTTP) and use
> URL mode.

## Flag Reference

| Flag | Mode | Description |
|------|------|-------------|
| `--list-tools` | URL or `--example` | List all SWAIG tools registered on the agent |
| `--exec <tool-name>` | URL only | Execute a SWAIG tool via the agent's `/swaig` endpoint |
| `--param <key=value>` | URL only | Pass a tool parameter (repeatable, used with `--exec`) |
| `--dump-swml` | URL only | Dump the agent's SWML document |
| `--example <name>` | example | Introspect the built `build/example_<name>` binary (list-tools only) |
| `--build-dir <path>` | example | Where to look for example binaries (default: `build`) |
| `--raw` | example | Print raw JSON without formatting |
| `--verbose`, `-v` | any | Show HTTP response headers / extra diagnostics |
| `--parse-only` | any | Validate the arguments and exit without any network call or binary spawn |
| `--dry-run` | any | Alias for `--parse-only` |
| `--help`, `-h` | any | Show usage |

## `--parse-only` / `--dry-run`

`--parse-only` validates an invocation's arguments and exits **without**
loading an agent, spawning a binary, or touching the network. It prints
exactly `parse OK` and exits `0` on valid arguments, exits `2` on invalid
ones, and is position-independent (honored even when it trails
`--exec <tool>`). This is what CI's DOC-CLI gate uses to prove documented
invocations parse against the real CLI.

```bash
bin/swaig-test http://localhost:3000/agent --exec get_time --parse-only
```

## Exit Codes

| Code | Meaning |
|------|---------|
| `0` | Success (or `parse OK` under `--parse-only`) |
| `1` | Runtime failure: connection refused, tool execution failed, missing example binary, no sentinel block |
| `2` | Invalid arguments under `--parse-only` (usage error) |

## Troubleshooting

- **`Failed to connect to <url>`** — the agent isn't running (start it
  first), or the port/route is wrong. The route is the second constructor
  argument of your `AgentBase` (e.g. `/agent`).
- **HTTP 401** — missing/wrong credentials; copy the auto-generated ones the
  agent prints at startup or set `SWML_BASIC_AUTH_USER` /
  `SWML_BASIC_AUTH_PASSWORD` in both processes.
- **`could not find example binary`** — build it first
  (`cmake --build build --target example_<name>`) or point `--build-dir` at
  the right build tree.
- **`did not emit __SWAIG_TOOLS_BEGIN__`** — the example doesn't call
  `service.serve()` (only serve() honors the `SWAIG_LIST_TOOLS=1`
  introspection sentinel) or it links an old `libsignalwire`.
