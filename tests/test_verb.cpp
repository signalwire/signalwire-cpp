// Verb mixin tests — pre/post answer verbs, answer config, clear methods

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using json = nlohmann::json;

// ========================================================================
// Pre-answer verbs
// ========================================================================

TEST(verb_add_pre_answer_verb) {
    AgentBase agent;
    agent.add_pre_answer_verb("play", json::object({{"url", "https://cdn.example.com/ring.mp3"}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main[0].contains("play"));
    ASSERT_EQ(main[0]["play"]["url"].get<std::string>(), "https://cdn.example.com/ring.mp3");
    return true;
}

TEST(verb_multiple_pre_answer_verbs) {
    AgentBase agent;
    agent.add_pre_answer_verb("play", json::object({{"url", "https://cdn.example.com/hold.mp3"}}));
    agent.add_pre_answer_verb("sleep", json(500));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main[0].contains("play"));
    ASSERT_TRUE(main[1].contains("sleep"));
    // Answer should come after
    ASSERT_TRUE(main[2].contains("answer"));
    return true;
}

TEST(verb_clear_pre_answer_verbs) {
    AgentBase agent;
    agent.add_pre_answer_verb("play", json::object());
    agent.clear_pre_answer_verbs();
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main[0].contains("answer"));
    return true;
}

// ========================================================================
// Post-answer verbs
// ========================================================================

TEST(verb_add_post_answer_verb) {
    AgentBase agent;
    agent.add_post_answer_verb("play", json::object({{"url", "https://cdn.example.com/welcome.mp3"}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // Sequence: answer, play, ai
    ASSERT_TRUE(main[0].contains("answer"));
    ASSERT_TRUE(main[1].contains("play"));
    ASSERT_TRUE(main[2].contains("ai"));
    return true;
}

TEST(verb_multiple_post_answer_verbs) {
    AgentBase agent;
    agent.add_post_answer_verb("play", json::object({{"url", "https://cdn.example.com/beep.mp3"}}));
    agent.add_post_answer_verb("sleep", json(200));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // answer(0), play(1), sleep(2), ai(3)
    ASSERT_TRUE(main[1].contains("play"));
    ASSERT_TRUE(main[2].contains("sleep"));
    ASSERT_TRUE(main[3].contains("ai"));
    return true;
}

TEST(verb_clear_post_answer_verbs) {
    AgentBase agent;
    agent.add_post_answer_verb("play", json::object());
    agent.clear_post_answer_verbs();
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // answer(0), ai(1) — no play in between
    ASSERT_TRUE(main[0].contains("answer"));
    ASSERT_TRUE(main[1].contains("ai"));
    return true;
}

// ========================================================================
// Post-AI verbs
// ========================================================================

TEST(verb_add_post_ai_verb) {
    AgentBase agent;
    agent.add_post_ai_verb("hangup", json::object());
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main.back().contains("hangup"));
    return true;
}

TEST(verb_multiple_post_ai_verbs) {
    AgentBase agent;
    agent.add_post_ai_verb("play", json::object({{"url", "https://cdn.example.com/goodbye.mp3"}}));
    agent.add_post_ai_verb("hangup", json::object());
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    // ai should be followed by play then hangup
    ASSERT_TRUE(main[main.size() - 2].contains("play"));
    ASSERT_TRUE(main[main.size() - 1].contains("hangup"));
    return true;
}

TEST(verb_clear_post_ai_verbs) {
    AgentBase agent;
    agent.add_post_ai_verb("hangup", json::object());
    agent.clear_post_ai_verbs();
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main.back().contains("ai"));
    return true;
}

// ========================================================================
// Default answer verb
// ========================================================================

TEST(verb_default_answer_verb) {
    AgentBase agent;
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main[0].contains("answer"));
    ASSERT_EQ(main[0]["answer"]["max_duration"].get<int>(), 3600);
    return true;
}

TEST(verb_custom_answer_verb) {
    AgentBase agent;
    agent.add_answer_verb("answer", json::object({{"max_duration", 7200}}));
    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];
    ASSERT_TRUE(main[0].contains("answer"));
    ASSERT_EQ(main[0]["answer"]["max_duration"].get<int>(), 7200);
    return true;
}

// ========================================================================
// Complete 5-phase pipeline
// ========================================================================

TEST(verb_full_5_phase_pipeline) {
    AgentBase agent;
    agent.add_pre_answer_verb("play", json::object({{"url", "https://cdn.example.com/ring.mp3"}}));
    agent.add_answer_verb("answer", json::object({{"max_duration", 1800}}));
    agent.add_post_answer_verb("play", json::object({{"url", "https://cdn.example.com/welcome.mp3"}}));
    agent.add_post_ai_verb("hangup", json::object());
    agent.set_prompt_text("Hello");

    json swml = agent.render_swml();
    auto& main = swml["sections"]["main"];

    // Phase 1: pre-answer
    ASSERT_TRUE(main[0].contains("play"));
    ASSERT_EQ(main[0]["play"]["url"].get<std::string>(), "https://cdn.example.com/ring.mp3");
    // Phase 2: answer
    ASSERT_TRUE(main[1].contains("answer"));
    ASSERT_EQ(main[1]["answer"]["max_duration"].get<int>(), 1800);
    // Phase 3: post-answer
    ASSERT_TRUE(main[2].contains("play"));
    ASSERT_EQ(main[2]["play"]["url"].get<std::string>(), "https://cdn.example.com/welcome.mp3");
    // Phase 4: ai
    ASSERT_TRUE(main[3].contains("ai"));
    // Phase 5: post-ai
    ASSERT_TRUE(main[4].contains("hangup"));

    ASSERT_EQ(main.size(), 5u);
    return true;
}

// ========================================================================
// Method chaining
// ========================================================================

TEST(verb_method_chaining) {
    AgentBase agent;
    // `play {}` is schema-INVALID (PlayWithURL/PlayWithURLS require url/urls),
    // and the render now goes through the validator, so use a real url.
    auto& ref = agent.add_pre_answer_verb("play",
                                          json::object({{"url", "https://cdn.example.com/r.mp3"}}))
                      .add_post_answer_verb("sleep", json(100))
                      .add_post_ai_verb("hangup", json::object());
    ASSERT_EQ(&ref, &agent);
    return true;
}
