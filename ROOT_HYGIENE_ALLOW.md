# ROOT_HYGIENE_ALLOW — repo-root files excused from the root-hygiene gate

Each file below is a load-bearing porting-audit **contract file** that the
shared `porting-sdk` audit scripts (and this repo's `scripts/run-ci.sh` verify
recipe) read at the repo root **by relative path**. Moving them under `eng/`
would break the shared cross-port pipeline, which this repo cannot edit. They
therefore stay at root and are allowlisted here rather than treated as clutter.

Format: `- <file> — reason (approver, date)`.

- CHECKLIST.md — porting-audit contract file read at repo root by porting-sdk audit scripts (orchestrator, 2026-07-06)
- DOC_AUDIT_IGNORE.md — porting-audit contract file read at repo root by porting-sdk audit_docs.py / ignore_ledger_verify.py (orchestrator, 2026-07-06)
- PORT_ADDITIONS.md — porting-audit contract file read at repo root by porting-sdk diff_port_signatures.py (orchestrator, 2026-07-06)
- PORT_EXAMPLE_OMISSIONS.md — porting-audit contract file read at repo root by porting-sdk audit_example_parity.py (orchestrator, 2026-07-06)
- PORT_OMISSIONS.md — porting-audit contract file read at repo root by porting-sdk diff_port_signatures.py (orchestrator, 2026-07-06)
- PORT_SIGNATURE_OMISSIONS.md — porting-audit contract file read at repo root by porting-sdk diff_port_signatures.py (orchestrator, 2026-07-06)
- PORT_TEST_OMISSIONS.md — porting-audit contract file read at repo root by porting-sdk audit scripts (orchestrator, 2026-07-06)
- REST_COVERAGE_GAPS.md — porting-audit contract file read at repo root by porting-sdk REST-coverage audit (orchestrator, 2026-07-06)
- audit_coverage.json — porting-audit artifact read at repo root by porting-sdk audit_coverage_map.py (orchestrator, 2026-07-06)
- audit_coverage_baseline.json — porting-audit artifact read at repo root by porting-sdk audit_coverage_map.py (orchestrator, 2026-07-06)
- port_signatures.json — porting-audit surface oracle read at repo root by porting-sdk diff/query/drift audit scripts (orchestrator, 2026-07-06)
- port_signatures.baseline.json — load-bearing SEMVER-DIFF release-floor file; mirrors port_signatures.json; must be at root, must not ship (orchestrator, 2026-07-13)
- port_surface.json — porting-audit surface oracle read at repo root by porting-sdk audit_docs.py / check_surface_freshness.py and this repo's run-ci.sh (orchestrator, 2026-07-06)
- ROOT_HYGIENE_ALLOW.md — this gate's own allowlist file (root-hygiene), lives at root by the gate's convention (orchestrator, 2026-07-06)
- DOC_LANG_ALLOW.md — the doc-lang-purity gate's allowlist file, lives at root by that gate's convention (orchestrator, 2026-07-06)
- DOC_LINKS_ALLOW.md — the doc-links gate's allowlist file, lives at root by that gate's convention (orchestrator, 2026-07-06)
- ARTIFACT_DENY_ALLOW.md — the artifact-deny gate's allowlist file, lives at root by that gate's convention (orchestrator, 2026-07-06)
- ROUTE_COLLISION_ALLOW.md — the route-collision gate's allowlist file, read at repo root by porting-sdk route_collision.py by that gate's convention (orchestrator, 2026-07-07)
- WIRE_VIOLATIONS_ALLOW.md — STRICT-MOCKS signed-exception ledger read by porting-sdk assert_no_wire_violations.py / examples_run.py / snippet_run.py at repo root (mike@signalwire.com, 2026-07-18)
