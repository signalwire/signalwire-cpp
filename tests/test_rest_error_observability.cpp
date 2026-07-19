// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// test_rest_error_observability.cpp — §6.6 error-observability: a thrown
// SignalWireRestError carries the response headers and the platform request
// id extracted from them (x-request-id / x-signalwire-request-id /
// request-id / x-amzn-requestid, case-insensitive, in that preference
// order). Mirrors signalwire-python's SignalWireRestError.headers /
// .request_id. Client-side observability only — NO wire-contract change.
//
// Included by tests/test_main.cpp.

#include <chrono>
#include <map>
#include <string>
#include <thread>

#include "httplib.h"
#include "signalwire/rest/http_client.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;
}  // namespace

// ---- Construction-level extraction -----------------------------------------

TEST(rest_error_request_id_case_insensitive) {
  SignalWireRestError err(404, "GET /x returned 404", "{}", "/x", "GET",
                          {{"X-Request-Id", "req-123"}});
  ASSERT_EQ(err.request_id(), std::string("req-123"));
  ASSERT_EQ(err.headers().at("X-Request-Id"), std::string("req-123"));
  return true;
}

TEST(rest_error_request_id_preference_order) {
  // No x-request-id: the next name in preference order wins over the later ones.
  SignalWireRestError err(500, "boom", "", "/x", "GET",
                          {{"request-id", "later"},
                           {"x-signalwire-request-id", "sw-9"},
                           {"x-amzn-requestid", "amzn-1"}});
  ASSERT_EQ(err.request_id(), std::string("sw-9"));

  // x-request-id present: always first choice.
  SignalWireRestError err2(500, "boom", "", "/x", "GET",
                           {{"x-signalwire-request-id", "sw-9"}, {"x-request-id", "top"}});
  ASSERT_EQ(err2.request_id(), std::string("top"));
  return true;
}

TEST(rest_error_request_id_absent_is_empty) {
  SignalWireRestError err(404, "GET /x returned 404", "{}", "/x", "GET",
                          {{"content-type", "application/json"}});
  ASSERT_TRUE(err.request_id().empty());
  ASSERT_EQ(err.headers().size(), 1u);
  // Message unchanged when no request id is present.
  ASSERT_EQ(std::string(err.what()), std::string("GET /x returned 404"));
  return true;
}

TEST(rest_error_message_carries_request_id) {
  // Mirrors python: message += f" (request-id: {id})" when present.
  SignalWireRestError err(404, "GET /x returned 404", "{}", "/x", "GET",
                          {{"x-request-id", "req-7"}});
  std::string what = err.what();
  ASSERT_TRUE(what.find("(request-id: req-7)") != std::string::npos);
  return true;
}

TEST(rest_error_default_ctor_no_headers) {
  // Back-compat: the pre-6.6 5-arg construction still works; headers empty.
  SignalWireRestError err(422, "unprocessable", "{}", "/y", "POST");
  ASSERT_TRUE(err.headers().empty());
  ASSERT_TRUE(err.request_id().empty());
  return true;
}

TEST(rest_transport_error_no_headers) {
  // A transport failure produced no response: headers/request_id empty.
  SignalWireRestTransportError err("Connection failed", "/z", "GET");
  ASSERT_TRUE(err.headers().empty());
  ASSERT_TRUE(err.request_id().empty());
  ASSERT_EQ(err.status(), 0);
  return true;
}

// ---- Populated from a real HTTP response ------------------------------------
//
// A local httplib server answers 404 with an x-request-id header; the thrown
// SignalWireRestError must carry that header map and the extracted id.

namespace {

class ErrorHeaderServer {
 public:
  ErrorHeaderServer() {
    server_.Get(".*", [](const httplib::Request&, httplib::Response& res) {
      res.status = 404;
      res.set_header("x-request-id", "req-wire-42");
      res.set_header("x-custom-echo", "observed");
      res.set_content("{\"error\":\"not found\"}", "application/json");
    });
    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    for (int i = 0; i < 50 && !server_.is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }
  ~ErrorHeaderServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
};

}  // namespace

TEST(rest_error_populated_from_response_headers) {
  ErrorHeaderServer srv;
  HttpClient http(srv.base_url(), "proj", "tok");

  bool threw = false;
  try {
    (void)http.get("/api/does/not/exist");
  } catch (const SignalWireRestError& e) {
    threw = true;
    ASSERT_EQ(e.status(), 404);
    ASSERT_EQ(e.request_id(), std::string("req-wire-42"));
    // The raw response header map is exposed (case as sent by the server).
    bool found_custom = false;
    for (const auto& [k, v] : e.headers()) {
      if (v == "observed") {
        found_custom = true;
      }
    }
    ASSERT_TRUE(found_custom);
    std::string what = e.what();
    ASSERT_TRUE(what.find("(request-id: req-wire-42)") != std::string::npos);
  }
  ASSERT_TRUE(threw);
  return true;
}
