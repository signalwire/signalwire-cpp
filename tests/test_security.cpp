// SessionManager tests

#include "signalwire/security/session_manager.hpp"
#include <thread>
#include <chrono>
#include <string_view>

using namespace signalwire::security;

TEST(session_manager_create_and_validate_token) {
    SessionManager sm;
    std::string token = sm.create_token("get_weather", "call-123", 3600);

    ASSERT_FALSE(token.empty());
    ASSERT_TRUE(token.find('.') != std::string::npos);

    ASSERT_TRUE(sm.validate_token(token, "get_weather", "call-123"));
    return true;
}

TEST(session_manager_wrong_function_name) {
    SessionManager sm;
    std::string token = sm.create_token("get_weather", "call-123", 3600);
    ASSERT_FALSE(sm.validate_token(token, "wrong_function", "call-123"));
    return true;
}

TEST(session_manager_wrong_call_id) {
    SessionManager sm;
    std::string token = sm.create_token("get_weather", "call-123", 3600);
    ASSERT_FALSE(sm.validate_token(token, "get_weather", "wrong-call-id"));
    return true;
}

TEST(session_manager_expired_token) {
    SessionManager sm;
    // Create a token that expires immediately (0 seconds)
    std::string token = sm.create_token("get_weather", "call-123", 0);
    // Sleep briefly to ensure expiry
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    ASSERT_FALSE(sm.validate_token(token, "get_weather", "call-123"));
    return true;
}

TEST(session_manager_tampered_token) {
    SessionManager sm;
    std::string token = sm.create_token("get_weather", "call-123", 3600);

    // Tamper with the signature
    if (!token.empty()) {
        token.back() = (token.back() == 'a') ? 'b' : 'a';
    }
    ASSERT_FALSE(sm.validate_token(token, "get_weather", "call-123"));
    return true;
}

TEST(session_manager_malformed_token_no_dot) {
    SessionManager sm;
    ASSERT_FALSE(sm.validate_token("notokenhere", "func", "call"));
    return true;
}

TEST(session_manager_malformed_token_empty) {
    SessionManager sm;
    ASSERT_FALSE(sm.validate_token("", "func", "call"));
    return true;
}

TEST(session_manager_different_secrets_reject) {
    std::vector<uint8_t> secret1(32, 0x01);
    std::vector<uint8_t> secret2(32, 0x02);

    SessionManager sm1(secret1);
    SessionManager sm2(secret2);

    std::string token = sm1.create_token("get_weather", "call-123", 3600);
    // Token from sm1 should not validate with sm2
    ASSERT_FALSE(sm2.validate_token(token, "get_weather", "call-123"));
    return true;
}

TEST(session_manager_same_secret_validates) {
    std::vector<uint8_t> secret(32, 0xAB);

    SessionManager sm1(secret);
    SessionManager sm2(secret);

    std::string token = sm1.create_token("func", "call-1", 3600);
    ASSERT_TRUE(sm2.validate_token(token, "func", "call-1"));
    return true;
}

TEST(session_manager_timing_safe_compare) {
    ASSERT_TRUE(SessionManager::timing_safe_compare("hello", "hello"));
    ASSERT_FALSE(SessionManager::timing_safe_compare("hello", "world"));
    ASSERT_FALSE(SessionManager::timing_safe_compare("short", "longer"));
    ASSERT_TRUE(SessionManager::timing_safe_compare("", ""));
    return true;
}

TEST(session_manager_token_format) {
    SessionManager sm;
    std::string token = sm.create_token("func", "call", 3600);

    // Token should have format: base64.hex
    auto dot = token.find('.');
    ASSERT_TRUE(dot != std::string::npos);

    std::string payload = token.substr(0, dot);
    std::string signature = token.substr(dot + 1);

    ASSERT_FALSE(payload.empty());
    ASSERT_FALSE(signature.empty());

    // Signature should be hex (SHA256 = 32 bytes = 64 hex chars)
    ASSERT_EQ(signature.size(), 64u);
    for (char c : signature) {
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    return true;
}

TEST(session_manager_constructor_rejects_short_secret) {
    std::vector<uint8_t> short_secret(8, 0x01);
    ASSERT_THROWS(SessionManager sm(short_secret));
    return true;
}

// --- std::string_view-param coverage --------------------------------------
//
// validate_token() takes its three params by std::string_view. These tests
// pin that the read-only-view contract is honoured: the function PARSES and
// HMAC-verifies the token within the call and retains nothing, so passing
// views over buffers whose lifetime is bounded by the call (a temporary, an
// owned std::string that is mutated/destroyed afterwards) must still produce
// the correct accept/reject decision. A dangling-view regression (e.g. if the
// impl ever stashed a view past the call) would corrupt these comparisons.
// They drive the real create/validate code path — no mocks.

TEST(session_manager_validate_token_string_view_args) {
    SessionManager sm;
    std::string token = sm.create_token("get_weather", "call-123", 3600);

    // Build explicit string_views over distinct owning buffers, then validate.
    std::string fn_buf = "get_weather";
    std::string cid_buf = "call-123";
    std::string_view tok_v{token};
    std::string_view fn_v{fn_buf};
    std::string_view cid_v{cid_buf};

    ASSERT_TRUE(sm.validate_token(tok_v, fn_v, cid_v));
    // Wrong-value views still reject (the comparison reads through the view).
    ASSERT_FALSE(sm.validate_token(tok_v, std::string_view{"wrong_fn"}, cid_v));
    ASSERT_FALSE(sm.validate_token(tok_v, fn_v, std::string_view{"wrong-call"}));
    return true;
}

TEST(session_manager_validate_token_temporary_string_args) {
    SessionManager sm;
    std::string token = sm.create_token("func-x", "call-77", 3600);

    // Pass freshly-constructed temporaries (string_view binds to each
    // temporary for the duration of the full call expression — the impl must
    // finish reading before the temporary dies, which it does).
    ASSERT_TRUE(sm.validate_token(token,
                                  std::string("func-x"),
                                  std::string("call-77")));

    // A sub-view of a larger buffer (no NUL terminator at the view's end):
    // proves validate_token relies on the view's length, not C-string scanning.
    std::string fn_haystack = "func-x__padding__";
    std::string_view fn_exact = std::string_view{fn_haystack}.substr(0, 6); // "func-x"
    ASSERT_TRUE(sm.validate_token(token, fn_exact, std::string_view{"call-77"}));
    // Over-long view (includes the padding) must NOT match.
    ASSERT_FALSE(sm.validate_token(token, std::string_view{fn_haystack},
                                   std::string_view{"call-77"}));
    return true;
}
