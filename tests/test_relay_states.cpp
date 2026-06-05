// Typed RELAY lifecycle-state enums (Tier-3 idiom) + Device shape struct.
//
// CallState / DialState / MessageState mirror the bare-string state vocabularies
// the Python reference keeps in relay/constants.py (CALL_STATE_*, dial_state,
// MESSAGE_STATE_*). The typed enums are ADDITIVE — the string accessors stay
// canonical (parity). These tests drive REAL behavior (no mocks):
//   * each enumerator maps to its exact wire string (== the const in
//     constants.hpp / constants.py);
//   * *_from_string round-trips every known value AND returns std::nullopt for
//     an unknown one (server-emitted sets can grow — must not throw);
//   * is_terminal() matches the reference's terminal definition
//     (ended / answered|failed / MESSAGE_TERMINAL_STATES);
//   * the three vocabularies stay DISTINCT (cross-parse rejection).
// Device.to_json() is asserted byte-identical to the hand-written {type,params}
// map the raw-json call sites build.

#include "signalwire/relay/states.hpp"
#include "signalwire/relay/device.hpp"
#include "signalwire/relay/constants.hpp"

using namespace signalwire::relay;
using json = nlohmann::json;

// ── CallState {created, ringing, answered, ending, ended} ──────────────────

// Each enumerator maps to the EXACT wire string the CALL_STATE_* const holds;
// ADL to_string agrees. (Asserting against the constants ties the enum to the
// same source of truth the string path uses.)
TEST(call_state_enum_maps_to_wire_string) {
    ASSERT_EQ(call_state_value(CallState::Created),  std::string(CALL_STATE_CREATED));
    ASSERT_EQ(call_state_value(CallState::Ringing),  std::string(CALL_STATE_RINGING));
    ASSERT_EQ(call_state_value(CallState::Answered), std::string(CALL_STATE_ANSWERED));
    ASSERT_EQ(call_state_value(CallState::Ending),   std::string(CALL_STATE_ENDING));
    ASSERT_EQ(call_state_value(CallState::Ended),    std::string(CALL_STATE_ENDED));
    ASSERT_EQ(to_string(CallState::Answered),        std::string("answered"));
    return true;
}

// Every known value round-trips string -> enum -> string.
TEST(call_state_from_string_round_trips_all_known) {
    for (auto v : {CallState::Created, CallState::Ringing, CallState::Answered,
                   CallState::Ending, CallState::Ended}) {
        auto parsed = call_state_from_string(call_state_value(v));
        ASSERT_TRUE(parsed.has_value());
        ASSERT_TRUE(parsed.value() == v);
    }
    return true;
}

// Unknown / future server value -> std::nullopt, NEVER throws.
TEST(call_state_from_string_unknown_is_nullopt) {
    ASSERT_FALSE(call_state_from_string("teleported").has_value());
    ASSERT_FALSE(call_state_from_string("").has_value());
    ASSERT_FALSE(call_state_from_string("ANSWERED").has_value());  // case-sensitive
    // dial/message values are NOT call states (vocabularies stay distinct).
    ASSERT_FALSE(call_state_from_string("dialing").has_value());
    ASSERT_FALSE(call_state_from_string("delivered").has_value());
    return true;
}

// Terminal == ended only.
TEST(call_state_is_terminal_only_ended) {
    ASSERT_TRUE(is_terminal(CallState::Ended));
    ASSERT_FALSE(is_terminal(CallState::Created));
    ASSERT_FALSE(is_terminal(CallState::Ringing));
    ASSERT_FALSE(is_terminal(CallState::Answered));
    ASSERT_FALSE(is_terminal(CallState::Ending));
    return true;
}

// ── DialState {dialing, answered, failed} ──────────────────────────────────
// Grounded in RELAY_IMPLEMENTATION_GUIDE.md line 193. DISTINCT from CallState:
// `failed` is dial-only; there is no dial created/ringing/ending.

TEST(dial_state_enum_maps_to_wire_string) {
    ASSERT_EQ(dial_state_value(DialState::Dialing),  std::string("dialing"));
    ASSERT_EQ(dial_state_value(DialState::Answered), std::string("answered"));
    ASSERT_EQ(dial_state_value(DialState::Failed),   std::string("failed"));
    ASSERT_EQ(to_string(DialState::Failed),          std::string("failed"));
    return true;
}

TEST(dial_state_from_string_round_trips_all_known) {
    for (auto v : {DialState::Dialing, DialState::Answered, DialState::Failed}) {
        auto parsed = dial_state_from_string(dial_state_value(v));
        ASSERT_TRUE(parsed.has_value());
        ASSERT_TRUE(parsed.value() == v);
    }
    return true;
}

TEST(dial_state_from_string_unknown_is_nullopt) {
    ASSERT_FALSE(dial_state_from_string("busy").has_value());
    ASSERT_FALSE(dial_state_from_string("").has_value());
    // call-only states (ringing/ending/created/ended) are NOT dial outcomes.
    ASSERT_FALSE(dial_state_from_string("ringing").has_value());
    ASSERT_FALSE(dial_state_from_string("ended").has_value());
    return true;
}

// Terminal == answered | failed (the dial RPC resolves on either). dialing is
// the in-progress non-terminal state.
TEST(dial_state_is_terminal_answered_or_failed) {
    ASSERT_TRUE(is_terminal(DialState::Answered));
    ASSERT_TRUE(is_terminal(DialState::Failed));
    ASSERT_FALSE(is_terminal(DialState::Dialing));
    return true;
}

// ── MessageState {queued,initiated,sent,delivered,undelivered,failed,received} ─

