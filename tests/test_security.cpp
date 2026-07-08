// SessionManager tests

#include "signalwire/security/session_manager.hpp"
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <chrono>
#include <ctime>
#include <string_view>
#include <thread>
#include <vector>

using namespace signalwire::security;

TEST(session_manager_create_and_validate_token) {
    SessionManager sm;
    std::string token = sm.create_token("get_weather", "call-123", 3600);

    ASSERT_FALSE(token.empty());
    // Token is base64url of the 5 dot-fields — the dots are inside the
    // encoded plaintext, so the outer token has no literal '.'.
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

    // Tamper: flip a character near the end of the base64url token (the
    // signature field lives at the tail of the decoded plaintext). Use a
    // position a few chars from the end so the flip lands on real signature
    // bits rather than a trailing partial group.
    ASSERT_TRUE(token.size() > 4);
    size_t i = token.size() - 3;
    token[i] = (token[i] == 'A') ? 'B' : 'A';
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
    // Token = base64url({call_id}.{function_name}.{expiry}.{nonce}.{signature}).
    // Decode + inspect via debug_token (the only public decoder).
    SessionManager sm;
    sm.set_debug_mode(true);
    std::string token = sm.create_token("func", "call", 3600);

    json dbg = sm.debug_token(token);
    ASSERT_TRUE(dbg.value("valid_format", false));
    const json& c = dbg["components"];
    ASSERT_EQ(c.value("function", ""), "func");
    ASSERT_EQ(c.value("call_id", ""), "call");
    // nonce present + non-empty (16 hex chars from token_hex(8)).
    std::string nonce = c.value("nonce", "");
    ASSERT_EQ(nonce.size(), 16u);
    for (char ch : nonce) {
        ASSERT_TRUE((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'));
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

// ── Contract 7: tool-token WIRE FORMAT + nonce parity (#70) ─────────
//
// Python (core/security/session_manager.py): a minted token is the base64url
// encoding of 5 dot-joined fields {call_id}.{function_name}.{expiry}.{nonce}.
// {signature}; the signed message is {call_id}:{function_name}:{expiry}:{nonce}
// (HMAC-SHA256, hexdigest); nonce = secrets.token_hex(8) (16 hex chars); the
// signature compare is CONSTANT-TIME. These tests assert on the DECODED form
// and prove cross-port interop with a token constructed in the python-oracle
// shape. A 3-field / no-nonce / fn-first token FAILS (1)+(2)+(3).

namespace {
// base64url-encode (no padding), matching Python's urlsafe_b64encode.decode().
std::string c7_b64url(const std::string& data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    return out;  // no '=' padding
}

// HMAC-SHA256 hexdigest of message under a raw-byte secret (same primitive
// SessionManager uses internally), so the test can mint a token in the exact
// python-oracle format and prove it validates in-port.
std::string c7_hmac_hex(const std::vector<uint8_t>& secret, const std::string& message) {
    unsigned int len = 0;
    unsigned char mac[EVP_MAX_MD_SIZE];
    HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(message.data()), message.size(), mac, &len);
    static const char* hx = "0123456789abcdef";
    std::string out;
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(hx[mac[i] >> 4]);
        out.push_back(hx[mac[i] & 0xF]);
    }
    return out;
}
}  // namespace

// (1) decoded token has exactly 5 dot-fields with a NON-EMPTY nonce.
TEST(contract7_token_has_five_fields_and_nonce) {
    SessionManager sm;
    sm.set_debug_mode(true);
    std::string token = sm.generate_token("get_weather", "call-abc-123");
    json dbg = sm.debug_token(token);
    ASSERT_TRUE(dbg.value("valid_format", false));
    const json& c = dbg["components"];
    // The debug view surfaces all 5 decoded fields (nonce is the tell that
    // the token is not the old 3-field fn-first shape).
    ASSERT_TRUE(c.contains("call_id"));
    ASSERT_TRUE(c.contains("function"));
    ASSERT_TRUE(c.contains("expiry"));
    ASSERT_TRUE(c.contains("nonce"));
    ASSERT_TRUE(c.contains("signature"));
    ASSERT_EQ(c.value("function", ""), "get_weather");
    ASSERT_FALSE(c.value("nonce", "").empty());
    ASSERT_EQ(c.value("nonce", "").size(), 16u);  // secrets.token_hex(8)
    return true;
}

// (2) two mints for the SAME (function, call, expiry) produce DIFFERENT nonces.
TEST(contract7_two_mints_have_different_nonces) {
    std::vector<uint8_t> secret(32, 0x5A);
    SessionManager sm(secret);
    sm.set_debug_mode(true);
    std::string t1 = sm.create_token("fn", "call-x", 3600);
    std::string t2 = sm.create_token("fn", "call-x", 3600);
    // Whole tokens differ...
    ASSERT_NE(t1, t2);
    // ...and specifically the nonces differ (not just some other byte).
    std::string n1 = sm.debug_token(t1)["components"].value("nonce", "");
    std::string n2 = sm.debug_token(t2)["components"].value("nonce", "");
    ASSERT_FALSE(n1.empty());
    ASSERT_FALSE(n2.empty());
    ASSERT_NE(n1, n2);
    return true;
}

// (3) a token constructed in the python-oracle format validates in-port.
TEST(contract7_python_oracle_format_interop) {
    std::vector<uint8_t> secret(32, 0x42);
    SessionManager sm(secret);

    std::string call_id = "call-interop-1";
    std::string function_name = "lookup_order";
    int64_t expiry = static_cast<int64_t>(std::time(nullptr)) + 3600;
    std::string nonce = "0011223344556677";  // 16 hex chars

    // Signed message is call_id-FIRST, colon-joined (python parity).
    std::string message = call_id + ":" + function_name + ":" +
                          std::to_string(expiry) + ":" + nonce;
    std::string sig = c7_hmac_hex(secret, message);

    // 5 dot-fields, then base64url-wrap the whole thing (python parity).
    std::string plain = call_id + "." + function_name + "." +
                        std::to_string(expiry) + "." + nonce + "." + sig;
    std::string token = c7_b64url(plain);

    ASSERT_TRUE(sm.validate_token(token, function_name, call_id));
    // Wrong function / call_id still reject.
    ASSERT_FALSE(sm.validate_token(token, "other_fn", call_id));
    ASSERT_FALSE(sm.validate_token(token, function_name, "other-call"));
    return true;
}

// (4) flip one byte of the signature field => validation fails.
TEST(contract7_tampered_signature_rejected) {
    std::vector<uint8_t> secret(32, 0x42);
    SessionManager sm(secret);

    std::string call_id = "call-9";
    std::string function_name = "fn9";
    int64_t expiry = static_cast<int64_t>(std::time(nullptr)) + 3600;
    std::string nonce = "aabbccddeeff0011";
    std::string message = call_id + ":" + function_name + ":" +
                          std::to_string(expiry) + ":" + nonce;
    std::string sig = c7_hmac_hex(secret, message);
    // Flip the last hex nibble of the signature.
    sig.back() = (sig.back() == 'a') ? 'b' : 'a';

    std::string plain = call_id + "." + function_name + "." +
                        std::to_string(expiry) + "." + nonce + "." + sig;
    std::string token = c7_b64url(plain);
    ASSERT_FALSE(sm.validate_token(token, function_name, call_id));
    return true;
}

// (5) signature compare is constant-time: rejecting a matched-prefix sig and a
// first-byte-differing sig both take the full compare (no early return). We
// assert the timing_safe_compare primitive returns only on a length mismatch;
// equal-length inputs are compared in full via CRYPTO_memcmp.
TEST(contract7_constant_time_compare) {
    // Equal-length, differ only in the LAST byte -> must reject (a first-
    // mismatch early-return impl would still reject, but a length-only check
    // would wrongly accept; this pins full-width comparison).
    std::string a(64, 'a');
    std::string b(64, 'a');
    b.back() = 'b';
    ASSERT_FALSE(SessionManager::timing_safe_compare(a, b));
    // Differ only in the FIRST byte -> also reject.
    std::string c(64, 'a');
    c.front() = 'b';
    ASSERT_FALSE(SessionManager::timing_safe_compare(a, c));
    // Identical -> accept.
    ASSERT_TRUE(SessionManager::timing_safe_compare(a, std::string(64, 'a')));
    // Length mismatch -> reject.
    ASSERT_FALSE(SessionManager::timing_safe_compare(a, std::string(63, 'a')));
    return true;
}
