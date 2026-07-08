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

at: nlohmann::json::at() / std::map::at() / std::vector::at() — vendored/stdlib element access
begin: std::string::begin / std::vector::begin
del: CrudResource/HttpClient DELETE verb — `delete` is a C++ keyword, so the real emitted method is `del`; the parity surface records the Python name `delete` (enumerate_surface RESERVED_WORD map del→delete), so docs correctly showing `.del(...)` won't resolve against port_surface.json
delete_: generated BaseResource/FabricResource DELETE verb — `delete` is a C++ keyword, so the real emitted method is `delete_`; the parity surface records the Python name `delete` (enumerate_surface RESERVED_WORD map delete_→delete), so docs correctly showing `.delete_(...)` won't resolve against port_surface.json
dump: nlohmann::json::dump() — vendored, not in port surface JSON
empty: std::string::empty / std::vector::empty / std::optional::empty
end: std::string::end / std::vector::end
erase: std::string::erase / std::vector::erase
push_back: std::vector::push_back
substr: std::string::substr
what: std::exception::what()
value: nlohmann::json::value() + std::optional::value + std::variant::value

## 2. nlohmann::json (vendored in deps/)


## 3. Python-syntax doc blocks (upstream Python docs not yet C++ rewritten)

tool: `@AgentBase.tool(...)` Python decorator; C++ uses `define_tool(...)` (which IS in the surface)
register: `server.register(agent)` Python API; C++ uses `register_agent(agent, route)` (which IS in the surface)
build_voicemail_document: Python example helper method
get_document: Python SWMLService accessor; C++ uses `document()` (present but naming differs)
get_full_url: Python request-info helper
get_section: Python ConfigManager
has_config: Python ConfigManager
register_customer_route: user-defined helper in Python routing example
register_product_route: user-defined helper in Python routing example
register_default_tools: Python-only skill helper
reset_document: Python SWMLService helper; C++ rebuilds the document per render
list_all_skill_sources: Python skill-registry diagnostic
validate_env_vars: Python skill base-class helper
validate_packages: Python skill base-class helper
add_swaig_query_params: Python plural alias; C++ uses `add_swaig_query_param()` (singular, IS in the surface)
add_directory: Python web-service static-file helper
remove_directory: Python web-service static-file helper

## 4. Python private helpers / decorators in upstream docs


## 5. Python FastAPI-only integration (no C++ equivalent)

include_router: FastAPI `APIRouter.include_router` — Python only

## 6. Python datetime / logging stdlib

basicConfig: logging.basicConfig()
warning: logging.Logger.warning()

## 7. Filename fragments (comment text matched by `.name(` regex)

cpp: "joke_agent.cpp (raw data_map)" in a comment in joke_skill.cpp

## 8. Relay-client Python-style async surface (C++ RELAY stub)

