// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_recordings_transcriptions.py.
//
// Both resources expose the same surface (list / get / delete) and use the
// account-scoped LAML path. Twelve tests total split across two resources.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kRecBase   = "/api/laml/2010-04-01/Accounts/test_proj/Recordings";
const std::string kTransBase = "/api/laml/2010-04-01/Accounts/test_proj/Transcriptions";
}

// ---------------------------------------------------------------------------
// CompatRecordings.list / get / delete
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_recordings_list_returns_paginated) {
    auto client = mocktest::make_client();
    auto result = client.compat().recordings.list();
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("recordings"));
    ASSERT_TRUE(result["recordings"].is_array());
    return true;
}

TEST(rest_mock_compat_recordings_list_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().recordings.list();
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRecBase);
    return true;
}

TEST(rest_mock_compat_recordings_get_returns_recording_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().recordings.get("RE_TEST");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("call_sid"));
    return true;
}

TEST(rest_mock_compat_recordings_get_journal_records_get_with_sid) {
    auto client = mocktest::make_client();
    client.compat().recordings.get("RE_GET");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRecBase + "/RE_GET");
    return true;
}

TEST(rest_mock_compat_recordings_delete_returns_dict) {
    auto client = mocktest::make_client();
    auto result = client.compat().recordings.delete_("RE_D");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_recordings_delete_journal_records_delete) {
    auto client = mocktest::make_client();
    client.compat().recordings.delete_("RE_DEL");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, kRecBase + "/RE_DEL");
    return true;
}

// ---------------------------------------------------------------------------
// CompatTranscriptions.list / get / delete
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_transcriptions_list_returns_paginated) {
    auto client = mocktest::make_client();
    auto result = client.compat().transcriptions.list();
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("transcriptions"));
    ASSERT_TRUE(result["transcriptions"].is_array());
    return true;
}

TEST(rest_mock_compat_transcriptions_list_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().transcriptions.list();
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kTransBase);
    return true;
}

TEST(rest_mock_compat_transcriptions_get_returns_transcription_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().transcriptions.get("TR_TEST");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("sid") || result.contains("duration"));
    return true;
}

TEST(rest_mock_compat_transcriptions_get_journal_records_get_with_sid) {
    auto client = mocktest::make_client();
    client.compat().transcriptions.get("TR_GET");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kTransBase + "/TR_GET");
    return true;
}

TEST(rest_mock_compat_transcriptions_delete_returns_dict) {
    auto client = mocktest::make_client();
    auto result = client.compat().transcriptions.delete_("TR_D");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_transcriptions_delete_journal_records_delete) {
    auto client = mocktest::make_client();
    client.compat().transcriptions.delete_("TR_DEL");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, kTransBase + "/TR_DEL");
    return true;
}
