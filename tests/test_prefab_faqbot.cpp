// Prefab FAQBot tests
#include "signalwire/prefabs/prefabs.hpp"
using namespace signalwire::prefabs;
using json = nlohmann::json;

TEST(prefab_faqbot_default) {
    FAQBotAgent agent;
    ASSERT_EQ(agent.name(), "faq_bot");
    return true;
}

TEST(prefab_faqbot_named) {
    FAQBotAgent agent("helpdesk", "/help");
    ASSERT_EQ(agent.name(), "helpdesk");
    return true;
}

TEST(prefab_faqbot_has_personality) {
    FAQBotAgent agent;
    ASSERT_TRUE(agent.prompt_has_section("Personality"));
    return true;
}

TEST(prefab_faqbot_set_faqs) {
    FAQBotAgent agent;
    agent.set_faqs({
        json::object({
            {"question", "What are your hours?"},
            {"answer", "9-5 weekdays"},
            {"keywords", json::array({"hours", "open"})}
        }),
        json::object({
            {"question", "Where are you located?"},
            {"answer", "123 Main St"},
            {"keywords", json::array({"location", "address"})}
        })
    });
    ASSERT_TRUE(agent.has_tool("search_faq"));
    return true;
}

TEST(prefab_faqbot_set_no_match_message) {
    FAQBotAgent agent;
    agent.set_no_match_message("I don't have that info.");
    return true;
}

TEST(prefab_faqbot_set_suggest_related) {
    FAQBotAgent agent;
    agent.set_suggest_related(true);
    return true;
}

TEST(prefab_faqbot_renders_swml) {
    FAQBotAgent agent;
    json swml = agent.render_swml();
    ASSERT_TRUE(swml.contains("version"));
    return true;
}

// Reference parity: FAQBotAgent.__init__ stores faqs / suggest_related /
// persona as public instance attributes. The port published suggest_related
// to global data without keeping it and had no persona at all.
TEST(prefab_faqbot_construction_params_readable) {
    FAQBotAgent agent;
    ASSERT_TRUE(agent.suggest_related());  // reference default: True
    ASSERT_EQ(agent.persona(),
              "You are a helpful FAQ bot that provides accurate answers to common questions.");
    ASSERT_EQ(agent.faqs().size(), 0u);

    agent.set_faqs({json::object({{"question", "Hours?"}, {"answer", "9-5"}})});
    agent.set_suggest_related(false);
    agent.set_persona("You are a terse FAQ bot.");

    ASSERT_EQ(agent.faqs().size(), 1u);
    ASSERT_EQ(agent.faqs()[0]["question"], "Hours?");
    ASSERT_FALSE(agent.suggest_related());
    ASSERT_EQ(agent.persona(), "You are a terse FAQ bot.");
    return true;
}

// An empty persona falls back to the reference default (`persona or "…"`).
TEST(prefab_faqbot_persona_empty_falls_back) {
    FAQBotAgent agent;
    agent.set_persona("");
    ASSERT_EQ(agent.persona(),
              "You are a helpful FAQ bot that provides accurate answers to common questions.");
    return true;
}
