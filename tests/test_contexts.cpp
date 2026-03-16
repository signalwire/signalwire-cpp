// Contexts & Steps tests

#include "signalwire/contexts/contexts.hpp"

using namespace signalwire::contexts;
using json = nlohmann::json;

// ========================================================================
// GatherQuestion
// ========================================================================

TEST(gather_question_basic) {
    GatherQuestion q("name", "What is your name?");
    auto j = q.to_json();
    ASSERT_EQ(j["key"].get<std::string>(), "name");
    ASSERT_EQ(j["question"].get<std::string>(), "What is your name?");
    return true;
}

TEST(gather_question_with_type_and_confirm) {
    GatherQuestion q("age", "How old are you?", "integer", true);
    auto j = q.to_json();
    ASSERT_EQ(j["type"].get<std::string>(), "integer");
    ASSERT_TRUE(j.contains("confirm"));
    ASSERT_EQ(j["confirm"].get<bool>(), true);
    return true;
}

// ========================================================================
// GatherInfo
// ========================================================================

TEST(gather_info_basic) {
    GatherInfo gi("user_data", "next_step", "Please answer these questions");
    gi.add_question("name", "What is your name?");
    gi.add_question("email", "What is your email?");
    auto j = gi.to_json();
    ASSERT_EQ(j["output_key"].get<std::string>(), "user_data");
    ASSERT_EQ(j["completion_action"].get<std::string>(), "next_step");
    ASSERT_EQ(j["questions"].size(), 2u);
    return true;
}

// ========================================================================
// Step
// ========================================================================

TEST(step_set_text) {
    Step s("greeting");
    s.set_text("Hello, welcome!");
    auto j = s.to_json();
    ASSERT_EQ(j["name"].get<std::string>(), "greeting");
    ASSERT_EQ(j["text"].get<std::string>(), "Hello, welcome!");
    return true;
}

TEST(step_add_section) {
    Step s("intro");
    s.add_section("Task", "Greet the user warmly");
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("text"));
    ASSERT_TRUE(j["text"].get<std::string>().find("Task") != std::string::npos);
    return true;
}

TEST(step_add_bullets) {
    Step s("process");
    s.add_bullets("Instructions", {"Be polite", "Ask their name"});
    auto j = s.to_json();
    std::string text = j["text"].get<std::string>();
    ASSERT_TRUE(text.find("Be polite") != std::string::npos);
    ASSERT_TRUE(text.find("Ask their name") != std::string::npos);
    return true;
}

TEST(step_criteria) {
    Step s("gather");
    s.set_text("Gather info");
    s.set_step_criteria("User has provided their name and email");
    auto j = s.to_json();
    ASSERT_EQ(j["step_criteria"].get<std::string>(), "User has provided their name and email");
    return true;
}

TEST(step_functions_none) {
    Step s("readonly");
    s.set_text("No tools");
    s.set_functions(std::string("none"));
    auto j = s.to_json();
    ASSERT_EQ(j["functions"].get<std::string>(), "none");
    return true;
}

TEST(step_functions_list) {
    Step s("limited");
    s.set_text("Limited tools");
    s.set_functions(std::vector<std::string>{"get_weather", "search"});
    auto j = s.to_json();
    ASSERT_TRUE(j["functions"].is_array());
    ASSERT_EQ(j["functions"].size(), 2u);
    return true;
}

TEST(step_valid_steps) {
    Step s("step1");
    s.set_text("First step");
    s.set_valid_steps({"step2", "step3"});
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("valid_steps"));
    ASSERT_EQ(j["valid_steps"].size(), 2u);
    return true;
}

TEST(step_valid_contexts) {
    Step s("step1");
    s.set_text("First");
    s.set_valid_contexts({"support", "sales"});
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("valid_contexts"));
    ASSERT_EQ(j["valid_contexts"].size(), 2u);
    return true;
}

TEST(step_end) {
    Step s("final");
    s.set_text("Goodbye");
    s.set_end(true);
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("end"));
    ASSERT_EQ(j["end"].get<bool>(), true);
    return true;
}

TEST(step_skip_user_turn) {
    Step s("auto");
    s.set_text("Auto advance");
    s.set_skip_user_turn(true);
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("skip_user_turn"));
    return true;
}

TEST(step_skip_to_next_step) {
    Step s("auto");
    s.set_text("Skip ahead");
    s.set_skip_to_next_step(true);
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("skip_to_next_step"));
    return true;
}

