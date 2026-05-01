// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_conferences.py.
//
// Covers all 12 uncovered Conference symbols: list/get/update on the
// conference itself, plus the participant, recording, and stream
// sub-resources.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kConfBase = "/api/laml/2010-04-01/Accounts/test_proj/Conferences";
}

// ---------------------------------------------------------------------------
// Conference itself
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_conferences_list_returns_paginated_list) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.list();
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("conferences"));
    ASSERT_TRUE(result["conferences"].is_array());
    ASSERT_TRUE(result.contains("page"));
    ASSERT_TRUE(result["page"].is_number_integer());
    return true;
}

TEST(rest_mock_compat_conferences_list_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().conferences.list();
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kConfBase);
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_compat_conferences_get_returns_conference_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.get("CF_TEST");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("friendly_name") || result.contains("status"));
    return true;
}

TEST(rest_mock_compat_conferences_get_journal_records_get_with_sid) {
    auto client = mocktest::make_client();
    client.compat().conferences.get("CF_GETSID");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kConfBase + "/CF_GETSID");
    return true;
}

TEST(rest_mock_compat_conferences_update_returns_updated_conference) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.update(
        "CF_X", {{"Status", "completed"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("friendly_name") || result.contains("status"));
    return true;
}

TEST(rest_mock_compat_conferences_update_journal_records_post_with_status) {
    auto client = mocktest::make_client();
    client.compat().conferences.update(
        "CF_UPD",
        {{"Status", "completed"}, {"AnnounceUrl", "https://a.b"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kConfBase + "/CF_UPD");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("completed"));
    ASSERT_EQ(j.body.value("AnnounceUrl", std::string()), std::string("https://a.b"));
    return true;
}

// ---------------------------------------------------------------------------
// Participants
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_conferences_get_participant_returns_participant) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.get_participant("CF_P", "CA_P");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("call_sid") || result.contains("conference_sid"));
    return true;
}

TEST(rest_mock_compat_conferences_get_participant_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().conferences.get_participant("CF_GP", "CA_GP");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kConfBase + "/CF_GP/Participants/CA_GP");
    return true;
}

TEST(rest_mock_compat_conferences_update_participant_returns_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.update_participant(
        "CF_UP", "CA_UP", {{"Muted", true}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("call_sid") || result.contains("conference_sid"));
    return true;
}

TEST(rest_mock_compat_conferences_update_participant_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().conferences.update_participant(
        "CF_M", "CA_M",
        {{"Muted", true}, {"Hold", false}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kConfBase + "/CF_M/Participants/CA_M");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Muted", false), true);
    ASSERT_EQ(j.body.value("Hold", true), false);
    return true;
}

TEST(rest_mock_compat_conferences_remove_participant_returns_dict) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.remove_participant("CF_R", "CA_R");
    // 204-style deletes return {} from the SDK. Either way we get a dict.
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_conferences_remove_participant_journal_records_delete) {
    auto client = mocktest::make_client();
    client.compat().conferences.remove_participant("CF_RM", "CA_RM");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, kConfBase + "/CF_RM/Participants/CA_RM");
    return true;
}

// ---------------------------------------------------------------------------
// Recordings
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_conferences_list_recordings_returns_paginated) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.list_recordings("CF_LR");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("recordings"));
    ASSERT_TRUE(result["recordings"].is_array());
    return true;
}

TEST(rest_mock_compat_conferences_list_recordings_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().conferences.list_recordings("CF_LRX");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kConfBase + "/CF_LRX/Recordings");
    return true;
}

TEST(rest_mock_compat_conferences_get_recording_returns_recording_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.get_recording("CF_GR", "RE_GR");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("call_sid"));
    return true;
}

TEST(rest_mock_compat_conferences_get_recording_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().conferences.get_recording("CF_GRX", "RE_GRX");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kConfBase + "/CF_GRX/Recordings/RE_GRX");
    return true;
}

TEST(rest_mock_compat_conferences_update_recording_returns_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.update_recording(
        "CF_URC", "RE_URC", {{"Status", "paused"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("status"));
    return true;
}

TEST(rest_mock_compat_conferences_update_recording_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().conferences.update_recording(
        "CF_UR", "RE_UR", {{"Status", "paused"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kConfBase + "/CF_UR/Recordings/RE_UR");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("paused"));
    return true;
}

TEST(rest_mock_compat_conferences_delete_recording_returns_dict) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.delete_recording("CF_DR", "RE_DR");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_conferences_delete_recording_journal_records_delete) {
    auto client = mocktest::make_client();
    client.compat().conferences.delete_recording("CF_DRX", "RE_DRX");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, kConfBase + "/CF_DRX/Recordings/RE_DRX");
    return true;
}

// ---------------------------------------------------------------------------
// Streams
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_conferences_start_stream_returns_stream_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.start_stream(
        "CF_SS", {{"Url", "wss://a.b/s"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("name"));
    return true;
}

TEST(rest_mock_compat_conferences_start_stream_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().conferences.start_stream(
        "CF_SSX", {{"Url", "wss://a.b/s"}, {"Name", "strm"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kConfBase + "/CF_SSX/Streams");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Url", std::string()), std::string("wss://a.b/s"));
    return true;
}

TEST(rest_mock_compat_conferences_stop_stream_returns_stream_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().conferences.stop_stream(
        "CF_TS", "ST_TS", {{"Status", "stopped"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("status"));
    return true;
}

TEST(rest_mock_compat_conferences_stop_stream_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().conferences.stop_stream(
        "CF_TSX", "ST_TSX", {{"Status", "stopped"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kConfBase + "/CF_TSX/Streams/ST_TSX");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("stopped"));
    return true;
}
