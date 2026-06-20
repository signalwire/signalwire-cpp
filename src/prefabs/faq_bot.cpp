// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

FAQBotAgent::FAQBotAgent(const std::string& name, const std::string& route, const std::string& host,
                         int port)
    : AgentBase(name, route, host, port) {
  prompt_add_section(
      "Personality",
      "You are a helpful FAQ bot. Answer questions based on the FAQ knowledge base.");
  prompt_add_section(
      "Instructions", "",
      {"Search the FAQ database for matching answers", "If no exact match, suggest related topics",
       "Be concise and accurate in responses"});
}

FAQBotAgent& FAQBotAgent::set_faqs(const std::vector<json>& faqs) {
  // Build FAQ knowledge as global data and prompt sections
  json faq_data = json::array();
  std::vector<std::string> bullets;

  for (const auto& faq : faqs) {
    faq_data.push_back(faq);
    std::string q = faq.value("question", "");
    bullets.push_back(q);
  }

  update_global_data(json::object({{"faqs", faq_data}}));
  prompt_add_section("FAQ Topics", "Available FAQ topics:", bullets);

  // Define search tool
  define_tool(
      "search_faq", "Search the FAQ database for answers",
      json::object({{"type", "object"},
                    {"properties",
                     json::object({{"query", json::object({{"type", "string"},
                                                           {"description", "Search query"}})}})},
                    {"required", json::array({"query"})}}),
      [faq_data](const json& args, const json&) -> swaig::FunctionResult {
        std::string query = args.value("query", "");
        std::string lower_query = query;
        std::transform(lower_query.begin(), lower_query.end(), lower_query.begin(), ::tolower);

        // Simple keyword matching
        for (const auto& faq : faq_data) {
          std::string q = faq.value("question", "");
          std::string lower_q = q;
          std::transform(lower_q.begin(), lower_q.end(), lower_q.begin(), ::tolower);

          if (lower_q.find(lower_query) != std::string::npos ||
              lower_query.find(lower_q) != std::string::npos) {
            return swaig::FunctionResult(faq.value("answer", ""));
          }

          // Check keywords
          if (faq.contains("keywords") && faq["keywords"].is_array()) {
            for (const auto& kw : faq["keywords"]) {
              std::string k = kw.get<std::string>();
              std::transform(k.begin(), k.end(), k.begin(), ::tolower);
              if (lower_query.find(k) != std::string::npos) {
                return swaig::FunctionResult(faq.value("answer", ""));
              }
            }
          }
        }

        return swaig::FunctionResult("No FAQ found matching your question. Please try rephrasing.");
      });

  return *this;
}

FAQBotAgent& FAQBotAgent::set_no_match_message(const std::string& msg) {
  update_global_data(json::object({{"no_match_message", msg}}));
  return *this;
}

FAQBotAgent& FAQBotAgent::set_suggest_related(bool suggest) {
  update_global_data(json::object({{"suggest_related", suggest}}));
  return *this;
}

}  // namespace prefabs
}  // namespace signalwire
