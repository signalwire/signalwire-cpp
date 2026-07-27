// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "signalwire/common.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"

namespace signalwire {
namespace skills {

namespace {

/// Drop each element matched by `xpaths` — tag AND its inner content —
/// before any tag-stripping runs. Mirrors the reference's
/// ``_fast_text_extract``, which walks ``self.remove_xpaths`` and calls
/// ``elem.drop_tree()`` on every hit, so a ``<script>``/``<style>`` body never
/// reaches the extracted text. Without this the naive tag-strip below turns
/// script source and CSS into "scraped content".
///
/// The reference's defaults are all plain ``//tag`` element selectors; that is
/// the shape supported here (a full XPath engine is not vendored). An entry the
/// resolver does not understand is skipped rather than silently mangling the
/// document.
std::string drop_xpath_elements(const std::string& html, const std::vector<std::string>& xpaths) {
  std::string out = html;
  for (const auto& xp : xpaths) {
    // "//tag" -> element name; anything richer is not resolvable here.
    if (xp.rfind("//", 0) != 0) {
      continue;
    }
    const std::string tag = xp.substr(2);
    if (tag.empty() ||
        tag.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") !=
            std::string::npos) {
      continue;
    }
    // <tag ...> ... </tag>  (non-greedy body, case-insensitive), plus the
    // self-closing / unpaired form so a stray "<noscript/>" also goes.
    const std::regex paired("<" + tag + R"((\s[^>]*)?>[\s\S]*?</)" + tag + R"(\s*>)",
                            std::regex::icase);
    out = std::regex_replace(out, paired, " ");
    const std::regex selfclosing("<" + tag + R"((\s[^>]*)?/>)", std::regex::icase);
    out = std::regex_replace(out, selfclosing, " ");
  }
  return out;
}

/// Strip HTML tags from `html` and collapse repeated whitespace, after first
/// dropping every element named by `remove_xpaths` (tag + content).
std::string strip_html(const std::string& html, const std::vector<std::string>& remove_xpaths) {
  static const std::regex tag_re(R"(<[^>]+>)");
  static const std::regex ws_re(R"(\s+)");
  std::string pruned = drop_xpath_elements(html, remove_xpaths);
  std::string no_tags = std::regex_replace(pruned, tag_re, " ");
  return std::regex_replace(no_tags, ws_re, " ");
}

/// Apply the SPIDER_BASE_URL override (used by audit fixtures) by replacing
/// the host portion of `url` with `base`. If `base` is empty or `url` has
/// no parseable host, returns `url` untouched. This is how Python's port
/// reroutes scrape requests to a loopback fixture for the audit while
/// keeping the per-call URL the LLM passed in.
std::string apply_base_override(const std::string& url, const std::string& base) {
  if (base.empty()) {
    return url;
  }
  auto scheme_end = url.find("://");
  if (scheme_end == std::string::npos) {
    return url;
  }
  auto host_start = scheme_end + 3;
  auto path_start = url.find('/', host_start);
  if (path_start == std::string::npos) {
    return base;
  }
  std::string path = url.substr(path_start);
  std::string b = base;
  while (!b.empty() && b.back() == '/') {
    b.pop_back();
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

  /// XPath expressions for the elements stripped from a fetched page before
  /// text extraction (reference: ``self.remove_xpaths``, set in ``__init__``
  /// to this same PREFILLED list — not an empty default). Callers read it to
  /// see what gets dropped; ``set_remove_xpaths`` replaces the set.
  [[nodiscard]] const std::vector<std::string>& remove_xpaths() const { return remove_xpaths_; }
  void set_remove_xpaths(const std::vector<std::string>& xpaths) { remove_xpaths_ = xpaths; }

  std::vector<swaig::ToolDefinition> register_tools() override {
    std::string prefix = get_param<std::string>(params_, "prefix", "");
    std::vector<swaig::ToolDefinition> tools;
    // Captured BY VALUE into the tool handlers: a ToolDefinition outlives the
    // skill instance that registered it (the agent owns the registry), so
    // capturing ``this`` would dangle.
    const std::vector<std::string> remove_xpaths = remove_xpaths_;

    tools.push_back(define_tool(
        prefix + "scrape_url", "Scrape content from a URL",
        json::object({{"type", "object"},
                      {"properties",
                       json::object({{"url", json::object({{"type", "string"},
                                                           {"description", "URL to scrape"}})}})},
                      {"required", json::array({"url"})}}),
        [remove_xpaths](const json& args, const json&) -> swaig::FunctionResult {
          std::string url = args.value("url", "");
          if (url.empty()) {
            return swaig::FunctionResult("No URL provided");
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
                text = strip_html(parsed["_raw_html"].get<std::string>(), remove_xpaths);
              } else {
                text = strip_html(resp.body, remove_xpaths);
              }
            } catch (...) {
              text = strip_html(resp.body, remove_xpaths);
            }
          } else {
            text = strip_html(resp.body, remove_xpaths);
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
        [remove_xpaths](const json& args, const json&) -> swaig::FunctionResult {
          // Crawl is implemented as a single-page scrape of the start
          // URL — the deeper crawl loop is a future feature; for now
          // we fetch the start page (real HTTP, real HTML strip) so
          // callers that hit this tool get real content rather than
          // canned text.
          std::string url = args.value("start_url", "");
          if (url.empty()) {
            return swaig::FunctionResult("No start URL provided");
          }

          std::string base = get_env("SPIDER_BASE_URL");
          std::string effective = apply_base_override(url, base);

          auto resp = http_get(effective);
          if (resp.status == 0) {
            return swaig::FunctionResult("Spider transport error: " + resp.error);
          }
          std::string text = strip_html(resp.body, remove_xpaths);
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
          if (url.empty()) {
            return swaig::FunctionResult("No URL provided");
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

 private:
  /// The reference PREFILLS this in ``__init__`` — it is not an empty default.
  /// Same seven selectors, same order.
  std::vector<std::string> remove_xpaths_{
      "//script", "//style", "//nav", "//header", "//footer", "//aside", "//noscript",
  };
};

REGISTER_SKILL(SpiderSkill)

}  // namespace skills
}  // namespace signalwire
