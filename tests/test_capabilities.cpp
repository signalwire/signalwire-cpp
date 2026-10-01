// Reading what a client declares it can render (signalwire-python
// tests/unit/core/test_capabilities.py). The rule pinned here: absence means
// no -- malformed or missing data resolves to "not declared".

#include "signalwire/core/capabilities.hpp"

namespace caps_test {

inline nlohmann::json body() {
  return {{"vars",
           {{"userVariables",
             {{"capabilities",
               {{"display_content", true}, {"transcript", true}, {"chat_handoff", false}}},
              {"metadata", {{"widget", {{"opened_at", "2026-01-01T00:00:00Z"}}}}}}}}}};
}

}  // namespace caps_test

using signalwire::core::capabilities::declared_capabilities;
using signalwire::core::capabilities::has_capability;
using signalwire::core::capabilities::user_variables;

TEST(capabilities_user_variables_extracts_nested_shape) {
  ASSERT_TRUE(user_variables(caps_test::body()).contains("capabilities"));
  return true;
}

TEST(capabilities_user_variables_missing_levels_yield_empty) {
  for (const auto& junk :
       {nlohmann::json(nullptr), nlohmann::json::object(), nlohmann::json("nonsense"),
        nlohmann::json(42), nlohmann::json{{"vars", nullptr}},
        nlohmann::json{{"vars", nlohmann::json::object()}},
        nlohmann::json{{"vars", {{"userVariables", nullptr}}}},
        nlohmann::json{{"vars", {{"userVariables", "not a dict"}}}}}) {
    ASSERT_EQ(user_variables(junk), nlohmann::json::object());
  }
  return true;
}

TEST(capabilities_only_truthy_names_are_declared) {
  auto caps = declared_capabilities(caps_test::body());
  ASSERT_EQ(caps, (std::set<std::string>{"display_content", "transcript"}));
  ASSERT_FALSE(has_capability(caps_test::body(), "chat_handoff"));
  ASSERT_TRUE(has_capability(caps_test::body(), "display_content"));
  return true;
}

TEST(capabilities_accepts_already_extracted_user_variables) {
  ASSERT_EQ(declared_capabilities(nlohmann::json{{"capabilities", {{"a", true}}}}),
            (std::set<std::string>{"a"}));
  ASSERT_TRUE(
      has_capability(nlohmann::json{{"capabilities", {{"future_thing", true}}}}, "future_thing"));
  return true;
}

TEST(capabilities_absence_and_malformation_mean_no) {
  for (const auto& junk :
       {nlohmann::json(nullptr), nlohmann::json::object(), nlohmann::json("nonsense"),
        nlohmann::json(42),
        nlohmann::json{{"vars", {{"userVariables", {{"capabilities", "not a dict"}}}}}},
        nlohmann::json{{"vars", {{"userVariables", {{"capabilities", nullptr}}}}}},
        nlohmann::json{{"vars", {{"userVariables", nlohmann::json::object()}}}}}) {
    ASSERT_TRUE(declared_capabilities(junk).empty());
    ASSERT_FALSE(has_capability(junk, "display_content"));
  }
  return true;
}
