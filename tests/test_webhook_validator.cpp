// Webhook signature validation tests — translated from
// signalwire-python/tests/unit/security/test_webhook_validator.py
//
// Cross-language SDK contract: every port must implement Scheme A (hex
// HMAC-SHA1 over url+rawBody for JSON/RELAY) and Scheme B (base64
// HMAC-SHA1 over url+sortedFormParams for cXML/Compat) per
// porting-sdk/webhooks.md. The three canonical vectors below come
// straight from the spec; if they break, this port has a real bug —
// DO NOT relax them.

#include "signalwire/security/webhook_validator.hpp"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace signalwire::security;

namespace {

// ---------------------------------------------------------------------------
// Local test helpers — reproduce signatures so we can exercise edge cases
// without baking pre-computed values into every test.
// ---------------------------------------------------------------------------

std::string local_b64(const std::string& data) {
    static const char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
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
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string local_hmac_sha1_b64(const std::string& key, const std::string& msg) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    HMAC(EVP_sha1(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
         out, &out_len);
    return local_b64(std::string(reinterpret_cast<const char*>(out), out_len));
}

std::string local_hmac_sha1_hex(const std::string& key, const std::string& msg) {
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int out_len = 0;
    HMAC(EVP_sha1(),
         key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(msg.data()), msg.size(),
         out, &out_len);
    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < out_len; ++i) {
        ss << std::setw(2) << static_cast<int>(out[i]);
    }
    return ss.str();
}

// ---------------------------------------------------------------------------
// Canonical vectors from porting-sdk/webhooks.md
// ---------------------------------------------------------------------------

const char* VEC_A_KEY  = "PSKtest1234567890abcdef";
const char* VEC_A_URL  = "https://example.ngrok.io/webhook";
const char* VEC_A_BODY = R"({"event":"call.state","params":{"call_id":"abc-123","state":"answered"}})";
const char* VEC_A_EXPECTED = "c3c08c1fefaf9ee198a100d5906765a6f394bf0f";

const char* VEC_B_KEY = "12345";
const char* VEC_B_URL = "https://mycompany.com/myapp.php?foo=1&bar=2";
// Form body that round-trips through urldecode back to the canonical
// (key, value) pairs Scheme B sorts and concatenates.
const char* VEC_B_BODY =
    "CallSid=CA1234567890ABCDE"
    "&Caller=%2B14158675309"
    "&Digits=1234"
    "&From=%2B14158675309"
    "&To=%2B18005551212";
const char* VEC_B_EXPECTED = "RSOYDt4T1cUTdK1PDd93/VVr8B8=";

const char* VEC_C_KEY  = "PSKtest1234567890abcdef";
const char* VEC_C_BODY = R"({"event":"call.state"})";
const char* VEC_C_URL =
    "https://example.ngrok.io/webhook?bodySHA256="
    "69f3cbfc18e386ef8236cb7008cd5a54b7fed637a8cb3373b5a1591d7f0fd5f4";
const char* VEC_C_EXPECTED = "dfO9ek8mxyFtn2nMz24plPmPfIY=";

FormParams vec_b_params() {
    return {
        {"CallSid", {"CA1234567890ABCDE"}},
        {"Caller",  {"+14158675309"}},
        {"Digits",  {"1234"}},
        {"From",    {"+14158675309"}},
        {"To",      {"+18005551212"}},
    };
}

} // namespace

// ===========================================================================
// Scheme A — RELAY/JSON (hex)
// ===========================================================================

TEST(webhook_scheme_a_positive_canonical_vector) {
    // Vector A: known JSON body + URL + key produces the known hex digest.
    ASSERT_TRUE(ValidateWebhookSignature(VEC_A_KEY, VEC_A_EXPECTED, VEC_A_URL, VEC_A_BODY));
    return true;
}

TEST(webhook_scheme_a_negative_tampered_body) {
    // Same key/url, body mutated → returns False.
    std::string tampered = VEC_A_BODY;
    auto pos = tampered.find("answered");
    ASSERT_TRUE(pos != std::string::npos);
    tampered.replace(pos, 8, "ringing!");  // same length, different content
    ASSERT_FALSE(ValidateWebhookSignature(VEC_A_KEY, VEC_A_EXPECTED, VEC_A_URL, tampered));
    return true;
}

TEST(webhook_scheme_a_negative_wrong_key) {
    ASSERT_FALSE(ValidateWebhookSignature("wrong-key", VEC_A_EXPECTED, VEC_A_URL, VEC_A_BODY));
    return true;
}

TEST(webhook_scheme_a_negative_wrong_url) {
    ASSERT_FALSE(ValidateWebhookSignature(VEC_A_KEY, VEC_A_EXPECTED,
                                          "https://example.ngrok.io/different", VEC_A_BODY));
    return true;
}

