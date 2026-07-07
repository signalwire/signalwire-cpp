# DOC_LANG_ALLOW — markdown files excused from the doc-lang-purity gate

Each file below intentionally retains Python code fences because it documents a
**Python-only component that is not ported to the C++ SDK (or any SDK)**. The
Python is the accurate, authoritative reference for that component, so rewriting
it into C++ would be inventing an API the port does not have.

Format: `- path — reason (approver, date)`.

- docs/mcp_gateway_reference.md — documents the MCP-to-SWAIG Gateway, an approved Python-only component not ported to any SDK (porting-sdk RULES §I.1); its Python fences are the authoritative reference for that Python service, so they are kept as-is (orchestrator, 2026-07-06)
