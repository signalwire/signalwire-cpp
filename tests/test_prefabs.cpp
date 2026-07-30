// Prefab agent tests

#include "signalwire/prefabs/prefabs.hpp"

using namespace signalwire::prefabs;
using json = nlohmann::json;

TEST(prefab_info_gatherer_creation) {
  InfoGathererAgent agent("ig", "/ig");
  ASSERT_EQ(agent.name(), "ig");
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  ASSERT_TRUE(agent.prompt_has_section("Instructions"));
  return true;
}

TEST(prefab_info_gatherer_set_questions) {
  InfoGathererAgent agent;
  agent.set_questions(
      {json::object({{"key_name", "name"}, {"question_text", "What is your name?"}}),
       json::object({{"key_name", "age"}, {"question_text", "How old are you?"}})});
  ASSERT_TRUE(agent.has_skill("info_gatherer"));
  return true;
}

TEST(prefab_survey_creation) {
  SurveyAgent agent("survey", "/survey");
  ASSERT_EQ(agent.name(), "survey");
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  ASSERT_TRUE(agent.prompt_has_section("Survey Instructions"));
  return true;
}

TEST(prefab_survey_set_questions) {
  SurveyAgent agent;
  agent.set_questions({json::object({{"question", "How satisfied are you?"}, {"type", "rating"}}),
                       json::object({{"question", "Any comments?"}, {"type", "open_ended"}})});
  ASSERT_TRUE(agent.has_tool("submit_survey_answer"));
  return true;
}

TEST(prefab_receptionist_creation) {
  ReceptionistAgent agent("receptionist", "/reception");
  ASSERT_EQ(agent.name(), "receptionist");
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_receptionist_set_departments) {
  ReceptionistAgent agent;
  agent.set_departments(json::object(
      {{"sales",
        json::object({{"url", "https://example.com/sales"}, {"description", "Sales team"}})},
       {"support",
        json::object({{"url", "https://example.com/support"}, {"description", "Support team"}})}}));
  ASSERT_TRUE(agent.has_skill("swml_transfer"));
  return true;
}

TEST(prefab_faq_bot_creation) {
  FAQBotAgent agent("faq", "/faq");
  ASSERT_EQ(agent.name(), "faq");
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_faq_bot_set_faqs) {
  FAQBotAgent agent;
  agent.set_faqs({json::object({{"question", "What are your hours?"},
                                {"answer", "We are open 9-5"},
                                {"keywords", json::array({"hours", "open", "close"})}})});
  ASSERT_TRUE(agent.has_tool("search_faq"));
  return true;
}

TEST(prefab_concierge_creation) {
  ConciergeAgent agent("concierge", "/concierge");
  ASSERT_EQ(agent.name(), "concierge");
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  return true;
}

TEST(prefab_concierge_set_amenities) {
  ConciergeAgent agent;
  agent.set_venue_name("Grand Hotel");
  agent.set_amenities(
      {json::object({{"name", "Pool"}, {"location", "Floor 2"}, {"available", true}}),
       json::object({{"name", "Gym"}, {"location", "Floor 1"}, {"available", true}})});
  ASSERT_TRUE(agent.has_tool("check_amenity"));
  ASSERT_TRUE(agent.prompt_has_section("Available Amenities"));
  return true;
}

TEST(prefab_concierge_set_hours) {
  ConciergeAgent agent;
  agent.set_hours(
      json::object({{"Monday", "9:00 AM - 9:00 PM"}, {"Tuesday", "9:00 AM - 9:00 PM"}}));
  ASSERT_TRUE(agent.prompt_has_section("Venue Hours"));
  return true;
}
