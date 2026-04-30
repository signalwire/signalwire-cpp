// Parity tests for signalwire::utils::url_validator::validate_url.
// Mirrors signalwire-python tests/unit/utils/test_url_validator.py.
// The DNS resolver is stubbed via _set_resolver() so the suite stays
// hermetic.

#include "signalwire/utils/url_validator.hpp"

#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

using namespace signalwire::utils::url_validator;

namespace {

void stub_resolver(const std::string& ip) {
    _set_resolver([ip](const std::string&) -> std::optional<std::vector<std::string>> {
        return std::vector<std::string>{ip};
    });
}

void stub_failed_resolver() {
    _set_resolver([](const std::string&) -> std::optional<std::vector<std::string>> {
        return std::nullopt;
    });
}

void reset_state() {
    _set_resolver(nullptr);
    ::unsetenv("SWML_ALLOW_PRIVATE_URLS");
}

}  // namespace

// --- Scheme ----------------------------------------------------------

TEST(url_validator_http_scheme_allowed) {
    reset_state();
    stub_resolver("1.2.3.4");
    ASSERT_TRUE(validate_url("http://example.com"));
    reset_state();
    return true;
}

TEST(url_validator_https_scheme_allowed) {
    reset_state();
    stub_resolver("1.2.3.4");
    ASSERT_TRUE(validate_url("https://example.com"));
    reset_state();
    return true;
}

TEST(url_validator_ftp_scheme_rejected) {
    reset_state();
    ASSERT_FALSE(validate_url("ftp://example.com"));
    return true;
}

TEST(url_validator_file_scheme_rejected) {
    reset_state();
    ASSERT_FALSE(validate_url("file:///etc/passwd"));
    return true;
}

TEST(url_validator_javascript_scheme_rejected) {
    reset_state();
    ASSERT_FALSE(validate_url("javascript:alert(1)"));
    return true;
}

// --- Hostname --------------------------------------------------------

TEST(url_validator_no_hostname_rejected) {
    reset_state();
    ASSERT_FALSE(validate_url("http://"));
    return true;
}

TEST(url_validator_unresolvable_hostname_rejected) {
    reset_state();
    stub_failed_resolver();
    ASSERT_FALSE(validate_url("http://nonexistent.invalid"));
    reset_state();
    return true;
}

// --- Blocked ranges -------------------------------------------------

TEST(url_validator_loopback_ipv4_rejected) {
    reset_state();
    stub_resolver("127.0.0.1");
    ASSERT_FALSE(validate_url("http://localhost"));
    reset_state();
    return true;
}

TEST(url_validator_rfc1918_10_rejected) {
    reset_state();
    stub_resolver("10.0.0.5");
    ASSERT_FALSE(validate_url("http://internal"));
    reset_state();
    return true;
}

TEST(url_validator_rfc1918_192_rejected) {
    reset_state();
    stub_resolver("192.168.1.1");
    ASSERT_FALSE(validate_url("http://router"));
    reset_state();
    return true;
}

TEST(url_validator_rfc1918_172_rejected) {
    reset_state();
    stub_resolver("172.16.0.1");
    ASSERT_FALSE(validate_url("http://corp"));
    reset_state();
    return true;
}

TEST(url_validator_link_local_metadata_rejected) {
    reset_state();
    stub_resolver("169.254.169.254");
    ASSERT_FALSE(validate_url("http://metadata"));
    reset_state();
    return true;
}

TEST(url_validator_zero_ip_rejected) {
    reset_state();
    stub_resolver("0.0.0.0");
    ASSERT_FALSE(validate_url("http://void"));
    reset_state();
    return true;
}

TEST(url_validator_ipv6_loopback_rejected) {
    reset_state();
    stub_resolver("::1");
    ASSERT_FALSE(validate_url("http://[::1]"));
    reset_state();
    return true;
}

TEST(url_validator_ipv6_link_local_rejected) {
    reset_state();
    stub_resolver("fe80::1");
    ASSERT_FALSE(validate_url("http://link-local"));
    reset_state();
    return true;
}

TEST(url_validator_ipv6_private_rejected) {
    reset_state();
    stub_resolver("fc00::1");
    ASSERT_FALSE(validate_url("http://ipv6-private"));
    reset_state();
    return true;
}

TEST(url_validator_public_ip_allowed) {
    reset_state();
    stub_resolver("8.8.8.8");
    ASSERT_TRUE(validate_url("http://dns.google"));
    reset_state();
    return true;
}

// --- allow_private bypass ------------------------------------------

TEST(url_validator_allow_private_param_bypasses_check) {
    reset_state();
    // No resolver stub: bypass short-circuits before DNS.
    ASSERT_TRUE(validate_url("http://10.0.0.5", true));
    return true;
}

TEST(url_validator_env_var_bypasses_check) {
    reset_state();
    ::setenv("SWML_ALLOW_PRIVATE_URLS", "true", 1);
    ASSERT_TRUE(validate_url("http://10.0.0.5"));
    reset_state();
    return true;
}

TEST(url_validator_env_var_yes_bypasses_check) {
    reset_state();
    ::setenv("SWML_ALLOW_PRIVATE_URLS", "YES", 1);
    ASSERT_TRUE(validate_url("http://10.0.0.5"));
    reset_state();
    return true;
}

TEST(url_validator_env_var_1_bypasses_check) {
    reset_state();
    ::setenv("SWML_ALLOW_PRIVATE_URLS", "1", 1);
    ASSERT_TRUE(validate_url("http://10.0.0.5"));
    reset_state();
    return true;
}

TEST(url_validator_env_var_false_does_not_bypass) {
    reset_state();
    ::setenv("SWML_ALLOW_PRIVATE_URLS", "false", 1);
    stub_resolver("10.0.0.5");
    ASSERT_FALSE(validate_url("http://internal"));
    reset_state();
    return true;
}

TEST(url_validator_blocked_networks_has_all_nine) {
    ASSERT_EQ(BLOCKED_NETWORKS.size(), static_cast<std::size_t>(9));
    return true;
}
