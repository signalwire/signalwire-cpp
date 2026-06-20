// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <regex>
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"

namespace signalwire {
namespace skills {

namespace {

/// Strip HTML tags from `html` and collapse repeated whitespace. Matches the
/// "naive HTML strip" Python's spider skill does for its scrape_url tool.
std::string strip_html(const std::string& html) {
  static const std::regex tag_re(R"(<[^>]+>)");
  static const std::regex ws_re(R"(\s+)");
  std::string no_tags = std::regex_replace(html, tag_re, " ");
  return std::regex_replace(no_tags, ws_re, " ");
}

/// Apply the SPIDER_BASE_URL override (used by audit fixtures) by replacing
/// the host portion of `url` with `base`. If `base` is empty or `url` has
/// no parseable host, returns `url` untouched. This is how Python's port
/// reroutes scrape requests to a loopback fixture for the audit while
/// keeping the per-call URL the LLM passed in.
std::string apply_base_override(const std::string& url, const std::string& base) {
  if (base.empty()) { return url;
}
  auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) { return url;
}
  auto host_start = scheme_end + 3;
  auto path_start = url.find('/', host_start);
  if (path_start == std::string::npos) {
    return base;
  }
  std::string path = url.substr(path_start);
  std::string b = base;
  while (!b.empty() && b.back() == '/') { b.pop_back();
}
  return b + path;
}

}  // namespace

/// Spider scrape skill — issues a real GET against the URL the LLM passes
/// in. Strips HTML tags from the response and returns the text payload.
/// Matches Python `SpiderSkill`'s scrape_url behavior.
///
/// `SPIDER_BASE_URL` env var overrides the host portion of the URL the
/// caller passes in (used by `audit_skills_dispatch.py` to redirect
/// scrape requests at a loopback fixture).
class SpiderSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "spider"; }
  std::string skill_description() const override {
    return "Fast web scraping and crawling capabilities";
  }
  bool supports_multiple_instances() const override { return true; }

  bool setup(const json& params) override {
    params_ = params;
    return true;
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    std::string prefix = get_param<std::string>(params_, "prefix", "");
    std::vector<swaig::ToolDefinition> tools;

    tools.push_back(define_tool(
        prefix + "scrape_url", "Scrape content from a URL",
        json::object({{"type", "object"},
                      {"properties",
                       json::object({{"url", json::object({{"type", "string"},
                                                           {"description", "URL to scrape"}})}})},
                      {"required", json::array({"url"})}}),
        [](const json& args, const json&) -> swaig::FunctionResult {
          std::string url = args.value("url", "");
          if (url.empty()) { return swaig::FunctionResult("No URL provided");
}

          std::string base = get_env("SPIDER_BASE_URL");
          std::string effective = apply_base_override(url, base);

          auto resp = http_get(effective);
          if (resp.status == 0) {
            return swaig::FunctionResult("Spider transport error: " + resp.error);
          }
          if (resp.status < 200 || resp.status >= 300) {
            return swaig::FunctionResult("Spider HTTP " + std::to_string(resp.status) + " from " +
                                         effective);
          }

          // The audit fixture serves JSON like {"_raw_html": "..."}.
          // Production spiders fetch real HTML directly. Detect both.
          std::string text;
          if (!resp.body.empty() && resp.body.front() == '{') {
            try {
              json parsed = json::parse(resp.body);
              if (parsed.contains("_raw_html") && parsed["_raw_html"].is_string()) {
                text = strip_html(parsed["_raw_html"].get<std::string>());
              } else {
                text = strip_html(resp.body);
              }
            } catch (...) {
              text = strip_html(resp.body);
            }
          } else {
            text = strip_html(resp.body);
          }

          std::ostringstream out;
          out << "Scraped content from " << effective << ":\n" << text;
          return swaig::FunctionResult(out.str());
        }));

    tools.push_back(define_tool(
        prefix + "crawl_site", "Crawl a website starting from URL",
        json::object({{"type", "object"},
                      {"properties",
                       json::object({{"start_url", json::object({{"type", "string"},
                                                                 {"description", "Start URL"}})}})},
                      {"required", json::array({"start_url"})}}),
        [](const json& args, const json&) -> swaig::FunctionResult {
          // Crawl is implemented as a single-page scrape of the start
          // URL — the deeper crawl loop is a future feature; for now
          // we fetch the start page (real HTTP, real HTML strip) so
          // callers that hit this tool get real content rather than
          // canned text.
          std::string url = args.value("start_url", "");
          if (url.empty()) { return swaig::FunctionResult("No start URL provided");
}

          std::string base = get_env("SPIDER_BASE_URL");
          std::string effective = apply_base_override(url, base);

          auto resp = http_get(effective);
          if (resp.status == 0) {
            return swaig::FunctionResult("Spider transport error: " + resp.error);
          }
          std::string text = strip_html(resp.body);
          return swaig::FunctionResult("Crawl page " + effective + ":\n" + text);
        }));

    tools.push_back(define_tool(
        prefix + "extract_structured_data", "Extract structured data from URL",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object({{"url", json::object({{"type", "string"},
                                                  {"description", "URL to extract from"}})}})},
             {"required", json::array({"url"})}}),
        [](const json& args, const json&) -> swaig::FunctionResult {
          std::string url = args.value("url", "");
          if (url.empty()) { return swaig::FunctionResult("No URL provided");
}
          std::string base = get_env("SPIDER_BASE_URL");
          std::string effective = apply_base_override(url, base);
          auto resp = http_get(effective);
          if (resp.status == 0) {
            return swaig::FunctionResult("Spider transport error: " + resp.error);
          }
          return swaig::FunctionResult(resp.body);
        }));

    return tools;
  }

  std::vector<std::string> get_hints() const override {
    return {"scrape", "crawl", "extract", "web page", "website", "spider"};
  }
};

REGISTER_SKILL(SpiderSkill)

}  // namespace skills
}  // namespace signalwire
