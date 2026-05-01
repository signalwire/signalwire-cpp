// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_queues.py.
//
// Covers CompatQueues.update + the Members sub-collection (list_members,
// get_member, dequeue_member).
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kQueueBase = "/api/laml/2010-04-01/Accounts/test_proj/Queues";
}

// ---------------------------------------------------------------------------
// CompatQueues.update -> POST /Queues/{sid}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_queues_update_returns_queue_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().queues.update(
        "QU_U", {{"FriendlyName", "updated"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("friendly_name") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_queues_update_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().queues.update(
        "QU_UU",
        {{"FriendlyName", "renamed"}, {"MaxSize", 200}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kQueueBase + "/QU_UU");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("renamed"));
    ASSERT_EQ(j.body.value("MaxSize", 0), 200);
    return true;
}

// ---------------------------------------------------------------------------
// list_members
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_queues_list_members_returns_paginated) {
    auto client = mocktest::make_client();
    auto result = client.compat().queues.list_members("QU_LM");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("queue_members"));
    ASSERT_TRUE(result["queue_members"].is_array());
    return true;
}

TEST(rest_mock_compat_queues_list_members_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().queues.list_members("QU_LMX");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kQueueBase + "/QU_LMX/Members");
    return true;
}

// ---------------------------------------------------------------------------
// get_member
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_queues_get_member_returns_member_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().queues.get_member("QU_GM", "CA_GM");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("call_sid") || result.contains("queue_sid"));
    return true;
}

TEST(rest_mock_compat_queues_get_member_journal_records_get) {
    auto client = mocktest::make_client();
    client.compat().queues.get_member("QU_GMX", "CA_GMX");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kQueueBase + "/QU_GMX/Members/CA_GMX");
    return true;
}

// ---------------------------------------------------------------------------
// dequeue_member
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_queues_dequeue_member_returns_member_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().queues.dequeue_member(
        "QU_DM", "CA_DM", {{"Url", "https://a.b"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("call_sid") || result.contains("queue_sid"));
    return true;
}

TEST(rest_mock_compat_queues_dequeue_member_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().queues.dequeue_member(
        "QU_DMX", "CA_DMX",
        {{"Url", "https://a.b/url"}, {"Method", "POST"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kQueueBase + "/QU_DMX/Members/CA_DMX");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Url", std::string()), std::string("https://a.b/url"));
    ASSERT_EQ(j.body.value("Method", std::string()), std::string("POST"));
    return true;
}
