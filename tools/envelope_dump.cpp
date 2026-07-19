// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// envelope_dump.cpp — the C++ port's ENVELOPE-DUMP program for the cross-port
// REST error-ENVELOPE behavioral differ (porting-sdk/scripts/diff_port_envelope.py).
//
// A wire-shape check (REST-COVERAGE) proves a route's SUCCESS body and that AN
// error status surfaces; it cannot express HOW the REST client handles an error
// envelope, a 429/503, a malformed body, a connection refused, or an OPT-IN
// retry. This program pins that behavior: it drives the SAME corpus the differ's
// Python oracle runs (porting-sdk/scripts/envelope_corpus.py — the single source
// of truth) against a live mock_signalwire (reusing the shared tests/mocktest.hpp
// harness), threading a RequestOptions envelope through each verb, and for each
// case observes the RAISED typed error reduced to the shared cross-port artifact:
//
//     {
//       "raised": bool,            // a typed error was raised (vs a success)
//       "error_kind": "typed"|"bare:<name>"|null,
//                                  // "typed" == a member of the SignalWireRestError
//                                  //   family; "bare:<Class>" == a leaked exception
//       "status_code": int|null,   // the HTTP status the client decoded (null for
//                                  //   a transport failure — no response reached)
//       "body_error_code": string|null,  // errors[0].code decoded from the body
//       "request_count": int       // journal hits for the path (1 == no retry,
//                                  //   0 == transport: nothing reached the server,
//                                  //   retries+1 for a retry-armed case)
//     }
//
// Prints ONE JSON object mapping corpus-id -> artifact to stdout; the differ
// byte-compares each entry against Python's golden oracle. Mirrors the PHP
// (scripts/emit_error_envelope.php) and Perl (bin/emit-envelope.pl) dumps.
//
// The corpus is duplicated here (there is no cross-language corpus loader) — it
// MUST stay in lock-step with porting-sdk/scripts/envelope_corpus.py.
//
// A ``transport`` case exercises the connection-refused path: the client is
// pointed at a DEAD port (a free port bound then released, nothing listening),
// so no mock scenario is armed and request_count is 0. A correct client raises
// its TYPED transport error (SignalWireRestTransportError, a member of the
// SignalWireRestError family, status 0 -> reported as null); a client leaking a
// bare cpp-httplib/std::runtime_error would report "bare:<name>" and fail the
// byte-compare.
//
// Run from the signalwire-cpp repo root (via the built binary):
//     ./build/envelope_dump

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <iostream>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "mocktest.hpp"
#include "signalwire/rest/http_client.hpp"
#include "signalwire/rest/request_options.hpp"
#include "signalwire/rest/rest_client.hpp"

using json = nlohmann::json;
using signalwire::rest::HttpClient;
using signalwire::rest::RequestOptions;
using signalwire::rest::RestClient;
using signalwire::rest::SignalWireRestError;

namespace {

// The endpoint every mock-armed GET case targets: a list route in all 10 ports'
// REST clients. endpoint_id is the mock's scenario/journal dispatch key.
const std::string kEndpoint = "fabric.list_fabric_addresses";
const std::string kCallPath = "/api/fabric/addresses";

// A POST route present in every port — for the idempotency-asymmetry cases.
const std::string kCreateEndpoint = "relay-rest.create_address";
const std::string kCreatePath = "/api/relay/rest/addresses";

// One corpus case: mirrors porting-sdk/scripts/envelope_corpus.py CORPUS.
// `has_scenario` selects whether a mock override is armed; `transport` selects
// the connection-refused path (dead port, no mock involved at all).
struct Case {
  std::string id;
  bool has_scenario = false;
  int status = 0;
  json response;  // may be an object (errors[]) or a raw string (malformed body)
  bool transport = false;

  // RequestOptions envelope (plan 4.2) — opt-in retry.
  int retries = 0;
  double retry_backoff = 0;
  bool has_request_options = false;

  // Arm the SAME override this many times (FIFO), so a retry-armed case sees
  // the failure on multiple attempts.
  int scenario_repeat = 1;

