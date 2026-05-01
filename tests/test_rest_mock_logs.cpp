// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_logs_mock.py.
//
// Logs sub-resources fan across four spec docs (message/voice/fax/logs).
// Each test asserts on body shape and on mocktest::journal_last().
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// Message Logs - /api/messaging/logs
// ---------------------------------------------------------------------------

TEST(rest_mock_logs_messages_list_returns_dict) {
    auto client = mocktest::make_client();
    auto body = client.logs().messages.list();
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/messaging/logs"));
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(*j.matched_route, std::string("message.list_message_logs"));
    return true;
}

TEST(rest_mock_logs_messages_get_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.logs().messages.get("ml-42");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/messaging/logs/ml-42"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// Voice Logs - /api/voice/logs
// ---------------------------------------------------------------------------

TEST(rest_mock_logs_voice_list_returns_dict) {
    auto client = mocktest::make_client();
    auto body = client.logs().voice.list();
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/voice/logs"));
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(*j.matched_route, std::string("voice.list_voice_logs"));
    return true;
}

TEST(rest_mock_logs_voice_get_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.logs().voice.get("vl-99");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/voice/logs/vl-99"));
    return true;
}

// ---------------------------------------------------------------------------
// Fax Logs - /api/fax/logs
// ---------------------------------------------------------------------------

TEST(rest_mock_logs_fax_list_returns_dict) {
    auto client = mocktest::make_client();
    auto body = client.logs().fax.list();
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fax/logs"));
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(*j.matched_route, std::string("fax.list_fax_logs"));
    return true;
}

TEST(rest_mock_logs_fax_get_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.logs().fax.get("fl-7");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fax/logs/fl-7"));
    return true;
}

// ---------------------------------------------------------------------------
// Conference Logs - /api/logs/conferences
// ---------------------------------------------------------------------------

TEST(rest_mock_logs_conferences_list_returns_dict) {
    auto client = mocktest::make_client();
    auto body = client.logs().conferences.list();
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/logs/conferences"));
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(*j.matched_route, std::string("logs.list_conferences"));
    return true;
}
