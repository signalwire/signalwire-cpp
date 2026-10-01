// Copyright (c) 2026 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <string>

namespace httplib {
class Server;
}

namespace signalwire {
namespace server {

/// A mountable set of routes: called with the server to register them on and
/// the path prefix (no trailing slash, possibly empty) to register them under.
/// AgentBase::mount() takes one; ai_chat::ChatGateway::router() and
/// ai_chat::HandoffRouter::router() return one.
using Router = std::function<void(httplib::Server& server, const std::string& prefix)>;

}  // namespace server
}  // namespace signalwire
