// Mock-relay-backed tests for the typed Call convenience wrappers.
//
// Covers the 12 thin typed wrappers restored in the Python reference and
// ported to C++:
//   play_tts / play_audio / play_silence / play_ringtone
//   detect_digit / detect_answering_machine / detect_fax
//   prompt_tts / prompt_audio
//   wait_for_answered / wait_for_ringing / wait_for_ending
//
// Each wrapper is a typed front for a generic Call method (play / detect /
// play_and_collect / a state-wait). The tests drive the SDK against the
// shared mock_relay server and assert the EXACT RELAY wire shape the wrapper
// builds (per RELAY_IMPLEMENTATION_GUIDE.md) lands on the journal — never a
// transport mock. State-wait wrappers are driven by mock-pushed state events.

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

std::string fresh_uuid_conv() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    std::ostringstream ss;
    ss << std::hex << gen();
    return ss.str();
}

template <class P>
bool spin_conv(P pred, int timeout_ms = 5000) {
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

Call* setup_answered_call_conv(RelayClient& client, const std::string& call_id) {
    Call* call = mt::drive_inbound_call(client, call_id, {"created"});
    if (!call) return nullptr;
    call->update_state("answered");
    return call;
}

json bare_event_conv(const std::string& event_type, const json& params) {
    json frame;
    frame["jsonrpc"] = "2.0";
    frame["id"] = fresh_uuid_conv();
    frame["method"] = "signalwire.event";
    frame["params"]["event_type"] = event_type;
    frame["params"]["params"] = params;
    return frame;
}

// The play/detect/play_and_collect wrappers auto-generate a control_id, so we
// assert on the FIRST (only) journal frame for the verb rather than a pinned
// id. The media/detect payload shape is the load-bearing assertion.
json first_recv_params(const std::string& method) {
    auto entries = mt::journal_recv(method);
    if (entries.empty()) return json::object();
    return entries[0].frame["params"];
}

} // namespace

// ===========================================================================
// play_tts — play [{type:"tts", params:{text, language?, gender?, voice?}}]
// ===========================================================================

TEST(relay_mock_play_tts_journals_tts_media) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-tts");
    ASSERT_TRUE(call != nullptr);

    call->play_tts("Hello world", "en-US", "female", "spore", -3.0);

    json p = first_recv_params("calling.play");
    ASSERT_EQ(p.value("call_id", ""), "conv-tts");
    // Exact RELAY shape: play is an array of one tts media object.
    ASSERT_TRUE(p["play"].is_array());
    ASSERT_EQ(p["play"].size(), static_cast<size_t>(1));
    ASSERT_EQ(p["play"][0].value("type", ""), "tts");
    json params = p["play"][0]["params"];
    ASSERT_EQ(params.value("text", ""), "Hello world");
    ASSERT_EQ(params.value("language", ""), "en-US");
    ASSERT_EQ(params.value("gender", ""), "female");
    ASSERT_EQ(params.value("voice", ""), "spore");
    // volume rides at the top level of the play frame.
    ASSERT_TRUE(p["volume"].get<double>() == -3.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_play_tts_omits_unset_optionals) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-tts-min");
    ASSERT_TRUE(call != nullptr);

    call->play_tts("Just text");

    json p = first_recv_params("calling.play");
    json params = p["play"][0]["params"];
    ASSERT_EQ(params.value("text", ""), "Just text");
    // Only-provided keys: no language/gender/voice on the wire, no volume.
    ASSERT_FALSE(params.contains("language"));
    ASSERT_FALSE(params.contains("gender"));
    ASSERT_FALSE(params.contains("voice"));
    ASSERT_FALSE(p.contains("volume"));
    client->disconnect();
    return true;
}

TEST(relay_mock_play_tts_resolves_on_finished) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-tts-fin");
    ASSERT_TRUE(call != nullptr);

    json events = json::array();
    events.push_back({{"emit", {{"state", "playing"}}}, {"delay_ms", 1}});
    events.push_back({{"emit", {{"state", "finished"}}}, {"delay_ms", 5}});
    mt::arm_method("calling.play", events);

    Action action = call->play_tts("done soon");
    ASSERT_TRUE(action.wait(5000));
    ASSERT_EQ(action.state(), "finished");
    client->disconnect();
    return true;
}

// ===========================================================================
// play_audio — play [{type:"audio", params:{url}}]
// ===========================================================================

