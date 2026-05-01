// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_fabric_mock.py.
//
// Closes audit gaps for: addresses, generic resources operations, SIP-endpoint
// sub-resources on subscribers, the call-flows / conference-rooms addresses
// sub-paths (which use SINGULAR ``call_flow`` / ``conference_room``), the
// full FabricTokens surface, and the CxmlApplicationsResource.create
// deliberate-failure path.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}

// ---------------------------------------------------------------------------
// Fabric Addresses (read-only top-level resource)
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_addresses_list_returns_data_collection) {
    auto client = mocktest::make_client();
    auto body = client.fabric().addresses.list();
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fabric/addresses"));
    ASSERT_TRUE(j.matched_route.has_value());
    ASSERT_EQ(*j.matched_route, std::string("fabric.list_fabric_addresses"));
    return true;
}

TEST(rest_mock_fabric_addresses_get_uses_address_id) {
    auto client = mocktest::make_client();
    auto body = client.fabric().addresses.get("addr-9001");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fabric/addresses/addr-9001"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// CxmlApplicationsResource.create -- deliberate refusal
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_cxml_applications_create_raises_not_implemented) {
    auto client = mocktest::make_client();
    bool threw = false;
    std::string what;
    try {
        client.fabric().cxml_applications.create({{"name", "never_built"}});
    } catch (const std::exception& e) {
        threw = true;
        what = e.what();
    }
    ASSERT_TRUE(threw);
    // Match Python's ``cXML applications cannot`` substring.
    ASSERT_TRUE(what.find("cXML applications cannot") != std::string::npos);
    // Nothing should have hit the wire.
    auto entries = mocktest::journal();
    ASSERT_TRUE(entries.empty());
    return true;
}

// ---------------------------------------------------------------------------
// CallFlowsResource.list_addresses -- singular 'call_flow' subpath
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_call_flows_list_addresses_uses_singular_path) {
    auto client = mocktest::make_client();
    auto body = client.fabric().call_flows.list_addresses("cf-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    // singular ``call_flow`` (NOT ``call_flows``) in the addresses sub-path.
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/call_flow/cf-1/addresses"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// ConferenceRoomsResource.list_addresses -- singular 'conference_room' subpath
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_conference_rooms_list_addresses_uses_singular_path) {
    auto client = mocktest::make_client();
    auto body = client.fabric().conference_rooms.list_addresses("cr-1");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/conference_room/cr-1/addresses"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// Subscribers SIP endpoint per-id ops
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_subscribers_get_sip_endpoint) {
    auto client = mocktest::make_client();
    auto body = client.fabric().subscribers.get_sip_endpoint("sub-1", "ep-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string(
        "/api/fabric/resources/subscribers/sub-1/sip_endpoints/ep-1"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_fabric_subscribers_update_sip_endpoint_uses_patch) {
    auto client = mocktest::make_client();
    auto body = client.fabric().subscribers.update_sip_endpoint(
        "sub-1", "ep-1", {{"username", "renamed"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("PATCH"));
    ASSERT_EQ(j.path, std::string(
        "/api/fabric/resources/subscribers/sub-1/sip_endpoints/ep-1"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("username", std::string()), std::string("renamed"));
    return true;
}

TEST(rest_mock_fabric_subscribers_delete_sip_endpoint) {
    auto client = mocktest::make_client();
    auto body = client.fabric().subscribers.delete_sip_endpoint("sub-1", "ep-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string(
        "/api/fabric/resources/subscribers/sub-1/sip_endpoints/ep-1"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// FabricTokens
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_tokens_create_invite_token) {
    auto client = mocktest::make_client();
    auto body = client.fabric().tokens.create_invite_token(
        {{"email", "invitee@example.com"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    // subscriber/invites uses the singular 'subscriber' path segment.
    ASSERT_EQ(j.path, std::string("/api/fabric/subscriber/invites"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("email", std::string()), std::string("invitee@example.com"));
    return true;
}

TEST(rest_mock_fabric_tokens_create_embed_token) {
    auto client = mocktest::make_client();
    auto body = client.fabric().tokens.create_embed_token(
        {{"allowed_addresses", json::array({"addr-1", "addr-2"})}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/fabric/embeds/tokens"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_TRUE(j.body.contains("allowed_addresses"));
    ASSERT_TRUE(j.body["allowed_addresses"].is_array());
    ASSERT_EQ(j.body["allowed_addresses"].size(), (size_t)2);
    ASSERT_EQ(j.body["allowed_addresses"][0].get<std::string>(), std::string("addr-1"));
    ASSERT_EQ(j.body["allowed_addresses"][1].get<std::string>(), std::string("addr-2"));
    return true;
}

TEST(rest_mock_fabric_tokens_refresh_subscriber_token) {
    auto client = mocktest::make_client();
    auto body = client.fabric().tokens.refresh_subscriber_token(
        {{"refresh_token", "abc-123"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/fabric/subscribers/tokens/refresh"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("refresh_token", std::string()), std::string("abc-123"));
    return true;
}

// ---------------------------------------------------------------------------
// GenericResources
// ---------------------------------------------------------------------------

TEST(rest_mock_fabric_resources_list_returns_data_collection) {
    auto client = mocktest::make_client();
    auto body = client.fabric().resources.list();
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_fabric_resources_get_returns_single_resource) {
    auto client = mocktest::make_client();
    auto body = client.fabric().resources.get("res-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/res-1"));
    return true;
}

TEST(rest_mock_fabric_resources_delete) {
    auto client = mocktest::make_client();
    auto body = client.fabric().resources.delete_("res-2");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/res-2"));
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_fabric_resources_list_addresses) {
    auto client = mocktest::make_client();
    auto body = client.fabric().resources.list_addresses("res-3");
    ASSERT_TRUE(body.is_object());
    ASSERT_TRUE(body.contains("data"));
    ASSERT_TRUE(body["data"].is_array());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/res-3/addresses"));
    return true;
}

TEST(rest_mock_fabric_resources_assign_domain_application) {
    auto client = mocktest::make_client();
    auto body = client.fabric().resources.assign_domain_application(
        "res-4", {{"domain_application_id", "da-7"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/res-4/domain_applications"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("domain_application_id", std::string()), std::string("da-7"));
    return true;
}
