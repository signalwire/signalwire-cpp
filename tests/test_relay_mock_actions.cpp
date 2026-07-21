// Mock-relay-backed tests for Action classes.
//
// Translated from
//   signalwire-python/tests/unit/relay/test_actions_mock.py
//
// For each major action verb (play, record, detect, collect,
// play_and_collect, pay, fax, tap, stream, transcribe, ai), drive the SDK
// against the mock and assert:
//   1. The on-wire calling.<verb> frame carries node_id/call_id/control_id
//      (per RELAY_IMPLEMENTATION_GUIDE.md).
//   2. Mock-pushed state events progress the action.
//   3. action.stop() (and pause/resume/volume where applicable) journals
//      the right sub-command frame.
//   4. on_completed callback fires on terminal events.
//   5. play_and_collect gotcha: ONLY the collect terminal event resolves.
//   6. detect gotcha: detect resolves on the first detect payload.

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

std::string fresh_uuid_act() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::ostringstream ss;
    ss << std::hex << gen();
    return ss.str();
}

template <class P>
bool spin(P pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

// Construct a "<call_id> answered" state by driving an inbound, waiting
// for the SDK's Call object, and forcing local state to "answered" so
// subsequent action calls don't think the call is gone.
Call* setup_answered_call(RelayClient& client, const std::string& call_id) {
    Call* call = mt::drive_inbound_call(client, call_id, {"created"});
    if (!call) return nullptr;
    call->update_state("answered");
    return call;
}

json bare_event(const std::string& event_type, const json& params) {
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = fresh_uuid_act();
    frame["method"] = "signalwire.event";
    frame["params"]["event_type"] = event_type;
    frame["params"]["params"] = params;
    return frame;
}

} // namespace

// ---------------------------------------------------------------------------
// PlayAction
// ---------------------------------------------------------------------------

TEST(relay_mock_play_journals_calling_play) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-play");
    ASSERT_TRUE(call != nullptr);
    json media = json::array({{{"type", "tts"}, {"params", {{"text", "hi"}}}}});
    call->play(media, 0.0, "play-ctl-1");

    auto entries = mt::journal_recv("calling.play");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("call_id", ""), "call-play");
    ASSERT_EQ(p.value("control_id", ""), "play-ctl-1");
    ASSERT_EQ(p["play"][0].value("type", ""), "tts");
    client->disconnect();
    return true;
}

TEST(relay_mock_play_resolves_on_finished_event) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-play-fin");
    ASSERT_TRUE(call != nullptr);

    json events = json::array();
    events.push_back({{"emit", {{"state", "playing"}}}, {"delay_ms", 1}});
    events.push_back({{"emit", {{"state", "finished"}}}, {"delay_ms", 5}});
    mt::arm_method("calling.play", events);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 1}}}}});
    Action action = call->play(media, 0.0, "play-ctl-fin");
    bool ok = action.wait(5000);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(action.completed());
    ASSERT_EQ(action.state(), "finished");
    client->disconnect();
    return true;
}

TEST(relay_mock_play_stop_journals_play_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-play-stop");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 60}}}}});
    Action action = call->play(media, 0.0, "play-ctl-stop");
    action.stop();

    spin([] { return !mt::journal_recv("calling.play.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.play.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "play-ctl-stop");
    client->disconnect();
    return true;
}

TEST(relay_mock_play_pause_resume_volume_journal) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-play-prv");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 60}}}}});
    Action action = call->play(media, 0.0, "play-ctl-prv");
    action.pause();
    action.resume();
    action.volume(-3.0);

    spin([] {
        return !mt::journal_recv("calling.play.pause").empty()
            && !mt::journal_recv("calling.play.resume").empty()
            && !mt::journal_recv("calling.play.volume").empty();
    }, 2000);

    ASSERT_FALSE(mt::journal_recv("calling.play.pause").empty());
    ASSERT_FALSE(mt::journal_recv("calling.play.resume").empty());
    auto vol = mt::journal_recv("calling.play.volume");
    ASSERT_FALSE(vol.empty());
    ASSERT_TRUE(vol.back().frame["params"]["volume"].get<double>() == -3.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_play_on_completed_callback_fires) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-play-cb");
    ASSERT_TRUE(call != nullptr);

    json events = json::array();
    events.push_back({{"emit", {{"state", "finished"}}}, {"delay_ms", 1}});
    mt::arm_method("calling.play", events);

    std::atomic<bool> fired{false};
    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 1}}}}});
    Action action = call->play(media, 0.0, "play-ctl-cb");
    action.on_completed([&](const Action& a) {
        if (a.state() == "finished") fired.store(true);
    });
    (void)action.wait(5000);
    spin([&] { return fired.load(); }, 2000);
    ASSERT_TRUE(fired.load());
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// RecordAction
// ---------------------------------------------------------------------------

