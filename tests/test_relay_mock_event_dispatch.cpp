// Mock-relay-backed tests for SDK event dispatch / routing.
//
// Translated from
//   signalwire-python/tests/unit/relay/test_event_dispatch_mock.py
//
// Focus: edge cases in the SDK's recv loop and event router that don't
// fit neatly into per-action / per-call test files.

#include "relay_mocktest.hpp"
#include "signalwire/relay/client.hpp"

#include <atomic>
#include <chrono>
#include <random>
#include <sstream>
#include <thread>

using namespace signalwire::relay;
namespace mt = signalwire::relay::mocktest;
using json = nlohmann::json;

namespace {

std::string fresh_uuid_evt() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::ostringstream ss;
    ss << std::hex << gen();
    return ss.str();
}

template <class P>
bool spin_evt(P pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

json bare_event_frame(const std::string& event_type, const json& params) {
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = fresh_uuid_evt();
    frame["method"] = "signalwire.event";
    frame["params"]["event_type"] = event_type;
    frame["params"]["params"] = params;
    return frame;
}

Call* setup_answered_call_evt(RelayClient& client, const std::string& call_id) {
    Call* call = mt::drive_inbound_call(client, call_id, {"created"});
    if (!call) return nullptr;
    call->update_state("answered");
    return call;
}

} // namespace

// ---------------------------------------------------------------------------
// Sub-command journaling
// ---------------------------------------------------------------------------

TEST(relay_mock_record_pause_journals_record_pause) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_evt(*client, "ec-rec-pa");
    ASSERT_TRUE(call != nullptr);

    json rp;
    rp["audio"]["format"] = "wav";
    Action action = call->record(rp, "ec-rec-pa-1");
    action.pause("continuous");

    spin_evt([] { return !mt::journal_recv("calling.record.pause").empty(); }, 2000);
    auto pauses = mt::journal_recv("calling.record.pause");
    ASSERT_FALSE(pauses.empty());
    json p = pauses.back().frame["params"];
    ASSERT_EQ(p.value("control_id", ""), "ec-rec-pa-1");
    ASSERT_EQ(p.value("behavior", ""), "continuous");
    client->disconnect();
    return true;
}

TEST(relay_mock_record_resume_journals_record_resume) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_evt(*client, "ec-rec-re");
    ASSERT_TRUE(call != nullptr);

    json rp;
    rp["audio"]["format"] = "wav";
    Action action = call->record(rp, "ec-rec-re-1");
    action.resume();

    spin_evt([] { return !mt::journal_recv("calling.record.resume").empty(); }, 2000);
    auto resumes = mt::journal_recv("calling.record.resume");
    ASSERT_FALSE(resumes.empty());
    ASSERT_EQ(resumes.back().frame["params"].value("control_id", ""), "ec-rec-re-1");
    client->disconnect();
    return true;
}

TEST(relay_mock_collect_start_input_timers_journals_correctly) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_evt(*client, "ec-col-sit");
    ASSERT_TRUE(call != nullptr);

    json cp;
    cp["digits"]["max"] = 4;
    cp["start_input_timers"] = false;
    Action action = call->collect(cp, "ec-col-sit-1");
    action.start_input_timers();

    spin_evt([] { return !mt::journal_recv("calling.collect.start_input_timers").empty(); }, 2000);
    auto starts = mt::journal_recv("calling.collect.start_input_timers");
    ASSERT_FALSE(starts.empty());
    ASSERT_EQ(starts.back().frame["params"].value("control_id", ""), "ec-col-sit-1");
    client->disconnect();
    return true;
}

TEST(relay_mock_play_volume_carries_negative_value) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_evt(*client, "ec-pvol");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 60}}}}});
    Action action = call->play(media, 0.0, "ec-pvol-1");
    action.volume(-5.5);

    spin_evt([] { return !mt::journal_recv("calling.play.volume").empty(); }, 2000);
    auto vol = mt::journal_recv("calling.play.volume");
    ASSERT_FALSE(vol.empty());
    ASSERT_TRUE(vol.back().frame["params"]["volume"].get<double>() == -5.5);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Unknown event types — recv loop survives
// ---------------------------------------------------------------------------

