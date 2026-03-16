#pragma once

#include <string>
#include <optional>
#include <vector>
#include <cstdint>

namespace signalwire {
namespace security {

/// Manages HMAC-SHA256 based session tokens for secure SWAIG tool calls.
///
/// Token format: base64(functionName:callID:expiryTimestamp) + "." + hex(hmac_signature)
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
    std::string create_token(const std::string& function_name,
                              const std::string& call_id,
                              int expiry_seconds = 3600) const;

    /// Validate a token
    /// @param token          The token to validate
    /// @param function_name  Expected function name
    /// @param call_id        Expected call ID
    /// @return true if token is valid and not expired
    bool validate_token(const std::string& token,
                        const std::string& function_name,
                        const std::string& call_id) const;

    /// Timing-safe comparison of two byte sequences
    static bool timing_safe_compare(const std::string& a, const std::string& b);

private:
    /// Compute HMAC-SHA256 of data using the secret
    std::string hmac_sha256(const std::string& data) const;

    /// Base64 encode
    static std::string base64_encode(const std::string& data);

    /// Base64 decode
    static std::string base64_decode(const std::string& encoded);

    /// Hex encode
    static std::string hex_encode(const std::vector<uint8_t>& data);

    /// Get current Unix timestamp
    static int64_t current_timestamp();

    std::vector<uint8_t> secret_;
};

} // namespace security
} // namespace signalwire
