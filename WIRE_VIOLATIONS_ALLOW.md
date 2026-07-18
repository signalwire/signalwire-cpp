# WIRE_VIOLATIONS_ALLOW.md — signed exceptions to the STRICT-MOCKS wire-truth gate

The STRICT-MOCKS consumer (`porting-sdk/scripts/assert_no_wire_violations.py`, wired
into REST-COVERAGE) reads the mock journal after a run and fails on ANY
`wire_violation` — a request/frame that put a shape on the wire the OpenAPI/RELAY
spec does not declare (an undeclared query param, an unknown body key, an unknown
frame field). A wire violation is a spec bug or a real defect; the fix is to make
the wire match the spec, NOT to allowlist it.

This file exists for the rare, genuinely-justified exception, and each entry needs a
human-signed reason. Format (one per line):

    - <kind>:<name> — reason (approver, date)

where `<kind>` is the violation kind (`unknown_query_param`, `unknown_body_key`,
`unknown_frame_field`, `duplicate_command_id`) and `<name>` is the offending
key/param name. A bare `kind:name` with no ` — reason` is NOT matched, so it cannot
silently widen the allowlist.

## Currently empty

No entries. The wired REST-COVERAGE gate runs wire-clean against the reference.

Two spec gaps that would otherwise have needed a park here (`page_size` on
`relay-rest.list_recordings`, `cursor`/`page_token` on `fabric.list_fabric_addresses`)
were instead FIXED AT THE SPEC (porting-sdk `fix/recordings-pagination-spec`), which
now declares those params — see that branch's `rest-apis/relay-rest/openapi.yaml` and
`rest-apis/fabric/openapi.yaml`. Once that branch merges, cpp's REST-COVERAGE run is
wire-clean with no allowlist entries needed for either.