  // Method + target: GET on kCallPath by default; POST cases override.
  std::string method = "GET";
  std::string endpoint = kEndpoint;
  std::string call_path = kCallPath;
  json post_body;
  bool is_post = false;
};

// Pick a free TCP port (bind 127.0.0.1:0, read it back, release) — used for the
// transport case: a DEAD port that nothing is listening on once released.
int dead_port() {
  int s = ::socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) {
    std::cerr << "envelope_dump: cannot create probe socket\n";
    std::exit(1);
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(s);
    std::cerr << "envelope_dump: cannot bind probe socket\n";
    std::exit(1);
  }
  socklen_t len = sizeof(addr);
  if (::getsockname(s, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    ::close(s);
    std::cerr << "envelope_dump: cannot read probe socket name\n";
    std::exit(1);
  }
  int port = ntohs(addr.sin_port);
  ::close(s);
  return port;
}

// Decode errors[0].code out of a raw response body (JSON text), or null when
// the body is non-JSON / has no errors[]. Mirrors the differ's
// _decode_body_error_code so the artifact is the same denominator everywhere.
json decode_body_error_code(const std::string& body) {
  json parsed = json::parse(body, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object()) {
    return nullptr;
  }
  if (!parsed.contains("errors") || !parsed["errors"].is_array() || parsed["errors"].empty()) {
    return nullptr;
  }
  const auto& first = parsed["errors"][0];
  if (!first.is_object() || !first.contains("code") || !first["code"].is_string()) {
    return nullptr;
  }
  return first["code"].get<std::string>();
}

std::vector<Case> corpus() {
  std::vector<Case> cases;

  // 200 success baseline: no error, nothing raised.
  {
    Case c;
    c.id = "envelope_200_success";
    cases.push_back(c);
  }

  // 404 well-formed errors[] envelope: typed error, decoded NOT_FOUND.
  {
    Case c;
    c.id = "envelope_404_typed";
    c.has_scenario = true;
    c.status = 404;
    c.response =
        json{{"errors", json::array({{{"code", "NOT_FOUND"}, {"message", "no such address"}}})}};
    cases.push_back(c);
  }

  // 429 + Retry-After: NO retry BY DEFAULT -- typed error on the first response.
  {
    Case c;
    c.id = "envelope_429_retry_after";
    c.has_scenario = true;
    c.status = 429;
    c.response =
        json{{"errors", json::array({{{"code", "RATE_LIMITED"}, {"message", "slow down"}}})}};
    cases.push_back(c);
  }

  // 503 service-unavailable: typed error immediately, no backoff/retry.
  {
    Case c;
    c.id = "envelope_503_unavailable";
    c.has_scenario = true;
    c.status = 503;
    c.response =
        json{{"errors", json::array({{{"code", "UNAVAILABLE"}, {"message", "maintenance"}}})}};
    cases.push_back(c);
  }

  // 500 with a NON-JSON body: still raise the typed error (status 500), do not
  // crash decoding; body_error_code is null (no errors[]).
  {
    Case c;
    c.id = "envelope_500_malformed_body";
    c.has_scenario = true;
    c.status = 500;
    c.response = json("not-json-at-all <garbage");
    cases.push_back(c);
  }

  // 200 whose body carries errors[]: 2xx == success, so NOTHING is raised.
  {
    Case c;
    c.id = "envelope_200_with_error_body";
    c.has_scenario = true;
    c.status = 200;
    c.response =
        json{{"errors", json::array({{{"code", "SOFT_FAIL"}, {"message", "ignored on 2xx"}}})}};
    cases.push_back(c);
  }

  // A 503 (the differ oracle delays it 200ms; the delay is not in the
  // artifact, so an un-delayed 503 reproduces the same golden values): one
  // typed 503 error, no retry.
  {
    Case c;
    c.id = "envelope_503_delayed";
    c.has_scenario = true;
    c.status = 503;
    c.response =
        json{{"errors", json::array({{{"code", "UNAVAILABLE"}, {"message", "slow-fail"}}})}};
    cases.push_back(c);
  }

  // Connection refused (dead port): the client must raise its TYPED transport
  // error, NOT a bare cpp-httplib/std::runtime_error. request_count == 0.
  {
    Case c;
    c.id = "envelope_transport_refused";
    c.transport = true;
    cases.push_back(c);
  }

  // ======================================================================
  // RequestOptions envelope — opt-in retry (plan 4.2). retry_backoff=0 so the
  // differ never waits on wall-clock; the observable is the attempt COUNT.
  // ======================================================================

  // GET 503 with retries=1: the single armed 503 is retried into the default
  // 200 => raised == false, request_count == 2.
  {
    Case c;
    c.id = "envelope_get_retry_once_succeeds";
    c.has_scenario = true;
    c.status = 503;
    c.response =
        json{{"errors", json::array({{{"code", "UNAVAILABLE"}, {"message", "transient"}}})}};
    c.retries = 1;
    c.retry_backoff = 0;
    c.has_request_options = true;
    c.scenario_repeat = 1;
    cases.push_back(c);
  }

  // GET 503 x2 with retries=1: retries exhausted => typed 503 raised,
  // request_count == 2.
  {
    Case c;
    c.id = "envelope_get_retry_exhausted";
    c.has_scenario = true;
    c.status = 503;
    c.response = json{{"errors", json::array({{{"code", "UNAVAILABLE"}, {"message", "down"}}})}};
    c.retries = 1;
    c.retry_backoff = 0;
    c.has_request_options = true;
    c.scenario_repeat = 2;
    cases.push_back(c);
  }

  // POST 500 with retries=2: a non-idempotent method must NOT retry 500 =>
  // request_count == 1, typed 500 raised.
  {
    Case c;
    c.id = "envelope_post_500_not_retried";
    c.has_scenario = true;
    c.status = 500;
    c.response = json{{"errors", json::array({{{"code", "SERVER_ERROR"}, {"message", "boom"}}})}};
    c.retries = 2;
    c.retry_backoff = 0;
    c.has_request_options = true;
    c.scenario_repeat = 1;
    c.method = "POST";
    c.endpoint = kCreateEndpoint;
    c.call_path = kCreatePath;
    c.post_body = json{{"label", "x"}};
    c.is_post = true;
    cases.push_back(c);
  }

  // POST 503 with retries=1: a throttle IS safe to retry for a non-idempotent
  // method => retried into the default success, request_count == 2.
  {
    Case c;
    c.id = "envelope_post_503_retried";
    c.has_scenario = true;
    c.status = 503;
    c.response =
        json{{"errors", json::array({{{"code", "UNAVAILABLE"}, {"message", "throttled"}}})}};
    c.retries = 1;
    c.retry_backoff = 0;
    c.has_request_options = true;
    c.scenario_repeat = 1;
    c.method = "POST";
    c.endpoint = kCreateEndpoint;
    c.call_path = kCreatePath;
    c.post_body = json{{"label", "x"}};
    c.is_post = true;
    cases.push_back(c);
  }

  return cases;
}

}  // namespace

