// Prompt mixin tests — POM, text, sections, subsections, rendering

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using json = nlohmann::json;

// ========================================================================
// Raw text prompt
// ========================================================================

TEST(prompt_set_text_and_get) {
    AgentBase agent;
    agent.set_prompt_text("You are a helpful assistant.");
    ASSERT_EQ(agent.get_prompt(), "You are a helpful assistant.");
    return true;
}

TEST(prompt_raw_text_overrides_pom) {
    AgentBase agent;
    agent.prompt_add_section("Section1", "Body1");
    agent.set_prompt_text("Raw override");
    // get_prompt returns raw text when set
    ASSERT_EQ(agent.get_prompt(), "Raw override");
    return true;
}

TEST(prompt_pom_used_when_no_raw_text) {
    AgentBase agent;
    agent.prompt_add_section("Greeting", "Hello user");
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("## Greeting") != std::string::npos);
    ASSERT_TRUE(prompt.find("Hello user") != std::string::npos);
    return true;
}

TEST(prompt_empty_when_nothing_set) {
    AgentBase agent;
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.empty());
    return true;
}

// ========================================================================
// POM sections
// ========================================================================

TEST(prompt_add_section_basic) {
    AgentBase agent;
    agent.prompt_add_section("Personality", "You are kind.");
    ASSERT_TRUE(agent.prompt_has_section("Personality"));
    ASSERT_FALSE(agent.prompt_has_section("Missing"));
    return true;
}

TEST(prompt_add_section_with_bullets) {
    AgentBase agent;
    agent.prompt_add_section("Rules", "", {"Rule A", "Rule B", "Rule C"});
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("- Rule A") != std::string::npos);
    ASSERT_TRUE(prompt.find("- Rule B") != std::string::npos);
    ASSERT_TRUE(prompt.find("- Rule C") != std::string::npos);
    return true;
}

TEST(prompt_add_section_body_and_bullets) {
    AgentBase agent;
    agent.prompt_add_section("Instructions", "Follow these steps:", {"Step 1", "Step 2"});
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Follow these steps:") != std::string::npos);
    ASSERT_TRUE(prompt.find("- Step 1") != std::string::npos);
    return true;
}

TEST(prompt_multiple_sections_ordering) {
    AgentBase agent;
    agent.prompt_add_section("Alpha", "First");
    agent.prompt_add_section("Beta", "Second");
    agent.prompt_add_section("Gamma", "Third");
    std::string prompt = agent.get_prompt();
    auto alpha_pos = prompt.find("Alpha");
    auto beta_pos = prompt.find("Beta");
    auto gamma_pos = prompt.find("Gamma");
    ASSERT_TRUE(alpha_pos < beta_pos);
    ASSERT_TRUE(beta_pos < gamma_pos);
    return true;
}

TEST(prompt_has_section_case_sensitive) {
    AgentBase agent;
    agent.prompt_add_section("MySection", "body");
    ASSERT_TRUE(agent.prompt_has_section("MySection"));
    ASSERT_FALSE(agent.prompt_has_section("mysection"));
    ASSERT_FALSE(agent.prompt_has_section("MYSECTION"));
    return true;
}

// ========================================================================
// Subsections
// ========================================================================

TEST(prompt_add_subsection_basic) {
    AgentBase agent;
    agent.prompt_add_section("Parent", "Parent body");
    agent.prompt_add_subsection("Parent", "Child", "Child body");
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("## Parent") != std::string::npos);
    ASSERT_TRUE(prompt.find("### Child") != std::string::npos);
    ASSERT_TRUE(prompt.find("Child body") != std::string::npos);
    return true;
}

TEST(prompt_add_subsection_with_bullets) {
    AgentBase agent;
    agent.prompt_add_section("Main", "");
    agent.prompt_add_subsection("Main", "Details", "", {"Detail 1", "Detail 2"});
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("### Details") != std::string::npos);
    ASSERT_TRUE(prompt.find("- Detail 1") != std::string::npos);
    return true;
}

TEST(prompt_add_subsection_to_nonexistent_parent) {
    AgentBase agent;
    // Adding subsection to non-existent parent should not crash; it just does nothing
    agent.prompt_add_subsection("MissingParent", "Child", "Body");
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Child") == std::string::npos);
    return true;
}

TEST(prompt_multiple_subsections) {
    AgentBase agent;
    agent.prompt_add_section("Root", "Root body");
    agent.prompt_add_subsection("Root", "SubA", "SubA body");
    agent.prompt_add_subsection("Root", "SubB", "SubB body");
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("### SubA") != std::string::npos);
    ASSERT_TRUE(prompt.find("### SubB") != std::string::npos);
    return true;
}

// ========================================================================
// Add to section (append)
// ========================================================================

TEST(prompt_add_to_existing_section_body) {
    AgentBase agent;
    agent.prompt_add_section("Rules", "Rule 1");
    agent.prompt_add_to_section("Rules", "Rule 2");
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Rule 1") != std::string::npos);
    ASSERT_TRUE(prompt.find("Rule 2") != std::string::npos);
    return true;
}

TEST(prompt_add_to_existing_section_bullets) {
    AgentBase agent;
    agent.prompt_add_section("Items", "", {"Item A"});
    agent.prompt_add_to_section("Items", "", {"Item B", "Item C"});
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("Item A") != std::string::npos);
    ASSERT_TRUE(prompt.find("Item B") != std::string::npos);
    ASSERT_TRUE(prompt.find("Item C") != std::string::npos);
    return true;
}