TEST(relay_mock_play_audio_journals_audio_media) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-aud");
    ASSERT_TRUE(call != nullptr);

    call->play_audio("https://cdn.example/greeting.mp3", 2.0);

    json p = first_recv_params("calling.play");
    ASSERT_EQ(p["play"][0].value("type", ""), "audio");
    ASSERT_EQ(p["play"][0]["params"].value("url", ""),
              "https://cdn.example/greeting.mp3");
    ASSERT_TRUE(p["volume"].get<double>() == 2.0);
    client->disconnect();
    return true;
}

// ===========================================================================
// play_silence — play [{type:"silence", params:{duration}}]
// ===========================================================================

TEST(relay_mock_play_silence_journals_silence_media) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-sil");
    ASSERT_TRUE(call != nullptr);

    call->play_silence(2.5);

    json p = first_recv_params("calling.play");
    ASSERT_EQ(p["play"][0].value("type", ""), "silence");
    ASSERT_TRUE(p["play"][0]["params"]["duration"].get<double>() == 2.5);
    client->disconnect();
    return true;
}

// ===========================================================================
// play_ringtone — play [{type:"ringtone", params:{name, duration?}}]
// ===========================================================================

TEST(relay_mock_play_ringtone_journals_ringtone_media) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-rt");
    ASSERT_TRUE(call != nullptr);

    call->play_ringtone("us", 5.0, 1.0);

    json p = first_recv_params("calling.play");
    ASSERT_EQ(p["play"][0].value("type", ""), "ringtone");
    ASSERT_EQ(p["play"][0]["params"].value("name", ""), "us");
    ASSERT_TRUE(p["play"][0]["params"]["duration"].get<double>() == 5.0);
    ASSERT_TRUE(p["volume"].get<double>() == 1.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_play_ringtone_omits_duration_when_unset) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-rt-min");
    ASSERT_TRUE(call != nullptr);

    call->play_ringtone("uk");

    json p = first_recv_params("calling.play");
    ASSERT_EQ(p["play"][0]["params"].value("name", ""), "uk");
    ASSERT_FALSE(p["play"][0]["params"].contains("duration"));
    client->disconnect();
    return true;
}

// ===========================================================================
// detect_digit — detect {type:"digit", params:{digits?}}
// ===========================================================================

TEST(relay_mock_detect_digit_journals_digit_detector) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-dd");
    ASSERT_TRUE(call != nullptr);

    call->detect_digit("123", 10.0);

    json p = first_recv_params("calling.detect");
    ASSERT_EQ(p["detect"].value("type", ""), "digit");
    ASSERT_EQ(p["detect"]["params"].value("digits", ""), "123");
    ASSERT_TRUE(p["timeout"].get<double>() == 10.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_detect_digit_omits_unset) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-dd-min");
    ASSERT_TRUE(call != nullptr);

    call->detect_digit();

    json p = first_recv_params("calling.detect");
    ASSERT_EQ(p["detect"].value("type", ""), "digit");
    ASSERT_FALSE(p["detect"]["params"].contains("digits"));
    ASSERT_FALSE(p.contains("timeout"));
    client->disconnect();
    return true;
}

TEST(relay_mock_detect_digit_resolves_on_detect_payload) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-dd-res");
    ASSERT_TRUE(call != nullptr);

    json events = json::array();
    json detect_payload;
    detect_payload["detect"]["type"] = "digit";
    detect_payload["detect"]["params"]["digits"] = "5";
    events.push_back({{"emit", detect_payload}, {"delay_ms", 1}});
    mt::arm_method("calling.detect", events);

    Action action = call->detect_digit("5");
    ASSERT_TRUE(action.wait(5000));
    ASSERT_TRUE(action.result().contains("detect"));
    ASSERT_EQ(action.result()["detect"].value("type", ""), "digit");
    client->disconnect();
    return true;
}

// ===========================================================================
// detect_answering_machine — detect {type:"machine", params:{...provided...}}
// ===========================================================================

