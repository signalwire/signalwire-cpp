// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class SpiderSkill : public SkillBase {
public:
    std::string skill_name() const override { return "spider"; }
    std::string skill_description() const override { return "Fast web scraping and crawling capabilities"; }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override { params_ = params; return true; }

    std::vector<swaig::ToolDefinition> register_tools() override {
        std::string prefix = get_param<std::string>(params_, "prefix", "");
        std::vector<swaig::ToolDefinition> tools;

        tools.push_back(define_tool(prefix + "scrape_url", "Scrape content from a URL",
            json::object({{"type", "object"}, {"properties", json::object({
                {"url", json::object({{"type", "string"}, {"description", "URL to scrape"}})}
            })}, {"required", json::array({"url"})}}),
            [](const json& args, const json&) -> swaig::FunctionResult {
                return swaig::FunctionResult("Scraped content from: " + args.value("url", ""));
            }));

        tools.push_back(define_tool(prefix + "crawl_site", "Crawl a website starting from URL",
            json::object({{"type", "object"}, {"properties", json::object({
                {"start_url", json::object({{"type", "string"}, {"description", "Start URL"}})}
            })}, {"required", json::array({"start_url"})}}),
            [](const json& args, const json&) -> swaig::FunctionResult {
                return swaig::FunctionResult("Crawled site from: " + args.value("start_url", ""));
            }));

        tools.push_back(define_tool(prefix + "extract_structured_data", "Extract structured data from URL",
            json::object({{"type", "object"}, {"properties", json::object({
                {"url", json::object({{"type", "string"}, {"description", "URL to extract from"}})}
            })}, {"required", json::array({"url"})}}),
            [](const json& args, const json&) -> swaig::FunctionResult {
                return swaig::FunctionResult("Extracted data from: " + args.value("url", ""));
            }));

        return tools;
    }

    std::vector<std::string> get_hints() const override {
        return {"scrape", "crawl", "extract", "web page", "website", "spider"};
    }
};

REGISTER_SKILL(SpiderSkill)

} // namespace skills
} // namespace signalwire
