// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_fabric_mock.py.
//
// Closes audit gaps for: addresses, generic resources operations, SIP-endpoint
// sub-resources on subscribers, the call-flows / conference-rooms addresses
// sub-paths (which use SINGULAR ``call_flow`` / ``conference_room``), and the
// full FabricTokens surface.
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
        "sub-1", "ep-1", {.username = "renamed"});
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

TEST(wire_regression_pin_fabric_tokens_create_invite_token_extras) {
    // create_subscriber_invite_token (POST /api/fabric/subscriber/invites) accepts
    // ONLY address_id (required) + expires_at -- per the vendored REST spec
    // SubscriberInviteTokenCreateRequest (no additionalProperties), which is the
    // strict mock's request-schema oracle. `email` is NOT accepted here (it is real
    // on the sibling create_subscriber endpoint, not this one), so there is no
    // invented extra to forward. Pin the two typed fields the spec declares.
    auto client = mocktest::make_client();
    auto body = client.fabric().tokens.create_invite_token(
        {.address_id = "addr-invite-1", .expires_at = 1725513600});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    // subscriber/invites uses the singular 'subscriber' path segment.
    ASSERT_EQ(j.path, std::string("/api/fabric/subscriber/invites"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("address_id", std::string()), std::string("addr-invite-1"));
    ASSERT_EQ(j.body.value("expires_at", 0), 1725513600);
    ASSERT_FALSE(j.body.contains("email"));
    return true;
}

TEST(wire_regression_pin_fabric_tokens_create_embed_token_base_body) {
    // create_embed_token (POST /api/fabric/embeds/tokens) accepts ONLY `token` --
    // verified against prime-rails Embed::Tokens::Operations::Create, which reads
    // solely params[:token] with no contract for allowed_addresses (that field is
    // real on the sibling create_guest_token endpoint, not this one). There is no
    // valid extra field to pin here, so this test pins the base token-only body
    // instead of asserting an invented field on the wire.
    auto client = mocktest::make_client();
    auto body = client.fabric().tokens.create_embed_token({.token = "embed-token-1"});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/fabric/embeds/tokens"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("token", std::string()), std::string("embed-token-1"));
    ASSERT_FALSE(j.body.contains("allowed_addresses"));
    return true;
}

TEST(rest_mock_fabric_tokens_refresh_subscriber_token) {
    auto client = mocktest::make_client();
    auto body = client.fabric().tokens.refresh_subscriber_token(
        {.refresh_token = "abc-123"});
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
        "res-4", {.domain_application_id = "da-7"});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, std::string("/api/fabric/resources/res-4/domain_applications"));
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("domain_application_id", std::string()), std::string("da-7"));
    return true;
}
