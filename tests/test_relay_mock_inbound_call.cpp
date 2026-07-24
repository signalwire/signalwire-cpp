// Mock-relay-backed tests for inbound calls (server-initiated)
//
// Translated from
//   signalwire-python/tests/unit/relay/test_inbound_call_mock.py
//
// The mock pushes a calling.call.receive frame; the SDK fires the
// registered on_call handler with a real Call object. Each test asserts
// (1) handler dispatch (2) on-wire shape from the journal and (3) state
// progression from server-pushed events.

#include "relay_mocktest.hpp"
#include "signalwire/relay/client.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <vector>

using namespace signalwire::relay;
namespace mt = signalwire::relay::mocktest;
using json = nlohmann::json;

namespace {

std::string fresh_uuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::ostringstream ss;
    ss << std::hex << gen() << "-" << gen();
    return ss.str();
}

// Build a signalwire.event(calling.call.state) frame for /__mock__/push.
json state_push_frame(const std::string& call_id,
                      const std::string& call_state,
                      const std::string& tag = "",
                      const std::string& direction = "inbound") {
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = fresh_uuid();
    frame["method"] = "signalwire.event";
    frame["params"]["event_type"] = "calling.call.state";
    json& p = frame["params"]["params"];
    p["call_id"] = call_id;
    p["node_id"] = "mock-relay-node-1";
    p["tag"] = tag;
    p["call_state"] = call_state;
    p["direction"] = direction;
    p["device"]["type"] = "phone";
    p["device"]["params"]["from_number"] = "+15551110000";
    p["device"]["params"]["to_number"] = "+15552220000";
    return frame;
}

