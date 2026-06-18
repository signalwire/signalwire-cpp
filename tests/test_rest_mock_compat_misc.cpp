// Mock-backed unit tests translated from
// signalwire-python/tests/unit/rest/test_compat_misc.py.
//
// Covers single-method gaps on:
//   - CompatApplications.update (POST)
//   - CompatLamlBins.update (POST)
//
// Included by tests/test_main.cpp.

#include "mocktest.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
// Per-test random project => read the AccountSid from the active scope.
std::string account_base() {
    return "/api/laml/2010-04-01/Accounts/" + mocktest::active_project();
}
}

// ---------------------------------------------------------------------------
// CompatApplications.update
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_applications_update_returns_application_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().applications.update(
        "AP_U", {{"FriendlyName", "updated"}});
    ASSERT_TRUE(result.is_object());
    // Application resources carry friendly_name + sid + voice_url.
    ASSERT_TRUE(result.contains("friendly_name") || result.contains("sid"));
    return true;
}

TEST(rest_mock_compat_applications_update_journal_records_post) {
    auto client = mocktest::make_client();
    (void)client.compat().applications.update(
        "AP_UU",
        {{"FriendlyName", "renamed"}, {"VoiceUrl", "https://a.b/v"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, account_base() + "/Applications/AP_UU");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("renamed"));
    ASSERT_EQ(j.body.value("VoiceUrl", std::string()), std::string("https://a.b/v"));
    return true;
}

// ---------------------------------------------------------------------------
// CompatLamlBins.update
// ---------------------------------------------------------------------------

TEST(rest_mock_compat_laml_bins_update_returns_bin_resource) {
    auto client = mocktest::make_client();
    auto result = client.compat().laml_bins.update(
        "LB_U", {{"FriendlyName", "updated"}});
    ASSERT_TRUE(result.is_object());
    // LAML bin resources carry friendly_name + sid + contents.
    ASSERT_TRUE(result.contains("friendly_name")
                || result.contains("sid")
                || result.contains("contents"));
    return true;
}

TEST(rest_mock_compat_laml_bins_update_journal_records_post) {
    auto client = mocktest::make_client();
    (void)client.compat().laml_bins.update(
        "LB_UU",
        {{"FriendlyName", "renamed"}, {"Contents", "<Response/>"}});
    auto j = mocktest::journal_last();
    ASSERT_EQ(j.method, std::string("POST"));
    ASSERT_EQ(j.path, account_base() + "/LamlBins/LB_UU");
    ASSERT_TRUE(j.body.is_object());
    ASSERT_EQ(j.body.value("FriendlyName", std::string()), std::string("renamed"));
    ASSERT_EQ(j.body.value("Contents", std::string()), std::string("<Response/>"));
    return true;
}
