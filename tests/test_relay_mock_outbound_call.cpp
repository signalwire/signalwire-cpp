// Mock-relay-backed tests for outbound calls (RelayClient::dial)
//
// Translated from
//   signalwire-python/tests/unit/relay/test_outbound_call_mock.py
//
// The dial flow is the most fragile RELAY surface: calling.dial returns
// a plain 200 with NO call_id; the actual call info arrives via
// subsequent calling.call.state (per leg) and calling.call.dial (with
// the winner) events keyed by `tag`.

#include "relay_mocktest.hpp"
#include "signalwire/relay/client.hpp"

#include <chrono>
#include <random>
#include <regex>
#include <sstream>
#include <thread>

using namespace signalwire::relay;
namespace mt = signalwire::relay::mocktest;
using json = nlohmann::json;

namespace {

template <class P>
bool spin_dial(P pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

json phone_device(const std::string& to = "+15551112222",
                  const std::string& frm = "+15553334444") {
    json d;
    d["type"] = "phone";
    d["params"]["to_number"] = to;
    d["params"]["from_number"] = frm;
    return d;
}

} // namespace

// ---------------------------------------------------------------------------
// Happy-path dial
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_resolves_to_call_with_winner_id) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-happy";
    arm["winner_call_id"] = "winner-1";
    arm["states"] = json::array({"created", "ringing", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    arm["delay_ms"] = 1;
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-happy", 5000);
    ASSERT_EQ(c.call_id(), "winner-1");
    ASSERT_EQ(c.tag(), "t-happy");
    ASSERT_EQ(c.state(), "answered");
    ASSERT_EQ(c.direction(), "outbound");
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_journal_records_calling_dial_frame) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-frame";
    arm["winner_call_id"] = "winner-frame";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    client->dial(devs, "t-frame", 5000);

    auto entries = mt::journal_recv("calling.dial");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("tag", ""), "t-frame");
    ASSERT_TRUE(p["devices"].is_array());
    ASSERT_EQ(p["devices"][0][0].value("type", ""), "phone");
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_with_max_duration_in_frame) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-md";
    arm["winner_call_id"] = "winner-md";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    client->dial(devs, "t-md", 5000, 300);

    auto entries = mt::journal_recv("calling.dial");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_EQ(entries[0].frame["params"]["max_duration"].get<int>(), 300);
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_auto_generates_uuid_tag_when_omitted) {
    auto client = mt::make_client();
    // Drive the answer push from a worker thread once the dial frame lands.
    // The active-session scope is a THREAD-LOCAL set on the test thread by
    // make_client(); a freshly spawned std::thread does NOT inherit it, so we
    // must re-establish it here. Without this the worker's journal reads see
    // the global journal and — fatally under parallel load — its mt::push()
    // BROADCASTS to every connected session, leaking this test's dial event
    // into a concurrent test's client.
    const std::string sid = client->session_id();
    std::thread pusher([&, sid]() {
        mt::set_active_session(sid);
        for (int i = 0; i < 200; ++i) {
            auto entries = mt::journal_recv("calling.dial");
            if (!entries.empty()) {
                std::string tag = entries.back().frame["params"].value("tag", "");
                json frame;
                frame["jsonrpc"] = "2.0";
                frame["id"] = "auto-tag-1";
                frame["method"] = "signalwire.event";
                frame["params"]["event_type"] = "calling.call.dial";
                json& p = frame["params"]["params"];
                p["tag"] = tag;
                p["node_id"] = "node-mock-1";
                p["dial_state"] = "answered";
                p["call"]["call_id"] = "auto-tag-winner";
                p["call"]["node_id"] = "node-mock-1";
                p["call"]["tag"] = tag;
                p["call"]["device"] = phone_device();
                p["call"]["dial_winner"] = true;
                mt::push(frame);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "", 5000);
    pusher.join();

    ASSERT_EQ(c.call_id(), "auto-tag-winner");
    // Tag is a UUID — match the same shape Python checks.
    std::regex uuid_re("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    ASSERT_TRUE(std::regex_match(c.tag(), uuid_re));
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Failure paths
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_failed_returns_empty_call) {
    auto client = mt::make_client();
    // Re-establish the thread-local active-session scope inside the worker (a
    // spawned std::thread doesn't inherit it). Otherwise mt::push() broadcasts
    // to all sessions and mt::journal_recv() reads the global journal — both
    // unsafe under the parallel runner.
    const std::string sid = client->session_id();
    std::thread pusher([&, sid]() {
        mt::set_active_session(sid);
        for (int i = 0; i < 200; ++i) {
            if (!mt::journal_recv("calling.dial").empty()) {
                json frame;
                frame["jsonrpc"] = "2.0";
                frame["id"] = "fail-1";
                frame["method"] = "signalwire.event";
                frame["params"]["event_type"] = "calling.call.dial";
                frame["params"]["params"]["tag"] = "t-fail";
                frame["params"]["params"]["node_id"] = "node-mock-1";
                frame["params"]["params"]["dial_state"] = "failed";
                frame["params"]["params"]["call"] = json::object();
                mt::push(frame);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    });

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-fail", 5000);
    pusher.join();
    // failure → empty Call (call_id is empty)
    ASSERT_EQ(c.call_id(), "");
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_timeout_when_no_event) {
    auto client = mt::make_client();
    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-timeout", 500);
    ASSERT_EQ(c.call_id(), "");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Parallel dial — winner + losers
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_winner_carries_dial_winner_true) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-winner";
    arm["winner_call_id"] = "WIN-ID";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    arm["losers"] = json::array({
        {{"call_id", "LOSE-A"}, {"states", json::array({"created", "ended"})}},
        {{"call_id", "LOSE-B"}, {"states", json::array({"created", "ended"})}},
    });
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-winner", 5000);
    ASSERT_EQ(c.call_id(), "WIN-ID");

    auto sends = mt::journal_send("calling.call.dial");
    ASSERT_FALSE(sends.empty());
    json final;
    bool found_answered = false;
    for (auto& e : sends) {
        if (e.frame["params"]["params"].value("dial_state", "") == "answered") {
            final = e.frame;
            found_answered = true;
            break;
        }
    }
    ASSERT_TRUE(found_answered);
    json inner = final["params"]["params"];
    ASSERT_TRUE(inner["call"]["dial_winner"].get<bool>());
    ASSERT_EQ(inner["call"].value("call_id", ""), "WIN-ID");
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_losers_get_state_events) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-losers";
    arm["winner_call_id"] = "WIN-2";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    arm["losers"] = json::array({
        {{"call_id", "L1"}, {"states", json::array({"created", "ended"})}},
    });
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    client->dial(devs, "t-losers", 5000);

    auto state_events = mt::journal_send("calling.call.state");
    bool found_ended = false;
    for (auto& e : state_events) {
        json p = e.frame["params"]["params"];
        if (p.value("call_id", "") == "L1" && p.value("call_state", "") == "ended") {
            found_ended = true;
            break;
        }
    }
    ASSERT_TRUE(found_ended);
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_losers_cleaned_up_from_calls) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-cleanup";
    arm["winner_call_id"] = "WIN-CL";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    arm["losers"] = json::array({
        {{"call_id", "LOSE-CL"}, {"states", json::array({"created", "ended"})}},
    });
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-cleanup", 5000);
    // Allow loser-state events to flow.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(client->find_call("LOSE-CL") == nullptr);
    ASSERT_TRUE(client->find_call(c.call_id()) != nullptr);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Devices shape
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_devices_serial_two_legs_on_wire) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-serial";
    arm["winner_call_id"] = "WIN-SER";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({
        json::array({phone_device("+15551110001"), phone_device("+15551110002")}),
    });
    client->dial(devs, "t-serial", 5000);
    auto entries = mt::journal_recv("calling.dial");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_EQ(entries[0].frame["params"]["devices"].size(), static_cast<size_t>(1));
    ASSERT_EQ(entries[0].frame["params"]["devices"][0].size(), static_cast<size_t>(2));
    ASSERT_EQ(
        entries[0].frame["params"]["devices"][0][0]["params"].value("to_number", ""),
        "+15551110001");
    client->disconnect();
    return true;
}

TEST(relay_mock_dial_devices_parallel_two_legs_on_wire) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-par";
    arm["winner_call_id"] = "WIN-PAR";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({
        json::array({phone_device("+15551110001")}),
        json::array({phone_device("+15551110002")}),
    });
    client->dial(devs, "t-par", 5000);
    auto entries = mt::journal_recv("calling.dial");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_EQ(entries[0].frame["params"]["devices"].size(), static_cast<size_t>(2));
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// State transitions during dial
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_records_call_state_progression_on_winner) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-prog";
    arm["winner_call_id"] = "WIN-PROG";
    arm["states"] = json::array({"created", "ringing", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-prog", 5000);
    auto state_events = mt::journal_send("calling.call.state");
    bool created = false, ringing = false, answered = false;
    for (auto& e : state_events) {
        json pp = e.frame["params"]["params"];
        if (pp.value("call_id", "") != "WIN-PROG") continue;
        if (pp.value("call_state", "") == "created") created = true;
        if (pp.value("call_state", "") == "ringing") ringing = true;
        if (pp.value("call_state", "") == "answered") answered = true;
    }
    ASSERT_TRUE(created);
    ASSERT_TRUE(ringing);
    ASSERT_TRUE(answered);
    ASSERT_EQ(c.state(), "answered");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// After dial — Call usable for subsequent commands
// ---------------------------------------------------------------------------

TEST(relay_mock_dialed_call_can_send_subsequent_command) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-after";
    arm["winner_call_id"] = "WIN-AFTER";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-after", 5000);
    c.hangup();
    auto ends = mt::journal_recv("calling.end");
    ASSERT_FALSE(ends.empty());
    ASSERT_EQ(ends.back().frame["params"].value("call_id", ""), "WIN-AFTER");
    client->disconnect();
    return true;
}

TEST(relay_mock_dialed_call_can_play) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-play";
    arm["winner_call_id"] = "WIN-PLAY";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "t-play", 5000);
    json media = json::array({{{"type", "tts"}, {"params", {{"text", "hi"}}}}});
    c.play(media);
    auto plays = mt::journal_recv("calling.play");
    ASSERT_FALSE(plays.empty());
    json p = plays.back().frame["params"];
    ASSERT_EQ(p.value("call_id", ""), "WIN-PLAY");
    ASSERT_EQ(p["play"][0].value("type", ""), "tts");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Tag preservation
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_preserves_explicit_tag) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "my-very-explicit-tag-99";
    arm["winner_call_id"] = "WIN-T";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "node-mock-1";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    Call c = client->dial(devs, "my-very-explicit-tag-99", 5000);
    ASSERT_EQ(c.tag(), "my-very-explicit-tag-99");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// JSON-RPC envelope
// ---------------------------------------------------------------------------

TEST(relay_mock_dial_uses_jsonrpc_2_0) {
    auto client = mt::make_client();
    json arm;
    arm["tag"] = "t-rpc";
    arm["winner_call_id"] = "W";
    arm["states"] = json::array({"created", "answered"});
    arm["node_id"] = "n";
    arm["device"] = phone_device();
    mt::arm_dial(arm);

    json devs = json::array({json::array({phone_device()})});
    client->dial(devs, "t-rpc", 5000);
    auto entries = mt::journal_recv("calling.dial");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_EQ(entries[0].frame.value("jsonrpc", ""), "2.0");
    ASSERT_EQ(entries[0].frame.value("method", ""), "calling.dial");
    ASSERT_TRUE(entries[0].frame.contains("id"));
    ASSERT_TRUE(entries[0].frame.contains("params"));
    client->disconnect();
    return true;
}
