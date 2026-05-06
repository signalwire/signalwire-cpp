// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_calling_mock.py.
//
// Every command in CallingNamespace is exercised against the local
// mock_signalwire server. Each test:
//
//   1. Calls the SDK method (no transport patching).
//   2. Asserts on the response body shape.
//   3. Asserts on mocktest::journal_last() so we know the SDK sent the
//      right wire request -- method, path, command field, id, params.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kCallsPath = "/api/calling/calls";
}

// ---------------------------------------------------------------------------
// Lifecycle: dial / update / transfer / disconnect
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_dial_forwards_codecs_array) {
    // OpenAPI spec for calling/calls dial gained an optional codecs param
    // (porting-sdk PR #1). dial(json) forwards arbitrary fields, so codecs
    // flows through without source changes; this test confirms the array
    // form reaches the wire.
    auto client = mocktest::make_client();
    auto body = client.calling().dial({
        {"url", "https://example.com/swml"},
        {"to", "+15551234567"},
        {"codecs", json::array({"OPUS", "G729", "VP8", "PCMA"})},
    });
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kCallsPath);
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(j.body.value("command", std::string()), std::string("dial"));
    ASSERT_FALSE(j.body.contains("id"));
    auto& params = j.body.at("params");
    ASSERT_EQ(params.value("to", std::string()), std::string("+15551234567"));
    ASSERT_TRUE(params.contains("codecs"));
    ASSERT_TRUE(params.at("codecs").is_array());
    ASSERT_EQ(params.at("codecs").size(), (size_t)4);
    ASSERT_EQ(params.at("codecs")[0].get<std::string>(), std::string("OPUS"));
    ASSERT_EQ(params.at("codecs")[1].get<std::string>(), std::string("G729"));
    ASSERT_EQ(params.at("codecs")[2].get<std::string>(), std::string("VP8"));
    ASSERT_EQ(params.at("codecs")[3].get<std::string>(), std::string("PCMA"));
    return true;
}

TEST(rest_mock_calling_dial_forwards_codecs_string) {
    // Same as above but with the comma-separated-string shape, which the
    // OpenAPI spec also accepts.
    auto client = mocktest::make_client();
    auto body = client.calling().dial({
        {"url", "https://example.com/swml"},
        {"to", "+15551234567"},
        {"codecs", "OPUS,G729,VP8,PCMA"},
    });
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("dial"));
    ASSERT_EQ(j.body.at("params").value("codecs", std::string()),
              std::string("OPUS,G729,VP8,PCMA"));
    return true;
}

