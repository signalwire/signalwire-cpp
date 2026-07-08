#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace signalwire {
namespace security {

using json = nlohmann::json;

/// Manages HMAC-SHA256 based session tokens for secure SWAIG tool calls.
///
/// Token format: the token
/// is the base64url-encoding of the 5 dot-joined fields
/// ``{call_id}.{function_name}.{expiry}.{nonce}.{signature}`` where
/// ``signature = hex(hmac_sha256("{call_id}:{function_name}:{expiry}:{nonce}"))``
/// and ``nonce`` is 16 hex chars (``secrets.token_hex(8)``). Validation
/// base64url-decodes, splits the 5 fields, recomputes the HMAC, and compares in
/// CONSTANT time.
class SessionManager {
 public:
  /// Construct with a random 32-byte secret
  SessionManager();

  /// Construct with a specific secret (for testing)
  explicit SessionManager(const std::vector<uint8_t>& secret);

  /// Create a signed token for a function call
  /// @param function_name  The SWAIG function name
  /// @param call_id        The call identifier
  /// @param expiry_seconds Seconds until expiry (default 3600)
  /// @return The signed token string
  std::string create_token(const std::string& function_name, const std::string& call_id,
                           int expiry_seconds = 3600) const;

  /// Validate a token
  /// @param token          The token to validate
  /// @param function_name  Expected function name
  /// @param call_id        Expected call ID
  /// @return true if token is valid and not expired
  ///
  /// All three params are consumed read-only (parsed/compared, never
  /// stored), so they take std::string_view — consistent with the
  /// webhook_validator entry points in this module. [[nodiscard]]: the
  /// validity result is the reason to call it.
  [[nodiscard]] bool validate_token(std::string_view token, std::string_view function_name,
                                    std::string_view call_id) const;

  /// Timing-safe comparison of two byte sequences
  static bool timing_safe_compare(const std::string& a, const std::string& b);

  // ── Python-surface token aliases ─────────────────────────────────
  //
  // The reference exposes generate_token / create_tool_token as the minting
  // names and validate_tool_token as a back-compat alias of validate_token.
  // C++ keeps the existing create_token/validate_token wire format untouched
  // and projects the reference names onto it (matching the Java/Ruby ports).

  /// Mint a signed token — Python's ``generate_token``. Delegates to
  /// ``create_token`` with the configured default expiry.
  std::string generate_token(const std::string& function_name, const std::string& call_id) const;

  /// Alias of ``generate_token`` — Python's ``create_tool_token``.
  std::string create_tool_token(const std::string& function_name, const std::string& call_id) const;

  /// Back-compat alias of ``validate_token`` — Python's
  /// ``validate_tool_token(function_name, token, call_id)``. NOTE the
  /// reference parameter order differs from ``validate_token``; this method
  /// mirrors that order and delegates.
  [[nodiscard]] bool validate_tool_token(std::string_view function_name, std::string_view token,
                                         std::string_view call_id) const;

  // ── Session lifecycle ────────────────────────────
  //
  // The reference SessionManager is stateless w.r.t. sessions; this port
  // keeps a REAL per-call metadata store (like the Ruby/Java/TS ports) so
  // the get/set metadata pair round-trips, while activation stays a
  // success hook. State is guarded by a mutex so it is thread-safe.

  /// Return ``call_id`` when non-empty; otherwise mint a fresh URL-safe
  /// session id (mirrors ``secrets.token_urlsafe(16)``). Creates the
  /// session's metadata entry.
  std::string create_session(const std::string& call_id = "");

  /// Legacy lifecycle hook — the manager is stateless w.r.t. activation,
  /// so this always reports success (creates the entry if absent).
  bool activate_session(const std::string& call_id);

  /// End a session: clears any metadata accumulated for ``call_id`` and
  /// reports success.
  bool end_session(const std::string& call_id);

  /// Fetch the metadata recorded for ``call_id``. Returns an empty (never
  /// null) object for unknown sessions; returns a copy so callers cannot
  /// mutate the internal store.
  json get_session_metadata(const std::string& call_id) const;

  /// Store a single ``key``/``value`` pair in ``call_id``'s metadata,
  /// merging with anything already recorded for that session.
  bool set_session_metadata(const std::string& call_id, const std::string& key, const json& value);

  /// Enable/disable token-internals decoding in ``debug_token`` (off by
  /// default). Mirrors the authoritative reference's ``_debug_mode`` gate.
  void set_debug_mode(bool enabled);

  /// Decode a token's components for inspection WITHOUT validating it.
  /// Requires ``set_debug_mode(true)`` first; otherwise returns
  /// ``{"error": "debug mode not enabled"}`` (matches the authoritative
  /// reference). On a well-formed token returns
  /// ``{valid_format, components, status}`` (call_id/signature truncated to
  /// 8 chars); on a malformed token returns ``{valid_format:false, ...}``.
  /// Decodes this port's token format
  /// (``base64(function:call_id:expiry).signature``).
  [[nodiscard]] json debug_token(const std::string& token) const;

 private:
  /// Compute HMAC-SHA256 of data using the secret
  std::string hmac_sha256(const std::string& data) const;

  /// Base64 encode
  static std::string base64_encode(const std::string& data);

  /// Base64 decode
  static std::string base64_decode(const std::string& encoded);

  /// Base64url encode (URL-safe alphabet, no padding) — matches Python's
  /// ``base64.urlsafe_b64encode(...).decode()`` used to wrap the whole token.
  static std::string base64url_encode(const std::string& data);

  /// Base64url decode (URL-safe alphabet, tolerates missing padding) —
  /// inverse of ``base64url_encode`` / Python's ``urlsafe_b64decode``.
  static std::string base64url_decode(const std::string& encoded);

  /// Hex encode
  static std::string hex_encode(const std::vector<uint8_t>& data);

  /// Generate a random nonce of ``bytes`` bytes as a hex string — mirrors
  /// Python's ``secrets.token_hex(bytes)`` (``2*bytes`` hex chars).
  static std::string token_hex(int bytes);

  /// Get current Unix timestamp
  static int64_t current_timestamp();

  std::vector<uint8_t> secret_;
  /// Default token lifetime in seconds, used by generate_token /
  /// create_tool_token (create_token still accepts an explicit override).
  int default_expiry_secs_ = 3600;

  /// Per-session metadata store: call_id -> (key -> value). Guarded by
  /// metadata_mutex_. A real store (not the reference's stateless no-op) so
  /// the get/set metadata pair round-trips.
  mutable std::mutex metadata_mutex_;
  std::map<std::string, json> session_metadata_;

  /// When false (the default), debug_token declines to decode internals.
  bool debug_mode_ = false;
};

}  // namespace security
}  // namespace signalwire
