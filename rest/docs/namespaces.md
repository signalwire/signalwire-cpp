# All Namespaces

Reference for every namespace beyond Fabric, Calling, and Compat (which have their own pages).

Every accessor is a method on `RestClient` (e.g. `client.phone_numbers()`), and
request bodies / query params are `nlohmann::json` objects (or, for query
params, a `std::map<std::string, std::string>`). All calls are synchronous and
throw `signalwire::rest::SignalWireRestError` on a non-2xx response.

## Phone Numbers

```cpp
// List your phone numbers
auto numbers = client.phone_numbers().list();
numbers = client.phone_numbers().list({{"name", "Main"}});

// Search available numbers to purchase
auto available = client.phone_numbers().search({{"area_code", "512"}, {"number_type", "local"}});

// Purchase a number
auto number = client.phone_numbers().create({{"number", "+15551234567"}});

// Get / update / release
number = client.phone_numbers().get("pn-uuid");
client.phone_numbers().update("pn-uuid", {{"name", "Support Line"}});
client.phone_numbers().del("pn-uuid");
```

See **[phone-binding.md](phone-binding.md)** for the typed `set_*` helpers that
route a number to an SWML webhook, cXML webhook, AI agent, call flow, or RELAY
application/topic.

## Addresses

```cpp
auto addresses = client.addresses().list();
auto address = client.addresses().create({
    {"label", "Office"}, {"street", "123 Main St"}, {"city", "Austin"}, {"state", "TX"}});
address = client.addresses().get("addr-uuid");
client.addresses().del("addr-uuid");
```

## Queues

```cpp
auto queues = client.queues().list();
auto queue = client.queues().create({{"name", "Support"}});
queue = client.queues().get("q-uuid");
client.queues().update("q-uuid", {{"name", "VIP Support"}});
client.queues().del("q-uuid");

// Members
auto members = client.queues().list_members("q-uuid");
auto next_member = client.queues().get_next_member("q-uuid");
auto member = client.queues().get_member("q-uuid", "member-uuid");
```

## Recordings

```cpp
auto recordings = client.recordings().list();
auto recording = client.recordings().get("rec-uuid");
client.recordings().del("rec-uuid");
```

## Number Groups

```cpp
auto groups = client.number_groups().list();
auto group = client.number_groups().create({{"name", "Marketing"}});
group = client.number_groups().get("ng-uuid");
client.number_groups().update("ng-uuid", {{"name", "Sales"}});
client.number_groups().del("ng-uuid");

// Memberships
auto memberships = client.number_groups().list_memberships("ng-uuid");
client.number_groups().add_membership("ng-uuid", {{"phone_number_id", "pn-uuid"}});
auto membership = client.number_groups().get_membership("mem-uuid");
client.number_groups().delete_membership("mem-uuid");
```

## Verified Caller IDs

```cpp
auto callers = client.verified_callers().list();
auto caller = client.verified_callers().create({{"phone_number", "+15551234567"}, {"name", "Office"}});
caller = client.verified_callers().get("vc-uuid");
client.verified_callers().update("vc-uuid", {{"name", "Main Office"}});
client.verified_callers().del("vc-uuid");

// Verification flow
client.verified_callers().redial_verification("vc-uuid");
client.verified_callers().submit_verification("vc-uuid", {{"code", "123456"}});
```

## SIP Profile

Singleton resource -- no ID needed:

```cpp
auto profile = client.sip_profile().get();
client.sip_profile().update({{"username", "myproject"}, {"password", "newsecret"}});
```

## Phone Number Lookup

```cpp
auto info = client.lookup().lookup("+15551234567");
```

The lookup hits `GET /api/relay/rest/lookup/phone_number/{e164}`.

## Short Codes

```cpp
auto codes = client.short_codes().list();
auto code = client.short_codes().get("sc-uuid");
client.short_codes().update("sc-uuid", {{"name", "Alerts"}});
```

## Imported Phone Numbers

```cpp
client.imported_numbers().create({{"number", "+15559999999"}, {"carrier", "external"}});
```

## MFA (Multi-Factor Authentication)

```cpp
// Request a verification code via SMS
auto result = client.mfa().sms({
    {"to", "+15551234567"},
    {"from", "+15559876543"},
    {"message", "Your code is {code}"},
});
std::string request_id = result["id"];

// Or via phone call
result = client.mfa().call({
    {"to", "+15551234567"},
    {"from", "+15559876543"},
});

// Verify the code
result = client.mfa().verify(request_id, {{"token", "123456"}});
```

## 10DLC Campaign Registry

