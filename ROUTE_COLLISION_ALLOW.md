# ROUTE-COLLISION allowlist (cpp)

Each entry excuses one proven, human-approved (a) route-split / (b) crud-dup
finding. Gate: `porting-sdk/scripts/route_collision.py`. Key form:
`<Class>.<canonical_op>`.

> RETIRED (plan a-bar 2026-07-19): the two `CallFlows.list_addresses` /
> `ConferenceRooms.list_addresses` route-split entries this file carried are GONE.
> porting-sdk's `route_collision.py` is SPEC-AWARE on the plan branch — it recognizes
> the fabric `call_flow`/`conference_room` SINGULAR address sub-paths as spec-faithful
> directly from `rest-apis/fabric/openapi.yaml` (DECISIONS RECORD, ROUTE-SPLIT ×4:
> "fix the check"), so they are no longer findings at all (verified: route-split=0
> with zero allow entries). This repo's CI pins porting-sdk `plan/a-bar-2026-07-18`
> (same commit as the retirement), so no lane still runs the old heuristic. If the
> pin ever reverts to a pre-spec-aware main, the gate reds and the entries come back
> from git history with re-approval.

## Entries

(none)
