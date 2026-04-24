# DOC_AUDIT_IGNORE — identifiers deliberately skipped by `audit_docs.py`

Each entry below is a name extracted from `docs/`, `rest/docs/`,
`relay/docs/`, `examples/`, `rest/examples/`, or `relay/examples/` that is NOT
part of the C++ port surface in `port_surface.json`, but is still a legitimate
reference. Every entry has a one-line rationale.

Categories:
1. C++ standard library / language
2. nlohmann::json (vendored in deps/json.hpp)
3. Python-syntax doc blocks (ported from upstream Python docs, not yet re-written for C++)
4. Python private helpers / decorators referenced in upstream docs
5. Python FastAPI-only integration (no C++ equivalent)
6. Python datetime / logging stdlib
7. Filename fragments picked up by the method-call regex (e.g. `joke_agent.cpp (` )
8. Relay-client Python-style async surface (C++ RELAY client is a stub)
9. REST namespace methods referenced in Python examples (C++ port exposes a
   narrower subset; see port_surface.json for what actually ships)

---

## 1. C++ standard library / language

begin: std::string::begin / std::vector::begin
dump: nlohmann::json::dump() — vendored, not in port surface JSON
empty: std::string::empty / std::vector::empty / std::optional::empty
end: std::string::end / std::vector::end
erase: std::string::erase / std::vector::erase
push_back: std::vector::push_back
substr: std::string::substr
what: std::exception::what()
value: nlohmann::json::value() + std::optional::value + std::variant::value

## 2. nlohmann::json (vendored in deps/)

to_dict: nlohmann::json helper docs use .to_dict() from upstream Python docs (we use .dump() in C++)

## 3. Python-syntax doc blocks (upstream Python docs not yet C++ rewritten)

tool: `@AgentBase.tool(...)` Python decorator; C++ uses `define_tool(...)` (which IS in the surface)
register: `server.register(agent)` Python API; C++ uses `register_agent(agent, route)` (which IS in the surface)
setGoal: Python POM convenience method; C++ uses `prompt_add_section("Goal", ...)`
setInstructions: Python POM convenience method; C++ uses `prompt_add_section("Instructions", ...)`
setPersonality: Python POM convenience method; C++ uses `prompt_add_section("Personality", ...)`
set_web_hook_url: Python-only convenience on `SwaigFunctionResult`
setup_sip_routing: Python AgentServer helper; C++ uses `enable_sip_routing()` + `map_sip_username()`
setup_google_search: Python skill-setup helper; C++ configures skills via `add_skill("web_search", {...})`
allow_functions: Python decorator / metadata hook
alert_ops_team: user-defined helper in Python example code
apply_custom_config: user-defined helper in Python dynamic-config example
apply_default_config: user-defined helper in Python dynamic-config example
build_document: Python SWMLService internal method; C++ uses `render_swml()` (which IS in the surface)
build_voicemail_document: Python example helper method
delete_state: Python state-mgmt API; C++ port does not yet expose state mutation
get_config: Python ConfigManager; C++ port does not expose a ConfigManager class
get_customer_config: user-defined helper in Python customer-config example
get_customer_settings: user-defined helper in Python customer-config example
get_customer_tier: user-defined helper in Python customer-config example
get_document: Python SWMLService accessor; C++ uses `document()` (present but naming differs)
get_full_url: Python request-info helper
get_section: Python ConfigManager
get_state: Python state-mgmt API; not exposed in C++ port
has_config: Python ConfigManager
handle_serverless_request: Python Lambda/Vercel helper; C++ Lambda integration pending
is_valid_customer: user-defined helper in Python customer-config example
load_user_preferences: user-defined helper in Python state example
on_completion_go_to: Python contexts helper; C++ equivalents are `set_valid_steps` / step fields
register_customer_route: user-defined helper in Python routing example
register_product_route: user-defined helper in Python routing example
register_routing_callback: Python-only dynamic routing callback
register_default_tools: Python-only skill helper
register_knowledge_base_tool: Python-only helper
register_verb_handler: Python SWMLService extension hook; not exposed in C++
reset_document: Python SWMLService helper; C++ rebuilds the document per render
schedule_follow_up: user-defined helper in Python example
send_to_analytics: user-defined helper in Python example
list_all_skill_sources: Python skill-registry diagnostic
validate_env_vars: Python skill base-class helper
validate_packages: Python skill base-class helper
set_web_hook_url: Python-only convenience
add_swaig_query_params: Python plural alias; C++ uses `add_swaig_query_param()` (singular, IS in the surface)
enable_record_call: Python AgentBase helper; C++ uses `record_call()` verb instead
add_application: Python skill-registration alias
add_directory: Python web-service static-file helper
remove_directory: Python web-service static-file helper

## 4. Python private helpers / decorators in upstream docs

_check_basic_auth: Python-internal; C++ uses `validate_auth()` in swml::Service
_configure_instructions: Python-internal helper documented in upstream
_get_new_messages: Python-internal messaging helper
_register_custom_tools: Python-internal hook
_register_default_tools: Python-internal hook
_setup_contexts: Python-internal hook
_setup_static_config: Python-internal hook
_test_api_connection: Python-internal test helper

## 5. Python FastAPI-only integration (no C++ equivalent)

as_router: FastAPI `APIRouter` integration — Python only
include_router: FastAPI `APIRouter.include_router` — Python only

## 6. Python datetime / logging stdlib

basicConfig: logging.basicConfig()
fromisoformat: datetime.fromisoformat()
getLogger: logging.getLogger()
isoformat: datetime.isoformat()
setLevel: logging.Logger.setLevel()
total_seconds: datetime.timedelta.total_seconds()
warning: logging.Logger.warning()
abspath: os.path.abspath()
Thread: threading.Thread