// ===========================================================================
// Scheme B — Compat/cXML (base64 form)
// ===========================================================================

TEST(webhook_scheme_b_positive_canonical_form_vector_via_raw_body) {
    // Form params via raw body → matches the canonical Twilio digest.
    ASSERT_TRUE(ValidateWebhookSignature(VEC_B_KEY, VEC_B_EXPECTED, VEC_B_URL, VEC_B_BODY));
    return true;
}

TEST(webhook_scheme_b_positive_via_validate_request_with_form_params) {
    // ValidateRequest(..., FormParams) goes straight to Scheme B with the
    // pre-parsed map.
    ASSERT_TRUE(ValidateRequest(VEC_B_KEY, VEC_B_EXPECTED, VEC_B_URL,
                                ParamsOrBody{vec_b_params()}));
    return true;
}

TEST(webhook_scheme_b_body_sha256_canonical_vector) {
    // Vector C: JSON body on compat surface, signature over URL with bodySHA256.
    ASSERT_TRUE(ValidateWebhookSignature(VEC_C_KEY, VEC_C_EXPECTED, VEC_C_URL, VEC_C_BODY));
    return true;
}

TEST(webhook_scheme_b_body_sha256_mismatch_rejected) {
    // bodySHA256 in the URL no longer matches the body → reject even though
    // the HMAC-over-URL would otherwise pass.
    ASSERT_FALSE(ValidateWebhookSignature(VEC_C_KEY, VEC_C_EXPECTED, VEC_C_URL,
                                          R"({"event":"DIFFERENT"})"));
    return true;
}

// ===========================================================================
// URL port normalization (REQUIRED per spec)
// ===========================================================================

TEST(webhook_url_with_port_accepted_when_request_has_no_port) {
    // Backend signed with :443 — request URL has no port → accept.
    std::string key = "test-key";
    std::string url_with_port = "https://example.com:443/webhook";
    std::string url_without_port = "https://example.com/webhook";
    std::string sig = local_hmac_sha1_b64(key, url_with_port);
    ASSERT_TRUE(ValidateWebhookSignature(key, sig, url_without_port, "{}"));
    return true;
}

TEST(webhook_url_without_port_accepted_when_request_has_standard_port) {
    // Backend signed without port — request URL has :443 → accept.
    std::string key = "test-key";
    std::string url_with_port = "https://example.com:443/webhook";
    std::string url_without_port = "https://example.com/webhook";
    std::string sig = local_hmac_sha1_b64(key, url_without_port);
    ASSERT_TRUE(ValidateWebhookSignature(key, sig, url_with_port, "{}"));
    return true;
}

TEST(webhook_url_http_port_80_normalization) {
    // http + :80 mirrors https + :443.
    std::string key = "test-key";
    std::string url_with_port = "http://example.com:80/path";
    std::string url_without_port = "http://example.com/path";
    std::string sig = local_hmac_sha1_b64(key, url_with_port);
    ASSERT_TRUE(ValidateWebhookSignature(key, sig, url_without_port, ""));
    return true;
}

// ===========================================================================
// Repeated form keys
// ===========================================================================

TEST(webhook_repeated_keys_concat_in_submission_order) {
    // To=a&To=b → signing string url + "ToaTob".
    std::string key = "test-key";
    std::string url = "https://example.com/hook";
    std::string body = "To=a&To=b";
    std::string sig = local_hmac_sha1_b64(key, url + "ToaTob");
    ASSERT_TRUE(ValidateWebhookSignature(key, sig, url, body));
    return true;
}

TEST(webhook_repeated_keys_swapped_order_is_a_different_signature) {
    // To=b&To=a is a different submission — rejecting it proves we
    // preserve order within repeated keys instead of lexically sorting.
    std::string key = "test-key";
    std::string url = "https://example.com/hook";
    std::string sig_for_ab = local_hmac_sha1_b64(key, url + "ToaTob");
    ASSERT_TRUE(ValidateWebhookSignature(key, sig_for_ab, url, "To=a&To=b"));
    ASSERT_FALSE(ValidateWebhookSignature(key, sig_for_ab, url, "To=b&To=a"));
    return true;
}

TEST(webhook_repeated_keys_via_form_params_struct) {
    // Same idea, exercised through ValidateRequest with FormParams.
    std::string key = "test-key";
    std::string url = "https://example.com/hook";
    std::string sig = local_hmac_sha1_b64(key, url + "ToaTob");
    FormParams params = {{"To", {"a", "b"}}};
    ASSERT_TRUE(ValidateRequest(key, sig, url, ParamsOrBody{params}));
    // Swap value order under the same key — must NOT match.
    FormParams swapped = {{"To", {"b", "a"}}};
    ASSERT_FALSE(ValidateRequest(key, sig, url, ParamsOrBody{swapped}));
    return true;
}

