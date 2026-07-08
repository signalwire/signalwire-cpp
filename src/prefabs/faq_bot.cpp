// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include <algorithm>
#include <cctype>

#include "signalwire/prefabs/prefabs.hpp"

namespace signalwire {
namespace prefabs {

namespace {
std::string to_lower(const std::string& s) {
  std::string out = s;
  std::transform(out.begin(), out.end(), out.begin(),
                 [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}
}  // namespace

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

  faqs_.assign(faqs.begin(), faqs.end());
  update_global_data(json::object({{"faqs", faq_data}}));
  prompt_add_section("FAQ Topics", "Available FAQ topics:", bullets);

  // Register the search_faqs tool bound to the member handler (ported from
  // the Java FAQBotAgent.search_faqs).
  define_tool(
      "search_faqs", "Search for FAQs matching a specific query or category",
      json::object(
          {{"type", "object"},
           {"properties",
            json::object(
                {{"query", json::object({{"type", "string"}, {"description", "The search query"}})},
                 {"category", json::object({{"type", "string"},
                                            {"description", "Optional category to filter by"}})}})},
           {"required", json::array({"query"})}}),
      [this](const json& args, const json& raw) { return search_faqs(args, raw); });

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

swaig::FunctionResult FAQBotAgent::search_faqs(const json& args, const json&) {
  std::string query = to_lower(args.value("query", ""));
  std::string category = to_lower(args.value("category", ""));

  // (score, question) pairs for matching FAQs.
  std::vector<std::pair<int, std::string>> results;

  for (const auto& faq : faqs_) {
    std::string question = to_lower(faq.value("question", ""));
    std::vector<std::string> categories;
    if (faq.contains("categories") && faq["categories"].is_array()) {
      for (const auto& c : faq["categories"]) {
        categories.push_back(to_lower(c.get<std::string>()));
      }
    }

    int match_score = 0;
    if (!query.empty() && question.find(query) != std::string::npos) {
      match_score += (query == question) ? 100 : 50;
      if (question.rfind(query, 0) == 0) {
        match_score += 25;
      }
    }
    if (!category.empty() &&
        std::find(categories.begin(), categories.end(), category) != categories.end()) {
      match_score += 30;
    }

    if (match_score > 0) {
      results.emplace_back(match_score, faq.value("question", ""));
    }
  }

  std::stable_sort(results.begin(), results.end(),
                   [](const auto& a, const auto& b) { return a.first > b.first; });

  if (!results.empty()) {
    std::string out = "Here are the most relevant FAQs:\n\n";
    size_t limit = std::min<size_t>(3, results.size());
    for (size_t i = 0; i < limit; ++i) {
      out += std::to_string(i + 1) + ". " + results[i].second + "\n";
    }
    return swaig::FunctionResult(out);
  }
  return swaig::FunctionResult("No matching FAQs found.");
}

FAQBotAgent& FAQBotAgent::on_summary(agent::SummaryCallback cb) {
  AgentBase::on_summary(std::move(cb));
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
