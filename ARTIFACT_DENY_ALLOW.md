# ARTIFACT_DENY_ALLOW — files excused from the artifact-deny gate

The C++ port's **published artifact** is the static library `libsignalwire.a`
built from `src/*.cpp` (`add_library(signalwire STATIC …)` in `CMakeLists.txt`
globs `src/*.cpp` only) plus the public headers under `include/`. There is no
`cmake --install` / CPack / `make dist` rule that packages the repository root or
the auxiliary build targets, so there is no package-list command to feed the
gate's authoritative `--listing` mode.

The `git ls-files` proxy therefore over-reports: every file below is a
porting-audit contract file, an audit JSON, or a **separate** `add_executable` /
example build-target — none is compiled into `libsignalwire.a` and none lives
under `src/` or `include/`, so none can ever enter the published artifact.

- `src/*.cpp` → library; `include/` → public headers. Contract/audit files live at
  root; audit harnesses are `examples/` targets; `emit_corpus`/`emit_skills` are
  standalone `add_executable` tool targets built only for the audit/test gates.

Format: `- path — reason (approver, date)`.

- CHECKLIST.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- DOC_AUDIT_IGNORE.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- PORT_ADDITIONS.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- PORT_EXAMPLE_OMISSIONS.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- PORT_OMISSIONS.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- PORT_SIGNATURE_OMISSIONS.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- PORT_TEST_OMISSIONS.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- REST_COVERAGE_GAPS.md — root porting-audit contract file; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- audit_coverage.json — root porting-audit artifact; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- audit_coverage_baseline.json — root porting-audit artifact; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- port_signatures.json — root porting-audit surface oracle; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- port_surface.json — root porting-audit surface oracle; not under src/ or include/, never packaged (orchestrator, 2026-07-06)
- examples/relay_audit_harness.cpp — audit-harness example build-target, not compiled into libsignalwire.a (orchestrator, 2026-07-06)
- examples/rest_audit_harness.cpp — audit-harness example build-target, not compiled into libsignalwire.a (orchestrator, 2026-07-06)
- examples/skills_audit_harness.cpp — audit-harness example build-target, not compiled into libsignalwire.a (orchestrator, 2026-07-06)
- tools/emit_corpus.cpp — standalone add_executable audit tool, not compiled into libsignalwire.a (orchestrator, 2026-07-06)
- tools/emit_skills.cpp — standalone add_executable audit tool, not compiled into libsignalwire.a (orchestrator, 2026-07-06)
