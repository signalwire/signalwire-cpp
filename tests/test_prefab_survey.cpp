// Prefab Survey tests
#include "signalwire/prefabs/prefabs.hpp"
using namespace signalwire::prefabs;
using json = nlohmann::json;

TEST(prefab_survey_default_construction) {
  SurveyAgent agent;
  ASSERT_EQ(agent.name(), "survey");
  return true;
}

TEST(prefab_survey_named) {
  SurveyAgent agent("csat", "/csat");
  ASSERT_EQ(agent.name(), "csat");
  return true;
}

TEST(prefab_survey_has_personality) {
  SurveyAgent agent;
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_survey_has_instructions) {
  SurveyAgent agent;
  ASSERT_TRUE(agent.prompt_has_section("Survey Instructions"));
  return true;
}

TEST(prefab_survey_set_questions_and_tool) {
  SurveyAgent agent;
  agent.set_questions({json::object({{"question", "Rate our service?"}, {"type", "rating"}}),
                       json::object({{"question", "Comments?"}, {"type", "open_ended"}})});
  ASSERT_TRUE(agent.has_tool("submit_survey_answer"));
  return true;
}

TEST(prefab_survey_set_completion_message) {
  SurveyAgent agent;
  agent.set_completion_message("Thanks for your feedback!");
  return true;
}

TEST(prefab_survey_set_intro_message) {
  SurveyAgent agent;
  agent.set_intro_message("Welcome to our survey.");
  return true;
}

TEST(prefab_survey_renders_swml) {
  SurveyAgent agent;
  json swml = agent.render_swml();
  ASSERT_TRUE(swml.contains("version"));
  ASSERT_TRUE(swml.contains("sections"));
  return true;
}

// Reference parity: SurveyAgent.__init__ stores survey_name / questions /
// brand_name / introduction / conclusion / max_retries as public instance
// attributes, with the documented defaults. The port rendered the intro and
// conclusion straight into the prompt without keeping them, so a caller could
// not read back the survey's own configuration.
TEST(prefab_survey_construction_params_readable) {
  SurveyAgent agent;
  // Defaults mirror the reference.
  ASSERT_EQ(agent.brand_name(), "Our Company");
  ASSERT_EQ(agent.max_retries(), 2);
  ASSERT_EQ(agent.conclusion(),
            "Thank you for completing our survey. Your feedback is valuable to us.");
  ASSERT_EQ(agent.questions().size(), 0u);

  agent.set_survey_name("customer satisfaction survey");
  ASSERT_EQ(agent.survey_name(), "customer satisfaction survey");
  // The reference derives the default introduction from survey_name.
  ASSERT_EQ(agent.introduction(),
            "Welcome to our customer satisfaction survey. We appreciate your participation.");

  agent.set_intro_message("Hi there!");
  agent.set_completion_message("All done.");
  agent.set_brand_name("Acme");
  agent.set_max_retries(5);
  agent.set_questions({json::object({{"id", "q1"}, {"question", "How are you?"}})});

  ASSERT_EQ(agent.introduction(), "Hi there!");
  ASSERT_EQ(agent.conclusion(), "All done.");
  ASSERT_EQ(agent.brand_name(), "Acme");
  ASSERT_EQ(agent.max_retries(), 5);
  ASSERT_EQ(agent.questions().size(), 1u);
  ASSERT_EQ(agent.questions()[0]["id"], "q1");
  return true;
}

// An empty brand_name falls back to the reference default rather than blanking.
TEST(prefab_survey_brand_name_empty_falls_back) {
  SurveyAgent agent;
  agent.set_brand_name("");
  ASSERT_EQ(agent.brand_name(), "Our Company");
  return true;
}
