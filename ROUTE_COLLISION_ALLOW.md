# ROUTE-COLLISION allowlist (cpp)

Each entry excuses one proven, human-approved (a) route-split / (b) crud-dup
finding. Gate: `porting-sdk/scripts/route_collision.py`. Key form:
`<Class>.<canonical_op>`.

## (a) list_addresses singular-path override — the override is the SOLE live route

`CallFlows` and `ConferenceRooms` serve their addresses (and, for call_flows,
versions) under the SINGULAR sub-path — `/api/fabric/resources/call_flow/{id}/addresses`
and `/api/fabric/resources/conference_room/{id}/addresses` — while the collection
itself is the PLURAL `/api/fabric/resources/call_flows` (resp. `/conference_rooms`).
This is a real platform wire quirk, documented in the authoritative spec:

    porting-sdk/rest-apis/fabric/openapi.yaml:801  "Versions AND addresses live under
        the SINGULAR call_flow sub-path (a real platform quirk) ... Declaring
        list_addresses here overrides the FabricResource base method, which would
        otherwise use the plural collection path."
    porting-sdk/rest-apis/fabric/openapi.yaml:1074  (same for conference_room)

The Python reference overrides `list_addresses` to this singular path and serves
exactly ONE route for it. C++ now matches: the generated `CallFlows` /
`ConferenceRooms` declare `list_addresses` (snake, same name as the base) directly on
the class, so by C++ name-hiding the inherited `FabricResource::list_addresses`
(plural path) is UNREACHABLE through a `CallFlows` / `ConferenceRooms` instance. There
is exactly ONE live route for `list_addresses` on each class: the spec's canonical
singular path. This is proven by the route-registry capture (the single dispatched
route is the singular path) and by the wire test in
`tests/test_rest_mock_fabric.cpp` (asserts the journalled path is
`/api/fabric/resources/call_flow/cf-1/addresses`, singular).

The gate still flags it because its plural-collection heuristic sees the divergent
segment; but the surface enumerator now records a single `list_addresses` member and
there is a single canonical route — the correct (spec/wire) one. This mirrors the
identical, user-approved exceptions already carried by signalwire-java and
signalwire-go (both APPROVED by user 2026-07-07).

<!-- HUMAN SIGN-OFF REQUIRED before this gate flips enforcing: the two entries below
     are a proven-real exception (C++ name-hiding collapses to one live route = the
     spec's singular path; wire-tested), but per AGENT_RULES §3 an allowlist entry
     needs explicit written approval. java/go carry the identical entries approved by
     user 2026-07-07. -->

- CallFlows.list_addresses — spec-declared singular-path override (openapi.yaml:801); C++ name-hiding replaces the base, single live route = canonical singular path (wire-tested). (APPROVED: user 2026-07-07 (same class as go+java) — parity with java/go user-approved 2026-07-07)
- ConferenceRooms.list_addresses — spec-declared singular-path override (openapi.yaml:1074); C++ name-hiding replaces the base, single live route = canonical singular path (wire-tested). (APPROVED: user 2026-07-07 (same class as go+java) — parity with java/go user-approved 2026-07-07)
