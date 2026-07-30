// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// secret_scrub_dump.cpp — the C++ port's SECRET-SCRUB-LIVE (PSDK-5) dump
// program for the cross-port behavioral differ
// (porting-sdk/scripts/diff_port_secret_scrub.py, corpus
// porting-sdk/scripts/secret_scrub_corpus.py).
//
// The BEHAVIORAL leg of the credential-hygiene contract: where the static
// SECRET-SCRUB gate greps for the raw-frame-log SHAPE, this drives the RELAY
// client through a REAL connect + an inbound ``signalwire.authorization.state``
// re-auth frame at ``SIGNALWIRE_LOG_LEVEL=debug`` using the fixture sentinels,
// captures EVERYTHING the process writes, and asserts none of the sentinels
// appear verbatim:
//
//   project             = PJ-TESTLEAK   (the outbound connect frame's
//                                        authentication.project)
//   token               = PT-TESTLEAK   (…authentication.token)
//   authorization_state = AENC-TESTLEAK (the inbound re-auth blob)
//
// The artifact is a per-sentinel ``{leaked: bool}`` derived from the ACTUALLY
// captured output, not from the source, so it is unfakeable: this program cannot
// report ``leaked: false`` while the library literally printed ``PT-TESTLEAK``.
//
// ## How the capture works (and why it is at the fd level)
//
// The C++ logger writes debug/info to ``std::cout`` and warn/error to
// ``std::cerr``, and the vendored IXWebSocket transport can write to either. A
// stream-buffer swap would miss anything that writes to the OS file descriptor
// directly. So the drive runs with fd 1 and fd 2 both redirected into a temp
// file: whatever the process emits during the drive lands there regardless of
// how it reached the descriptor. After the drive the descriptors are restored,
// the capture is classified, and it is FORWARDED to the real stderr (so the
// differ's own subprocess-stderr capture sees the identical bytes) while stdout
// carries only the JSON.
//
// The temp file is created inside the repo-local ``.sw-tmp/`` (derived from the
// binary's own location), never a machine-wide temp dir.
//
// Protocol: stdout = ONE JSON object mapping corpus sentinel id ->
// {leaked: bool}.
//
// Build: the CMake target `secret_scrub_dump`; the differ invokes
// `build/secret_scrub_dump`.

#include <arpa/inet.h>
#include <fcntl.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "signalwire/logging.hpp"
#include "signalwire/relay/client.hpp"

using json = nlohmann::json;
namespace relay = signalwire::relay;

namespace {

// Fixture sentinels — byte-identical to
// porting-sdk/scripts/secret_scrub_corpus.py.
const char* kProject = "PJ-TESTLEAK";
const char* kToken = "PT-TESTLEAK";
const char* kAuthorizationState = "AENC-TESTLEAK";

int pick_free_port() {
  int sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return 0;
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  int port = 0;
  if (bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
    socklen_t len = sizeof(addr);
    if (getsockname(sock, reinterpret_cast<sockaddr*>(&addr), &len) == 0) {
      port = ntohs(addr.sin_port);
    }
  }
  close(sock);
  return port;
}

// ScrubWS — an in-process WS server that (a) ANSWERS signalwire.connect so the
// outbound connect frame carrying the sentinel project/token is actually sent
// (and therefore actually reaches whatever log sites exist), and (b) then PUSHES
// a signalwire.authorization.state event carrying the sentinel blob so the
// inbound frame is actually received and processed. This drives BOTH credential
// log sites with the sentinels present — the same two sites the python oracle
// drives via its _FakeWS.
class ScrubWS {
 public:
  ScrubWS() {
    port_ = pick_free_port();
    server_ = std::make_unique<ix::WebSocketServer>(port_, "127.0.0.1");
    server_->setOnClientMessageCallback([this](const std::shared_ptr<ix::ConnectionState>&,
                                               ix::WebSocket& ws,
                                               const ix::WebSocketMessagePtr& msg) {
      if (msg->type == ix::WebSocketMessageType::Message) {
        handle(ws, msg->str);
      }
    });
    server_->disablePerMessageDeflate();
    server_->listen();
    server_->start();
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
  }
  ~ScrubWS() {
    if (server_) {
      server_->stop();
    }
  }
  ScrubWS(const ScrubWS&) = delete;
  ScrubWS& operator=(const ScrubWS&) = delete;

  std::string host() const { return "127.0.0.1:" + std::to_string(port_); }
  bool saw_connect() const { return saw_connect_.load(); }
  bool pushed_reauth() const { return pushed_reauth_.load(); }

