# RestClient Reference

## Construction

`RestClient` is constructed from a space hostname, project ID, and token, or
from the environment:

```cpp
// Explicit (space, project_id, token)
RestClient client("example.signalwire.com", "your-project-id", "your-api-token");

// From SIGNALWIRE_SPACE / SIGNALWIRE_PROJECT_ID / SIGNALWIRE_API_TOKEN
auto from_env_client = RestClient::from_env();

// Explicit base URL (e.g. pointing at a loopback fixture in tests)
auto with_url = RestClient::with_base_url("http://127.0.0.1:8080", "project-id", "token");
```

Authentication uses HTTP Basic Auth (`project_id:token`). The read-only
accessors `project_id()` and `http_client()` expose the project ID and the
underlying `HttpClient` (useful for raw requests against routes the typed
namespaces don't cover).

## Namespaces

Every API surface is available as a namespace accessor method on the client
(e.g. `client.fabric()`). There are 21 namespaces.

### Fabric API — `client.fabric()`

| Member | Description |
|--------|-------------|
| `swml_scripts` | SWML script resources (CRUD + addresses) |
| `swml_webhooks` | SWML webhook resources (auto-materialized; read-only) |
| `ai_agents` | AI agent resources |
| `relay_applications` | Relay application resources |
| `call_flows` | Call flow resources (+ versions / deploy) |
| `conference_rooms` | Conference room resources |
| `freeswitch_connectors` | FreeSWITCH connector resources |
| `subscribers` | Subscriber resources (+ SIP endpoints) |
| `sip_endpoints` | SIP endpoint resources |
| `sip_gateways` | SIP gateway resources |
| `cxml_scripts` | cXML script resources |
| `cxml_webhooks` | cXML webhook resources (auto-materialized; read-only) |
| `cxml_applications` | cXML application resources (no create) |
| `resources` | Generic resource operations |
| `addresses` | Fabric addresses (list/get only) |
| `tokens` | Subscriber/guest/invite/embed token creation |

The 13 standard resource types live under `/api/fabric/resources/<type>`;
`resources`, `addresses`, and `tokens` use their own bases. See
**[fabric.md](fabric.md)**.

### Calling API — `client.calling()`

| Accessor | Description |
|----------|-------------|
| `client.calling()` | REST call control -- 37 commands via POST |

See **[calling.md](calling.md)**.

### Relay REST Resources

| Accessor | Description |
|----------|-------------|
| `client.phone_numbers()` | Phone number management (+ search, + typed `set_*` binding helpers) |
| `client.addresses()` | Address management |
| `client.queues()` | Queue management (+ members) |
| `client.recordings()` | Recording management |
| `client.number_groups()` | Number group management (+ memberships) |
| `client.verified_callers()` | Verified caller ID management (+ verification flow) |
| `client.sip_profile()` | Project SIP profile (get/update) |
| `client.lookup()` | Phone number lookup |
| `client.short_codes()` | Short code management |
| `client.imported_numbers()` | Import external phone numbers |
| `client.mfa()` | Multi-factor authentication (SMS/call/verify) |
| `client.registry()` | 10DLC brand/campaign registry |

### Other APIs

| Accessor | Description |
|----------|-------------|
| `client.datasphere()` | Datasphere document management and semantic search |
| `client.video()` | Video rooms, sessions, recordings, conferences |
| `client.logs()` | Message, voice, fax, and conference logs |
| `client.project()` | API token management |
| `client.pubsub()` | PubSub token creation |
| `client.chat()` | Chat token creation |
| `client.compat()` | Twilio-compatible LAML API |

## Error Handling

```cpp
#include <signalwire/rest/rest_client.hpp>

using namespace signalwire::rest;

try {
    auto agent = client.fabric().ai_agents.get("bad-id");
} catch (const SignalWireRestError& e) {
    std::cerr << e.status() << "\n";  // 404
    std::cerr << e.what() << "\n";    // error message
    std::cerr << e.body() << "\n";    // raw response body
}
```

`SignalWireRestError` is thrown on any non-2xx HTTP response.

### Error Members

| Member | Type | Description |
|--------|------|-------------|
| `status()` | `int` | HTTP status code |
| `body()` | `const std::string&` | Raw response body |
| `what()` | `const char*` | Error message (from `std::runtime_error`) |

## Pagination

`PaginatedIterator` (in `signalwire/rest/http_client.hpp`) walks paginated
responses lazily, following `links.next` until exhausted:

```cpp
PaginatedIterator it(client.http_client(), "/api/fabric/resources/ai_agents");
while (it.has_next()) {
    auto item = it.next();
    // ...
}
```

## Client Behavior

- The same `HttpClient` (cpp-httplib + OpenSSL) is shared across all namespaces.
- Content-Type is `application/json`.
- For `https://` hosts, the system trust store is used; `SSL_CERT_FILE` is
  honored automatically for a custom CA bundle.
- DELETE requests returning 204 return an empty JSON object.
