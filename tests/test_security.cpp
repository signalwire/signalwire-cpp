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

// ── Session-tracking + Python-surface aliases ──────────────────────

TEST(session_manager_generate_and_validate_tool_token) {
    // Fixed secret so the token round-trips deterministically.
    std::vector<uint8_t> secret(32, 0x42);
    SessionManager sm(secret);

    std::string token = sm.generate_token("do_thing", "call-abc");
    ASSERT_FALSE(token.empty());
    // create_tool_token is an alias of generate_token.
    std::string token2 = sm.create_tool_token("do_thing", "call-abc");
    ASSERT_FALSE(token2.empty());

    // validate_tool_token(function_name, token, call_id) reorders to
    // validate_token(token, function_name, call_id).
    ASSERT_TRUE(sm.validate_tool_token("do_thing", token, "call-abc"));
    ASSERT_FALSE(sm.validate_tool_token("other_fn", token, "call-abc"));
    ASSERT_FALSE(sm.validate_tool_token("do_thing", token, "wrong-call"));
    return true;
}

TEST(session_manager_create_session_returns_given_id) {
    SessionManager sm;
    ASSERT_EQ(sm.create_session("call-existing"), "call-existing");
    return true;
}

TEST(session_manager_create_session_mints_id) {
    SessionManager sm;
    std::string id = sm.create_session();
    ASSERT_FALSE(id.empty());
    // URL-safe: no '+', '/', or '=' padding.
    ASSERT_TRUE(id.find('+') == std::string::npos);
    ASSERT_TRUE(id.find('/') == std::string::npos);
    ASSERT_TRUE(id.find('=') == std::string::npos);
    // Two mints differ.
    ASSERT_NE(id, sm.create_session());
    return true;
}

TEST(session_manager_metadata_round_trips) {
    SessionManager sm;
    sm.create_session("call-1");
    // Unknown session -> empty object, never null.
    ASSERT_TRUE(sm.get_session_metadata("call-unknown").is_object());
    ASSERT_TRUE(sm.get_session_metadata("call-unknown").empty());

    ASSERT_TRUE(sm.set_session_metadata("call-1", "name", "Alice"));
    ASSERT_TRUE(sm.set_session_metadata("call-1", "count", 3));
    json md = sm.get_session_metadata("call-1");
    ASSERT_EQ(md.value("name", ""), "Alice");
    ASSERT_EQ(md.value("count", 0), 3);

    // Returned copy is independent of the internal store.
    md["name"] = "Mutated";
    ASSERT_EQ(sm.get_session_metadata("call-1").value("name", ""), "Alice");
    return true;
}

TEST(session_manager_activate_and_end_session) {
    SessionManager sm;
    ASSERT_TRUE(sm.activate_session("call-2"));
    sm.set_session_metadata("call-2", "k", "v");
    ASSERT_EQ(sm.get_session_metadata("call-2").value("k", ""), "v");

    ASSERT_TRUE(sm.end_session("call-2"));
    // Metadata cleared after end.
    ASSERT_TRUE(sm.get_session_metadata("call-2").empty());
    return true;
}

TEST(session_manager_debug_token_disabled_by_default) {
    SessionManager sm;
    std::string token = sm.generate_token("dbg_fn", "callabc");
    // Debug mode off (default) -> error, no decode.
    json dbg = sm.debug_token(token);
    ASSERT_EQ(dbg.value("error", ""), "debug mode not enabled");
    return true;
}

TEST(session_manager_debug_token_valid) {
    std::vector<uint8_t> secret(32, 0x11);
    SessionManager sm(secret);
    sm.set_debug_mode(true);
    std::string token = sm.generate_token("dbg_fn", "callabc");  // 7 chars -> not truncated

    json dbg = sm.debug_token(token);
    ASSERT_TRUE(dbg.value("valid_format", false));
    ASSERT_TRUE(dbg.contains("components"));
    ASSERT_EQ(dbg["components"].value("function", ""), "dbg_fn");
    ASSERT_EQ(dbg["components"].value("call_id", ""), "callabc");
    ASSERT_TRUE(dbg.contains("status"));
    // Fresh token: not expired.
    ASSERT_FALSE(dbg["status"].value("is_expired", true));
    return true;
}

TEST(session_manager_debug_token_truncates_long_fields) {
    SessionManager sm;
    sm.set_debug_mode(true);
    std::string token = sm.generate_token("fn", "this-is-a-very-long-call-id");
    json dbg = sm.debug_token(token);
    ASSERT_TRUE(dbg.value("valid_format", false));
    // call_id truncated to 8 chars + "..."
    ASSERT_EQ(dbg["components"].value("call_id", ""), "this-is-...");
    return true;
}

TEST(session_manager_debug_token_malformed) {
    SessionManager sm;
    sm.set_debug_mode(true);
    json dbg = sm.debug_token("not-a-token");
    ASSERT_FALSE(dbg.value("valid_format", true));
    return true;
}
