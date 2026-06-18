// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_messages_faxes.py.
//
// Covers CompatMessages (update, get_media, delete_media) and CompatFaxes
// (update, list_media, get_media, delete_media).
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// CompatMessages
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_messages_update_returns_message_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().messages.update("MM_TEST", {{"Body", "updated body"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("body") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_messages_update_journal_records_post) {
    auto client = mocktest::make_client();
    (void)client.compat().messages.update("MM_U1", {{"Body", "x"}, {"Status", "canceled"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Messages/MM_U1"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Body", std::string()), std::string("x"));
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("canceled"));
    return true;
}

TEST(rest_mock_compat_messages_get_media_returns_media_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().messages.get_media("MM_GM", "ME_GM");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("content_type") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_messages_get_media_journal_records_get) {
    auto client = mocktest::make_client();
    (void)client.compat().messages.get_media("MM_X", "ME_X");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Messages/MM_X/Media/ME_X"));
    return true;
}

TEST(rest_mock_compat_messages_delete_media_no_exception) {
    auto client = mocktest::make_client();
    auto result = client.compat().messages.delete_media("MM_DM", "ME_DM");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_messages_delete_media_journal_records_delete) {
    auto client = mocktest::make_client();
    (void)client.compat().messages.delete_media("MM_D", "ME_D");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Messages/MM_D/Media/ME_D"));
    return true;
}

// ---------------------------------------------------------------------------
// CompatFaxes
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_faxes_update_returns_fax_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().faxes.update("FX_U", {{"Status", "canceled"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("status") || result.contains("direction"));
    return true;
}

TEST(rest_mock_compat_faxes_update_journal_records_post) {
    auto client = mocktest::make_client();
    (void)client.compat().faxes.update("FX_U2", {{"Status", "canceled"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Faxes/FX_U2"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Status", std::string()), std::string("canceled"));
    return true;
}

TEST(rest_mock_compat_faxes_list_media_returns_paginated_list) {
    auto client = mocktest::make_client();
    auto result = client.compat().faxes.list_media("FX_LM");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("media") || result.contains("fax_media"));
    return true;
}

TEST(rest_mock_compat_faxes_list_media_journal_records_get) {
    auto client = mocktest::make_client();
    (void)client.compat().faxes.list_media("FX_LM_X");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Faxes/FX_LM_X/Media"));
    return true;
}

TEST(rest_mock_compat_faxes_get_media_returns_fax_media_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().faxes.get_media("FX_GM", "ME_GM");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("content_type") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_faxes_get_media_journal_records_get) {
    auto client = mocktest::make_client();
    (void)client.compat().faxes.get_media("FX_G", "ME_G");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Faxes/FX_G/Media/ME_G"));
    return true;
}

TEST(rest_mock_compat_faxes_delete_media_no_exception) {
    auto client = mocktest::make_client();
    auto result = client.compat().faxes.delete_media("FX_DM", "ME_DM");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_faxes_delete_media_journal_records_delete) {
    auto client = mocktest::make_client();
    (void)client.compat().faxes.delete_media("FX_D", "ME_D");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/Faxes/FX_D/Media/ME_D"));
    return true;
}
