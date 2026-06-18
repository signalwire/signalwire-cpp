// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_phone_numbers.py.
//
// Covers CompatPhoneNumbers:
//   list, get, update, delete, purchase, import_number,
//   list_available_countries, search_toll_free
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// list
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_list_returns_paginated_list) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.list();
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("incoming_phone_numbers"));
    ASSERT_TRUE(result["incoming_phone_numbers"].is_array());
    return true;
}

TEST(rest_mock_compat_phone_numbers_list_journal_records_get) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.list();
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/IncomingPhoneNumbers"));
    return true;
}

// ---------------------------------------------------------------------------
// get
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_get_returns_phone_number_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.get("PN_TEST");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("phone_number") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_phone_numbers_get_journal_records_get_with_sid) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.get("PN_GET");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/IncomingPhoneNumbers/PN_GET"));
    return true;
}

// ---------------------------------------------------------------------------
// update
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_update_returns_phone_number_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.update("PN_U", {{"FriendlyName", "updated"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("phone_number") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_phone_numbers_update_journal_records_post_with_friendly_name) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.update(
        "PN_UU",
        {{"FriendlyName", "updated"}, {"VoiceUrl", "https://a.b/v"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/IncomingPhoneNumbers/PN_UU"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("updated"));
    ASSERT_EQ(j.body.value("VoiceUrl", std::string()), std::string("https://a.b/v"));
    return true;
}

// ---------------------------------------------------------------------------
// delete
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_delete_no_exception) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.delete_("PN_D");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_phone_numbers_delete_journal_records_delete_at_phone_number_path) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.delete_("PN_DEL");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/IncomingPhoneNumbers/PN_DEL"));
    return true;
}

// ---------------------------------------------------------------------------
// purchase
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_purchase_returns_purchased_number) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.purchase({{"PhoneNumber", "+15555550100"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("phone_number") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_phone_numbers_purchase_journal_records_post_with_phone_number) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.purchase(
        {{"PhoneNumber", "+15555550100"}, {"FriendlyName", "Main"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/IncomingPhoneNumbers"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("PhoneNumber", std::string()), std::string("+15555550100"));
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("Main"));
    return true;
}

// ---------------------------------------------------------------------------
// import_number
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_import_number_returns_imported_number) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.import_number(
        {{"PhoneNumber", "+15555550111"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("phone_number") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_phone_numbers_import_number_journal_records_post) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.import_number(
        {{"PhoneNumber", "+15555550111"}, {"VoiceUrl", "https://a.b/v"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/ImportedPhoneNumbers"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("PhoneNumber", std::string()), std::string("+15555550111"));
    return true;
}

// ---------------------------------------------------------------------------
// list_available_countries
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_list_available_countries_returns_collection) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.list_available_countries();
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("countries"));
    ASSERT_TRUE(result["countries"].is_array());
    return true;
}

TEST(rest_mock_compat_phone_numbers_list_available_countries_journal_records_get) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.list_available_countries();
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/AvailablePhoneNumbers"));
    return true;
}

// ---------------------------------------------------------------------------
// search_toll_free
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_phone_numbers_search_toll_free_returns_available_numbers) {
    auto client = mocktest::make_client();
    auto result = client.compat().phone_numbers.search_toll_free("US", {{"AreaCode", "800"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("available_phone_numbers"));
    ASSERT_TRUE(result["available_phone_numbers"].is_array());
    return true;
}

TEST(rest_mock_compat_phone_numbers_search_toll_free_journal_records_get_with_area_code) {
    auto client = mocktest::make_client();
    (void)client.compat().phone_numbers.search_toll_free("US", {{"AreaCode", "888"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/laml/2010-04-01/Accounts/" + mocktest::active_project() + "/AvailablePhoneNumbers/US/TollFree"));
    auto it = j.query_params.find("AreaCode");
    ASSERT_TRUE(it != j.query_params.end());
    ASSERT_TRUE(!it->second.empty());
    ASSERT_EQ(it->second.front(), std::string("888"));
    return true;
}