## 7. Filename fragments (comment text matched by `.name(` regex)

cpp: "joke_agent.cpp (raw data_map)" in a comment in joke_skill.cpp

## 8. Relay-client Python-style async surface (C++ RELAY stub)

The C++ RELAY client is documented but implemented as a stub — transport layer
(WebSocket) not yet present. The Python relay client method names appear in
relay/docs/*.md and are kept pending transport implementation.

bind_digit: relay Call.bind_digit()
clear_digit_bindings: relay Call.clear_digit_bindings()
denoise_stop: relay Call.denoise_stop()
detect_stop: relay Call.detect_stop()
echo: relay Call.echo()
from_payload: relay event helper
leave_conference: relay Call.leave_conference()
leave_room: relay Call.leave_room()
on: relay Call.on(event, callback)
play_and_collect: relay Call.play_and_collect()
queue_enter: relay Call.queue_enter()
queue_leave: relay Call.queue_leave()
pass_: relay Call.pass_() — Python reserved-word workaround
receive: relay Client.receive()
refer: relay Call.refer()
stream: relay Call.stream()
transcribe: relay Call.transcribe()
unreceive: relay Client.unreceive()
volume: relay Call.volume()
wait_for: relay action.wait_for()

## 9. REST namespace methods referenced in Python examples

The `rest/docs/*.md` pages were ported from upstream Python docs and still
document the Python REST surface. The C++ port exposes a narrower, typed
subset (see `signalwire.rest.*` entries in port_surface.json, especially
`RestClient`, `CallingNamespace`, `CompatNamespace`, `PhoneNumbersNamespace`,
`FabricNamespace`, `VideoNamespace`).

### rest/docs/calling.md — Python CallingNamespace extras
ai_hold: not in C++ CallingNamespace
ai_message: not in C++ CallingNamespace
ai_stop: not in C++ CallingNamespace
ai_unhold: not in C++ CallingNamespace
collect_start_input_timers: not in C++ CallingNamespace
collect_stop: not in C++ CallingNamespace
play_pause: not in C++ CallingNamespace
play_resume: not in C++ CallingNamespace
play_stop: not in C++ CallingNamespace
play_volume: not in C++ CallingNamespace
receive_fax_stop: not in C++ CallingNamespace
record_pause: not in C++ CallingNamespace
record_resume: not in C++ CallingNamespace
record_stop: not in C++ CallingNamespace
send_fax_stop: not in C++ CallingNamespace
stream_stop: not in C++ CallingNamespace
tap_stop: not in C++ CallingNamespace
transcribe_stop: not in C++ CallingNamespace

### rest/docs/compat.md — Python CompatNamespace extras
delete_media: compat.messages/faxes.delete_media — not in C++ CompatNamespace
delete_recording: compat.conferences.delete_recording — not in C++
dequeue_member: compat.queues.dequeue_member — not in C++
get_media: compat.messages/faxes.get_media — not in C++
get_member: compat.queues.get_member — not in C++
get_participant: compat.conferences.get_participant — not in C++
get_recording: compat.conferences.get_recording — not in C++
import_number: compat.phone_numbers.import_number — not in C++
list_available_countries: compat.phone_numbers.list_available_countries — not in C++
list_media: compat.messages/faxes.list_media — not in C++
list_members: compat.queues.list_members — not in C++
list_participants: compat.conferences.list_participants — not in C++
list_recordings: compat.recordings.list_recordings — not in C++
purchase: compat.phone_numbers.purchase — not in C++
remove_participant: compat.conferences.remove_participant — not in C++
search_local: compat.phone_numbers.search_local — not in C++
search_toll_free: compat.phone_numbers.search_toll_free — not in C++
start_recording: compat.calls.start_recording — not in C++
start_stream: compat.calls/compat.conferences.start_stream — not in C++
stop_stream: compat.calls/compat.conferences.stop_stream — not in C++
update_participant: compat.conferences.update_participant — not in C++
update_recording: compat.recordings/compat.conferences.update_recording — not in C++

### rest/docs/namespaces.md — Python-only namespace methods
add_membership: number_groups.add_membership — not in C++
call: sms.call — not in C++
create_campaign: registry.create_campaign — not in C++
create_order: short_codes.create_order — not in C++
create_stream: video.room_sessions.create_stream — not in C++
delete_chunk: datasphere.delete_chunk — not in C++
delete_membership: number_groups.delete_membership — not in C++
get_chunk: datasphere.get_chunk — not in C++
get_membership: number_groups.get_membership — not in C++
get_next_member: queues.get_next_member — not in C++
list_addresses: addresses.list_addresses (alias) — not in C++
list_campaigns: registry.list_campaigns — not in C++
list_chunks: datasphere.list_chunks — not in C++
list_conference_tokens: video.list_conference_tokens — not in C++
list_events: pubsub.list_events / logs.list_events — not in C++
list_memberships: number_groups.list_memberships — not in C++
list_numbers: short_codes.list_numbers — not in C++
list_orders: short_codes.list_orders — not in C++
list_streams: video.room_sessions.list_streams — not in C++
phone_number: lookup.phone_number — not in C++
redial_verification: verified_callers.redial_verification — not in C++
sms: sip_profile.sms — not in C++
submit_verification: verified_callers.submit_verification — not in C++
verify: verified_callers.verify — not in C++

### rest/docs/getting-started.md
list_addresses: not in C++ (see above)

### docs/api_reference.md / docs/swaig_reference.md — Python helpers
start: service.start() — Python; C++ uses serve() (which IS in the surface)

### relay/docs/events.md / relay/docs/getting-started.md
(covered in section 8)
