// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_calls_streams.py.
//
// Each subtest mirrors one Python TestCase method and asserts on both the
// SDK response shape and the wire request the mock journaled.
//
// The TEST() macro is provided by tests/test_main.cpp -- this file is
// included from there, not compiled separately.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// CompatCalls::start_stream -> POST /Calls/{sid}/Streams
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_calls_start_stream_returns_stream_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().calls.start_stream(
        "CA_TEST",
        {{"Url", "wss://example.com/stream"}, {"Name", "my-stream"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("name"));
    return true;
}

TEST(rest_mock_compat_calls_start_stream_journal_records_post) {
    auto client = mocktest::make_client();
    (void)client.compat().calls.start_stream(
        "CA_JX1",
        {{"Url", "wss://a.b/s"}, {"Name", "strm-x"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/test_proj/Calls/CA_JX1/Streams"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Url", std::string()), std::string("wss://a.b/s"));
    ASSERT_EQ(j.body.value("Name", std::string()), std::string("strm-x"));
    return true;
}

// ---------------------------------------------------------------------------
// CompatCalls::stop_stream -> POST .../Streams/{stream_sid}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_calls_stop_stream_returns_stream_resource_with_status) {
    auto client = mocktest::make_client();
    auto result = client.compat().calls.stop_stream(
        "CA_T1", "ST_T1", {{"Status", "stopped"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("status"));
    return true;
}

TEST(rest_mock_compat_calls_stop_stream_journal_records_post_to_specific_stream) {
    auto client = mocktest::make_client();
    (void)client.compat().calls.stop_stream("CA_S1", "ST_S1", {{"Status", "stopped"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/test_proj/Calls/CA_S1/Streams/ST_S1"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("stopped"));
    return true;
}

// ---------------------------------------------------------------------------
// CompatCalls::update_recording -> POST .../Calls/{sid}/Recordings/{rec_sid}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_calls_update_recording_returns_recording_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().calls.update_recording(
        "CA_T2", "RE_T2", {{"Status", "paused"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("status"));
    return true;
}

TEST(rest_mock_compat_calls_update_recording_journal_records_post_to_specific_recording) {
    auto client = mocktest::make_client();
    (void)client.compat().calls.update_recording("CA_R1", "RE_R1", {{"Status", "paused"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/test_proj/Calls/CA_R1/Recordings/RE_R1"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("paused"));
    return true;
}