TEST(step_gather_info) {
    Step s("gather");
    s.set_text("Gathering info");
    s.set_gather_info("customer_data", "process", "Let me get your details");
    s.add_gather_question("name", "What is your name?");
    s.add_gather_question("email", "What is your email?", "string", true);
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("gather_info"));
    ASSERT_EQ(j["gather_info"]["questions"].size(), 2u);
    return true;
}

TEST(step_reset_params) {
    Step s("switch");
    s.set_text("Switching");
    s.set_reset_system_prompt("New system prompt");
    s.set_reset_user_prompt("New user prompt");
    s.set_reset_consolidate(true);
    s.set_reset_full_reset(true);
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("reset"));
    ASSERT_EQ(j["reset"]["system_prompt"].get<std::string>(), "New system prompt");
    ASSERT_EQ(j["reset"]["user_prompt"].get<std::string>(), "New user prompt");
    ASSERT_EQ(j["reset"]["consolidate"].get<bool>(), true);
    ASSERT_EQ(j["reset"]["full_reset"].get<bool>(), true);
    return true;
}

TEST(step_clear_sections) {
    Step s("test");
    s.add_section("Title", "Body");
    s.clear_sections();
    s.set_text("New text");
    auto j = s.to_json();
    ASSERT_EQ(j["text"].get<std::string>(), "New text");
    return true;
}

// ========================================================================
// Context
// ========================================================================

TEST(context_add_step) {
    Context ctx("default");
    ctx.add_step("greeting").set_text("Hello!");
    ctx.add_step("farewell").set_text("Goodbye!");
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("steps"));
    ASSERT_EQ(j["steps"].size(), 2u);
    ASSERT_EQ(j["steps"][0]["name"].get<std::string>(), "greeting");
    ASSERT_EQ(j["steps"][1]["name"].get<std::string>(), "farewell");
    return true;
}

TEST(context_step_with_kwargs) {
    Context ctx("default");
    ctx.add_step("greet", "Greet the user", {"Be polite", "Ask name"}, "User greeted");
    auto j = ctx.to_json();
    ASSERT_EQ(j["steps"].size(), 1u);
    ASSERT_TRUE(j["steps"][0].contains("step_criteria"));
    return true;
}

TEST(context_get_step) {
    Context ctx("default");
    ctx.add_step("step1").set_text("Step 1");
    auto* s = ctx.get_step("step1");
    ASSERT_TRUE(s != nullptr);
    ASSERT_TRUE(ctx.get_step("nonexistent") == nullptr);
    return true;
}

TEST(context_remove_step) {
    Context ctx("default");
    ctx.add_step("step1").set_text("Step 1");
    ctx.add_step("step2").set_text("Step 2");
    ctx.remove_step("step1");
    auto j = ctx.to_json();
    ASSERT_EQ(j["steps"].size(), 1u);
    ASSERT_EQ(j["steps"][0]["name"].get<std::string>(), "step2");
    return true;
}

TEST(context_move_step) {
    Context ctx("default");
    ctx.add_step("step1").set_text("Step 1");
    ctx.add_step("step2").set_text("Step 2");
    ctx.add_step("step3").set_text("Step 3");
    ctx.move_step("step3", 0);
    auto j = ctx.to_json();
    ASSERT_EQ(j["steps"][0]["name"].get<std::string>(), "step3");
    return true;
}

TEST(context_set_system_prompt) {
    Context ctx("support");
    ctx.add_step("intro").set_text("Welcome to support");
    ctx.set_system_prompt("You are a support agent");
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("system_prompt"));
    return true;
}

TEST(context_set_prompt) {
    Context ctx("default");
    ctx.add_step("s1").set_text("Step");
    ctx.set_prompt("Context prompt text");
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("prompt"));
    return true;
}

TEST(context_set_post_prompt) {
    Context ctx("default");
    ctx.add_step("s1").set_text("Step");
    ctx.set_post_prompt("Summary please");
    auto j = ctx.to_json();
    ASSERT_EQ(j["post_prompt"].get<std::string>(), "Summary please");
    return true;
}

TEST(context_valid_contexts) {
    Context ctx("main");
    ctx.add_step("s1").set_text("Step");
    ctx.set_valid_contexts({"support", "sales"});
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("valid_contexts"));
    ASSERT_EQ(j["valid_contexts"].size(), 2u);
    return true;
}

