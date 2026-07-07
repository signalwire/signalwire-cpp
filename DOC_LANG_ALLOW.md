# DOC_LANG_ALLOW — markdown files excused from the doc-lang-purity gate

Each file below intentionally retains Python code fences because it documents a
**Python-only component that is not ported to the C++ SDK (or any SDK)**. The
Python is the accurate, authoritative reference for that component, so rewriting
it into C++ would be inventing an API the port does not have.

Format: `- path — reason (approver, date)`.

_(no entries — the sole entry, the dead Python-only mcp_gateway skill reference doc, was deleted 2026-07-07; #104)_
