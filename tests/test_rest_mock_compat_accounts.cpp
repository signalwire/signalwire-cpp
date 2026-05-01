// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_accounts.py.
//
// Drives ``client.compat().accounts.*`` against the local mock_signalwire
// HTTP server. Each test asserts on the SDK return value AND on the
// ``mocktest::journal_last()`` record so neither half is allowed to drift.
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
const std::string kAccountsRoot = "/api/laml/2010-04-01/Accounts";
}

// ---------------------------------------------------------------------------
// CompatAccounts.create -> POST /api/laml/2010-04-01/Accounts
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_accounts_create_returns_account_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().accounts.create({{"FriendlyName", "Sub-A"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("friendly_name"));
    return true;
}

TEST(rest_mock_compat_accounts_create_journal_records_post) {
    auto client = mocktest::make_client();
    client.compat().accounts.create({{"FriendlyName", "Sub-B"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kAccountsRoot);
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("Sub-B"));
    ASSERT_TRUE(j.response_status.has_value());
    int s = *j.response_status;
    ASSERT_TRUE(s >= 200 && s < 400);
    return true;
}

// ---------------------------------------------------------------------------
// CompatAccounts.get -> GET /api/laml/2010-04-01/Accounts/{sid}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_accounts_get_returns_account_for_sid) {
    auto client = mocktest::make_client();
    auto result = client.compat().accounts.get("AC123");
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("friendly_name"));
    return true;
}

TEST(rest_mock_compat_accounts_get_journal_records_get_with_sid) {
    auto client = mocktest::make_client();
    client.compat().accounts.get("AC_SAMPLE_SID");
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("GET"));
    ASSERT_EQ(j.path, kAccountsRoot + "/AC_SAMPLE_SID");
    ASSERT_TRUE(j.matched_route.has_value());
    return true;
}

// ---------------------------------------------------------------------------
// CompatAccounts.update -> POST /api/laml/2010-04-01/Accounts/{sid}
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_accounts_update_returns_updated_account) {
    auto client = mocktest::make_client();
    auto result = client.compat().accounts.update("AC123", {{"FriendlyName", "Renamed"}});
    ASSERT_TRUE(result.is_object());
    ASSERT_TRUE(result.contains("friendly_name"));
    return true;
}

TEST(rest_mock_compat_accounts_update_journal_records_post_to_account_path) {
    auto client = mocktest::make_client();
    client.compat().accounts.update("AC_X", {{"FriendlyName", "NewName"}});
    auto j = mocktest::journal_last();
    // Twilio-compat update uses POST (not PATCH/PUT).
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, kAccountsRoot + "/AC_X");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("NewName"));
    return true;
}