 private:
  void handle(ix::WebSocket& ws, const std::string& raw) {
    json msg = json::parse(raw, nullptr, false);
    if (msg.is_discarded() || !msg.is_object() || !msg.contains("method")) {
      return;
    }
    const std::string id = msg.value("id", "");
    const std::string method = msg.value("method", "");

    if (method == "signalwire.connect") {
      saw_connect_ = true;
      ws.sendText(json{
          {"jsonrpc", "2.0"},
          {"id", id},
          {"result", {{"protocol", "default"}, {"identity", "i"}, {"sessionid", "sess-scrub"}}}}
                      .dump());
      // The inbound re-auth blob — the payload a debug ``<< {raw}`` log site
      // would leak.
      ws.sendText(json{{"jsonrpc", "2.0"},
                       {"method", "signalwire.event"},
                       {"params",
                        {{"event_type", "signalwire.authorization.state"},
                         {"params", {{"authorization_state", kAuthorizationState}}}}}}
                      .dump());
      pushed_reauth_ = true;
      return;
    }
    ws.sendText(json{{"jsonrpc", "2.0"}, {"id", id}, {"result", {{"code", "200"}}}}.dump());
  }

  std::unique_ptr<ix::WebSocketServer> server_;
  int port_ = 0;
  std::atomic<bool> saw_connect_{false};
  std::atomic<bool> pushed_reauth_{false};
};

// A repo-local scratch dir for the capture file, derived from the running
// binary's location (build/ -> <repo>/.sw-tmp). Never a machine-wide temp dir.
std::filesystem::path scratch_dir() {
  std::error_code ec;
  std::filesystem::path base = std::filesystem::current_path(ec);
  if (ec) {
    base = ".";
  }
  // The dump runs with cwd = the port root (the differ's contract), but tolerate
  // being run from build/ too.
  if (base.filename() == "build") {
    base = base.parent_path();
  }
  std::filesystem::path dir = base / ".sw-tmp";
  std::filesystem::create_directories(dir, ec);
  return dir;
}

// Drive the connect + re-auth with fd 1 AND fd 2 redirected into `capture_path`,
// so everything the process emits during the drive is observable regardless of
// which stream or API produced it. Returns the captured text.
std::string drive_and_capture(const std::filesystem::path& capture_path) {
  const int saved_out = dup(1);
  const int saved_err = dup(2);
  const int cap = open(capture_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
  if (cap < 0 || saved_out < 0 || saved_err < 0) {
    std::cerr << "secret-scrub-dump: cannot open capture file\n";
    return "";
  }

  std::cout.flush();
  std::cerr.flush();
  dup2(cap, 1);
  dup2(cap, 2);

  {
    // Debug level is the whole point: a leak that only appears at debug must be
    // observable here.
    signalwire::get_logger().unsuppress();
    signalwire::get_logger().set_level(signalwire::LogLevel::Debug);

    ScrubWS fake;
    ::setenv("SIGNALWIRE_RELAY_SCHEME", "ws", 1);
    relay::RelayClient client(kProject, kToken, fake.host(), {"default"});
    (void)client.connect();
    // Let the receive loop process the pushed re-auth frame (the inbound log
    // site). Bounded — never hangs.
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    client.disconnect();

    // Fail LOUD (into the capture, which is forwarded to stderr) if the drive
    // did not actually happen — a dump that never exercised the log sites would
    // report leaked:false vacuously.
    if (!fake.saw_connect()) {
      std::cerr << "secret-scrub-dump: FATAL the connect frame never reached the server\n";
    }
    if (!fake.pushed_reauth()) {
      std::cerr << "secret-scrub-dump: FATAL the re-auth frame was never pushed\n";
    }
  }

  std::cout.flush();
  std::cerr.flush();
  fsync(cap);
  dup2(saved_out, 1);
  dup2(saved_err, 2);
  close(cap);
  close(saved_out);
  close(saved_err);

  std::ifstream in(capture_path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

}  // namespace

int main() {
  ix::initNetSystem();

  const std::filesystem::path capture = scratch_dir() / "secret_scrub_capture.log";
  const std::string captured = drive_and_capture(capture);

  // Forward the captured output to the real stderr so the differ's own
  // subprocess-stderr capture sees the identical bytes; stdout stays pure JSON.
  if (!captured.empty()) {
    std::cerr << captured;
  }

  json out = json::object();
  out["project"] = json::object({{"leaked", captured.find(kProject) != std::string::npos}});
  out["token"] = json::object({{"leaked", captured.find(kToken) != std::string::npos}});
  out["authorization_state"] =
      json::object({{"leaked", captured.find(kAuthorizationState) != std::string::npos}});

  std::cout << out.dump() << '\n';

  std::error_code ec;
  std::filesystem::remove(capture, ec);
  ix::uninitNetSystem();
  return 0;
}
