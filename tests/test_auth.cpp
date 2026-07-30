// Auth mixin tests — basic auth, timing-safe, auto-generated, env vars

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/common.hpp"

using namespace signalwire::agent;
using json = nlohmann::json;

TEST(auth_set_auth_basic) {
  AgentBase agent;
  agent.set_auth("myuser", "mypass");
  ASSERT_EQ(agent.auth_username(), "myuser");
  ASSERT_EQ(agent.auth_password(), "mypass");
  return true;
}

TEST(auth_set_auth_chaining) {
  AgentBase agent;
  auto& ref = agent.set_auth("u", "p");
  ASSERT_EQ(&ref, &agent);
  return true;
}

TEST(auth_default_empty) {
  AgentBase agent;
  ASSERT_TRUE(agent.auth_username().empty());
  ASSERT_TRUE(agent.auth_password().empty());
  return true;
}

TEST(auth_override) {
  AgentBase agent;
  agent.set_auth("user1", "pass1");
  agent.set_auth("user2", "pass2");
  ASSERT_EQ(agent.auth_username(), "user2");
  ASSERT_EQ(agent.auth_password(), "pass2");
  return true;
}

// ========================================================================
// Timing-safe compare
// ========================================================================

TEST(auth_timing_safe_compare_equal) {
  ASSERT_TRUE(signalwire::timing_safe_compare("test", "test"));
  return true;
}

TEST(auth_timing_safe_compare_different) {
  ASSERT_FALSE(signalwire::timing_safe_compare("test", "TEST"));
  return true;
}

TEST(auth_timing_safe_compare_length_mismatch) {
  ASSERT_FALSE(signalwire::timing_safe_compare("short", "longer_string"));
  return true;
}

TEST(auth_timing_safe_compare_empty) {
  ASSERT_TRUE(signalwire::timing_safe_compare("", ""));
  return true;
}

TEST(auth_timing_safe_compare_one_empty) {
  ASSERT_FALSE(signalwire::timing_safe_compare("", "notempty"));
  ASSERT_FALSE(signalwire::timing_safe_compare("notempty", ""));
  return true;
}

// ========================================================================
// Base64
// ========================================================================

TEST(auth_base64_encode_basic) {
  ASSERT_EQ(signalwire::base64_encode("hello"), "aGVsbG8=");
  return true;
}

TEST(auth_base64_encode_empty) {
  ASSERT_EQ(signalwire::base64_encode(""), "");
  return true;
}

TEST(auth_base64_roundtrip) {
  std::string original = "user:password123!@#";
  std::string encoded = signalwire::base64_encode(original);
  std::string decoded = signalwire::base64_decode(encoded);
  ASSERT_EQ(decoded, original);
  return true;
}

TEST(auth_base64_roundtrip_binary) {
  std::string binary = std::string("\x00\x01\x02\xff\xfe", 5);
  std::string encoded = signalwire::base64_encode(binary);
  std::string decoded = signalwire::base64_decode(encoded);
  ASSERT_EQ(decoded, binary);
  return true;
}

// ========================================================================
// URL encode
// ========================================================================

TEST(auth_url_encode_no_special) {
  ASSERT_EQ(signalwire::url_encode("hello"), "hello");
  return true;
}

TEST(auth_url_encode_spaces) {
  std::string encoded = signalwire::url_encode("hello world");
  ASSERT_TRUE(encoded.find("%20") != std::string::npos);
  return true;
}

TEST(auth_url_encode_special_chars) {
  std::string encoded = signalwire::url_encode("user@host.com");
  ASSERT_TRUE(encoded.find("%40") != std::string::npos);
  return true;
}

// ========================================================================
// Random password generation
// ========================================================================

TEST(auth_generate_random_password_length) {
  std::string pw = signalwire::generate_random_password(16);
  ASSERT_EQ(pw.size(), 16u);
  return true;
}

TEST(auth_generate_random_password_default_length) {
  std::string pw = signalwire::generate_random_password();
  ASSERT_EQ(pw.size(), 32u);
  return true;
}

TEST(auth_generate_random_password_unique) {
  std::string pw1 = signalwire::generate_random_password(32);
  std::string pw2 = signalwire::generate_random_password(32);
  ASSERT_NE(pw1, pw2);
  return true;
}

TEST(auth_generate_random_password_alphanumeric) {
  std::string pw = signalwire::generate_random_password(100);
  for (char c : pw) {
    ASSERT_TRUE((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
  }
  return true;
}

// ========================================================================
// UUID generation
// ========================================================================

TEST(auth_generate_uuid_format) {
  std::string uuid = signalwire::generate_uuid();
  // Should have 4 hyphens
  int hyphens = 0;
  for (char c : uuid) {
    if (c == '-') hyphens++;
  }
  ASSERT_EQ(hyphens, 4);
  // Should be roughly 36 chars (8-4-4-4-12)
  ASSERT_TRUE(uuid.size() >= 32u);
  return true;
}

TEST(auth_generate_uuid_unique) {
  std::string u1 = signalwire::generate_uuid();
  std::string u2 = signalwire::generate_uuid();
  ASSERT_NE(u1, u2);
  return true;
}