TEST(relay_mock_record_journals_calling_record) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-rec");
    ASSERT_TRUE(call != nullptr);

    json record_params;
    record_params["audio"]["format"] = "mp3";
    call->record(record_params, "rec-ctl-1");

    auto entries = mt::journal_recv("calling.record");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("call_id", ""), "call-rec");
    ASSERT_EQ(p.value("control_id", ""), "rec-ctl-1");
    ASSERT_EQ(p["record"]["audio"].value("format", ""), "mp3");
    client->disconnect();
    return true;
}

TEST(relay_mock_record_resolves_on_finished_event) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-rec-fin");
    ASSERT_TRUE(call != nullptr);

    json events = json::array();
    events.push_back({{"emit", {{"state", "recording"}}}, {"delay_ms", 1}});
    events.push_back({{"emit", {{"state", "finished"}, {"url", "http://r.wav"}}}, {"delay_ms", 5}});
    mt::arm_method("calling.record", events);

    json rp;
    rp["audio"]["format"] = "wav";
    Action action = call->record(rp, "rec-ctl-fin");
    bool ok = action.wait(5000);
    ASSERT_TRUE(ok);
    ASSERT_EQ(action.state(), "finished");
    client->disconnect();
    return true;
}

TEST(relay_mock_record_stop_journals_record_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-rec-stop");
    ASSERT_TRUE(call != nullptr);

    json rp;
    rp["audio"]["format"] = "wav";
    Action action = call->record(rp, "rec-ctl-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.record.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.record.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "rec-ctl-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// DetectAction — gotcha: resolves on first detect payload
// ---------------------------------------------------------------------------

TEST(relay_mock_detect_resolves_on_first_detect_payload) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-det");
    ASSERT_TRUE(call != nullptr);

    json events = json::array();
    json detect_payload;
    detect_payload["detect"]["type"] = "machine";
    detect_payload["detect"]["params"]["event"] = "MACHINE";
    events.push_back({{"emit", detect_payload}, {"delay_ms", 1}});
    events.push_back({{"emit", {{"state", "finished"}}}, {"delay_ms", 10}});
    mt::arm_method("calling.detect", events);

    json dp;
    dp["detect"]["type"] = "machine";
    dp["detect"]["params"] = json::object();
    Action action = call->detect(dp, "det-ctl-1");
    bool ok = action.wait(5000);
    ASSERT_TRUE(ok);
    // Resolved with the detect payload (not state(finished)).
    json result = action.result();
    ASSERT_TRUE(result.contains("detect"));
    ASSERT_EQ(result["detect"].value("type", ""), "machine");
    client->disconnect();
    return true;
}

TEST(relay_mock_detect_stop_journals_detect_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-det-stop");
    ASSERT_TRUE(call != nullptr);

    json dp;
    dp["detect"]["type"] = "fax";
    dp["detect"]["params"] = json::object();
    Action action = call->detect(dp, "det-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.detect.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.detect.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "det-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// CollectAction (play_and_collect) — gotcha: ignore play(finished)
// ---------------------------------------------------------------------------

TEST(relay_mock_play_and_collect_journals_play_and_collect) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-pac");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "tts"}, {"params", {{"text", "Press 1"}}}}});
    json collect;
    collect["digits"]["max"] = 1;
    call->play_and_collect(media, collect, "pac-ctl-1");

    auto entries = mt::journal_recv("calling.play_and_collect");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("call_id", ""), "call-pac");
    ASSERT_EQ(p["play"][0].value("type", ""), "tts");
    ASSERT_EQ(p["collect"]["digits"]["max"].get<int>(), 1);
    client->disconnect();
    return true;
}

TEST(relay_mock_play_and_collect_resolves_on_collect_event_only) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-pac-go");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 1}}}}});
    json collect;
    collect["digits"]["max"] = 1;
    Action action = call->play_and_collect(media, collect, "pac-go");

    // Push a play(finished) — must NOT resolve the action.
    mt::push(bare_event("calling.call.play", {
        {"call_id", "call-pac-go"},
        {"control_id", "pac-go"},
        {"state", "finished"},
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_FALSE(action.completed());

    // Push a collect event — resolves.
    json collect_event;
    collect_event["call_id"] = "call-pac-go";
    collect_event["control_id"] = "pac-go";
    collect_event["result"]["type"] = "digit";
    collect_event["result"]["params"]["digits"] = "1";
    mt::push(bare_event("calling.call.collect", collect_event));

    bool ok = action.wait(2000);
    ASSERT_TRUE(ok);
    ASSERT_EQ(action.result()["result"].value("type", ""), "digit");
    client->disconnect();
    return true;
}

TEST(relay_mock_play_and_collect_stop_journals_pac_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-pac-stop");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 1}}}}});
    json collect;
    collect["digits"]["max"] = 1;
    Action action = call->play_and_collect(media, collect, "pac-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.play_and_collect.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.play_and_collect.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "pac-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// StandaloneCollectAction
// ---------------------------------------------------------------------------

TEST(relay_mock_collect_journals_calling_collect) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-col");
    ASSERT_TRUE(call != nullptr);

    json cp;
    cp["digits"]["max"] = 4;
    Action action = call->collect(cp, "col-ctl");

    auto entries = mt::journal_recv("calling.collect");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p["digits"]["max"].get<int>(), 4);
    ASSERT_EQ(p.value("control_id", ""), "col-ctl");
    client->disconnect();
    return true;
}

TEST(relay_mock_collect_stop_journals_collect_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-col-stop");
    ASSERT_TRUE(call != nullptr);

    json cp;
    cp["digits"]["max"] = 4;
    Action action = call->collect(cp, "col-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.collect.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.collect.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "col-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// PayAction
// ---------------------------------------------------------------------------

TEST(relay_mock_pay_journals_calling_pay) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-pay");
    ASSERT_TRUE(call != nullptr);

    json pp;
    pp["payment_connector_url"] = "https://pay.example/connect";
    pp["charge_amount"] = "9.99";
    call->pay(pp, "pay-ctl");

    auto entries = mt::journal_recv("calling.pay");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("payment_connector_url", ""), "https://pay.example/connect");
    ASSERT_EQ(p.value("control_id", ""), "pay-ctl");
    ASSERT_EQ(p.value("charge_amount", ""), "9.99");
    client->disconnect();
    return true;
}

TEST(relay_mock_pay_returns_action) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-pay-act");
    ASSERT_TRUE(call != nullptr);
    json pp;
    pp["payment_connector_url"] = "https://pay.example/connect";
    Action action = call->pay(pp, "pay-act");
    ASSERT_EQ(action.control_id(), "pay-act");
    client->disconnect();
    return true;
}

TEST(relay_mock_pay_stop_journals_pay_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-pay-stop");
    ASSERT_TRUE(call != nullptr);
    json pp;
    pp["payment_connector_url"] = "https://pay.example/connect";
    Action action = call->pay(pp, "pay-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.pay.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.pay.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "pay-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// FaxAction
// ---------------------------------------------------------------------------

TEST(relay_mock_send_fax_journals_calling_send_fax) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-sfax");
    ASSERT_TRUE(call != nullptr);
    call->send_fax("https://docs.example/test.pdf", "", "+15551112222", "sfax-ctl");

    auto entries = mt::journal_recv("calling.send_fax");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("document", ""), "https://docs.example/test.pdf");
    ASSERT_EQ(p.value("identity", ""), "+15551112222");
    ASSERT_EQ(p.value("control_id", ""), "sfax-ctl");
    client->disconnect();
    return true;
}

TEST(relay_mock_receive_fax_returns_action) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-rfax");
    ASSERT_TRUE(call != nullptr);
    Action action = call->receive_fax("rfax-ctl");
    ASSERT_EQ(action.control_id(), "rfax-ctl");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// TapAction
// ---------------------------------------------------------------------------

TEST(relay_mock_tap_journals_calling_tap) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-tap");
    ASSERT_TRUE(call != nullptr);

    // The authoritative calling.tap schema requires tap={type, params} (the
    // tap descriptor's params are mandatory — same requirement the python
    // reference tests encode, tap={"type":"audio","params":{...}}). A tap
    // missing params is a malformed frame the STRICT mock now 400s (surfaced by
    // the A2 raise); supply the required params.
    json tp;
    tp["tap"]["type"] = "audio";
    tp["tap"]["params"]["direction"] = "both";
    tp["device"]["type"] = "rtp";
    tp["device"]["params"]["addr"] = "203.0.113.1";
    tp["device"]["params"]["port"] = 4000;
    call->tap(tp, "tap-ctl");

    auto entries = mt::journal_recv("calling.tap");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p["tap"].value("type", ""), "audio");
    ASSERT_EQ(p["device"]["params"]["port"].get<int>(), 4000);
    ASSERT_EQ(p.value("control_id", ""), "tap-ctl");
    client->disconnect();
    return true;
}