```cpp
// Brands
auto brands = client.registry().brands.list();
auto brand = client.registry().brands.create({{"name", "My Brand"}, {"ein", "12-3456789"}});
brand = client.registry().brands.get("brand-uuid");

// Campaigns under a brand
auto campaigns = client.registry().brands.list_campaigns("brand-uuid");
auto campaign = client.registry().brands.create_campaign("brand-uuid", {{"description", "Alerts"}});

// Campaign management
campaign = client.registry().campaigns.get("camp-uuid");
client.registry().campaigns.update("camp-uuid", {{"description", "Updated alerts"}});

// Number assignments
auto numbers = client.registry().campaigns.list_numbers("camp-uuid");
auto orders = client.registry().campaigns.list_orders("camp-uuid");
auto order = client.registry().campaigns.create_order("camp-uuid", {{"phone_number_ids", {"pn-1"}}});
order = client.registry().orders.get("order-uuid");
```

`client.registry().numbers` exposes a single `delete_(number_id)` method
(removing a number assignment).

## Datasphere

```cpp
// Documents (standard CRUD via list / create / get / update / del)
auto docs = client.datasphere().documents.list();
auto doc = client.datasphere().documents.create({{"url", "https://example.com/doc.pdf"}, {"tags", {"support"}}});
doc = client.datasphere().documents.get("doc-uuid");
client.datasphere().documents.update("doc-uuid", {{"tags", {"support", "billing"}}});
client.datasphere().documents.del("doc-uuid");

// Semantic search
auto results = client.datasphere().documents.search({
    {"query_string", "How do I reset my password?"},
    {"tags", {"support"}},
    {"count", 5},
});

// Chunks
auto chunks = client.datasphere().documents.list_chunks("doc-uuid");
auto chunk = client.datasphere().documents.get_chunk("doc-uuid", "chunk-uuid");
client.datasphere().documents.delete_chunk("doc-uuid", "chunk-uuid");
```

## Video

```cpp
// Rooms
auto rooms = client.video().rooms.list();
auto room = client.video().rooms.create({{"name", "standup"}, {"max_members", 10}});
room = client.video().rooms.get("room-uuid");
client.video().rooms.update("room-uuid", {{"max_members", 20}});
client.video().rooms.del("room-uuid");
client.video().rooms.list_streams("room-uuid");
client.video().rooms.create_stream("room-uuid", {{"url", "rtmp://example.com/live"}});

// Room tokens
auto token = client.video().room_tokens.create({{"room_name", "standup"}, {"user_name", "alice"}});

// Room sessions
auto sessions = client.video().room_sessions.list({{"room_name", "standup"}});
auto session = client.video().room_sessions.get("session-uuid");
auto events = client.video().room_sessions.list_events("session-uuid");
auto members = client.video().room_sessions.list_members("session-uuid");
auto recordings = client.video().room_sessions.list_recordings("session-uuid");

// Room recordings (also: room_recordings.delete_ removes one)
auto recs = client.video().room_recordings.list();
auto rec = client.video().room_recordings.get("rec-uuid");
events = client.video().room_recordings.list_events("rec-uuid");

// Conferences
auto confs = client.video().conferences.list();
auto conf = client.video().conferences.create({{"name", "all-hands"}, {"quality", "720p"}});
conf = client.video().conferences.get("conf-uuid");
client.video().conferences.update("conf-uuid", {{"quality", "1080p"}});
client.video().conferences.del("conf-uuid");
auto tokens = client.video().conferences.list_conference_tokens("conf-uuid");
client.video().conferences.list_streams("conf-uuid");
client.video().conferences.create_stream("conf-uuid", {{"url", "rtmp://example.com/live"}});

// Conference tokens
token = client.video().conference_tokens.get("token-uuid");
client.video().conference_tokens.reset("token-uuid");

// Streams (also: streams.delete_ removes one)
auto stream = client.video().streams.get("stream-uuid");
client.video().streams.update("stream-uuid", {{"url", "rtmp://example.com/new"}});
```

## Logs

All log endpoints are read-only.

```cpp
// Message logs
auto logs = client.logs().messages.list({{"include_deleted", "true"}});
auto log = client.logs().messages.get("log-uuid");

// Voice logs (with events)
logs = client.logs().voice.list();
log = client.logs().voice.get("log-uuid");
auto events = client.logs().voice.list_events("log-uuid");

// Fax logs
logs = client.logs().fax.list();
log = client.logs().fax.get("log-uuid");

// Conference logs
logs = client.logs().conferences.list();
```

## Project Tokens

```cpp
auto token = client.project().tokens.create({
    {"name", "ci-token"},
    {"permissions", {"calling", "messaging", "numbers"}},
});
client.project().tokens.update("token-uuid", {{"name", "renamed-token"}});
// Remove a token: call project().tokens.delete_ with the token id
```

## PubSub Tokens

```cpp
auto token = client.pubsub().create_token({
    {"ttl", 60},
    {"channels", {{{"name", "updates"}, {"read", true}, {"write", false}}}},
    {"member_id", "user-123"},
});
```

## Chat Tokens

```cpp
auto token = client.chat().create_token({
    {"ttl", 60},
    {"channels", {{{"name", "support"}, {"read", true}, {"write", true}}}},
    {"member_id", "user-123"},
});
```
