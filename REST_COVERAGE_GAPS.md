# REST coverage — signalwire-cpp accepted SDK gaps

Canonical REST routes the **C++ SDK** does not implement, so they cannot reach
success+error coverage here. These are the per-port half of the `REST-COVERAGE`
allowlist (type `sdk-gap`); the universal gaps (the two doubled-path spec
artifacts + the video.get_room routing collision) live in
`porting-sdk/REST_COVERAGE_BASELINE.md` and are NOT repeated here.

Format: `<endpoint_id>: sdk-gap — <rationale>`. If a method is later added, the
route becomes coverable and the checker fails on the now-stale entry until it's
removed.

These 18 gaps match the python/go/java/typescript/ruby accepted gaps exactly.
C++ reaches 286/307 with these allowlisted (it carries the chat/pubsub
create_token + verified_callers verification SDK methods, so those routes ARE
covered).

## fabric — dialogflow_agents resource not wired

fabric.list_dialogflow_agents: sdk-gap — no dialogflow_agents resource on the fabric namespace (same gap python carries).
fabric.get_dialogflow_agent: sdk-gap — no dialogflow_agents resource.
fabric.update_dialogflow_agent: sdk-gap — no dialogflow_agents resource.
fabric.delete_dialogflow_agent: sdk-gap — no dialogflow_agents resource.
fabric.list_dialogflow_agent_addresses: sdk-gap — no dialogflow_agents resource.

## relay-rest — SIP endpoints + domain applications have no relay-rest namespace

relay-rest.list_sip_endpoints: sdk-gap — no SDK namespace for /api/relay/rest/endpoints/sip; SIP endpoints live under the Fabric base path instead (same as python).
relay-rest.create_sip_endpoint: sdk-gap — see above.
relay-rest.retrieve_sip_endpoint: sdk-gap — see above.
relay-rest.update_sip_endpoint: sdk-gap — see above.
relay-rest.delete_sip_endpoint: sdk-gap — see above.
relay-rest.list_domain_applications: sdk-gap — no SDK namespace for /api/relay/rest/domain_applications; only a Fabric assign-domain-application exists (same as python).
relay-rest.create_domain_application: sdk-gap — see above.
relay-rest.retrieve_domain_application: sdk-gap — see above.
relay-rest.update_domain_application: sdk-gap — see above.
relay-rest.delete_domain_application: sdk-gap — see above.

## video — video logs have no accessor

video.list_logs: sdk-gap — VideoNamespace exposes no logs accessor for GET /api/video/logs (same as python).
video.get_log: sdk-gap — no video logs accessor (see above).

## compatibility — bare per-country available-numbers node

compatibility.list_available_phone_number_resources_by_country: sdk-gap — CompatPhoneNumbers exposes list_available_countries + search_local (/{IsoCountry}/Local) + search_toll_free (/{IsoCountry}/TollFree), but no method hits the bare /AvailablePhoneNumbers/{IsoCountry} node (same as python).
