// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_registry_mock.py.
//
// Closes audit gaps for the 10DLC Campaign Registry namespace: brands,
// campaigns, orders, and numbers. All endpoints sit under
// ``/api/relay/rest/registry/beta``.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kRegBase = "/api/relay/rest/registry/beta";
}

// ---------------------------------------------------------------------------
// Brands
// ---------------------------------------------------------------------------

TEST(rest_mock_registry_brands_list_returns_dict) {
    auto client = mocktest::make_client();
    auto body = client.registry().brands.list();
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRegBase + "/brands");
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_registry_brands_get_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.registry().brands.get("brand-77");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRegBase + "/brands/brand-77");
    return true;
}

TEST(rest_mock_registry_brands_list_campaigns_uses_brand_subpath) {
    auto client = mocktest::make_client();
    auto body = client.registry().brands.list_campaigns("brand-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRegBase + "/brands/brand-1/campaigns");
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_registry_brands_create_campaign_posts_to_brand_subpath) {
    auto client = mocktest::make_client();
    auto body = client.registry().brands.create_campaign(
        "brand-2",
        {{"usecase", "LOW_VOLUME"}, {"description", "MFA"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kRegBase + "/brands/brand-2/campaigns");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("usecase", std::string()), std::string("LOW_VOLUME"));
    ASSERT_EQ(j.body.value("description", std::string()), std::string("MFA"));
    return true;
}

// ---------------------------------------------------------------------------
// Campaigns
// ---------------------------------------------------------------------------

TEST(rest_mock_registry_campaigns_get_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.registry().campaigns.get("camp-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRegBase + "/campaigns/camp-1");
    return true;
}

TEST(rest_mock_registry_campaigns_update_uses_put) {
    auto client = mocktest::make_client();
    auto body = client.registry().campaigns.update(
        "camp-2", {{"description", "Updated"}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    // Python parity: PUT, not PATCH.
    ASSERT_EQ(j.method, std::string("PUT"));
    ASSERT_EQ(j.path, kRegBase + "/campaigns/camp-2");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("description", std::string()), std::string("Updated"));
    return true;
}

TEST(rest_mock_registry_campaigns_list_numbers_uses_numbers_subpath) {
    auto client = mocktest::make_client();
    auto body = client.registry().campaigns.list_numbers("camp-3");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRegBase + "/campaigns/camp-3/numbers");
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

TEST(rest_mock_registry_campaigns_create_order_posts_to_orders_subpath) {
    auto client = mocktest::make_client();
    auto body = client.registry().campaigns.create_order(
        "camp-4",
        {{"numbers", json::array({"pn-1", "pn-2"})}});
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kRegBase + "/campaigns/camp-4/orders");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_TRUE(j.body.contains("numbers"));
    ASSERT_TRUE(j.body["numbers"].is_array());
    ASSERT_EQ(j.body["numbers"].size(), (size_t)2);
    ASSERT_EQ(j.body["numbers"][0].get<std::string>(), std::string("pn-1"));
    ASSERT_EQ(j.body["numbers"][1].get<std::string>(), std::string("pn-2"));
    return true;
}

// ---------------------------------------------------------------------------
// Orders
// ---------------------------------------------------------------------------

TEST(rest_mock_registry_orders_get_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.registry().orders.get("order-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kRegBase + "/orders/order-1");
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// Numbers (10DLC assigned phone numbers)
// ---------------------------------------------------------------------------

TEST(rest_mock_registry_numbers_delete_uses_id_in_path) {
    auto client = mocktest::make_client();
    auto body = client.registry().numbers.delete_("num-1");
    ASSERT_TRUE(body.is_object());
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, kRegBase + "/numbers/num-1");
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}
