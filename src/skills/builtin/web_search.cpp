// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"
#include "signalwire/skills/web_search_core.hpp"

namespace signalwire {
namespace skills {

/// Google Custom Search API skill — issues a real GET against Google's
/// `customsearch/v1` endpoint, parses the `items[]` results, and returns
/// a human-readable summary. Matches Python's `WebSearchSkill` behavior.
///
/// Configuration:
///   - api_key (or env GOOGLE_SEARCH_API_KEY / GOOGLE_API_KEY)
///   - search_engine_id (or env GOOGLE_SEARCH_ENGINE_ID / GOOGLE_CSE_ID)
///   - num_results (default 3)
///   - tool_name (default "web_search")
///   - WEB_SEARCH_BASE_URL env var overrides the upstream URL (used by
///     `audit_skills_dispatch.py` to point the skill at a fixture)
class WebSearchSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "web_search"; }
  std::string skill_description() const override {
    return "Search the web for information using Google Custom Search API";
  }
  std::string skill_version() const override { return "2.0.0"; }
  bool supports_multiple_instances() const override { return true; }

  bool setup(const json& params) override {
    params_ = params;
    api_key_ = get_param_or_env(params, "api_key", "GOOGLE_SEARCH_API_KEY");
    if (api_key_.empty()) {
      api_key_ = get_env("GOOGLE_API_KEY");
    }
    search_engine_id_ = get_param_or_env(params, "search_engine_id", "GOOGLE_SEARCH_ENGINE_ID");
    if (search_engine_id_.empty()) {
      search_engine_id_ = get_env("GOOGLE_CSE_ID");
    }
    tool_name_ = get_param<std::string>(params, "tool_name", "web_search");
    num_results_ = get_param<int>(params, "num_results", 3);
    // Optional prefix/postfix wrapped around every successful (non-empty)
    // search response. Mechanical cue for the calling agent (e.g. "tell
    // the user this came from a public web search") without prompt-side
    // rules. Mirrors response_format_callback in native_vector_search.
    response_prefix_ = get_param<std::string>(params, "response_prefix", "");
    response_postfix_ = get_param<std::string>(params, "response_postfix", "");
    // Latency-control params (Python parity: 51101da + 295745b). Shared
    // implementation in web_search_core.hpp; identical to WebSearchSkillR
    // in skill_registry.cpp (the registered class). overall_deadline +
    // per_page_timeout are the contract; parallel_scrape is best-effort.
    lp_.per_page_timeout = get_param<double>(params, "per_page_timeout", 2.0);
    lp_.overall_deadline = get_param<double>(params, "overall_deadline", 10.0);
    lp_.parallel_scrape = get_param<bool>(params, "parallel_scrape", true);
    lp_.snippets_only = get_param<bool>(params, "snippets_only", false);
    max_content_length_ =
        static_cast<std::size_t>(get_param<int>(params, "max_content_length", 32768));
    return !api_key_.empty() && !search_engine_id_.empty();
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        tool_name_,
        "Search the web for high-quality information, automatically filtering low-quality results",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object({{"query", json::object({{"type", "string"},
                                                    {"description", "Search query"}})}})}}),
        [this](const json& args, const json&) -> swaig::FunctionResult {
          std::string query = args.value("query", "");
          if (query.empty()) {
            return swaig::FunctionResult("No search query provided");
          }

          // Base URL (default Google) plus the API path. When
          // WEB_SEARCH_BASE_URL points at a fixture, we still emit
          // `/customsearch/v1` so the audit's path-substring check
          // sees the documented endpoint. Strip a trailing slash on
          // the base so we don't end up with `//`.
          std::string base = get_env("WEB_SEARCH_BASE_URL", "https://www.googleapis.com");
          while (!base.empty() && base.back() == '/') {
            base.pop_back();
          }
          std::ostringstream url;
          url << base << "/customsearch/v1"
              << "?key=" << url_encode(api_key_) << "&cx=" << url_encode(search_engine_id_)
              << "&q=" << url_encode(query) << "&num=" << num_results_;

          // The CSE fetch is the single non-cancelable step; the
          // overall_deadline budget starts after it (inside core::run).
          auto resp = http_get(url.str());
          if (resp.status == 0) {
            return swaig::FunctionResult("Web search transport error: " + resp.error);
          }
          if (resp.status < 200 || resp.status >= 300) {
            return swaig::FunctionResult("Web search HTTP " + std::to_string(resp.status) + ": " +
                                         resp.body);
          }

          // Parse Google CSE response shape:
          //   { "items": [ {"title", "link", "snippet"}, ... ] }
          json parsed;
          try {
            parsed = json::parse(resp.body);
          } catch (const json::parse_error& e) {
            return swaig::FunctionResult(std::string("Web search response parse error: ") +
                                         e.what());
          }

          auto cands = web_search_core::parse_cse_items(parsed);
          std::string no_items = "Web search results for '" + query + "':\n(no results)";
          // run() handles snippets_only / deadline-bounded scraping /
          // snippet fallback (Python 51101da). Returns UNWRAPPED body.
          std::string response =
              web_search_core::run(query, cands, lp_, num_results_, max_content_length_, no_items);
          // Wrap the success / snippet / scraped response with the
          // configured prefix/postfix. Error / transport-error / no-API-key
          // / no-items paths are NOT wrapped, matching Python (8aad242).
          if (response != no_items) {
            if (!response_prefix_.empty()) {
              response = response_prefix_ + "\n\n" + response;
            }
            if (!response_postfix_.empty()) {
              response = response + "\n\n" + response_postfix_;
            }
          }
          return swaig::FunctionResult(response);
        })};
  }

  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Web Search Capability (Quality Enhanced)",
             "You can search the web for current information.",
             {"Use " + tool_name_ + " to find information online",
              "Results are automatically filtered for quality"}}};
  }

  json get_global_data() const override {
    return json::object({{"web_search_enabled", true},
                         {"search_provider", "Google Custom Search"},
                         {"quality_filtering", true}});
  }

  json get_parameter_schema() const override {
    // Advertise the 6 latency / response params (Python parity: 295745b).
    return web_search_core::schema_fragment();
  }

 private:
  std::string api_key_;
  std::string search_engine_id_;
  std::string tool_name_ = "web_search";
  int num_results_ = 3;
  std::string response_prefix_;
  std::string response_postfix_;
  // Latency-control params (shared core in web_search_core.hpp).
  web_search_core::LatencyParams lp_;
  std::size_t max_content_length_ = 32768;
};

REGISTER_SKILL(WebSearchSkill)

}  // namespace skills
}  // namespace signalwire
