// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

// Shared core for the web_search skill's latency-control + result formatting.
//
// Ports Python signalwire/skills/web_search/skill.py (51101da + 295745b):
//   per_page_timeout  — per-page fetch timeout (the HTTP client's per-request
//                       timeout). Default 2.0s.
//   overall_deadline  — wall-clock budget for the whole tool call, enforced
//                       with std::chrono::steady_clock. In-flight scrapes are
//                       abandoned once it fires. Default 10.0s. THIS IS THE
//                       CONTRACT — a slow site must never blow past the kernel
//                       webhook timeout (~55s).
//   parallel_scrape   — scrape all candidate result links concurrently via
//                       std::async, harvesting whatever finishes before the
//                       deadline (best-effort, NOT contracted). Default true.
//   snippets_only     — skip scraping entirely and format the CSE snippets
//                       directly. Sub-second. Default false.
//   + snippet fallback — when no scraped result survives (every page failed,
//                       was empty, or the deadline fired) we format the CSE
//                       snippets we already have rather than returning an empty
//                       "no results" message, so the model always gets useful
//                       context back.
//
// This header is included by BOTH web_search implementations that exist in the
// tree — the registered `WebSearchSkillR` in skill_registry.cpp and the
// `WebSearchSkill` in builtin/web_search.cpp — so the latency contract lives in
// exactly one place and the two skills can never drift. (The builtin's
// REGISTER_SKILL is dead-stripped from the static archive; WebSearchSkillR is
// what actually runs. Both delegate here.)

#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "signalwire/skills/skills_http.hpp"