// Wait until predicate returns true or timeout (ms). Polling is the
// pragmatic match for the SDK's read-thread + main-thread split here.
template <class P>
bool spin_until(P pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// Basic inbound-call handler dispatch
// ---------------------------------------------------------------------------

TEST(relay_mock_inbound_handler_fires_with_call_object) {
    auto client = mt::make_client();
    std::atomic<int> seen_count{0};
    std::string seen_id;
    std::mutex mtx;
    std::condition_variable cv;

    client->on_call([&](Call& c) {
        std::unique_lock<std::mutex> lock(mtx);
        seen_id = c.call_id();
        seen_count.fetch_add(1);
        cv.notify_all();
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-handler";
    opts.from_number = "+15551110000";
    opts.to_number = "+15552220000";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(5),
                    [&] { return seen_count.load() == 1; });
    }
    ASSERT_EQ(seen_count.load(), 1);
    ASSERT_EQ(seen_id, "c-handler");

    // The mock journal should have logged a calling.call.receive send.
    auto sends = mt::journal_send("calling.call.receive");
    ASSERT_FALSE(sends.empty());

    client->disconnect();
    return true;
}

TEST(relay_mock_inbound_call_object_has_correct_id_and_direction) {
    auto client = mt::make_client();
    std::string seen_id;
    std::string seen_dir;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    client->on_call([&](Call& c) {
        std::unique_lock<std::mutex> lock(mtx);
        seen_id = c.call_id();
        seen_dir = c.direction();
        done = true;
        cv.notify_all();
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-dir";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });
    }
    ASSERT_EQ(seen_id, "c-dir");
    ASSERT_EQ(seen_dir, "inbound");
    client->disconnect();
    return true;
}

TEST(relay_mock_inbound_call_carries_from_to) {
    auto client = mt::make_client();
    std::string seen_from;
    std::string seen_to;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    client->on_call([&](Call& c) {
        std::unique_lock<std::mutex> lock(mtx);
        seen_from = c.from();
        seen_to = c.to();
        done = true;
        cv.notify_all();
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-from-to";
    opts.from_number = "+15551112233";
    opts.to_number = "+15554445566";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });
    }
    ASSERT_EQ(seen_from, "+15551112233");
    ASSERT_EQ(seen_to, "+15554445566");
    client->disconnect();
    return true;
}

TEST(relay_mock_inbound_call_initial_state_is_created) {
    auto client = mt::make_client();
    std::string seen_state;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;

    client->on_call([&](Call& c) {
        std::unique_lock<std::mutex> lock(mtx);
        seen_state = c.state();
        done = true;
        cv.notify_all();
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-state";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    {
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait_for(lock, std::chrono::seconds(5), [&] { return done; });
    }
    ASSERT_EQ(seen_state, "created");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Handler answers — calling.answer journaled
// ---------------------------------------------------------------------------

TEST(relay_mock_answer_in_handler_journals_calling_answer) {
    auto client = mt::make_client();
    std::atomic<bool> answered{false};

    client->on_call([&](Call& c) {
        c.answer();
        answered.store(true);
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-ans";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    spin_until([&] { return answered.load(); });
    ASSERT_TRUE(answered.load());
    // Allow the answer round-trip to land in the journal.
    spin_until([] {
        return !mt::journal_recv("calling.answer").empty();
    }, 2000);

    auto answers = mt::journal_recv("calling.answer");
    ASSERT_FALSE(answers.empty());
    ASSERT_EQ(answers.back().frame["params"].value("call_id", ""), "c-ans");
    client->disconnect();
    return true;
}

TEST(relay_mock_answer_then_state_event_advances_call_state) {
    auto client = mt::make_client();

    client->on_call([&](Call& c) { c.answer(); });

    Call* c = mt::drive_inbound_call(*client, "c-ans-state", {"created"});
    ASSERT_TRUE(c != nullptr);
    // Wait for calling.answer to land so we know the handler ran.
    spin_until([] {
        return !mt::journal_recv("calling.answer").empty();
    }, 2000);

    // Push state(answered).
    mt::push(state_push_frame("c-ans-state", "answered"));
    bool advanced = spin_until([&] { return c->state() == "answered"; }, 2000);
    ASSERT_TRUE(advanced);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Handler hangs up
// ---------------------------------------------------------------------------

TEST(relay_mock_hangup_in_handler_journals_calling_end) {
    auto client = mt::make_client();
    std::atomic<bool> hung{false};

    client->on_call([&](Call& c) {
        c.hangup("busy");
        hung.store(true);
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-hangup";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    spin_until([&] { return hung.load(); });
    spin_until([] {
        return !mt::journal_recv("calling.end").empty();
    }, 2000);

    auto ends = mt::journal_recv("calling.end");
    ASSERT_FALSE(ends.empty());
    json p = ends.back().frame["params"];
    ASSERT_EQ(p.value("call_id", ""), "c-hangup");
    ASSERT_EQ(p.value("reason", ""), "busy");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Multiple inbound calls
// ---------------------------------------------------------------------------

TEST(relay_mock_multiple_inbound_calls_each_unique_object) {
    auto client = mt::make_client();
    std::vector<std::string> seen_ids;
    std::mutex mtx;

    client->on_call([&](Call& c) {
        std::lock_guard<std::mutex> lock(mtx);
        seen_ids.push_back(c.call_id());
    });

    mt::InboundCallOpts a;
    a.call_id = "c-seq-1";
    a.delay_ms = 5;
    mt::inbound_call(a);

    spin_until([&] {
        std::lock_guard<std::mutex> lock(mtx);
        return seen_ids.size() == 1;
    }, 3000);

    mt::InboundCallOpts b;
    b.call_id = "c-seq-2";
    b.delay_ms = 5;
    mt::inbound_call(b);

    spin_until([&] {
        std::lock_guard<std::mutex> lock(mtx);
        return seen_ids.size() == 2;
    }, 3000);

    {
        std::lock_guard<std::mutex> lock(mtx);
        ASSERT_EQ(seen_ids.size(), static_cast<size_t>(2));
        ASSERT_EQ(seen_ids[0], "c-seq-1");
        ASSERT_EQ(seen_ids[1], "c-seq-2");
    }
    client->disconnect();
    return true;
}

TEST(relay_mock_multiple_inbound_calls_no_state_bleed) {
    auto client = mt::make_client();

    client->on_call([&](Call& c) { c.answer(); });

    Call* a = mt::drive_inbound_call(*client, "cb-1", {"created"});
    ASSERT_TRUE(a != nullptr);
    Call* b = mt::drive_inbound_call(*client, "cb-2", {"created"});
    ASSERT_TRUE(b != nullptr);

    mt::push(state_push_frame("cb-1", "answered"));
    spin_until([&] { return a->state() == "answered"; }, 2000);
    ASSERT_EQ(a->state(), "answered");
    ASSERT_NE(b->state(), "answered");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Scripted state sequences
// ---------------------------------------------------------------------------

TEST(relay_mock_scripted_state_sequence_advances_call) {
    auto client = mt::make_client();

    client->on_call([&](Call& c) { c.answer(); });

    Call* c = mt::drive_inbound_call(*client, "c-scripted", {"created"});
    ASSERT_TRUE(c != nullptr);
    spin_until([] {
        return !mt::journal_recv("calling.answer").empty();
    }, 2000);

    mt::push(state_push_frame("c-scripted", "answered"));
    mt::push(state_push_frame("c-scripted", "ended"));
    bool ended = spin_until([&] { return c->state() == "ended"; }, 2000);
    ASSERT_TRUE(ended);
    // Ended call should be removed from registry.
    ASSERT_TRUE(client->find_call("c-scripted") == nullptr);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Handler exception doesn't crash client
// ---------------------------------------------------------------------------

TEST(relay_mock_handler_exception_does_not_crash) {
    auto client = mt::make_client();
    std::atomic<bool> fired{false};

    client->on_call([&](Call&) {
        fired.store(true);
        throw std::runtime_error("intentional from handler");
    });

    mt::InboundCallOpts opts;
    opts.call_id = "c-raise";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    spin_until([&] { return fired.load(); });
    // Give the recv loop time to absorb the exception.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ASSERT_TRUE(client->is_connected());
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// scenario_play — full inbound flow
// ---------------------------------------------------------------------------

TEST(relay_mock_scenario_play_full_inbound_flow) {
    auto client = mt::make_client();
    std::atomic<bool> handler_started{false};
    Call* captured = nullptr;
    std::mutex mtx;

    client->on_call([&](Call& c) {
        {
            std::lock_guard<std::mutex> lock(mtx);
            captured = &c;
        }
        c.answer();
        handler_started.store(true);
    });

    mt::wait_for_session(2000);

    json receive_frame;
    receive_frame["jsonrpc"] = "2.0";
    receive_frame["id"] = fresh_uuid();
    receive_frame["method"] = "signalwire.event";
    receive_frame["params"]["event_type"] = "calling.call.receive";
    json& rp = receive_frame["params"]["params"];
    rp["call_id"] = "c-scen";
    rp["node_id"] = "mock-relay-node-1";
    rp["tag"] = "";
    rp["call_state"] = "created";
    rp["direction"] = "inbound";
    rp["device"]["type"] = "phone";
    rp["device"]["params"]["from_number"] = "+15551110000";
    rp["device"]["params"]["to_number"] = "+15552220000";
    rp["context"] = "default";

    json ops = json::array();
    ops.push_back({{"push", {{"frame", receive_frame}}}});
    ops.push_back({{"expect_recv", {{"method", "calling.answer"}, {"timeout_ms", 5000}}}});
    ops.push_back({{"push", {{"frame", state_push_frame("c-scen", "answered")}}}});
    ops.push_back({{"sleep_ms", 50}});
    ops.push_back({{"push", {{"frame", state_push_frame("c-scen", "ended")}}}});

    json result = mt::scenario_play(ops);
    ASSERT_EQ(result.value("status", ""), "completed");
    ASSERT_TRUE(handler_started.load());

    bool ended = spin_until([&] {
        std::lock_guard<std::mutex> lock(mtx);
        return captured != nullptr && captured->state() == "ended";
    }, 3000);
    ASSERT_TRUE(ended);
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Wire shape — the calling.call.receive frame the mock pushed
// ---------------------------------------------------------------------------

TEST(relay_mock_inbound_journal_records_calling_call_receive) {
    auto client = mt::make_client();
    std::atomic<bool> done{false};
    client->on_call([&](Call&) { done.store(true); });

    mt::InboundCallOpts opts;
    opts.call_id = "c-wire";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    spin_until([&] { return done.load(); });

    auto sends = mt::journal_send("calling.call.receive");
    ASSERT_FALSE(sends.empty());
    json inner = sends.back().frame["params"]["params"];
    ASSERT_EQ(inner.value("call_id", ""), "c-wire");
    ASSERT_EQ(inner.value("direction", ""), "inbound");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// MAP-BOUNDS (r5 F5.4): max_active_calls cap is enforced at insertion time.
// With a small cap and terminal events SUPPRESSED (never push "ended"), the
// calls_ registry must not grow past the cap — the overflow inbound call is
// dropped (never registered), so find_call() returns nullptr for it while the
// first `cap` calls remain registered.
// ---------------------------------------------------------------------------

TEST(relay_mock_inbound_respects_max_active_calls_cap) {
    auto client = mt::make_client_with_config(
        [](RelayConfig& cfg) { cfg.max_active_calls = 2; });

    std::atomic<int> handled{0};
    client->on_call([&](Call&) { handled.fetch_add(1); });

    // Drive 2 inbound calls up to the cap. drive_inbound_call waits until the
    // SDK has registered the Call, and we never push a terminal event so the
    // entries persist (the leak scenario).
    Call* a = mt::drive_inbound_call(*client, "cap-1", {"created"});
    Call* b = mt::drive_inbound_call(*client, "cap-2", {"created"});
    ASSERT_TRUE(a != nullptr);
    ASSERT_TRUE(b != nullptr);
    ASSERT_TRUE(client->find_call("cap-1") != nullptr);
    ASSERT_TRUE(client->find_call("cap-2") != nullptr);

    // The 3rd inbound call is over the cap — it must be DROPPED, never handled,
    // never registered.
    mt::InboundCallOpts over;
    over.call_id = "cap-3-overflow";
    over.delay_ms = 5;
    mt::inbound_call(over);

    // Give the recv loop ample time to (not) register it.
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT_TRUE(client->find_call("cap-3-overflow") == nullptr);
    ASSERT_EQ(handled.load(), 2);  // only the 2 under-cap calls dispatched

    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Inbound without a registered handler — does not crash
// ---------------------------------------------------------------------------

TEST(relay_mock_inbound_without_handler_does_not_crash) {
    auto client = mt::make_client();
    // No on_call registered.
    mt::InboundCallOpts opts;
    opts.call_id = "c-nohandler";
    opts.delay_ms = 5;
    mt::inbound_call(opts);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    ASSERT_TRUE(client->is_connected());
    client->disconnect();
    return true;
}
