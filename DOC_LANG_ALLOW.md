# DOC_LANG_ALLOW — markdown files excused from the doc-lang-purity gate

Each file below intentionally retains Python code fences because it documents a
**Python-only component that is not ported to the C++ SDK (or any SDK)**. The
Python is the accurate, authoritative reference for that component, so rewriting
it into C++ would be inventing an API the port does not have.

Format: `- path — reason (approver, date)`.

_(none — docs/mcp_gateway_reference.md was rewritten to document the C++ `mcp_gateway` client skill; it no longer contains Python fences, so its allow entry was removed.)_