TEST(rest_mock_calling_update) {
    auto client = mocktest::make_client();
    auto body = client.calling().update({{"id", "call-1"}, {"state", "hold"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kCallsPath);
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(j.body.value("command", std::string()), std::string("update"));
    ASSERT_FALSE(j.body.contains("id"));
    auto& params = j.body.at("params");
    ASSERT_EQ(params.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(params.value("state", std::string()), std::string("hold"));
    return true;
}

TEST(rest_mock_calling_transfer) {
    auto client = mocktest::make_client();
    auto body = client.calling().transfer(
        "call-123",
        {{"destination", "+15551234567"}, {"from_number", "+15559876543"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kCallsPath);
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.transfer"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-123"));
    ASSERT_EQ(j.body.at("params").value("destination", std::string()),
              std::string("+15551234567"));
    ASSERT_EQ(j.body.at("params").value("from_number", std::string()),
              std::string("+15559876543"));
    return true;
}

TEST(rest_mock_calling_disconnect) {
    auto client = mocktest::make_client();
    auto body = client.calling().disconnect("call-456", {{"reason", "busy"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kCallsPath);
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.disconnect"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-456"));
    ASSERT_EQ(j.body.at("params").value("reason", std::string()), std::string("busy"));
    return true;
}

// ---------------------------------------------------------------------------
// Play
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_play_pause) {
    auto client = mocktest::make_client();
    auto body = client.calling().play_pause("call-1", {{"control_id", "ctrl-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.path, kCallsPath);
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.play.pause"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("ctrl-1"));
    return true;
}

TEST(rest_mock_calling_play_resume) {
    auto client = mocktest::make_client();
    auto body = client.calling().play_resume("call-1", {{"control_id", "ctrl-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.play.resume"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("ctrl-1"));
    return true;
}

TEST(rest_mock_calling_play_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().play_stop("call-1", {{"control_id", "ctrl-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.play.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

TEST(rest_mock_calling_play_volume) {
    auto client = mocktest::make_client();
    auto body = client.calling().play_volume(
        "call-1", {{"control_id", "ctrl-1"}, {"volume", 2.5}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.play.volume"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("volume", 0.0), 2.5);
    return true;
}

// ---------------------------------------------------------------------------
// Record
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_record) {
    auto client = mocktest::make_client();
    auto body = client.calling().record(
        "call-1", {{"record", json::object({{"format", "mp3"}})}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.record"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    auto& rec = j.body.at("params").at("record");
    ASSERT_EQ(rec.value("format", std::string()), std::string("mp3"));
    return true;
}

TEST(rest_mock_calling_record_pause) {
    auto client = mocktest::make_client();
    auto body = client.calling().record_pause("call-1", {{"control_id", "rec-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.record.pause"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("rec-1"));
    return true;
}

TEST(rest_mock_calling_record_resume) {
    auto client = mocktest::make_client();
    auto body = client.calling().record_resume("call-1", {{"control_id", "rec-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.record.resume"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("rec-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Collect
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_collect) {
    auto client = mocktest::make_client();
    auto body = client.calling().collect(
        "call-1",
        {{"initial_timeout", 5}, {"digits", json::object({{"max", 4}})}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.collect"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("initial_timeout", 0), 5);
    return true;
}

TEST(rest_mock_calling_collect_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().collect_stop("call-1", {{"control_id", "col-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.collect.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("col-1"));
    return true;
}

TEST(rest_mock_calling_collect_start_input_timers) {
    auto client = mocktest::make_client();
    auto body = client.calling().collect_start_input_timers(
        "call-1", {{"control_id", "col-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()),
              std::string("calling.collect.start_input_timers"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("col-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Detect
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_detect) {
    auto client = mocktest::make_client();
    auto body = client.calling().detect(
        "call-1",
        {{"detect", json::object({{"type", "machine"}, {"params", json::object()}})}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.detect"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").at("detect").value("type", std::string()),
              std::string("machine"));
    return true;
}

TEST(rest_mock_calling_detect_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().detect_stop("call-1", {{"control_id", "det-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.detect.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("det-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Tap
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_tap) {
    auto client = mocktest::make_client();
    auto body = client.calling().tap(
        "call-1",
        {{"tap", json::object({{"type", "audio"}})},
         {"device", json::object({{"type", "rtp"}})}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.tap"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").at("tap").value("type", std::string()),
              std::string("audio"));
    return true;
}

TEST(rest_mock_calling_tap_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().tap_stop("call-1", {{"control_id", "tap-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.tap.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("tap-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Stream
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_stream) {
    auto client = mocktest::make_client();
    auto body = client.calling().stream("call-1", {{"url", "wss://example.com/audio"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.stream"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("url", std::string()),
              std::string("wss://example.com/audio"));
    return true;
}

TEST(rest_mock_calling_stream_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().stream_stop("call-1", {{"control_id", "stream-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.stream.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("stream-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Denoise
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_denoise) {
    auto client = mocktest::make_client();
    auto body = client.calling().denoise("call-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.denoise"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

TEST(rest_mock_calling_denoise_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().denoise_stop("call-1", {{"control_id", "dn-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.denoise.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("dn-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Transcribe
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_transcribe) {
    auto client = mocktest::make_client();
    auto body = client.calling().transcribe(
        "call-1",
        {{"language", "en-US"},
         {"transcribe", json::object({{"engine", "google"}})}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.transcribe"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("language", std::string()),
              std::string("en-US"));
    return true;
}

TEST(rest_mock_calling_transcribe_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().transcribe_stop("call-1", {{"control_id", "tr-1"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()),
              std::string("calling.transcribe.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("control_id", std::string()),
              std::string("tr-1"));
    return true;
}

// ---------------------------------------------------------------------------
// AI commands
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_ai_hold) {
    auto client = mocktest::make_client();
    auto body = client.calling().ai_hold("call-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.ai_hold"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

TEST(rest_mock_calling_ai_unhold) {
    auto client = mocktest::make_client();
    auto body = client.calling().ai_unhold("call-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.ai_unhold"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

TEST(rest_mock_calling_ai_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().ai_stop("call-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.ai.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

// ---------------------------------------------------------------------------
// Live transcribe / translate
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_live_transcribe) {
    auto client = mocktest::make_client();
    auto body = client.calling().live_transcribe("call-1", {{"language", "en-US"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()),
              std::string("calling.live_transcribe"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("language", std::string()),
              std::string("en-US"));
    return true;
}

TEST(rest_mock_calling_live_translate) {
    auto client = mocktest::make_client();
    auto body = client.calling().live_translate(
        "call-1", {{"source_language", "en"}, {"target_language", "es"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()),
              std::string("calling.live_translate"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("source_language", std::string()),
              std::string("en"));
    ASSERT_EQ(j.body.at("params").value("target_language", std::string()),
              std::string("es"));
    return true;
}

// ---------------------------------------------------------------------------
// Fax
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_send_fax_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().send_fax_stop("call-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()),
              std::string("calling.send_fax.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

TEST(rest_mock_calling_receive_fax_stop) {
    auto client = mocktest::make_client();
    auto body = client.calling().receive_fax_stop("call-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()),
              std::string("calling.receive_fax.stop"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    return true;
}

// ---------------------------------------------------------------------------
// SIP refer + custom user_event
// ---------------------------------------------------------------------------

TEST(rest_mock_calling_refer) {
    auto client = mocktest::make_client();
    auto body = client.calling().refer("call-1", {{"to", "sip:other@example.com"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.refer"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("to", std::string()),
              std::string("sip:other@example.com"));
    return true;
}

TEST(rest_mock_calling_user_event) {
    auto client = mocktest::make_client();
    auto body = client.calling().user_event(
        "call-1",
        {{"event_name", "my-event"},
         {"payload", json::object({{"foo", "bar"}})}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.body.value("command", std::string()), std::string("calling.user_event"));
    ASSERT_EQ(j.body.value("id", std::string()), std::string("call-1"));
    ASSERT_EQ(j.body.at("params").value("event_name", std::string()),
              std::string("my-event"));
    ASSERT_EQ(j.body.at("params").at("payload").value("foo", std::string()),
              std::string("bar"));
    return true;
}
