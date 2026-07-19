// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// test_request_options.cpp — unit tests for the REST RequestOptions envelope
// (plan 4.2), mirroring signalwire-python
// tests/unit/rest/test_request_options.py, driven over the SHARED mock
// (mocktest.hpp make_client + scenario_set). No transport mock.
//
// Covers: merge() shallow-override semantics; opt-in retry into a 200; retries
// exhausted surfacing the typed 503; the POST idempotency asymmetry (500 NOT
// retried, 503 retried); cooperative abort_signal (raises the transport error
// before anything reaches the mock).
//
// Included by tests/test_main.cpp.

#include <atomic>

#include "mocktest.hpp"
#include "signalwire/rest/request_options.hpp"

namespace {
using namespace signalwire::rest;
using nlohmann::json;

// The fixed endpoints the corpus targets — a GET list route + a POST create
// route present in every port's client.
const std::string kGetEndpoint = "fabric.list_fabric_addresses";
const std::string kGetPath = "/api/fabric/addresses";
const std::string kCreateEndpoint = "relay-rest.create_address";
const std::string kCreatePath = "/api/relay/rest/addresses";

// Count this client's journal hits for a path (scoped to the active auth
// header by make_client()).
int journal_count_for(const std::string& path) {
  int n = 0;
  for (const auto& j : mocktest::journal()) {
    if (j.path == path) {
      ++n;
    }
  }
  return n;
}
}  // namespace

// ---------------------------------------------------------------------------
// merge(): override's set field wins; an unset field inherits.
// ---------------------------------------------------------------------------

TEST(request_options_merge_override_wins) {
  RequestOptions base;
  base.timeout = 30.0;
  base.retries = 0;
  base.retry_backoff = 0.5;

  RequestOptions over;
  over.retries = 3;  // set: wins
  // timeout / retry_backoff unset on `over`: inherit from base.

  RequestOptions merged = base.merge(over);
  ASSERT_TRUE(merged.retries.has_value());
  ASSERT_EQ(*merged.retries, 3);
  ASSERT_TRUE(merged.timeout.has_value());
  ASSERT_EQ(*merged.timeout, 30.0);
  ASSERT_TRUE(merged.retry_backoff.has_value());
  ASSERT_EQ(*merged.retry_backoff, 0.5);
  return true;
}

TEST(request_options_merge_unset_inherits) {
  RequestOptions base;
  base.timeout = 12.5;
  base.retries = 2;

  RequestOptions over;  // entirely empty: merged == base
  RequestOptions merged = base.merge(over);
  ASSERT_TRUE(merged.timeout.has_value());
  ASSERT_EQ(*merged.timeout, 12.5);
  ASSERT_TRUE(merged.retries.has_value());
  ASSERT_EQ(*merged.retries, 2);

  // And the empty base + set override => override values surface (the
  // client-default-then-per-request path).
  RequestOptions empty;
  RequestOptions per;
  per.timeout = 1.0;
  per.abort_signal = nullptr;
  RequestOptions m2 = empty.merge(per);
  ASSERT_TRUE(m2.timeout.has_value());
  ASSERT_EQ(*m2.timeout, 1.0);
  ASSERT_FALSE(m2.retries.has_value());  // still unset -> resolves to built-in
  return true;
}

// ---------------------------------------------------------------------------
// GET 503 with retries=1: the single armed 503 is retried into the default
// 200 => no throw, journal count == 2.
// ---------------------------------------------------------------------------

TEST(request_options_get_retry_once_succeeds) {
  auto client = mocktest::make_client();
  mocktest::scenario_set(kGetEndpoint, 503,
                         json{{"errors", json::array({{{"code", "UNAVAILABLE"},
                                                       {"message", "transient"}}})}});

  RequestOptions ro;
  ro.retries = 1;
  ro.retry_backoff = 0;

  bool threw = false;
  try {
    auto body = client.http_client().get(kGetPath, {}, ro);
    ASSERT_TRUE(body.is_object());
  } catch (const SignalWireRestError&) {
    threw = true;
  }
  ASSERT_FALSE(threw);
  ASSERT_EQ(journal_count_for(kGetPath), 2);
  return true;
}