TEST(relay_mock_unknown_event_type_does_not_crash) {
    auto client = mt::make_client();
    mt::push(bare_event_frame("nonsense.unknown", {{"foo", "bar"}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(client->is_connected());
    client->disconnect();
    return true;
}

TEST(relay_mock_event_with_bad_call_id_is_dropped) {
    auto client = mt::make_client();
    mt::push(bare_event_frame("calling.call.play", {
        {"call_id", "no-such-call-bogus"},
        {"control_id", "stranger"},
        {"state", "playing"},
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(client->is_connected());
    client->disconnect();
    return true;
}

TEST(relay_mock_event_with_empty_event_type_is_dropped) {
    auto client = mt::make_client();
    mt::push(bare_event_frame("", {{"call_id", "x"}}));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(client->is_connected());
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Multi-action concurrency
// ---------------------------------------------------------------------------

TEST(relay_mock_three_concurrent_actions_resolve_independently) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_evt(*client, "ec-3acts");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 60}}}}});
    Action play1 = call->play(media, 0.0, "3a-p1");
    Action play2 = call->play(media, 0.0, "3a-p2");
    json rp;
    rp["audio"]["format"] = "wav";
    Action rec = call->record(rp, "3a-r1");

    // Fire only play1's finished.
    mt::push(bare_event_frame("calling.call.play", {
        {"call_id", "ec-3acts"},
        {"control_id", "3a-p1"},
        {"state", "finished"},
    }));
    bool ok = play1.wait(2000);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(play1.completed());
    ASSERT_FALSE(play2.completed());
    ASSERT_FALSE(rec.completed());

    // Fire play2's.
    mt::push(bare_event_frame("calling.call.play", {
        {"call_id", "ec-3acts"},
        {"control_id", "3a-p2"},
        {"state", "finished"},
    }));
    ok = play2.wait(2000);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(play2.completed());
    ASSERT_FALSE(rec.completed());
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Event ACK round-trip
// ---------------------------------------------------------------------------

TEST(relay_mock_event_ack_sent_back_to_server) {
    auto client = mt::make_client();
    std::string evt_id = "evt-ack-test-1";
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = evt_id;
    frame["method"] = "signalwire.event";
    frame["params"]["event_type"] = "calling.call.play";
    frame["params"]["params"]["call_id"] = "anything";
    frame["params"]["params"]["control_id"] = "x";
    frame["params"]["params"]["state"] = "playing";
    mt::push(frame);

    spin_evt([&] {
        for (auto& e : mt::journal()) {
            if (e.direction != "recv") continue;
            if (!e.frame.contains("id")) continue;
            if (e.frame["id"].get<std::string>() != evt_id) continue;
            if (e.frame.contains("result")) return true;
        }
        return false;
    }, 2000);

    bool found = false;
    for (auto& e : mt::journal()) {
        if (e.direction != "recv") continue;
        if (!e.frame.contains("id")) continue;
        if (e.frame["id"].get<std::string>() != evt_id) continue;
        if (e.frame.contains("result")) { found = true; break; }
    }
    ASSERT_TRUE(found);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Server ping handling
// ---------------------------------------------------------------------------

TEST(relay_mock_server_ping_acked_by_sdk) {
    auto client = mt::make_client();
    std::string ping_id = "ping-test-1";
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = ping_id;
    frame["method"] = "signalwire.ping";
    frame["params"] = json::object();
    mt::push(frame);

    spin_evt([&] {
        for (auto& e : mt::journal()) {
            if (e.direction != "recv") continue;
            if (!e.frame.contains("id")) continue;
            if (e.frame["id"].get<std::string>() != ping_id) continue;
            if (e.frame.contains("result")) return true;
        }
        return false;
    }, 2000);

    bool found = false;
    for (auto& e : mt::journal()) {
        if (e.direction != "recv") continue;
        if (!e.frame.contains("id")) continue;
        if (e.frame["id"].get<std::string>() != ping_id) continue;
        if (e.frame.contains("result")) { found = true; break; }
    }
    ASSERT_TRUE(found);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Calling.error doesn't crash recv loop
// ---------------------------------------------------------------------------

TEST(relay_mock_calling_error_event_does_not_crash) {
    auto client = mt::make_client();
    mt::push(bare_event_frame("calling.error", {
        {"code", "5001"},
        {"message", "synthetic error"},
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_TRUE(client->is_connected());
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// State event for an answered call updates Call.state
// ---------------------------------------------------------------------------

TEST(relay_mock_call_state_event_updates_state) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_evt(*client, "ec-stt");
    ASSERT_TRUE(call != nullptr);

    mt::push(bare_event_frame("calling.call.state", {
        {"call_id", "ec-stt"},
        {"call_state", "ending"},
        {"direction", "inbound"},
    }));
    bool ok = spin_evt([&] { return call->state() == "ending"; }, 2000);
    ASSERT_TRUE(ok);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Generic on_event observer fires
// ---------------------------------------------------------------------------

TEST(relay_mock_on_event_observer_fires_on_unknown_type) {
    auto client = mt::make_client();
    std::atomic<bool> fired{false};
    std::atomic<int> count{0};
    client->on_event([&](const RelayEvent& ev) {
        if (ev.event_type == "weird.unknown") {
            fired.store(true);
        }
        count.fetch_add(1);
    });

    mt::push(bare_event_frame("weird.unknown", {{"x", 1}}));
    spin_evt([&] { return fired.load(); }, 2000);
    ASSERT_TRUE(fired.load());
    client->disconnect();
    return true;
}