TEST(relay_mock_tap_stop_journals_tap_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-tap-stop");
    ASSERT_TRUE(call != nullptr);

    json tp;
    tp["tap"]["type"] = "audio";
    tp["tap"]["params"]["direction"] = "both";
    tp["device"]["type"] = "rtp";
    tp["device"]["params"]["addr"] = "203.0.113.1";
    tp["device"]["params"]["port"] = 4000;
    Action action = call->tap(tp, "tap-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.tap.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.tap.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "tap-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// StreamAction
// ---------------------------------------------------------------------------

TEST(relay_mock_stream_journals_calling_stream) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-strm");
    ASSERT_TRUE(call != nullptr);

    json sp;
    sp["url"] = "wss://stream.example/audio";
    sp["codec"] = "OPUS@48000h";
    call->stream(sp, "strm-ctl");

    auto entries = mt::journal_recv("calling.stream");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p.value("url", ""), "wss://stream.example/audio");
    ASSERT_EQ(p.value("codec", ""), "OPUS@48000h");
    ASSERT_EQ(p.value("control_id", ""), "strm-ctl");
    client->disconnect();
    return true;
}

TEST(relay_mock_stream_stop_journals_stream_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-strm-stop");
    ASSERT_TRUE(call != nullptr);

    json sp;
    sp["url"] = "wss://stream.example/audio";
    Action action = call->stream(sp, "strm-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.stream.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.stream.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "strm-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// TranscribeAction
// ---------------------------------------------------------------------------

TEST(relay_mock_transcribe_journals_calling_transcribe) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-tr");
    ASSERT_TRUE(call != nullptr);

    Action action = call->transcribe(json::object(), "tr-ctl");
    auto entries = mt::journal_recv("calling.transcribe");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    ASSERT_EQ(entries[0].frame["params"].value("control_id", ""), "tr-ctl");
    client->disconnect();
    return true;
}

TEST(relay_mock_transcribe_stop_journals_transcribe_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-tr-stop");
    ASSERT_TRUE(call != nullptr);

    Action action = call->transcribe(json::object(), "tr-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.transcribe.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.transcribe.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "tr-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// AIAction
// ---------------------------------------------------------------------------

TEST(relay_mock_ai_journals_calling_ai) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-ai");
    ASSERT_TRUE(call != nullptr);

    json ap;
    ap["prompt"]["text"] = "You are helpful.";
    Action action = call->ai(ap, "ai-ctl");

    auto entries = mt::journal_recv("calling.ai");
    ASSERT_EQ(entries.size(), static_cast<size_t>(1));
    json p = entries[0].frame["params"];
    ASSERT_EQ(p["prompt"].value("text", ""), "You are helpful.");
    ASSERT_EQ(p.value("control_id", ""), "ai-ctl");
    client->disconnect();
    return true;
}

TEST(relay_mock_ai_stop_journals_ai_stop) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-ai-stop");
    ASSERT_TRUE(call != nullptr);

    json ap;
    ap["prompt"]["text"] = "You are helpful.";
    Action action = call->ai(ap, "ai-stop");
    action.stop();
    spin([] { return !mt::journal_recv("calling.ai.stop").empty(); }, 2000);
    auto stops = mt::journal_recv("calling.ai.stop");
    ASSERT_FALSE(stops.empty());
    ASSERT_EQ(stops.back().frame["params"].value("control_id", ""), "ai-stop");
    client->disconnect();
    return true;
}

// ---------------------------------------------------------------------------
// Concurrent control_id correlation
// ---------------------------------------------------------------------------

TEST(relay_mock_concurrent_play_and_record_route_independently) {
    auto client = mt::make_client();
    Call* call = setup_answered_call(*client, "call-multi");
    ASSERT_TRUE(call != nullptr);

    json media = json::array({{{"type", "silence"}, {"params", {{"duration", 60}}}}});
    Action play_action = call->play(media, 0.0, "ctl-play-x");
    json rp;
    rp["audio"]["format"] = "wav";
    Action record_action = call->record(rp, "ctl-rec-y");
    ASSERT_EQ(play_action.control_id(), "ctl-play-x");
    ASSERT_EQ(record_action.control_id(), "ctl-rec-y");

    // Push only play1's finished.
    mt::push(bare_event("calling.call.play", {
        {"call_id", "call-multi"},
        {"control_id", "ctl-play-x"},
        {"state", "finished"},
    }));
    bool ok = play_action.wait(2000);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(play_action.completed());
    ASSERT_FALSE(record_action.completed());
    client->disconnect();
    return true;
}
