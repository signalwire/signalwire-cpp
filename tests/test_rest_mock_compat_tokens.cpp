// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_tokens.py.
//
// Covers CompatTokens.create / .update / .delete. ``CompatTokens`` extends
// BaseResource (not CrudResource), so its update uses PATCH rather than
// POST.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kTokensBase = "/api/laml/2010-04-01/Accounts/test_proj/tokens";
}

// ---------------------------------------------------------------------------
// CompatTokens.create -> POST .../tokens
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_tokens_create_returns_token_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().tokens.create({{"Ttl", 3600}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("token") || result.contains("id"));
    return true;
}

TEST(rest_mock_compat_tokens_create_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().tokens.create(
        {{"Ttl", 3600}, {"Name", "api-key"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kTokensBase);
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Ttl", 0), 3600);
    ASSERT_EQ(j.body.value("Name", std::string()), std::string("api-key"));
    return true;
}

// ---------------------------------------------------------------------------
// CompatTokens.update -> PATCH .../tokens/{id}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_tokens_update_returns_token_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().tokens.update("TK_U", {{"Ttl", 7200}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("token") || result.contains("id"));
    return true;
}

TEST(rest_mock_compat_tokens_update_journal_records_patch) {
    auto client = mocktest::make_client();
    client.compat().tokens.update("TK_UU", {{"Ttl", 7200}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("PATCH"));
    ASSERT_EQ(j.path, kTokensBase + "/TK_UU");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("Ttl", 0), 7200);
    return true;
}

// ---------------------------------------------------------------------------
// CompatTokens.delete -> DELETE .../tokens/{id}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_tokens_delete_returns_dict) {
    auto client = mocktest::make_client();
    auto result = client.compat().tokens.delete_("TK_D");
    ASSERT_TRUE(result.is_object());
    return true;
}

TEST(rest_mock_compat_tokens_delete_journal_records_delete) {
    auto client = mocktest::make_client();
    client.compat().tokens.delete_("TK_DEL");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("DELETE"));
    ASSERT_EQ(j.path, kTokensBase + "/TK_DEL");
    return true;
}
