// Parity tests for signalwire::security::security_utils.
// Mirrors signalwire-python tests for
// signalwire/core/security/security_utils.py:
//   FilterSensitiveHeaders  <-> filter_sensitive_headers
//   RedactUrl               <-> redact_url
//   IsValidHostname         <-> is_valid_hostname

#include "signalwire/security/security_utils.hpp"

#include <map>
#include <string>

using namespace signalwire::security::security_utils;

// --- FilterSensitiveHeaders ------------------------------------------

TEST(security_utils_filter_strips_sensitive_headers) {
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer secret"},
        {"Cookie", "session=abc"},
        {"X-Api-Key", "key123"},
        {"Proxy-Authorization", "Basic xyz"},
        {"Set-Cookie", "a=b"},
        {"Content-Type", "application/json"},
    };
    auto filtered = FilterSensitiveHeaders(headers);
    // Only the non-sensitive header survives.
    ASSERT_EQ(filtered.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(filtered.count("Content-Type") == 1);
    ASSERT_EQ(filtered.at("Content-Type"), std::string("application/json"));
    // Every sensitive key is gone.
    ASSERT_TRUE(filtered.count("Authorization") == 0);
    ASSERT_TRUE(filtered.count("Cookie") == 0);
    ASSERT_TRUE(filtered.count("X-Api-Key") == 0);
    ASSERT_TRUE(filtered.count("Proxy-Authorization") == 0);
    ASSERT_TRUE(filtered.count("Set-Cookie") == 0);
    return true;
}

TEST(security_utils_filter_is_case_insensitive_on_keys) {
    // Sensitivity compares case-insensitively, regardless of input casing.
    std::map<std::string, std::string> headers = {
        {"AUTHORIZATION", "Bearer secret"},
        {"cookie", "x=y"},
        {"X-API-KEY", "key"},
        {"X-Request-Id", "req-1"},
    };
    auto filtered = FilterSensitiveHeaders(headers);
    ASSERT_EQ(filtered.size(), static_cast<std::size_t>(1));
    ASSERT_TRUE(filtered.count("X-Request-Id") == 1);
    return true;
}

TEST(security_utils_filter_preserves_non_sensitive_casing) {
    // Kept keys retain their original casing exactly.
    std::map<std::string, std::string> headers = {
        {"X-Custom-Header", "value"},
        {"aCcEpT", "text/html"},
    };
    auto filtered = FilterSensitiveHeaders(headers);
    ASSERT_EQ(filtered.size(), static_cast<std::size_t>(2));
    ASSERT_TRUE(filtered.count("X-Custom-Header") == 1);
    ASSERT_TRUE(filtered.count("aCcEpT") == 1);
    return true;
}

TEST(security_utils_filter_empty_input_yields_empty_map) {
    std::map<std::string, std::string> empty;
    auto filtered = FilterSensitiveHeaders(empty);
    ASSERT_TRUE(filtered.empty());
    return true;
}

// --- RedactUrl -------------------------------------------------------

TEST(security_utils_redact_masks_password) {
    ASSERT_EQ(RedactUrl("https://user:secret@host/path"),
              std::string("https://user:****@host/path"));
    return true;
}

TEST(security_utils_redact_url_without_credentials_unchanged) {
    ASSERT_EQ(RedactUrl("https://host/path"), std::string("https://host/path"));
    // A lone username (no password) is not credentials per the pattern.
    ASSERT_EQ(RedactUrl("https://user@host/path"),
              std::string("https://user@host/path"));
    return true;
}

TEST(security_utils_redact_empty_string_unchanged) {
    ASSERT_EQ(RedactUrl(""), std::string(""));
    return true;
}

TEST(security_utils_redact_preserves_user_and_host) {
    // The username and everything after the userinfo are untouched.
    ASSERT_EQ(RedactUrl("sip://alice:p4ss@example.com:5060"),
              std::string("sip://alice:****@example.com:5060"));
    return true;
}

// --- IsValidHostname -------------------------------------------------

TEST(security_utils_hostname_valid_accepts_plain_host) {
    ASSERT_TRUE(IsValidHostname("example.com"));
    ASSERT_TRUE(IsValidHostname("sub.domain.example.com"));
    ASSERT_TRUE(IsValidHostname("localhost"));
    ASSERT_TRUE(IsValidHostname("192.168.1.1"));
    return true;
}

TEST(security_utils_hostname_empty_rejected) {
    ASSERT_FALSE(IsValidHostname(""));
    return true;
}

TEST(security_utils_hostname_with_whitespace_rejected) {
    ASSERT_FALSE(IsValidHostname("bad host"));
    ASSERT_FALSE(IsValidHostname("host\t"));
    ASSERT_FALSE(IsValidHostname("host\n"));
    return true;
}

TEST(security_utils_hostname_with_slashes_rejected) {
    ASSERT_FALSE(IsValidHostname("host/path"));
    ASSERT_FALSE(IsValidHostname("host\\path"));
    return true;
}

TEST(security_utils_hostname_with_control_chars_rejected) {
    ASSERT_FALSE(IsValidHostname(std::string("host\x01")));
    ASSERT_FALSE(IsValidHostname(std::string("host\x7f")));
    ASSERT_FALSE(IsValidHostname(std::string("ho\x00st", 5)));
    return true;
}