TEST(context_consolidate_and_reset) {
    Context ctx("support");
    ctx.add_step("s1").set_text("Step");
    ctx.set_consolidate(true);
    ctx.set_full_reset(true);
    ctx.set_isolated(true);
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("consolidate"));
    ASSERT_TRUE(j.contains("full_reset"));
    ASSERT_TRUE(j.contains("isolated"));
    return true;
}

TEST(context_fillers) {
    Context ctx("default");
    ctx.add_step("s1").set_text("Step");
    ctx.add_enter_filler("en-US", {"Welcome!", "Hello there!"});
    ctx.add_exit_filler("en-US", {"Goodbye!", "Thank you!"});
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("enter_fillers"));
    ASSERT_TRUE(j.contains("exit_fillers"));
    return true;
}

TEST(context_add_system_section) {
    Context ctx("default");
    ctx.add_step("s1").set_text("Step");
    ctx.add_system_section("Personality", "Be friendly");
    ctx.add_system_bullets("Rules", {"Rule 1", "Rule 2"});
    auto j = ctx.to_json();
    ASSERT_TRUE(j.contains("system_prompt"));
    return true;
}

TEST(context_add_prompt_sections) {
    Context ctx("default");
    ctx.add_step("s1").set_text("Step");
    ctx.add_section("Overview", "This is an overview");
    ctx.add_bullets("Guidelines", {"Guideline 1", "Guideline 2"});
    auto j = ctx.to_json();
    // Should use rendered prompt since we added sections
    ASSERT_TRUE(j.contains("prompt"));
    return true;
}

// ========================================================================
// ContextBuilder
// ========================================================================

TEST(context_builder_single_default) {
    ContextBuilder cb;
    auto& ctx = cb.add_context("default");
    ctx.add_step("greeting").set_text("Hello!");
    cb.validate(); // Should not throw
    auto j = cb.to_json();
    ASSERT_TRUE(j.contains("default"));
    return true;
}

TEST(context_builder_single_non_default_fails) {
    ContextBuilder cb;
    auto& ctx = cb.add_context("main");
    ctx.add_step("s1").set_text("Step");
    ASSERT_THROWS(cb.validate());
    return true;
}

TEST(context_builder_multiple_contexts) {
    ContextBuilder cb;
    auto& main = cb.add_context("default");
    main.add_step("s1").set_text("Main step");
    main.set_valid_contexts({"support"});

    auto& support = cb.add_context("support");
    support.add_step("s1").set_text("Support step");

    cb.validate(); // Should not throw
    auto j = cb.to_json();
    ASSERT_TRUE(j.contains("default"));
    ASSERT_TRUE(j.contains("support"));
    return true;
}

TEST(context_builder_get_context) {
    ContextBuilder cb;
    cb.add_context("default").add_step("s1").set_text("Step");
    ASSERT_TRUE(cb.get_context("default") != nullptr);
    ASSERT_TRUE(cb.get_context("nonexistent") == nullptr);
    return true;
}

TEST(context_builder_max_contexts) {
    ContextBuilder cb;
    for (int i = 0; i < MAX_CONTEXTS; ++i) {
        auto& c = cb.add_context("ctx_" + std::to_string(i));
        c.add_step("s").set_text("Step");
    }
    // Adding one more should throw
    ASSERT_THROWS(cb.add_context("overflow"));
    return true;
}

TEST(context_builder_preserves_order) {
    ContextBuilder cb;
    cb.add_context("default").add_step("s1").set_text("First");
    cb.add_context("second").add_step("s1").set_text("Second");
    cb.add_context("third").add_step("s1").set_text("Third");

    auto j = cb.to_json();
    // Check that keys come out in insertion order
    auto it = j.begin();
    ASSERT_EQ(it.key(), "default");
    ++it;
    ASSERT_EQ(it.key(), "second");
    ++it;
    ASSERT_EQ(it.key(), "third");
    return true;
}

TEST(context_step_order_preserved) {
    Context ctx("default");
    ctx.add_step("alpha").set_text("A");
    ctx.add_step("beta").set_text("B");
    ctx.add_step("gamma").set_text("C");
    auto j = ctx.to_json();
    ASSERT_EQ(j["steps"][0]["name"].get<std::string>(), "alpha");
    ASSERT_EQ(j["steps"][1]["name"].get<std::string>(), "beta");
    ASSERT_EQ(j["steps"][2]["name"].get<std::string>(), "gamma");
    return true;
}