// ---------------------------------------------------------------------------
// GET 503 armed twice with retries=1: retries exhausted => typed 503 raised,
// journal count == 2.
// ---------------------------------------------------------------------------

TEST(request_options_get_retry_exhausted) {
  auto client = mocktest::make_client();
  json err = json{{"errors", json::array({{{"code", "UNAVAILABLE"}, {"message", "down"}}})}};
  mocktest::scenario_set(kGetEndpoint, 503, err);
  mocktest::scenario_set(kGetEndpoint, 503, err);

  RequestOptions ro;
  ro.retries = 1;
  ro.retry_backoff = 0;

  int status = -1;
  bool threw = false;
  try {
    auto body = client.http_client().get(kGetPath, {}, ro);
    (void)body;
  } catch (const SignalWireRestError& e) {
    threw = true;
    status = e.status();
  }
  ASSERT_TRUE(threw);
  ASSERT_EQ(status, 503);
  ASSERT_EQ(journal_count_for(kGetPath), 2);
  return true;
}

// ---------------------------------------------------------------------------
// POST 500 with retries=2: a non-idempotent method must NOT retry 500 =>
// typed 500 raised, journal count == 1 despite retries armed.
// ---------------------------------------------------------------------------

TEST(request_options_post_500_not_retried) {
  auto client = mocktest::make_client();
  mocktest::scenario_set(kCreateEndpoint, 500,
                         json{{"errors", json::array({{{"code", "SERVER_ERROR"},
                                                       {"message", "boom"}}})}});

  RequestOptions ro;
  ro.retries = 2;
  ro.retry_backoff = 0;

  int status = -1;
  bool threw = false;
  try {
    auto body = client.http_client().post(kCreatePath, json{{"label", "x"}}, ro);
    (void)body;
  } catch (const SignalWireRestError& e) {
    threw = true;
    status = e.status();
  }
  ASSERT_TRUE(threw);
  ASSERT_EQ(status, 500);
  ASSERT_EQ(journal_count_for(kCreatePath), 1);
  return true;
}

// ---------------------------------------------------------------------------
// POST 503 with retries=1: a throttle IS safe to retry for a non-idempotent
// method => retried into the default success, journal count == 2.
// ---------------------------------------------------------------------------

TEST(request_options_post_503_retried) {
  auto client = mocktest::make_client();
  mocktest::scenario_set(kCreateEndpoint, 503,
                         json{{"errors", json::array({{{"code", "UNAVAILABLE"},
                                                       {"message", "throttled"}}})}});

  RequestOptions ro;
  ro.retries = 1;
  ro.retry_backoff = 0;

  bool threw = false;
  try {
    auto body = client.http_client().post(kCreatePath, json{{"label", "x"}}, ro);
    ASSERT_TRUE(body.is_object());
  } catch (const SignalWireRestError&) {
    threw = true;
  }
  ASSERT_FALSE(threw);
  ASSERT_EQ(journal_count_for(kCreatePath), 2);
  return true;
}

// ---------------------------------------------------------------------------
// abort_signal already set => the request raises the TYPED transport error
// (status 0) BEFORE anything reaches the mock (journal count == 0).
// ---------------------------------------------------------------------------

TEST(request_options_abort_signal_before_send) {
  auto client = mocktest::make_client();
  std::atomic<bool> flag{true};

  RequestOptions ro;
  ro.abort_signal = &flag;

  int status = -1;
  bool threw_transport = false;
  try {
    auto body = client.http_client().get(kGetPath, {}, ro);
    (void)body;
  } catch (const SignalWireRestTransportError& e) {
    threw_transport = true;
    status = e.status();
  } catch (const SignalWireRestError&) {
    // Any other typed error means abort did not short-circuit — leave the
    // transport flag false so the assertion below fails loudly.
  }
  ASSERT_TRUE(threw_transport);
  ASSERT_EQ(status, 0);
  ASSERT_EQ(journal_count_for(kGetPath), 0);
  return true;
}
