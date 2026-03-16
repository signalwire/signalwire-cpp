// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

// Event struct implementations are inline in relay_event.hpp.
// This translation unit ensures the header compiles independently.

#include "signalwire/relay/relay_event.hpp"

namespace signalwire {
namespace relay {

// Explicit template instantiation is not needed for these types.
// All parsing logic lives in the static from_json / from_relay_event methods
// defined inline in the header.

} // namespace relay
} // namespace signalwire
