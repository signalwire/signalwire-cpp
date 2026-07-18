// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_small_namespaces_mock.py.
//
// Covers the small REST namespaces:
//   - addresses (list/create/get/delete)
//   - recordings (list/get/delete)
//   - short_codes (list/get/update)
//   - imported_numbers (create)
//   - mfa (call)
//   - sip_profile (update)
//   - number_groups (list_memberships/delete_membership)
//   - project.tokens (update/delete)
//   - datasphere.documents (get_chunk)
//   - queues (get_member)
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// Addresses
// ---------------------------------------------------------------------------

TEST(rest_mock_addresses_list) {
    auto client = mocktest::make_client();
    auto body = client.addresses().list({{"page_size", "10"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/addresses"));
    ASSERT_TRUE(j.matched_route.has_value());
    auto it = j.query_params.find("page_size");
    ASSERT_TRUE(it != j.query_params.end());
    ASSERT_TRUE(!it->second.empty());
    ASSERT_EQ(it->second.front(), std::string("10"));
    return true;
}

TEST(rest_mock_addresses_create) {
    auto client = mocktest::make_client();
    auto body = client.addresses().create({
        .country = "US",
        .first_name = "Ada",
        .last_name = "Lovelace",
        .address_type = "commercial",
    });
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/addresses"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("address_type", std::string()), std::string("commercial"));
    ASSERT_EQ(j.body.value("first_name", std::string()), std::string("Ada"));
    ASSERT_EQ(j.body.value("country", std::string()), std::string("US"));
    return true;
}

TEST(rest_mock_addresses_get) {
    auto client = mocktest::make_client();
    auto body = client.addresses().get("addr-123");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/addresses/addr-123"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_addresses_delete) {
    auto client = mocktest::make_client();
    auto body = client.addresses().delete_("addr-123");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/addresses/addr-123"));
    ASSERT_TRUE(j.response_status.has_value());
    int s = *j.response_status;
    ASSERT_TRUE(s == 200 || s == 202 || s == 204);
    return true;
}

// ---------------------------------------------------------------------------
// Recordings
// ---------------------------------------------------------------------------

TEST(rest_mock_recordings_list) {
    auto client = mocktest::make_client();
    auto body = client.recordings().list({{"page_size", "5"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/recordings"));
    auto it = j.query_params.find("page_size");
    ASSERT_TRUE(it != j.query_params.end());
    ASSERT_TRUE(!it->second.empty());
    ASSERT_EQ(it->second.front(), std::string("5"));
    return true;
}

TEST(rest_mock_recordings_get) {
    auto client = mocktest::make_client();
    auto body = client.recordings().get("rec-123");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/recordings/rec-123"));
    return true;
}

TEST(rest_mock_recordings_delete) {
    auto client = mocktest::make_client();
    auto body = client.recordings().delete_("rec-123");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/recordings/rec-123"));
    ASSERT_TRUE(j.response_status.has_value());
    int s = *j.response_status;
    ASSERT_TRUE(s == 200 || s == 202 || s == 204);
    return true;
}

// ---------------------------------------------------------------------------
// Short Codes
// ---------------------------------------------------------------------------

TEST(rest_mock_short_codes_list) {
    auto client = mocktest::make_client();
    auto body = client.short_codes().list({{"page_size", "20"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/short_codes"));
    return true;
}

TEST(rest_mock_short_codes_get) {
    auto client = mocktest::make_client();
    auto body = client.short_codes().get("sc-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/short_codes/sc-1"));
    return true;
}

TEST(rest_mock_short_codes_update) {
    auto client = mocktest::make_client();
    auto body = client.short_codes().update("sc-1", {.name = "Marketing SMS"});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("PUT"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/short_codes/sc-1"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("name", std::string()), std::string("Marketing SMS"));
    return true;
}

// ---------------------------------------------------------------------------
// Imported Numbers
// ---------------------------------------------------------------------------

TEST(wire_regression_pin_imported_numbers_create_extras) {
    auto client = mocktest::make_client();
    auto body = client.imported_numbers().create({
        .number = "+15551234567",
        .extras = {
            {"sip_username", "alice"},
            {"sip_password", "secret"},
            {"sip_proxy", "sip.example.com"},
        },
    });
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/imported_phone_numbers"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("number", std::string()), std::string("+15551234567"));
    ASSERT_EQ(j.body.value("sip_username", std::string()), std::string("alice"));
    ASSERT_EQ(j.body.value("sip_proxy", std::string()), std::string("sip.example.com"));
    return true;
}

// ---------------------------------------------------------------------------
// MFA
// ---------------------------------------------------------------------------

TEST(rest_mock_mfa_call) {
    auto client = mocktest::make_client();
    // Wire key is "from" -- CallParams already types this field; C++ has no
    // reserved-word conflict with "from", so no "from_" escape is needed here.
    auto body = client.mfa().call({
        .to = "+15551234567",
        .from = std::string("+15559876543"),
        .message = "Your code is {code}",
    });
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/mfa/call"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("to", std::string()), std::string("+15551234567"));
    ASSERT_EQ(j.body.value("from", std::string()), std::string("+15559876543"));
    ASSERT_EQ(j.body.value("message", std::string()),
              std::string("Your code is {code}"));
    return true;
}

// ---------------------------------------------------------------------------
// SIP Profile
// ---------------------------------------------------------------------------

TEST(rest_mock_sip_profile_update) {
    auto client = mocktest::make_client();
    // Wire key is domain_identifier (UpdateSipProfileRequest), not a bare
    // "domain" -- SipProfile::UpdateParams already types this field.
    auto body = client.sip_profile().update({
        .domain_identifier = std::string("myco.sip.signalwire.com"),
        .default_codecs = json::array({"PCMU", "PCMA"}),
    });
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("domain_identifier") || body.contains("default_codecs"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("PUT"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/sip_profile"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("domain_identifier", std::string()),
              std::string("myco.sip.signalwire.com"));
    ASSERT_TRUE(j.body.contains("default_codecs"));
    ASSERT_TRUE(j.body["default_codecs"].is_array());
    return true;
}

// ---------------------------------------------------------------------------
// Number Groups
// ---------------------------------------------------------------------------

TEST(rest_mock_number_groups_list_memberships) {
    auto client = mocktest::make_client();
    auto body = client.number_groups().list_memberships("ng-1", {{"page_size", "10"}});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/relay/rest/number_groups/ng-1/number_group_memberships"));
    auto it = j.query_params.find("page_size");
    ASSERT_TRUE(it != j.query_params.end());
    ASSERT_TRUE(!it->second.empty());
    ASSERT_EQ(it->second.front(), std::string("10"));
    return true;
}

TEST(rest_mock_number_groups_delete_membership) {
    auto client = mocktest::make_client();
    auto body = client.number_groups().delete_membership("mem-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/number_group_memberships/mem-1"));
    ASSERT_TRUE(j.response_status.has_value());
    int s = *j.response_status;
    ASSERT_TRUE(s == 200 || s == 202 || s == 204);
    return true;
}

// ---------------------------------------------------------------------------
// Project tokens
// ---------------------------------------------------------------------------

TEST(rest_mock_project_tokens_update) {
    auto client = mocktest::make_client();
    auto body = client.project().tokens.update("tok-1", {.name = "renamed-token"});
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("PATCH"));
    ASSERT_EQ(j.path, std::string("/api/project/tokens/tok-1"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("name", std::string()), std::string("renamed-token"));
    return true;
}

TEST(rest_mock_project_tokens_delete) {
    auto client = mocktest::make_client();
    auto body = client.project().tokens.delete_("tok-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/project/tokens/tok-1"));
    ASSERT_TRUE(j.response_status.has_value());
    int s = *j.response_status;
    ASSERT_TRUE(s == 200 || s == 202 || s == 204);
    return true;
}

// ---------------------------------------------------------------------------
// Datasphere
// ---------------------------------------------------------------------------

TEST(rest_mock_datasphere_get_chunk) {
    auto client = mocktest::make_client();
    auto body = client.datasphere().documents.get_chunk("doc-1", "chunk-99");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path,
              std::string("/api/datasphere/documents/doc-1/chunks/chunk-99"));
    return true;
}

// ---------------------------------------------------------------------------
// Queues
// ---------------------------------------------------------------------------

TEST(rest_mock_queues_get_member) {
    auto client = mocktest::make_client();
    auto body = client.queues().get_member("q-1", "mem-7");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("queue_id") || body.contains("call_id"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/relay/rest/queues/q-1/members/mem-7"));
    return true;
}