namespace signalwire {
namespace skills {
namespace web_search_core {

using json = nlohmann::json;
using steady_clock = std::chrono::steady_clock;

/// One Google CSE hit: title / link / snippet, plus the page text once a
/// scrape succeeds.
struct Candidate {
  std::string title;
  std::string link;
  std::string snippet;
  std::string content;   // populated only after a successful scrape
  bool scraped = false;  // true once `content` holds a usable page body
};

/// Latency-control configuration read from the skill params.
struct LatencyParams {
  double per_page_timeout = 2.0;   // seconds
  double overall_deadline = 10.0;  // seconds
  bool parallel_scrape = true;
  bool snippets_only = false;
};

/// Parse a Google CSE `{ "items": [ {title, link, snippet}, ... ] }` body into
/// candidate hits. Unknown / missing fields default to empty strings.
[[nodiscard]] inline std::vector<Candidate> parse_cse_items(const json& parsed) {
  std::vector<Candidate> out;
  if (parsed.contains("items") && parsed["items"].is_array()) {
    for (const auto& it : parsed["items"]) {
      Candidate c;
      c.title = it.value("title", "");
      c.link = it.value("link", "");
      c.snippet = it.value("snippet", "");
      out.push_back(std::move(c));
    }
  }
  return out;
}

/// Format Google CSE snippets without fetching the underlying pages. Used for
/// the `snippets_only` fast path AND as the graceful fallback when scraping is
/// abandoned by the overall_deadline. Always non-empty when there is at least
/// one candidate. Mirrors Python's `_format_snippet_results`.
[[nodiscard]] inline std::string format_snippet_results(const std::string& query,
                                                        const std::vector<Candidate>& cands,
                                                        int num_results) {
  std::size_t top = static_cast<std::size_t>(num_results < 1 ? 1 : num_results);
  top = std::min(top, cands.size());
  std::ostringstream out;
  out << "Snippet-only results for '" << query << "' (page content not scraped):\n\n";
  for (std::size_t i = 0; i < top; ++i) {
    const auto& c = cands[i];
    std::string snip = c.snippet;
    // Trim leading/trailing whitespace (mirrors Python's .strip()).
    auto b = snip.find_first_not_of(" \t\r\n");
    auto e = snip.find_last_not_of(" \t\r\n");
    if (b == std::string::npos) {
      snip.clear();
    } else {
      snip = snip.substr(b, e - b + 1);
}
    out << "=== RESULT " << (i + 1) << " ===\n"
        << "Title: " << c.title << "\n"
        << "URL: " << c.link << "\n"
        << "Snippet: " << snip << "\n\n";
  }
  std::string s = out.str();
  // Drop the trailing blank line so wrapping is tidy.
  while (!s.empty() && (s.back() == '\n')) { s.pop_back();
}
  return s;
}

/// Format the successfully-scraped results. Returned when at least one page was
/// fetched within budget. Keeps the historical "Web search results for '<q>'"
/// header shape so existing parse-assertions still see it, but enriches each
/// entry with the fetched page content (truncated to `per_result_limit`).
[[nodiscard]] inline std::string format_scraped_results(const std::string& query,
                                                        const std::vector<Candidate>& scraped,
                                                        std::size_t total_candidates,
                                                        std::size_t per_result_limit) {
  std::ostringstream out;
  out << "Web search results for '" << query << "':\n";
  out << "Showing " << scraped.size() << " of " << total_candidates
      << " results (page content scraped):\n\n";
  int idx = 1;
  for (const auto& c : scraped) {
    out << "=== RESULT " << idx++ << " ===\n"
        << "Title: " << c.title << "\n"
        << "URL: " << c.link << "\n"
        << "Snippet: " << c.snippet << "\n"
        << "Content:\n";
    std::string content = c.content;
    if (content.size() > per_result_limit) {
      content = content.substr(0, per_result_limit) + "...";
    }
    out << content << "\n"
        << "==================================================\n\n";
  }
  std::string s = out.str();
  while (!s.empty() && (s.back() == '\n')) { s.pop_back();
}
  return s;
}

/// Fetch one candidate's page under the per_page_timeout bound. A 2xx response
/// with a non-empty body is a usable scrape; everything else (transport error,
/// non-2xx, empty body) yields an unscraped candidate. The per_page_timeout is
/// converted to milliseconds so a sub-second budget is honored exactly.
[[nodiscard]] inline Candidate scrape_one(const Candidate& in, double per_page_timeout) {
  Candidate c = in;
  if (c.link.empty()) { return c;
}
  long timeout_ms = static_cast<long>(per_page_timeout * 1000.0);
  // Belt-and-suspenders: http_get_ms already converts transport/scheme
  // failures into status 0, but guard here too so a scrape can never throw
  // out of the std::async task (which would terminate the process).
  try {
    auto resp = http_get_ms(c.link, {}, timeout_ms);
    if (resp.status >= 200 && resp.status < 300 && !resp.body.empty()) {
      c.content = resp.body;
      c.scraped = true;
    }
  } catch (...) {
    // Swallow: a failed scrape leaves the candidate unscraped and must never
    // propagate out of the std::async task (that would terminate the process).
    c.scraped = false;
  }
  return c;
}

/// Scrape the candidate result links under the overall_deadline budget,
/// returning only the ones that produced usable page content before time ran
/// out. The overall_deadline is enforced in BOTH parallel and sequential modes
/// via std::chrono::steady_clock — that is the contract.
///
/// Parallel mode (best-effort): dispatch every scrape via std::async, then
/// future::wait_until(deadline) each. Whatever has not finished by the deadline
/// is abandoned — its future is moved into a detached reaper so the abandoned
/// fetch (already bounded by per_page_timeout) is cleaned up off the hot path
/// and never blocks the response. std::async's own future destructor would
/// otherwise JOIN the task, defeating the deadline; the reaper sidesteps that.
///
/// Sequential mode: scrape one link at a time, breaking out the instant the
/// deadline has passed.
[[nodiscard]] inline std::vector<Candidate> scrape_candidates(
    const std::vector<Candidate>& cands, const LatencyParams& lp,
    steady_clock::time_point deadline_at) {
  std::vector<Candidate> scraped;

  if (!lp.parallel_scrape) {
    for (const auto& c : cands) {
      if (steady_clock::now() >= deadline_at) { break;  // out of budget
}
      Candidate r = scrape_one(c, lp.per_page_timeout);
      if (r.scraped) { scraped.push_back(std::move(r));
}
    }
    return scraped;
  }

  // Parallel: launch all scrapes, then harvest whatever finishes before the
  // deadline.
  std::vector<std::future<Candidate>> futures;
  futures.reserve(cands.size());
  double ppt = lp.per_page_timeout;
  for (const auto& c : cands) {
    // Capture the candidate behind a shared_ptr so the closure copies only a
    // noexcept pointer. Copying a Candidate into the closure could throw
    // (string allocation), and that copy happens as the async task is
    // constructed — outside the body's try/catch — so it would otherwise be
    // able to escape the task (which std::async treats as terminating).
    auto c_ptr = std::make_shared<Candidate>(c);
    futures.push_back(std::async(std::launch::async, [c_ptr, ppt]() {
      try {
        return scrape_one(*c_ptr, ppt);
      } catch (...) {
        // Swallow: scrape_one is already exception-safe, but guarantee no
        // exception escapes the async task. Returning the (already-constructed)
        // input candidate via its noexcept move keeps this path non-throwing.
        return std::move(*c_ptr);
      }
    }));
  }

  // Anything still running at the deadline is moved here and reaped by a
  // detached thread so the abandoned future's blocking destructor runs OFF
  // the caller's path. The fetch is per_page_timeout-bounded, so the reaper
  // terminates promptly on its own.
  auto leftovers = std::make_shared<std::vector<std::future<Candidate>>>();

  for (auto& f : futures) {
    auto now = steady_clock::now();
    if (now >= deadline_at) {
      // Deadline already blown — abandon this and everything after it.
      if (f.valid()) { leftovers->push_back(std::move(f));
}
      continue;
    }
    if (f.wait_until(deadline_at) == std::future_status::ready) {
      Candidate r = f.get();
      if (r.scraped) { scraped.push_back(std::move(r));
}
    } else {
      // Timed out at the deadline — abandon.
      if (f.valid()) { leftovers->push_back(std::move(f));
}
    }
  }

  if (!leftovers->empty()) {
    std::thread([leftovers]() {
      for (auto& f : *leftovers) {
        if (f.valid()) { f.wait();  // bounded by per_page_timeout
}
      }
    }).detach();
  }

  return scraped;
}

/// Run the full latency-controlled web_search flow given an already-fetched and
/// parsed CSE candidate list. Returns the response body (UNWRAPPED — the caller
/// applies response_prefix/response_postfix). `had_items` distinguishes "CSE
/// returned zero items" (return empty_no_items_message) from "CSE returned
/// items but none scraped" (snippet fallback).
///
/// Steps mirror Python's search_and_scrape_best:
///   1. snippets_only       -> format CSE snippets directly.
///   2. scrape under deadline.
///   3. no scraped survivors -> snippet fallback (non-empty).
///   4. else                 -> format scraped results.
[[nodiscard]] inline std::string run(const std::string& query, const std::vector<Candidate>& candidates,
                                     const LatencyParams& lp, int num_results,
                                     std::size_t max_content_length,
                                     const std::string& empty_no_items_message) {
  if (candidates.empty()) {
    return empty_no_items_message;
  }

  // snippets_only fast path: skip scraping entirely.
  if (lp.snippets_only) {
    return format_snippet_results(query, candidates, num_results);
  }

  // overall_deadline budget starts now (after the non-cancelable CSE fetch,
  // which the caller already did). steady_clock is monotonic — immune to
  // wall-clock jumps.
  auto deadline_at = steady_clock::now() +
                     std::chrono::milliseconds(static_cast<long>(lp.overall_deadline * 1000.0));

  std::vector<Candidate> scraped = scrape_candidates(candidates, lp, deadline_at);

  if (scraped.empty()) {
    // Time ran out or every page failed/was below threshold. Fall back to
    // CSE snippets so we return SOMETHING useful before the kernel webhook
    // timeout fires (Python parity).
    return format_snippet_results(query, candidates, num_results);
  }

  // Keep at most num_results scraped survivors.
  std::size_t keep = static_cast<std::size_t>(num_results < 1 ? 1 : num_results);
  if (scraped.size() > keep) { scraped.resize(keep);
}

  // Per-result content budget (mirrors the spirit of Python's calculation).
  std::size_t overhead = scraped.size() * 400;
  std::size_t available =
      max_content_length > overhead ? max_content_length - overhead : max_content_length;
  std::size_t per_result_limit = scraped.empty() ? 2000 : available / scraped.size();
  if (per_result_limit < 2000) { per_result_limit = 2000;
}

  return format_scraped_results(query, scraped, candidates.size(), per_result_limit);
}

/// Build the parameter-schema fragment advertising the 6 latency / response
/// params. Merged into each skill's get_parameter_schema(). Mirrors Python's
/// get_parameter_schema entries (295745b) and the Go reference port.
[[nodiscard]] inline json schema_fragment() {
  return json::object(
      {{"response_prefix",
        json::object({{"type", "string"},
                      {"description", "Optional text prepended to every non-empty search result."},
                      {"default", ""},
                      {"required", false}})},
       {"response_postfix",
        json::object({{"type", "string"},
                      {"description", "Optional text appended to every non-empty search result."},
                      {"default", ""},
                      {"required", false}})},
       {"per_page_timeout",
        json::object({{"type", "number"},
                      {"description", "Maximum seconds to wait on a single page scrape."},
                      {"default", 2.0},
                      {"required", false},
                      {"min", 0.1}})},
       {"overall_deadline",
        json::object({{"type", "number"},
                      {"description",
                       "Wall-clock budget in seconds for the whole tool call. In-flight scrapes "
                       "are abandoned past this so the response beats the kernel webhook timeout."},
                      {"default", 10.0},
                      {"required", false},
                      {"min", 1.0}})},
       {"parallel_scrape",
        json::object(
            {{"type", "boolean"},
             {"description", "Scrape all candidate pages concurrently instead of sequentially."},
             {"default", true},
             {"required", false}})},
       {"snippets_only", json::object({{"type", "boolean"},
                                       {"description",
                                        "Skip page scraping entirely and return Google CSE "
                                        "snippets only. Fastest mode (sub-second)."},
                                       {"default", false},
                                       {"required", false}})}});
}

}  // namespace web_search_core
}  // namespace skills
}  // namespace signalwire
