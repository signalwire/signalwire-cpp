// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

class NativeVectorSearchSkill : public SkillBase {
public:
    std::string skill_name() const override { return "native_vector_search"; }
    std::string skill_description() const override {
        return "Search document indexes using vector similarity and keyword search (local or remote)";
    }
    bool supports_multiple_instances() const override { return true; }

    bool setup(const json& params) override {
        params_ = params;
        tool_name_ = get_param<std::string>(params, "tool_name", "search_knowledge");
        remote_url_ = get_param<std::string>(params, "remote_url", "");
        index_name_ = get_param<std::string>(params, "index_name", "");
        description_ = get_param<std::string>(params, "description",
            "Search the local knowledge base for information");
        return !remote_url_.empty() || params.contains("index_file");
    }

    std::vector<swaig::ToolDefinition> register_tools() override {
        int count = get_param<int>(params_, "count", 3);

        return {define_tool(tool_name_, description_,
            json::object({{"type", "object"}, {"properties", json::object({
                {"query", json::object({{"type", "string"}, {"description", "Search query"}})},
                {"count", json::object({{"type", "integer"}, {"description", "Number of results"}})}
            })}, {"required", json::array({"query"})}}),
            [this, count](const json& args, const json&) -> swaig::FunctionResult {
                std::string query = args.value("query", "");
                int n = args.value("count", count);
                return swaig::FunctionResult(
                    "Vector search for '" + query + "' (top " + std::to_string(n) +
                    "): [Would query " + (remote_url_.empty() ? "local index" : remote_url_) + "]");
            })};
    }

    std::vector<std::string> get_hints() const override {
        std::vector<std::string> h = {"search", "find", "look up", "documentation", "knowledge base"};
        if (params_.contains("hints") && params_["hints"].is_array()) {
            for (const auto& hint : params_["hints"]) {
                h.push_back(hint.get<std::string>());
            }
        }
        return h;
    }

private:
    std::string tool_name_, remote_url_, index_name_, description_;
};

REGISTER_SKILL(NativeVectorSearchSkill)

} // namespace skills
} // namespace signalwire
