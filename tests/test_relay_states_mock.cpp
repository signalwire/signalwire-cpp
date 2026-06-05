// Mock-relay-backed tests for the typed lifecycle-state ACCESSORS.
//
// Proves the Tier-3 typed accessors return the right enum for a REAL dispatched
// event off the shared mock-relay server, and AGREE with the existing string
// accessor:
//   * Call::call_state()        vs Call::state()        — calling.call.state
//   * Message::message_state()  vs Message::state()      — messaging.state
//   * DialEvent::dial_state_enum() vs the dial_state str — calling.call.dial
//     (reconstructed from the real frame the mock emits via an on_event observer)
// No mocks of transport: every assertion is driven by a frame the mock server
// actually pushes over the WebSocket.

#include "relay_mocktest.hpp"
#include "signalwire/relay/client.hpp"
#include "signalwire/relay/message.hpp"
#include "signalwire/relay/states.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

using namespace signalwire::relay;
namespace mt = signalwire::relay::mocktest;
using json = nlohmann::json;

namespace {

std::string fresh_uuid_st() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::ostringstream ss;
    ss << std::hex << gen();
    return ss.str();
}

template <class P>
bool spin_st(P pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

json event_frame(const std::string& event_type, const json& params) {
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = fresh_uuid_st();
    frame["method"] = "signalwire.event";
    frame["params"]["event_type"] = event_type;
    frame["params"]["params"] = params;
    return frame;
}

json messaging_state_frame_st(const std::string& message_id, const std::string& state) {
    return event_frame("messaging.state",
                       {{"message_id", message_id}, {"message_state", state}});
}

Call* setup_answered_call_st(RelayClient& client, const std::string& call_id) {
    Call* call = mt::drive_inbound_call(client, call_id, {"created"});
    if (!call) return nullptr;
    call->update_state("answered");
    return call;
}

} // namespace

// ---------------------------------------------------------------------------
// Call::call_state() — typed accessor off a real calling.call.state event
// ---------------------------------------------------------------------------

// After a real calling.call.state -> "ending" event, Call::call_state() returns
// CallState::Ending AND agrees with the bare-string state().
TEST(relay_mock_call_state_accessor_returns_enum_for_dispatched_event) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_st(*client, "st-acc-1");
    ASSERT_TRUE(call != nullptr);

    // Initially answered (set above): typed accessor agrees with string.
    ASSERT_EQ(call->state(), "answered");
    ASSERT_TRUE(call->call_state().has_value());
    ASSERT_TRUE(call->call_state().value() == CallState::Answered);

    // Drive a REAL state event over the wire.
    mt::push(event_frame("calling.call.state", {
        {"call_id", "st-acc-1"},
        {"call_state", "ending"},
        {"direction", "inbound"},
    }));
    bool ok = spin_st([&] { return call->state() == "ending"; }, 2000);
    ASSERT_TRUE(ok);

    // Typed accessor reflects the dispatched value and AGREES with the string:
    // call_state_value(enum) == state().
    ASSERT_TRUE(call->call_state().has_value());
    ASSERT_TRUE(call->call_state().value() == CallState::Ending);
    ASSERT_EQ(call_state_value(call->call_state().value()), call->state());
    ASSERT_FALSE(is_terminal(call->call_state().value()));  // ending is not terminal

    client->disconnect();
    return true;
}

// Through the terminal "ended" event, call_state() -> CallState::Ended and
// is_terminal() agrees. (Drive the real frame; the SDK unregisters the call on
// ended, but the Call* we hold keeps its shared state.)
TEST(relay_mock_call_state_accessor_terminal_on_ended) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_st(*client, "st-acc-end");
    ASSERT_TRUE(call != nullptr);

    mt::push(event_frame("calling.call.state", {
        {"call_id", "st-acc-end"},
        {"call_state", "ended"},
        {"direction", "inbound"},
    }));
    bool ok = spin_st([&] { return call->state() == "ended"; }, 2000);
    ASSERT_TRUE(ok);

    ASSERT_TRUE(call->call_state().has_value());
    ASSERT_TRUE(call->call_state().value() == CallState::Ended);
    ASSERT_TRUE(is_terminal(call->call_state().value()));
    ASSERT_TRUE(call->is_ended());  // string-predicate parity
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Message::message_state() — typed accessor off a real messaging.state event
// ---------------------------------------------------------------------------

