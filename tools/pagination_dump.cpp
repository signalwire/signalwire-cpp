// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// pagination_dump.cpp — the C++ port's PAGINATION-CORPUS program for the
// cross-port pagination differ (porting-sdk/scripts/diff_port_pagination.py,
// corpus porting-sdk/scripts/pagination_corpus.py).
//
// It runs the shared pagination corpus (mirrored natively below — the corpus is
// the single source of truth) through the C++ SDK's REST PaginatedIterator and
// prints ONE JSON object mapping
//
//     corpus-id -> classification
//
// to stdout, where each classification is the shared cross-port shape:
//
//     empty_page_with_next:   { "continued_past_empty": bool, "items_seen": int }
//     repeating_cursor_guard: { "loop_guarded": bool, "hung": bool }
//     exhaustion:             { "terminated": bool, "total_items": int }
//
// The differ builds the golden by running the same corpus against the Python
// reference PaginatedIterator; this program emits the byte-identical
// classifications for a passing port.
//
// Each fixture is driven against an in-process cpp-httplib server that serves the
// fixture's page bodies (data + links.next) FIFO on the list endpoint — exactly
// the capability the mock_signalwire scenario store gives the Python differ, with
// no mock change. The repeating-cursor fixture is walked under a bounded watchdog
// thread: a paginator with no cycle guard would loop forever, and the watchdog
// reds it LOUD (hung:true) rather than hanging this program.
//
// Only stdout carries JSON; the SDK logger is suppressed so nothing else lands
// on stdout.

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <map>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#include "signalwire/logging.hpp"
#include "signalwire/rest/http_client.hpp"

using json = nlohmann::json;
using signalwire::rest::HttpClient;
using signalwire::rest::PaginatedIterator;

namespace {

// LIST_PATH / the page-cursor URL builder MUST match pagination_corpus.py exactly
// so the armed page bodies (and thus the differ's structural compare) line up.
const char* kListPath = "/api/fabric/addresses";

std::string next_url(const std::string& tok) {
  return std::string("http://mock.test") + kListPath + "?page_token=" + tok;
}

// One armed page: data items + an optional links.next cursor ("" => terminal).
struct Page {
  std::vector<json> data;
  std::string next;

  json body() const {
    json b;
    b["data"] = json::array();
    for (const auto& d : data) {
      b["data"].push_back(d);
    }
    json links = json::object();
    if (!next.empty()) {
      links["next"] = next;
    }
    b["links"] = links;
    return b;
  }
};

struct Fixture {
  std::string id;
  std::string kind;
  std::vector<Page> pages;
};

// CORPUS mirrors pagination_corpus.py CORPUS (single source of truth).
std::vector<Fixture> corpus() {
  return {
      {"empty_page_with_next",
       "empty_page_with_next",
       {
           {{}, next_url("EP_page2")},                 // empty page WITH next
           {{json{{"id", "found-after-empty"}}}, ""},  // terminal page with the item
       }},
      {"repeating_cursor_guard",
       "repeating_cursor_guard",
       {
           {{json{{"id", "loop-1"}}}, next_url("LOOP")},
           {{json{{"id", "loop-2"}}}, next_url("LOOP")},  // SAME next -> cycle
       }},
      {"exhaustion",
       "exhaustion",
       {
           {{json{{"id", "x-1"}}, json{{"id", "x-2"}}}, next_url("EX_page2")},
           {{json{{"id", "x-3"}}, json{{"id", "x-4"}}}, next_url("EX_page3")},
           {{json{{"id", "x-5"}}}, ""},
       }},
  };
}

// In-process server: serve the fixture's pages FIFO on kListPath. The iterator
// carries only the next cursor's QUERY into the next request (re-hitting
// kListPath), so serving strictly in order is correct. A request beyond the
// armed pages returns a terminal empty page so a mis-guarded walk still
// terminates the HTTP layer (the classifier separately marks hung via the
// watchdog).
class FixtureServer {
 public:
  explicit FixtureServer(const Fixture& f) : pages_(f.pages) {
    server_.Get(kListPath, [this](const httplib::Request&, httplib::Response& res) {
      int i = idx_.fetch_add(1);
      Page pg;
      if (i >= 0 && static_cast<size_t>(i) < pages_.size()) {
        pg = pages_[static_cast<size_t>(i)];
      }  // else default-constructed terminal empty page (no next)
      res.status = 200;
      res.set_content(pg.body().dump(), "application/json");
    });
    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    for (int i = 0; i < 200 && !server_.is_running(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
  ~FixtureServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  FixtureServer(const FixtureServer&) = delete;
  FixtureServer& operator=(const FixtureServer&) = delete;

  std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

 private:
  std::vector<Page> pages_;
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::atomic<int> idx_{0};
};

// Walk the fixture through the SDK PaginatedIterator under a bounded watchdog.
// Returns (item-ids, hung). A missing cycle guard loops forever -> hung=true.
std::pair<std::vector<std::string>, bool> walk(const Fixture& f) {
  FixtureServer srv(f);
  HttpClient http(srv.base_url(), "pagination_proj", "pagination_tok");

  std::vector<std::string> ids;
  std::mutex m;
  std::condition_variable cv;
  bool done = false;

  std::thread worker([&]() {
    PaginatedIterator it(http, kListPath, {}, "data");
    std::vector<std::string> got;
    while (it.has_next()) {
      json item = it.next();
      if (item.is_object() && item.contains("id") && item["id"].is_string()) {
        got.push_back(item["id"].get<std::string>());
      }
    }
    {
      std::lock_guard<std::mutex> lk(m);
      ids = std::move(got);
      done = true;
    }
    cv.notify_all();
  });

  bool hung = false;
  {
    std::unique_lock<std::mutex> lk(m);
    if (!cv.wait_for(lk, std::chrono::seconds(3), [&] { return done; })) {
      hung = true;  // no cycle guard -> never terminated
    }
  }

  if (hung) {
    // The worker is stuck in an infinite fetch loop; detach so this program can
    // still emit its JSON and exit (the process teardown reaps it). The server
    // in `srv` outlives the detached worker via this scope only until return —
    // but a hung walk is already a RED classification, so a clean join is moot.
    worker.detach();
  } else {
    worker.join();
  }
  return {ids, hung};
}

json classify(const Fixture& f) {
  auto [ids, hung] = walk(f);
  if (f.kind == "empty_page_with_next") {
    bool continued = ids.size() == 1 && ids[0] == "found-after-empty";
    return {{"continued_past_empty", continued}, {"items_seen", static_cast<int>(ids.size())}};
  }
  if (f.kind == "repeating_cursor_guard") {
    bool guarded = !hung && ids.size() == 2 && ids[0] == "loop-1" && ids[1] == "loop-2";
    return {{"loop_guarded", guarded}, {"hung", hung}};
  }
  if (f.kind == "exhaustion") {
    std::vector<std::string> want = {"x-1", "x-2", "x-3", "x-4", "x-5"};
    bool terminated = !hung && ids == want;
    return {{"terminated", terminated}, {"total_items", static_cast<int>(ids.size())}};
  }
  return json::object();
}

}  // namespace

int main() {
  signalwire::Logger::instance().suppress();
  json out = json::object();
  for (const auto& f : corpus()) {
    out[f.id] = classify(f);
  }
  std::cout << out.dump() << "\n";
  return 0;
}