TEST(relay_mock_detect_amd_journals_machine_detector) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-amd");
    ASSERT_TRUE(call != nullptr);

    json amd;
    amd["initial_timeout"] = 4.0;
    amd["machine_words_threshold"] = 6;
    amd["detect_interruptions"] = true;
    call->detect_answering_machine(amd, 30.0);

    json p = first_recv_params("calling.detect");
    ASSERT_EQ(p["detect"].value("type", ""), "machine");
    json params = p["detect"]["params"];
    ASSERT_TRUE(params["initial_timeout"].get<double>() == 4.0);
    ASSERT_EQ(params["machine_words_threshold"].get<int>(), 6);
    ASSERT_EQ(params.value("detect_interruptions", false), true);
    // Only-provided keys: an AMD knob we didn't pass must not appear.
    ASSERT_FALSE(params.contains("end_silence_timeout"));
    ASSERT_TRUE(p["timeout"].get<double>() == 30.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_detect_amd_empty_params_ok) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-amd-min");
    ASSERT_TRUE(call != nullptr);

    call->detect_answering_machine();

    json p = first_recv_params("calling.detect");
    ASSERT_EQ(p["detect"].value("type", ""), "machine");
    ASSERT_TRUE(p["detect"]["params"].is_object());
    ASSERT_TRUE(p["detect"]["params"].empty());
    ASSERT_FALSE(p.contains("timeout"));
    client->disconnect();
    return true;
}

// ===========================================================================
// detect_fax — detect {type:"fax", params:{tone?}}
// ===========================================================================

TEST(relay_mock_detect_fax_journals_fax_detector) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-fax");
    ASSERT_TRUE(call != nullptr);

    call->detect_fax("CED", 15.0);

    json p = first_recv_params("calling.detect");
    ASSERT_EQ(p["detect"].value("type", ""), "fax");
    ASSERT_EQ(p["detect"]["params"].value("tone", ""), "CED");
    ASSERT_TRUE(p["timeout"].get<double>() == 15.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_detect_fax_omits_tone_when_unset) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-fax-min");
    ASSERT_TRUE(call != nullptr);

    call->detect_fax();

    json p = first_recv_params("calling.detect");
    ASSERT_EQ(p["detect"].value("type", ""), "fax");
    ASSERT_FALSE(p["detect"]["params"].contains("tone"));
    client->disconnect();
    return true;
}

// ===========================================================================
// prompt_tts — play_and_collect tts media + collect
// ===========================================================================

TEST(relay_mock_prompt_tts_journals_pac_tts_media) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-ptts");
    ASSERT_TRUE(call != nullptr);

    json collect;
    collect["digits"]["max"] = 4;
    call->prompt_tts("Enter your PIN", collect, "en-US", "male", "josh");

    json p = first_recv_params("calling.play_and_collect");
    ASSERT_EQ(p["play"][0].value("type", ""), "tts");
    json params = p["play"][0]["params"];
    ASSERT_EQ(params.value("text", ""), "Enter your PIN");
    ASSERT_EQ(params.value("language", ""), "en-US");
    ASSERT_EQ(params.value("gender", ""), "male");
    ASSERT_EQ(params.value("voice", ""), "josh");
    ASSERT_EQ(p["collect"]["digits"]["max"].get<int>(), 4);
    client->disconnect();
    return true;
}

TEST(relay_mock_prompt_tts_with_volume_journals_volume) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-ptts-vol");
    ASSERT_TRUE(call != nullptr);

    json collect;
    collect["digits"]["max"] = 1;
    call->prompt_tts("Press 1", collect, "", "", "", 4.0);

    json p = first_recv_params("calling.play_and_collect");
    ASSERT_EQ(p["play"][0].value("type", ""), "tts");
    ASSERT_EQ(p["play"][0]["params"].value("text", ""), "Press 1");
    ASSERT_TRUE(p["volume"].get<double>() == 4.0);
    client->disconnect();
    return true;
}

