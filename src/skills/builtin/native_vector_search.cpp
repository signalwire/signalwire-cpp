// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <cstdio>

#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/skills/skills_http.hpp"

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
    // Strip a single trailing slash so remote_url_ + "/search" is well-formed
    // (mirrors Python's remote_base_url derivation).
    if (!remote_url_.empty() && remote_url_.back() == '/') {
      remote_url_.pop_back();
    }
    return !remote_url_.empty() || params.contains("index_file");
  }

  std::vector<swaig::ToolDefinition> register_tools() override {
    int count = get_param<int>(params_, "count", 3);

    return {define_tool(
        tool_name_, description_,
        json::object(
            {{"type", "object"},
             {"properties",
              json::object(
                  {{"query", json::object({{"type", "string"}, {"description", "Search query"}})},
                   {"count",
                    json::object({{"type", "integer"}, {"description", "Number of results"}})}})},
             {"required", json::array({"query"})}}),
        [this, count](const json& args, const json&) -> swaig::FunctionResult {
          std::string query = args.value("query", "");
          int n = args.value("count", count);
          if (query.empty()) {
            return swaig::FunctionResult("Please provide a search query.");
          }
          if (!remote_url_.empty()) {
            return search_remote(query, n);
          }
          // Local-index mode: the on-device vector backend is not part of the
          // C++ port's dependency set. Report clearly rather than silently
          // returning a stub sentinel.
          return swaig::FunctionResult("Vector search for '" + query + "' (top " +
                                       std::to_string(n) +
                                       "): local index mode is not supported in this build; "
                                       "configure a remote_url for network search.");
        })};
  }

 private:
  /// Network mode (Python _search_remote): POST {query,index_name,count,...} to
  /// <remote_url>/search and format the returned results into the tool result.
  swaig::FunctionResult search_remote(const std::string& query, int count) const {
    json request = json::object({
        {"query", query},
        {"index_name", index_name_},
        {"count", count},
        {"similarity_threshold", get_param<double>(params_, "similarity_threshold", 0.0)},
        {"tags", params_.contains("tags") ? params_["tags"] : json::array()},
    });

    SkillHttpResponse resp =
        http_post(remote_url_ + "/search", request.dump(), "application/json", {}, 30);

    if (resp.status == 0) {
      return swaig::FunctionResult("Remote search error: " + resp.error);
    }
    if (resp.status != 200) {
      return swaig::FunctionResult("Remote search failed with status " +
                                   std::to_string(resp.status));
    }

    json data;
    try {
      data = json::parse(resp.body);
    } catch (const std::exception& e) {
      return swaig::FunctionResult(std::string("Remote search response parse error: ") + e.what());
    }

    const json results =
        data.contains("results") && data["results"].is_array() ? data["results"] : json::array();
    if (results.empty()) {
      return swaig::FunctionResult("No search results found for '" + query + "'.");
    }

    // Format the remote {content,score,metadata} results (mirrors Python's
    // "Found N relevant results" rendering, reduced to the fields the wire
    // response guarantees).
    std::string response =
        "Found " + std::to_string(results.size()) + " relevant results for '" + query + "':\n";
    int i = 1;
    for (const auto& result : results) {
      std::string content = result.value("content", "");
      double score = result.value("score", 0.0);
      std::string filename;
      if (result.contains("metadata") && result["metadata"].is_object()) {
        filename = result["metadata"].value("filename", "");
      }
      char score_buf[16];
      std::snprintf(score_buf, sizeof(score_buf), "%.2f", score);
      response += "\n**Result " + std::to_string(i) + "**";
      if (!filename.empty()) {
        response += " (from " + filename + ", relevance: " + score_buf + ")";
      } else {
        response += " (relevance: " + std::string(score_buf) + ")";
      }
      response += "\n" + content + "\n";
      ++i;
    }
    return swaig::FunctionResult(response);
  }

 public:
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

}  // namespace skills
}  // namespace signalwire
