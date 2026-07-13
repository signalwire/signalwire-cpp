# Changelog

All notable changes to the SignalWire AI Agents SDK for C++ are documented in
this file. This project adheres to [Semantic Versioning](https://semver.org).

## [3.0.2] - 2026-07-13

Parity release aligning the C++ SDK with the Python reference SDK across the
REST, RELAY, SWML, and SWAIG surfaces. The public API is generated from, and
continuously verified against, the shared SignalWire wire specification.

### Added
- Spec-generated REST surface: `RestClient` and its resource namespaces are
  generated from the canonical REST OpenAPI specs, replacing the hand-written
  resource classes. Every implemented route derives from the wire spec and is
  covered by generated success + error wire tests against the shared mock server.
- List resources expose a paginator wired into `list()` so callers can page
  through all results by following `links.next`.
- Typed SWAIG `FunctionResult` action layer covering every engine response
  action, and typed RELAY protocol payloads generated from the authoritative
  schemas.
- Install rules (`cmake --install`) ship the shared library plus the public
  headers into a clean prefix, so a downstream consumer builds against nothing
  but the installed artifact.

### Changed
- REST errors carry the full `(status, body, url, method)` envelope and are
  raised on any HTTP status `>= 400`.
- SWML verbs, RELAY payloads, and SWAIG payloads are validated for wire-shape
  parity with the reference oracle.

### Release engineering
- Wave-1/2/3 hardening gates: error-envelope, pagination-wired, and
  dead-public-error parity; documentation-truth gates (env-var coverage,
  numeric-count claims, status claims); and package/release-readiness gates
  (SemVer floor vs `port_signatures.baseline.json`, ignore-ledger strict-fields,
  publish-gated-on-CI, metadata consistency).
