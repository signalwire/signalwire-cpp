// wire_dump.cpp — the C++ port's WIRE-CRYPTO dump program for the cross-port
// wire differ (porting-sdk/scripts/diff_port_wire.py).
//
// It runs the shared wire_crypto corpus against the C++ SDK's native security
// surface (SessionManager tokens, webhook-signature validation, redact/filter
// helpers) and prints ONE JSON object mapping
//
//     case-id -> observable-artifact
//
// to stdout. The differ canonicalizes both sides and byte-compares each entry
// against the python oracle. Only stdout carries JSON; logs go to stderr.
//
// The corpus sentinels (the oracle token / tampered token / oracle webhook
// signature) are materialized here from the fixed per-case SECRET exactly as
// the oracle materializes them, using OpenSSL directly (like the Go reference
// dump signalwire-go/cmd/wire-dump, which uses crypto/hmac). The SDK does not
// expose a standalone HMAC/base64url helper, so — matching Go — the dump builds
// the oracle-format token itself and hands it to the SDK's validate_token.
//
// Build: a CMake target `wire_dump` (see CMakeLists.txt). Mirrors the Go dump.

#include <openssl/hmac.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "signalwire/security/security_utils.hpp"
#include "signalwire/security/session_manager.hpp"
#include "signalwire/security/webhook_validator.hpp"

using json = nlohmann::json;
using signalwire::security::SessionManager;
using signalwire::security::ValidateWebhookSignature;
using signalwire::security::security_utils::FilterSensitiveHeaders;
using signalwire::security::security_utils::RedactUrl;

namespace {

// SECRET mirrors wire_crypto_corpus.SECRET ("a" * 64).
const std::string kSecret(64, 'a');

// Fixed deterministic oracle materials (mirror diff_port_wire).
const int64_t kOracleExpiry = 9999999999;       // far-future expiry
const char* kOracleNonce = "0123456789abcdef";  // fixed 16-hex nonce

// hex(HMAC(evp, key, data)).
std::string hmac_hex(const EVP_MD* evp, const std::string& key, const std::string& data) {
  std::array<unsigned char, EVP_MAX_MD_SIZE> md{};
  unsigned int md_len = 0;
  HMAC(evp, key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), md.data(), &md_len);
  static const char* kHex = "0123456789abcdef";
  std::string out;
  out.reserve(md_len * 2);
  for (unsigned int i = 0; i < md_len; ++i) {
    out.push_back(kHex[md[i] >> 4]);
    out.push_back(kHex[md[i] & 0x0F]);
  }
  return out;
}

// base64url (URL-safe alphabet, WITH padding — matches Python's
// base64.urlsafe_b64encode used to wrap the whole token, which the Go dump
// mirrors via base64.URLEncoding).
std::string base64url_encode(const std::string& in) {
  static const char* kAlpha = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
  std::string out;
  size_t i = 0;
  for (; i + 2 < in.size(); i += 3) {
    uint32_t n = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8) |
                 static_cast<uint8_t>(in[i + 2]);
    out.push_back(kAlpha[(n >> 18) & 63]);
    out.push_back(kAlpha[(n >> 12) & 63]);
    out.push_back(kAlpha[(n >> 6) & 63]);
    out.push_back(kAlpha[n & 63]);
  }
  size_t rem = in.size() - i;
  if (rem == 1) {
    uint32_t n = static_cast<uint8_t>(in[i]) << 16;
    out.push_back(kAlpha[(n >> 18) & 63]);
    out.push_back(kAlpha[(n >> 12) & 63]);
    out.push_back('=');
    out.push_back('=');
  } else if (rem == 2) {
    uint32_t n = (static_cast<uint8_t>(in[i]) << 16) | (static_cast<uint8_t>(in[i + 1]) << 8);
    out.push_back(kAlpha[(n >> 18) & 63]);
    out.push_back(kAlpha[(n >> 12) & 63]);
    out.push_back(kAlpha[(n >> 6) & 63]);
    out.push_back('=');
  }
  return out;
}

// oracle_token builds a token in the SDK wire format
// (call_id.fn.expiry.nonce.sig, base64url) from the fixed SECRET — the mirror
// of diff_port_wire._oracle_token and the Go dump's oracleToken.
std::string oracle_token(const std::string& call_id, const std::string& fn) {
  std::string msg = call_id + ":" + fn + ":" + std::to_string(kOracleExpiry) + ":" + kOracleNonce;
  std::string sig = hmac_hex(EVP_sha256(), kSecret, msg);
  std::string raw =
      call_id + "." + fn + "." + std::to_string(kOracleExpiry) + "." + kOracleNonce + "." + sig;
  return base64url_encode(raw);
}

// oracle_sig: hex(HMAC-SHA1(key, url+body)) — the correct webhook signature.
std::string oracle_sig(const std::string& url, const std::string& body, const std::string& key) {
  return hmac_hex(EVP_sha1(), key, url + body);
}

}  // namespace