// ===========================================================================
// Error modes
// ===========================================================================

TEST(webhook_missing_signature_returns_false) {
    // Empty signature header → False, no exception.
    ASSERT_FALSE(ValidateWebhookSignature(VEC_A_KEY, "", VEC_A_URL, VEC_A_BODY));
    return true;
}

TEST(webhook_missing_signing_key_throws) {
    // Empty signing key → invalid_argument (programming error).
    ASSERT_THROWS(ValidateWebhookSignature("", "sig", VEC_A_URL, VEC_A_BODY));
    return true;
}

TEST(webhook_missing_signing_key_throws_in_validate_request) {
    ASSERT_THROWS(ValidateRequest("", "sig", VEC_A_URL,
                                  ParamsOrBody{std::string{VEC_A_BODY}}));
    return true;
}

TEST(webhook_malformed_signature_returns_false_without_throwing) {
    // Wrong length, weird chars, base64 noise — none should throw.
    const std::vector<std::string> garbage = {"xyz", "!!!!",
                                              std::string(100, 'a'),
                                              "%%notbase64%%"};
    for (const auto& g : garbage) {
        ASSERT_FALSE(ValidateWebhookSignature(VEC_A_KEY, g, VEC_A_URL, VEC_A_BODY));
    }
    return true;
}

TEST(webhook_validate_request_missing_signature_returns_false) {
    ASSERT_FALSE(ValidateRequest(VEC_B_KEY, "", VEC_B_URL,
                                 ParamsOrBody{vec_b_params()}));
    return true;
}

// ===========================================================================
// validate_request dispatch
// ===========================================================================

TEST(webhook_validate_request_with_string_arg_delegates_to_combined) {
    // String 4th arg behaves identically to ValidateWebhookSignature.
    ASSERT_TRUE(ValidateRequest(VEC_A_KEY, VEC_A_EXPECTED, VEC_A_URL,
                                ParamsOrBody{std::string{VEC_A_BODY}}));
    return true;
}

TEST(webhook_validate_request_with_map_arg_runs_scheme_b_directly) {
    ASSERT_TRUE(ValidateRequest(VEC_B_KEY, VEC_B_EXPECTED, VEC_B_URL,
                                ParamsOrBody{vec_b_params()}));
    return true;
}

// ===========================================================================
// Constant-time compare — read the source rather than try to time it.
// Timing tests are flaky in CI; the spec names the function to use.
// ===========================================================================

TEST(webhook_validator_source_uses_crypto_memcmp) {
    std::ifstream f(std::string(PROJECT_SOURCE_DIR) +
                    "/src/security/webhook_validator.cpp");
    ASSERT_TRUE(f.is_open());
    std::stringstream buf;
    buf << f.rdbuf();
    std::string src = buf.str();
    // Must use OpenSSL's CRYPTO_memcmp — the porting-sdk webhook spec
    // explicitly names it for C++ ports. Plain == on the digest leaks
    // the secret over repeated requests.
    ASSERT_TRUE(src.find("CRYPTO_memcmp") != std::string::npos);
    return true;
}

// ===========================================================================
// Additional sanity: signature recomputed locally matches
// ===========================================================================

TEST(webhook_scheme_a_recomputed_matches) {
    // Independent reconstruction of the Scheme A digest using OpenSSL HMAC.
    std::string key = "PSKtest1234567890abcdef";
    std::string url = "https://example.ngrok.io/webhook";
    std::string body = R"({"event":"call.state","params":{"call_id":"abc-123","state":"answered"}})";
    std::string expected = local_hmac_sha1_hex(key, url + body);
    ASSERT_TRUE(ValidateWebhookSignature(key, expected, url, body));
    // Sanity: matches the spec value
    ASSERT_TRUE(expected == VEC_A_EXPECTED);
    return true;
}

TEST(webhook_scheme_b_form_concat_matches_canonical_signing_string) {
    // Independent computation: build the canonical signing string
    // url + "CallSidCA...CallerToabc..." and verify it produces the
    // canonical base64 digest.
    std::string key = "12345";
    std::string url = "https://mycompany.com/myapp.php?foo=1&bar=2";
    std::string concat =
        std::string(url) +
        "CallSid" "CA1234567890ABCDE" +
        "Caller" "+14158675309" +
        "Digits" "1234" +
        "From" "+14158675309" +
        "To" "+18005551212";
    std::string expected = local_hmac_sha1_b64(key, concat);
    // Spec value must round-trip
    ASSERT_TRUE(expected == VEC_B_EXPECTED);
    // And the validator must accept it
    ASSERT_TRUE(ValidateRequest(key, expected, url, ParamsOrBody{vec_b_params()}));
    return true;
}