The C++ RELAY client is documented but implemented as a stub — transport layer
(WebSocket) not yet present. The Python relay client method names appear in
relay/docs/*.md and are kept pending transport implementation.

denoise_stop: relay Call.denoise_stop()
detect_stop: relay Call.detect_stop()
echo: relay Call.echo()
on: relay Call.on(event, callback)
play_and_collect: relay Call.play_and_collect()
receive: relay Client.receive()
refer: relay Call.refer()
stream: relay Call.stream()
transcribe: relay Call.transcribe()
volume: relay Call.volume()

## 9. REST namespace methods referenced in Python examples

The `rest/docs/*.md` pages were ported from upstream Python docs and still
document the Python REST surface. The C++ port exposes a narrower, typed
subset (see `signalwire.rest.*` entries in port_surface.json, especially
`RestClient`, `CallingNamespace`, `PhoneNumbersNamespace`,
`FabricNamespace`, `VideoNamespace`).

### rest/docs/calling.md — Python CallingNamespace extras

### rest/docs/namespaces.md — Python-only namespace methods

### rest/docs/getting-started.md

### docs/api_reference.md / docs/swaig_reference.md — Python helpers
start: service.start() — Python; C++ uses serve() (which IS in the surface)

### relay/docs/events.md / relay/docs/getting-started.md
(covered in section 8)

## 10. Audit harness vocabulary (added when shipping audit harnesses)

The relay/skills/rest audit harnesses use stdlib names that the doc-audit
regex picks up but that aren't part of the SDK's public surface. They are
listed here so audit_docs stays clean.

load: std::atomic<T>::load() — used by relay_audit_harness to read a flag
store: std::atomic<T>::store() — used by relay_audit_harness to set a flag
position: std::sub_match::position() — used by skills_audit_harness regex iteration
pop_back: std::string::pop_back() — used by skills_audit_harness URL trimming
send_raw_request: relay::RelayClient::send_raw_request() — present in the
  C++ surface but the enumerator currently misses public methods declared on
  the same line block as overloaded factories; recorded here pending a
  fix to scripts/enumerate_surface.py
list_tool_names: swml::Service::list_tool_names() — same enumerator-blind
  spot as send_raw_request; the symbol exists on the C++ surface
set_content: httplib::Response::set_content() — third-party (cpp-httplib),
  shows up in swmlservice_ai_sidecar.cpp because the example registers a
  custom routing callback that writes a response directly
Post: httplib::Server::Post() — third-party (cpp-httplib), used by
  swmlservice_ai_sidecar.cpp's custom route registration
listen: httplib::Server::listen() — third-party (cpp-httplib), called on the
  Server returned by swml::Service::as_router() in swml_service_guide.md
c_str: std::string::c_str() — used by swmlservice_ai_sidecar.cpp when
  building a c-string for httplib's route registration
back: std::string::back() / std::vector::back() — stdlib container access
compare: std::string::compare() — stdlib string comparison
contains: nlohmann::json::contains() / std::map::contains() (C++20)
find_first_not_of: std::string::find_first_not_of() — stdlib string scan
find_last_not_of: std::string::find_last_not_of() — stdlib string scan
handler: ToolDefinition::handler field reference (data member, not a method)
is_array: nlohmann::json::is_array() — vendored
is_boolean: nlohmann::json::is_boolean() — vendored
is_null: nlohmann::json::is_null() — vendored
is_number: nlohmann::json::is_number() — vendored
is_object: nlohmann::json::is_object() — vendored
is_string: nlohmann::json::is_string() — vendored
length: std::sub_match::length() / std::string::length() — stdlib
front: std::string::front() — stdlib container access

## 11. C++ stdlib / std::chrono / json / internal helpers + migration-doc names (2026-07-08)

Pre-existing DOC-AUDIT flags surfaced when CI re-ran; none is a public SDK-surface
method (stdlib, std::chrono literals, nlohmann::json calls, internal free-function
helpers, or an intentionally-shown old name in the migration guide).

array: std::array / nlohmann::json::array() — stdlib/vendored
atof: std::atof — C stdlib string-to-double
atoi: std::atoi — C stdlib string-to-int
exit: std::exit — C stdlib process exit
getline: std::getline — stdlib stream read
hours: std::chrono::hours — stdlib duration literal
invalid_argument: std::invalid_argument — stdlib exception type
localtime: std::localtime — C stdlib time conversion
make_pair: std::make_pair — stdlib utility
milliseconds: std::chrono::milliseconds — stdlib duration literal
object: nlohmann::json::object() — vendored json factory
seconds: std::chrono::seconds — stdlib duration literal
setenv: POSIX setenv() — libc environment setter
signal: POSIX signal() — libc signal handler registration (kubernetes_ready_agent example)
sleep_for: std::this_thread::sleep_for — stdlib thread sleep
sregex_iterator: std::sregex_iterator — stdlib regex iteration (skills_audit_harness)
super: Python-style `super` shown in an architecture.md contrast block; C++ uses explicit base-class calls
time: std::time — C stdlib wall-clock read
tolower: std::tolower — stdlib char case fold
toupper: std::toupper — stdlib char case fold
transform: std::transform — stdlib algorithm
get_env: `signalwire::common::get_env` internal env-read helper, not a public SDK-surface method
http_get: `signalwire::skills::http_get` internal skill HTTP helper (skills_http.hpp), not on the public surface
http_post: `signalwire::skills::http_post` internal skill HTTP helper (skills_http.hpp), not on the public surface
ensure_builtin_skills_registered: internal skill-registry bootstrap free function (skill_registry.hpp), not a public method
to_wire_string: internal `signalwire::rest::to_wire_string` inline helper (phone_call_handler.hpp), not on the public surface
SignalWireClient: the OLD 2.x class name shown verbatim in docs/MIGRATION-2.0.md's rename table (renamed to RestClient); intentionally documented, not a live symbol