int main() {
  json out = json::object();

  std::vector<uint8_t> secret_bytes(kSecret.begin(), kSecret.end());
  SessionManager sm(secret_bytes);

  // token_format: generate a token via the SDK, decode its wire-format fields.
  {
    std::string token = sm.generate_token("my_func", "call_1");
    // Decode base64url -> raw dotted string. The SDK produces a padded
    // base64url token; nlohmann/std don't decode base64url, so decode inline.
    // Simpler: re-derive fields by regenerating is impossible (random nonce),
    // so decode the token the SDK emitted.
    auto b64url_decode = [](const std::string& s) {
      auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
      };
      std::string decoded;
      uint32_t buf = 0;
      int bits = 0;
      for (char c : s) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
          bits -= 8;
          decoded.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
      }
      return decoded;
    };
    std::string raw = b64url_decode(token);
    std::vector<std::string> parts;
    {
      std::string cur;
      for (char c : raw) {
        if (c == '.') {
          parts.push_back(cur);
          cur.clear();
        } else {
          cur.push_back(c);
        }
      }
      parts.push_back(cur);
    }
    std::string nonce = parts.size() > 3 ? parts[3] : "";
    bool nonce_is_hex = parts.size() > 3;
    for (char c : nonce) {
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
        nonce_is_hex = false;
        break;
      }
    }
    json tf = json::object();
    tf["n_fields"] = parts.size();
    tf["call_id"] = parts.size() > 0 ? json(parts[0]) : json(nullptr);
    tf["function_name"] = parts.size() > 1 ? json(parts[1]) : json(nullptr);
    tf["nonce_len"] = nonce.size();
    tf["nonce_is_hex"] = nonce_is_hex;
    out["token_format"] = tf;
  }

  // token_nonce_distinct: two generations must differ (random nonce).
  {
    std::string n1 = sm.generate_token("f", "c");
    std::string n2 = sm.generate_token("f", "c");
    out["token_nonce_distinct"] = json{{"distinct", n1 != n2}};
  }

  // token_interop: validate an oracle-format token built from SECRET.
  {
    std::string tok = oracle_token("oracle_call", "oracle_fn");
    out["token_interop"] = json{{"valid", sm.validate_token(tok, "oracle_fn", "oracle_call")}};
  }

  // token_tamper_rejected: a one-byte-flipped signature must fail. Build the
  // oracle token, decode, flip the first byte of the signature (after the last
  // '.'), re-encode — the mirror of Go's tamperedToken / _tampered_token.
  {
    std::string tok = oracle_token("c", "f");
    // decode
    auto b64url_decode = [](const std::string& s) {
      auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '-') return 62;
        if (c == '_') return 63;
        return -1;
      };
      std::string decoded;
      uint32_t buf = 0;
      int bits = 0;
      for (char c : s) {
        if (c == '=') break;
        int v = val(c);
        if (v < 0) continue;
        buf = (buf << 6) | static_cast<uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
          bits -= 8;
          decoded.push_back(static_cast<char>((buf >> bits) & 0xFF));
        }
      }
      return decoded;
    };
    std::string raw = b64url_decode(tok);
    size_t last = raw.rfind('.');
    if (last != std::string::npos && last + 1 < raw.size()) {
      char& b = raw[last + 1];
      b = (b == 'f') ? 'e' : 'f';
    }
    std::string tampered = base64url_encode(raw);
    out["token_tamper_rejected"] = json{{"valid", sm.validate_token(tampered, "f", "c")}};
  }

  // wire_validate_webhook_signature: correct HMAC-SHA1 -> valid.
  const std::string wh_url = "https://example.com/hook";
  const std::string wh_body = R"({"event":"call.created"})";
  out["wire_validate_webhook_signature"] =
      json{{"valid", ValidateWebhookSignature(kSecret, oracle_sig(wh_url, wh_body, kSecret), wh_url,
                                              wh_body)}};

  // wire_validate_webhook_signature_bad: wrong sig -> invalid.
  {
    std::string bad;
    for (int i = 0; i < 8; ++i) bad += "deadbeef";
    out["wire_validate_webhook_signature_bad"] =
        json{{"valid", ValidateWebhookSignature(kSecret, bad, wh_url, wh_body)}};
  }

  // wire_redact_url: credentials + token redacted, structure preserved.
  out["wire_redact_url"] =
      json{{"redacted", RedactUrl("https://user:s3cr3t@api.signalwire.com/path?token=abc")}};

  // wire_filter_sensitive_headers: authorization + x-api-key dropped,
  // content-type kept.
  {
    std::map<std::string, std::string> headers = {
        {"Authorization", "Bearer x"}, {"X-Api-Key", "y"}, {"Content-Type", "application/json"}};
    auto filtered = FilterSensitiveHeaders(headers);
    json fj = json::object();
    for (const auto& [k, v] : filtered) fj[k] = v;
    out["wire_filter_sensitive_headers"] = json{{"filtered", fj}};
  }

  std::cout << out.dump() << "\n";
  return 0;
}
