// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#include "signalwire/utils/serverless.hpp"

#include "signalwire/core/logging_config.hpp"

namespace signalwire {
namespace utils {

bool is_serverless_mode() {
    return signalwire::core::logging_config::get_execution_mode() != "server";
}

}  // namespace utils
}  // namespace signalwire