TEST(relay_mock_prompt_tts_resolves_on_collect_only) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-ptts-res");
    ASSERT_TRUE(call != nullptr);

    json collect;
    collect["digits"]["max"] = 1;
    Action action = call->prompt_tts("Press 1", collect);

    // A play(finished) for this control_id must NOT resolve the prompt.
    mt::push(bare_event_conv("calling.call.play", {
        {"call_id", "conv-ptts-res"},
        {"control_id", action.control_id()},
        {"state", "finished"},
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_FALSE(action.completed());

    // A collect event resolves it.
    json collect_event;
    collect_event["call_id"] = "conv-ptts-res";
    collect_event["control_id"] = action.control_id();
    collect_event["result"]["type"] = "digit";
    collect_event["result"]["params"]["digits"] = "1";
    mt::push(bare_event_conv("calling.call.collect", collect_event));

    ASSERT_TRUE(action.wait(2000));
    ASSERT_EQ(action.result()["result"].value("type", ""), "digit");
    client->disconnect();
    return true;
}

// ===========================================================================
// prompt_audio — play_and_collect audio media + collect
// ===========================================================================

TEST(relay_mock_prompt_audio_journals_pac_audio_media) {
    auto client = mt::make_client();
    Call* call = setup_answered_call_conv(*client, "conv-paud");
    ASSERT_TRUE(call != nullptr);

    json collect;
    collect["speech"]["end_silence_timeout"] = 1.0;
    call->prompt_audio("https://cdn.example/menu.wav", collect, 1.5);

    json p = first_recv_params("calling.play_and_collect");
    ASSERT_EQ(p["play"][0].value("type", ""), "audio");
    ASSERT_EQ(p["play"][0]["params"].value("url", ""),
              "https://cdn.example/menu.wav");
    ASSERT_TRUE(p["collect"]["speech"]["end_silence_timeout"].get<double>() == 1.0);
    ASSERT_TRUE(p["volume"].get<double>() == 1.5);
    client->disconnect();
    return true;
}

// ===========================================================================
// wait_for_answered / wait_for_ringing / wait_for_ending
//   created < ringing < answered < ending < ended; immediate if at/past.
// ===========================================================================

TEST(relay_mock_wait_for_answered_short_circuits_when_past) {
    auto client = mt::make_client();
    // setup_answered_call_conv forces state to "answered".
    Call* call = setup_answered_call_conv(*client, "conv-wfa-now");
    ASSERT_TRUE(call != nullptr);

    // Already answered -> ringing wait returns immediately (past target),
    // answered wait returns immediately (at target).
    ASSERT_TRUE(call->wait_for_ringing(500));
    ASSERT_TRUE(call->wait_for_answered(500));
    client->disconnect();
    return true;
}

TEST(relay_mock_wait_for_answered_blocks_until_state_event) {
    auto client = mt::make_client();
    Call* call = mt::drive_inbound_call(*client, "conv-wfa-block", {"created"});
    ASSERT_TRUE(call != nullptr);
    // Call is at "created"; answered is in the future.

    std::atomic<bool> returned{false};
    std::atomic<bool> result{false};
    std::thread waiter([&] {
        result.store(call->wait_for_answered(4000));
        returned.store(true);
    });

    // Not satisfied yet.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_FALSE(returned.load());

    // Push a server-side state(answered) for this call. handle_call_state
    // routes it into Call::update_state, which notifies the state CV.
    mt::push(bare_event_conv("calling.call.state", {
        {"call_id", "conv-wfa-block"},
        {"node_id", call->node_id()},
        {"call_state", "answered"},
    }));

    ASSERT_TRUE(spin_conv([&] { return returned.load(); }, 4000));
    ASSERT_TRUE(result.load());
    waiter.join();
    client->disconnect();
    return true;
}

TEST(relay_mock_wait_for_ending_reached_by_ended_state) {
    auto client = mt::make_client();
    Call* call = mt::drive_inbound_call(*client, "conv-wfe", {"created"});
    ASSERT_TRUE(call != nullptr);

    std::atomic<bool> returned{false};
    std::atomic<bool> result{false};
    std::thread waiter([&] {
        // Waiting for "ending"; an "ended" state (rank past ending) also
        // satisfies it per the lifecycle ordering.
        result.store(call->wait_for_ending(4000));
        returned.store(true);
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ASSERT_FALSE(returned.load());

    mt::push(bare_event_conv("calling.call.state", {
        {"call_id", "conv-wfe"},
        {"node_id", call->node_id()},
        {"call_state", "ended"},
    }));

    ASSERT_TRUE(spin_conv([&] { return returned.load(); }, 4000));
    ASSERT_TRUE(result.load());
    waiter.join();
    client->disconnect();
    return true;
}

TEST(relay_mock_wait_for_ringing_times_out_when_unreached) {
    auto client = mt::make_client();
    Call* call = mt::drive_inbound_call(*client, "conv-wfr-to", {"created"});
    ASSERT_TRUE(call != nullptr);
    // Stay at "created"; ringing never arrives. A short wait must time out
    // and return false (not hang, not spuriously succeed).
    ASSERT_FALSE(call->wait_for_ringing(200));
    client->disconnect();
    return true;
}