TEST(prompt_add_to_nonexistent_section_creates_it) {
    AgentBase agent;
    agent.prompt_add_to_section("NewSection", "Body text");
    ASSERT_TRUE(agent.prompt_has_section("NewSection"));
    std::string prompt = agent.get_prompt();
    ASSERT_TRUE(prompt.find("NewSection") != std::string::npos);
    return true;
}

// ========================================================================
// POM JSON rendering
// ========================================================================

TEST(prompt_pom_json_structure) {
    AgentBase agent;
    agent.set_use_pom(true);
    agent.prompt_add_section("Title1", "Body1", {"B1"});
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            auto& prompt = verb["ai"]["prompt"];
            ASSERT_TRUE(prompt.contains("pom"));
            ASSERT_EQ(prompt["pom"].size(), 1u);
            ASSERT_EQ(prompt["pom"][0]["title"].get<std::string>(), "Title1");
            ASSERT_EQ(prompt["pom"][0]["body"].get<std::string>(), "Body1");
            ASSERT_EQ(prompt["pom"][0]["bullets"].size(), 1u);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(prompt_pom_json_subsection_structure) {
    AgentBase agent;
    agent.set_use_pom(true);
    agent.prompt_add_section("Parent", "PBody");
    agent.prompt_add_subsection("Parent", "Child", "CBody", {"CB1"});
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            auto& pom = verb["ai"]["prompt"]["pom"][0];
            ASSERT_TRUE(pom.contains("subsections"));
            ASSERT_EQ(pom["subsections"].size(), 1u);
            ASSERT_EQ(pom["subsections"][0]["title"].get<std::string>(), "Child");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(prompt_text_mode_rendering) {
    AgentBase agent;
    agent.set_use_pom(false);
    agent.set_prompt_text("Plain text prompt");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            ASSERT_EQ(verb["ai"]["prompt"]["text"].get<std::string>(), "Plain text prompt");
            ASSERT_FALSE(verb["ai"]["prompt"].contains("pom"));
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(prompt_pom_section_to_json_empty_body) {
    PomSection section;
    section.title = "Test";
    section.body = "";
    section.bullets = {};
    auto j = section.to_json();
    ASSERT_EQ(j["title"].get<std::string>(), "Test");
    ASSERT_FALSE(j.contains("body"));
    ASSERT_FALSE(j.contains("bullets"));
    return true;
}

TEST(prompt_post_prompt_in_swml) {
    AgentBase agent;
    agent.set_post_prompt("Summarize the conversation");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("post_prompt")) {
            ASSERT_EQ(verb["ai"]["post_prompt"]["text"].get<std::string>(),
                       "Summarize the conversation");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(prompt_post_prompt_url_in_swml) {
    AgentBase agent;
    agent.set_post_prompt_url("https://example.com/summary");
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("post_prompt_url")) {
            ASSERT_EQ(verb["ai"]["post_prompt_url"].get<std::string>(),
                       "https://example.com/summary");
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(prompt_llm_params_in_prompt_section) {
    AgentBase agent;
    agent.set_prompt_text("Hello");
    agent.set_prompt_llm_params(json::object({{"temperature", 0.5}, {"top_p", 0.9}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("prompt")) {
            auto& prompt = verb["ai"]["prompt"];
            ASSERT_EQ(prompt["temperature"].get<double>(), 0.5);
            ASSERT_EQ(prompt["top_p"].get<double>(), 0.9);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

TEST(prompt_post_prompt_llm_params) {
    AgentBase agent;
    agent.set_post_prompt("Summary");
    agent.set_post_prompt_llm_params(json::object({{"temperature", 0.2}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai") && verb["ai"].contains("post_prompt")) {
            ASSERT_EQ(verb["ai"]["post_prompt"]["temperature"].get<double>(), 0.2);
            return true;
        }
    }
    ASSERT_TRUE(false);
    return true;
}

// ========================================================================
// pom() accessor (Python parity: agent.pom)
//
// Mirrors signalwire-python tests/unit/core/test_agent_base.py::
//   TestAgentBasePromptMethods::test_set_prompt_pom_succeeds_when_use_pom_true
// ========================================================================

TEST(pom_returns_sections_after_prompt_add_section) {
    AgentBase agent;
    agent.prompt_add_section("Greeting", "Hello");
    auto pom = agent.pom();
    ASSERT_TRUE(pom.has_value());
    ASSERT_EQ(pom->size(), 1u);
    ASSERT_EQ((*pom)[0]["title"].get<std::string>(), "Greeting");
    ASSERT_EQ((*pom)[0]["body"].get<std::string>(), "Hello");
    return true;
}

TEST(pom_nullopt_when_use_pom_false) {
    AgentBase agent;
    agent.set_use_pom(false);
    auto pom = agent.pom();
    ASSERT_TRUE(!pom.has_value());
    return true;
}

TEST(pom_returns_copy_not_internal_vector) {
    AgentBase agent;
    agent.prompt_add_section("Original", "Body");

    auto pom = agent.pom();
    ASSERT_TRUE(pom.has_value());
    ASSERT_EQ(pom->size(), 1u);

    // Mutate the returned vector; agent state must be unaffected.
    pom->push_back(json::object({{"title", "Injected"}}));
    (*pom)[0]["title"] = "Hijacked";

    auto fresh = agent.pom();
    ASSERT_TRUE(fresh.has_value());
    ASSERT_EQ(fresh->size(), 1u);
    ASSERT_EQ((*fresh)[0]["title"].get<std::string>(), "Original");
    return true;
}
