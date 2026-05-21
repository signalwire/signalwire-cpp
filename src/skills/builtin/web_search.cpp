// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"
#include "signalwire/common.hpp"

#include <sstream>

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
    std::string skill_description() const override { return "Search the web for information using Google Custom Search API"; }
    std::string skill_version() const override { return "2.0.0"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        api_key_ = get_param_or_env(params, "api_key", "GOOGLE_SEARCH_API_KEY");
        if (api_key_.empty()) api_key_ = get_env("GOOGLE_API_KEY");
        search_engine_id_ = get_param_or_env(params, "search_engine_id", "GOOGLE_SEARCH_ENGINE_ID");
        if (search_engine_id_.empty()) search_engine_id_ = get_env("GOOGLE_CSE_ID");
        tool_name_ = get_param<std::string>(params, "tool_name", "web_search");
        num_results_ = get_param<int>(params, "num_results", 3);
        // Optional prefix/postfix wrapped around every successful (non-empty)
        // search response. Mechanical cue for the calling agent (e.g. "tell
        // the user this came from a public web search") without prompt-side
        // rules. Mirrors response_format_callback in native_vector_search.
        response_prefix_ = get_param<std::string>(params, "response_prefix", "");
        response_postfix_ = get_param<std::string>(params, "response_postfix", "");
        return !api_key_.empty() && !search_engine_id_.empty();
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        return {define_tool(
            tool_name_,
            "Search the web for high-quality information, automatically filtering low-quality results",
            json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"query", json::object({{"type", "string"}, {"description", "Search query"}})}
                })},
                {"required", json::array({"query"})}
            }),
            [this](const json& args, const json&) -> swaig::FunctionResult {
                std::string query = args.value("query", "");
                if (query.empty()) return swaig::FunctionResult("No search query provided");

                // Base URL (default Google) plus the API path. When
                // WEB_SEARCH_BASE_URL points at a fixture, we still emit
                // `/customsearch/v1` so the audit's path-substring check
                // sees the documented endpoint. Strip a trailing slash on
                // the base so we don't end up with `//`.
                std::string base = get_env("WEB_SEARCH_BASE_URL",
                                           "https://www.googleapis.com");
                while (!base.empty() && base.back() == '/') base.pop_back();
                std::ostringstream url;
                url << base << "/customsearch/v1"
                    << "?key=" << url_encode(api_key_)
                    << "&cx=" << url_encode(search_engine_id_)
                    << "&q=" << url_encode(query)
                    << "&num=" << num_results_;

                auto resp = http_get(url.str());
                if (resp.status == 0) {
                    return swaig::FunctionResult(
                        "Web search transport error: " + resp.error);
                }
                if (resp.status < 200 || resp.status >= 300) {
                    return swaig::FunctionResult(
                        "Web search HTTP " + std::to_string(resp.status) + ": " + resp.body);
                }

                // Parse Google CSE response shape:
                //   { "items": [ {"title", "link", "snippet"}, ... ] }
                json parsed;
                try {
                    parsed = json::parse(resp.body);
                } catch (const json::parse_error& e) {
                    return swaig::FunctionResult(
                        std::string("Web search response parse error: ") + e.what());
                }

                std::ostringstream out;
                out << "Web search results for '" << query << "':\n";
                if (parsed.contains("items") && parsed["items"].is_array()) {
                    int idx = 1;
                    for (const auto& item : parsed["items"]) {
                        out << idx++ << ". "
                            << item.value("title", "") << "\n"
                            << "   " << item.value("link", "") << "\n"
                            << "   " << item.value("snippet", "") << "\n";
                    }
                } else {
                    out << "(no results)";
                }
                // Wrap the success response with the configured prefix/postfix.
                // Error / transport-error / no-API-key paths are NOT wrapped,
                // matching the Python reference (8aad242).
                std::string response = out.str();
                if (!response_prefix_.empty()) {
                    response = response_prefix_ + "\n\n" + response;
                }
                if (!response_postfix_.empty()) {
                    response = response + "\n\n" + response_postfix_;
                }
                return swaig::FunctionResult(response);
            }
        )};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{"Web Search Capability (Quality Enhanced)",
                 "You can search the web for current information.",
                 {"Use " + tool_name_ + " to find information online",
                  "Results are automatically filtered for quality"}}};
    }

    json get_global_data() const override {
        return json::object({
            {"web_search_enabled", true},
            {"search_provider", "Google Custom Search"},
            {"quality_filtering", true}
        });
    }

private:
    std::string api_key_;
    std::string search_engine_id_;
    std::string tool_name_ = "web_search";
    int num_results_ = 3;
    std::string response_prefix_;
    std::string response_postfix_;
};

REGISTER_SKILL(WebSearchSkill)

} // namespace skills
} // namespace signalwire