TEST(relay_mock_message_state_accessor_returns_enum_for_dispatched_event) {
    auto client = mt::make_client();
    Message msg = client->send_message("+15553334444", "+15551112222", "hi");
    ASSERT_FALSE(msg.message_id.empty());

    // Initial state queued: typed accessor agrees with string.
    ASSERT_EQ(msg.state(), "queued");
    ASSERT_TRUE(msg.message_state().has_value());
    ASSERT_TRUE(msg.message_state().value() == MessageState::Queued);
    ASSERT_FALSE(is_terminal(msg.message_state().value()));

    // Push the REAL terminal delivered state.
    mt::push(messaging_state_frame_st(msg.message_id, "delivered"));
    bool ok = msg.wait(5000);
    ASSERT_TRUE(ok);

    ASSERT_EQ(msg.state(), "delivered");
    ASSERT_TRUE(msg.message_state().has_value());
    ASSERT_TRUE(msg.message_state().value() == MessageState::Delivered);
    // Agreement: enum normalizes back to the same wire string.
    ASSERT_EQ(message_state_value(msg.message_state().value()), msg.state());
    ASSERT_TRUE(is_terminal(msg.message_state().value()));
    ASSERT_TRUE(msg.is_delivered());  // string-predicate parity
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// DialEvent::dial_state_enum() — off the REAL calling.call.dial frame the mock
// emits at the end of the dial dance.
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_state_accessor_returns_enum_for_dispatched_dial_event) {
    auto client = mt::make_client();

    // Capture the raw calling.call.dial event the mock pushes (the SDK consumes
    // it internally to resolve dial(); we also observe it to exercise the typed
    // accessor on the REAL frame).
    std::mutex m;
    std::optional<DialEvent> captured;
    client->on_event([&](const RelayEvent& ev) {
        if (ev.event_type == "calling.call.dial") {
            std::lock_guard<std::mutex> lk(m);
            captured = DialEvent::from_relay_event(ev);
        }
    });

    json arm;
    arm["tag"] = "st-dial";
    arm["winner_call_id"] = "st-dial-winner";
    arm["states"] = json::array({"created", "ringing", "answered"});
    arm["node_id"] = "node-st";
    arm["device"] = json{{"type", "phone"},
                         {"params", {{"to_number", "+15551112222"},
                                     {"from_number", "+15553334444"}}}};
    arm["delay_ms"] = 1;
    mt::arm_dial(arm);

    json devs = json::array({json::array({json{
        {"type", "phone"},
        {"params", {{"to_number", "+15551112222"}, {"from_number", "+15553334444"}}}}})});
    Call c = client->dial(devs, "st-dial", 5000);
    ASSERT_EQ(c.call_id(), "st-dial-winner");

    // The real dial frame carried dial_state:"answered" (mock server.py).
    bool ok = spin_st([&] {
        std::lock_guard<std::mutex> lk(m);
        return captured.has_value();
    }, 2000);
    ASSERT_TRUE(ok);

    std::lock_guard<std::mutex> lk(m);
    ASSERT_EQ(captured->dial_state, "answered");          // bare-string field
    ASSERT_TRUE(captured->dial_state_enum().has_value());  // typed accessor
    ASSERT_TRUE(captured->dial_state_enum().value() == DialState::Answered);
    // Agreement + terminal: answered is a terminal dial outcome.
    ASSERT_EQ(dial_state_value(captured->dial_state_enum().value()), captured->dial_state);
    ASSERT_TRUE(is_terminal(captured->dial_state_enum().value()));
    client->disconnect();
    return true;
}