TEST(message_state_enum_maps_to_wire_string) {
    ASSERT_EQ(message_state_value(MessageState::Queued),      std::string(MESSAGE_STATE_QUEUED));
    ASSERT_EQ(message_state_value(MessageState::Initiated),   std::string(MESSAGE_STATE_INITIATED));
    ASSERT_EQ(message_state_value(MessageState::Sent),        std::string(MESSAGE_STATE_SENT));
    ASSERT_EQ(message_state_value(MessageState::Delivered),   std::string(MESSAGE_STATE_DELIVERED));
    ASSERT_EQ(message_state_value(MessageState::Undelivered), std::string(MESSAGE_STATE_UNDELIVERED));
    ASSERT_EQ(message_state_value(MessageState::Failed),      std::string(MESSAGE_STATE_FAILED));
    ASSERT_EQ(message_state_value(MessageState::Received),    std::string(MESSAGE_STATE_RECEIVED));
    ASSERT_EQ(to_string(MessageState::Delivered),            std::string("delivered"));
    return true;
}

TEST(message_state_from_string_round_trips_all_known) {
    for (auto v : {MessageState::Queued, MessageState::Initiated, MessageState::Sent,
                   MessageState::Delivered, MessageState::Undelivered,
                   MessageState::Failed, MessageState::Received}) {
        auto parsed = message_state_from_string(message_state_value(v));
        ASSERT_TRUE(parsed.has_value());
        ASSERT_TRUE(parsed.value() == v);
    }
    return true;
}

TEST(message_state_from_string_unknown_is_nullopt) {
    ASSERT_FALSE(message_state_from_string("expired").has_value());
    ASSERT_FALSE(message_state_from_string("").has_value());
    // call/dial-only values are NOT message states.
    ASSERT_FALSE(message_state_from_string("ringing").has_value());
    ASSERT_FALSE(message_state_from_string("dialing").has_value());
    return true;
}

// Terminal == MESSAGE_TERMINAL_STATES {delivered, undelivered, failed}.
TEST(message_state_is_terminal_matches_terminal_set) {
    ASSERT_TRUE(is_terminal(MessageState::Delivered));
    ASSERT_TRUE(is_terminal(MessageState::Undelivered));
    ASSERT_TRUE(is_terminal(MessageState::Failed));
    ASSERT_FALSE(is_terminal(MessageState::Queued));
    ASSERT_FALSE(is_terminal(MessageState::Initiated));
    ASSERT_FALSE(is_terminal(MessageState::Sent));
    ASSERT_FALSE(is_terminal(MessageState::Received));
    return true;
}

// ── The three vocabularies must NEVER be conflated ─────────────────────────
// `failed` is a member of BOTH DialState and MessageState but NOT CallState;
// `answered` is in BOTH CallState and DialState but NOT MessageState. The
// parsers are independent — a value parses (or not) per its own enum only.
TEST(state_vocabularies_stay_distinct) {
    // "failed": dial yes, message yes, call no.
    ASSERT_TRUE(dial_state_from_string("failed").has_value());
    ASSERT_TRUE(message_state_from_string("failed").has_value());
    ASSERT_FALSE(call_state_from_string("failed").has_value());
    // "answered": call yes, dial yes, message no.
    ASSERT_TRUE(call_state_from_string("answered").has_value());
    ASSERT_TRUE(dial_state_from_string("answered").has_value());
    ASSERT_FALSE(message_state_from_string("answered").has_value());
    // "delivered": message only.
    ASSERT_TRUE(message_state_from_string("delivered").has_value());
    ASSERT_FALSE(call_state_from_string("delivered").has_value());
    ASSERT_FALSE(dial_state_from_string("delivered").has_value());
    return true;
}

// ── Device {type, params} -> to_json() byte-identical to the hand map ──────

// Device.to_json() must equal the exact {"type":..,"params":..} object the
// raw-json call sites (e.g. the phone_device() helper in the dial mock tests)
// assemble by hand.
TEST(device_to_json_byte_identical_to_hand_map) {
    json hand;
    hand["type"] = "phone";
    hand["params"]["to_number"] = "+15551112222";
    hand["params"]["from_number"] = "+15553334444";

    Device dev{"phone", {{"to_number", "+15551112222"},
                         {"from_number", "+15553334444"}}};
    ASSERT_EQ(dev.to_json(), hand);
    // The discriminant survives verbatim (open set, kept a string).
    ASSERT_EQ(dev.to_json()["type"].get<std::string>(), "phone");
    return true;
}

// Empty params default to an (object), not null — matching the wire shape where
// a device always carries a params object.
TEST(device_to_json_empty_params_is_object) {
    Device dev{"sip"};
    json j = dev.to_json();
    ASSERT_TRUE(j["params"].is_object());
    ASSERT_TRUE(j["params"].empty());
    ASSERT_EQ(j["type"].get<std::string>(), "sip");
    return true;
}

// A Device built for a dial/connect leg serializes to the same shape the
// existing raw-json connect()/dial() devices expect: an object with the two
// required-ish keys. Nesting it the way dial() wants (array-of-arrays) keeps
// the wire identical.
TEST(device_nests_into_dial_devices_shape) {
    Device dev{"phone", {{"to_number", "+15550001111"}}};
    json devices = json::array({ json::array({ dev.to_json() }) });
    // outer array -> serial groups; inner array -> parallel within a group.
    ASSERT_TRUE(devices.is_array());
    ASSERT_EQ(devices[0][0]["type"].get<std::string>(), "phone");
    ASSERT_EQ(devices[0][0]["params"]["to_number"].get<std::string>(), "+15550001111");
    return true;
}
