// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// token_interop_mint.cpp — the C++ port's TOKEN-INTEROP mint fixture for the
// cross-port checker (porting-sdk/scripts/diff_port_token_interop.py).
//
// The contract being proven is property 3 of the SWAIG tool-token contract: a
// token this port MINTS must validate under the REFERENCE's own decoder. The
// other two properties (that a token is minted at all; that the HMAC is keyed
// with the ``secret_key`` STRING's bytes) already had coverage — this one did
// not, and a port can pass both and still emit a token no other implementation
// accepts, in which case every secure tool call fails authentication in
// production.
//
// PROTOCOL: read the FIXED mint inputs from the environment (the checker owns
// them, so this fixture cannot drift from the values it is verified against),
// construct a ``SessionManager`` with that secret key, mint ONE token, and print
// JUST the token on stdout. Anything else goes to stderr.
//
// Run from the signalwire-cpp repo root, after building:
//
//   build/token_interop_mint

#include <cstdlib>
#include <iostream>
#include <string>

#include "signalwire/security/session_manager.hpp"

namespace {

// Read a required fixed mint input from the environment, or fail loud.
std::string required(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    std::cerr << name
              << " is not set — the TOKEN-INTEROP checker supplies the fixed mint "
                 "inputs in the environment; run this via "
                 "diff_port_token_interop.py --mint-cmd.\n";
    std::exit(1);
  }
  return std::string(value);
}

}  // namespace

int main() {
  const std::string secret_key = required("SW_TOKEN_INTEROP_SECRET_KEY");
  const std::string call_id = required("SW_TOKEN_INTEROP_CALL_ID");
  const std::string function_name = required("SW_TOKEN_INTEROP_FUNCTION_NAME");

  // Default expiry — the token must carry a FUTURE expiry, which the checker
  // verifies. The (int, const std::string&) constructor takes the reference's
  // ``secret_key`` STRING, whose bytes key the HMAC (NOT 32 raw bytes).
  const signalwire::security::SessionManager manager(900, secret_key);
  std::cout << manager.generate_token(function_name, call_id) << std::endl;
  return 0;
}
