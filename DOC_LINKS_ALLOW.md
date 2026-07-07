# DOC_LINKS_ALLOW — dead-link findings excused from the doc-links gate

Two justified classes of exception. Format: `- <md-path>::<target> — reason`.

## Class 1 — cross-repo references to porting-sdk shared-infra docs

`phone-binding.md` lives in the sibling `porting-sdk` shared-infra repo (cloned
adjacent per the port-matrix layout). The relative path `../porting-sdk/…` is
correct on disk but escapes THIS repo's root, so `git ls-files` cannot resolve
it — the gate flags it structurally, not because it's wrong.

- CHECKLIST.md::../porting-sdk/phone-binding.md — cross-repo link to the porting-sdk shared-infra phone-binding spec; correct on the adjacent-clone layout, unresolvable by git ls-files (orchestrator, 2026-07-06)
- PORT_OMISSIONS.md::../porting-sdk/phone-binding.md — cross-repo link to the porting-sdk shared-infra phone-binding spec; correct on the adjacent-clone layout, unresolvable by git ls-files (orchestrator, 2026-07-06)

## Class 2 — C++ lambda captures mis-parsed as markdown links

The gate's `[text](target)` regex matches an empty-bracket C++ lambda capture
`[](const json& args)` / `[](Call& call)` / `[](unsigned …)` inside a ```cpp code
fence as a link with empty text and target `const` / `Call&` / `unsigned`. These
are C++ syntax in code examples, not markdown links — a gate false positive on
legitimate code (the gate scans whole-file text, not skipping fenced blocks).

- CLAUDE.md::const — C++ lambda capture `[](const …)` in a code fence, not a link (orchestrator, 2026-07-06)
- README.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- README.md::Call& — C++ lambda capture `[](Call& …)` in a code fence, not a link (orchestrator, 2026-07-06)
- docs/agent_guide.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/api_reference.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/architecture.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/contexts_guide.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/datamap_guide.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/mcp_integration.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/sdk_features.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/skills_system.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/swml_service_guide.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/swml_service_guide.md::unsigned — C++ lambda capture `[](unsigned …)` in a code fence, not a link (orchestrator, 2026-07-06)
- docs/third_party_skills.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- docs/web_service.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/README.md::Call& — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/call-methods.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/client-reference.md::Call& — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/client-reference.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/events.md::Call& — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/events.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/getting-started.md::Call& — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/messaging.md::Call& — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
- relay/docs/messaging.md::const — C++ lambda capture in a code fence, not a link (orchestrator, 2026-07-06)
