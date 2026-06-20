// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"

namespace signalwire {
namespace skills {

/// Wikipedia search skill — issues a real GET against the Wikipedia API
/// `/w/api.php` endpoint with `action=query&list=search`, parses the
/// `query.search[]` results, and returns titles + snippets. Matches the
/// Python `WikipediaSearchSkill` upstream call shape.
///
/// `WIKIPEDIA_BASE_URL` env var overrides the API root (used by
/// `audit_skills_dispatch.py` to point the skill at its loopback fixture).
class WikipediaSearchSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "wikipedia_search"; }
  std::string skill_description() const override {
    return "Search Wikipedia for information about a topic and get article summaries";
  }

  bool setup(const json& params) override {
    params_ = params;
    num_results_ = get_param<int>(params, "num_results", 1);
    no_results_msg_ = get_param<std::string>(params, "no_results_message",
                                             "No Wikipedia articles found for that topic.");
    return true;
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        "search_wiki", "Search Wikipedia for information about a topic and get article summaries",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object({{"query", json::object({{"type", "string"},
                                                    {"description", "Topic to search"}})}})},
             {"required", json::array({"query"})}}),
        [this](const json& args, const json&) -> swaig::FunctionResult {
          std::string query = args.value("query", "");
          if (query.empty()) return swaig::FunctionResult(no_results_msg_);

          // Wikipedia API root — base host overridable via
          // WIKIPEDIA_BASE_URL (used by the audit fixture). The
          // path portion is always `/w/api.php` so the audit's
          // path-substring match succeeds even when pointed at a
          // loopback fixture.
          std::string base = get_env("WIKIPEDIA_BASE_URL", "https://en.wikipedia.org");
          while (!base.empty() && base.back() == '/') base.pop_back();
          std::ostringstream url;
          url << base << "/w/api.php"
              << "?action=query&list=search&format=json"
              << "&srlimit=" << num_results_ << "&srsearch=" << url_encode(query);

          auto resp = http_get(url.str());
          if (resp.status == 0) {
            return swaig::FunctionResult("Wikipedia transport error: " + resp.error);
          }
          if (resp.status < 200 || resp.status >= 300) {
            return swaig::FunctionResult("Wikipedia HTTP " + std::to_string(resp.status) + ": " +
                                         resp.body);
          }

          json parsed;
          try {
            parsed = json::parse(resp.body);
          } catch (const json::parse_error& e) {
            return swaig::FunctionResult(std::string("Wikipedia response parse error: ") +
                                         e.what());
          }

          std::ostringstream out;
          out << "Wikipedia search for '" << query << "':\n";
          bool any = false;
          if (parsed.contains("query") && parsed["query"].contains("search") &&
              parsed["query"]["search"].is_array()) {
            for (const auto& hit : parsed["query"]["search"]) {
              out << "- " << hit.value("title", "") << ": " << hit.value("snippet", "") << "\n";
              any = true;
            }
          }
          if (!any) {
            return swaig::FunctionResult(no_results_msg_);
          }
          return swaig::FunctionResult(out.str());
        })};
  }

  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Wikipedia Search",
             "You can search Wikipedia for factual information.",
             {"Use search_wiki to find information on Wikipedia"}}};
  }

 private:
  int num_results_ = 1;
  std::string no_results_msg_;
};

REGISTER_SKILL(WikipediaSearchSkill)

}  // namespace skills
}  // namespace signalwire