int main() {
  json out = json::object();

  for (const auto& c : corpus()) {
    json artifact = {
        {"raised", false},        {"error_kind", nullptr},
        {"status_code", nullptr}, {"body_error_code", nullptr},
        {"request_count", 0},
    };

    RestClient client = [&]() -> RestClient {
      if (c.transport) {
        // Point at a DEAD port -- nothing listening once released. No mock
        // harness involved (a fresh, disposable RestClient), so no journal
        // scoping applies; request_count stays 0 by construction.
        int port = dead_port();
        return RestClient::with_base_url("http://127.0.0.1:" + std::to_string(port),
                                         "envelope_proj", "envelope_tok");
      }
      // A fresh mock-backed client per case: unique random project -> unique
      // auth header -> an isolated (empty-start) scoped journal view, so
      // request_count is exact without needing an explicit reset.
      auto mc = signalwire::rest::mocktest::make_client();
      if (c.has_scenario) {
        // Arm the SAME override scenario_repeat times (FIFO), so a retry-armed
        // case sees the failure on every attempt it is armed for.
        for (int i = 0; i < c.scenario_repeat; ++i) {
          signalwire::rest::mocktest::scenario_set(c.endpoint, c.status, c.response);
        }
      }
      return mc;
    }();

    // Build the per-request options envelope.
    RequestOptions ro;
    if (c.has_request_options) {
      ro.retries = c.retries;
      ro.retry_backoff = c.retry_backoff;
    }

    try {
      json body;
      if (c.is_post) {
        body = client.http_client().post(c.call_path, c.post_body, ro);
      } else {
        body = client.http_client().get(c.call_path, {}, ro);
      }
      (void)body;
    } catch (const SignalWireRestError& e) {
      // A member of the typed error family (HTTP error OR transport error).
      artifact["raised"] = true;
      artifact["error_kind"] = "typed";
      // status() == 0 => a transport failure (no HTTP response); report null
      // so the artifact matches the oracle (python raises status_code=None).
      artifact["status_code"] = e.status() == 0 ? json(nullptr) : json(e.status());
      artifact["body_error_code"] = decode_body_error_code(e.body());
    } catch (const std::exception& e) {
      // A leaked, non-family exception -- the contract violation the gate
      // catches.
      artifact["raised"] = true;
      artifact["error_kind"] = std::string("bare:") + typeid(e).name();
    }

    if (!c.transport) {
      // Count journal hits for the path (retry check: 1 == no retry, retries+1
      // for a retry-armed case). Scoped to this case's client via the
      // thread-local active scope make_client() set, so a concurrent run can't
      // cross-contaminate.
      int count = 0;
      for (const auto& j : signalwire::rest::mocktest::journal()) {
        if (j.path == c.call_path) {
          ++count;
        }
      }
      artifact["request_count"] = count;
      signalwire::rest::mocktest::clear_active_scope();
    }

    out[c.id] = artifact;
  }

  std::cout << out.dump() << "\n";
  return 0;
}
