// Copyright (c) 2025 SignalWire
//
// Licensed under the MIT License.
// See LICENSE file in the project root for full license information.

#pragma once

namespace signalwire {
namespace utils {

/**
 * Cross-language SDK contract: `signalwire.utils.is_serverless_mode`
 * returns `true` whenever the SDK is running inside any short-lived /
 * event-driven invocation environment (anything other than `"server"`).
 *
 * Mirrors `signalwire.utils.is_serverless_mode` in the Python reference.
 *
 * @return `true` unless the detected mode is `"server"`.
 */
bool is_serverless_mode();

}  // namespace utils
}  // namespace signalwire
