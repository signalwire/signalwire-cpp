// Copyright (c) 2025 SignalWire — MIT License
// Demonstrates contexts/steps system with multi-persona workflows.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("Computer Sales", "/contexts-demo");

    agent.prompt_add_section("Instructions",
        "Follow the structured sales workflow.", {
            "Complete each step's criteria before advancing",
            "Be helpful and consultative"
        });

    auto& ctx = agent.define_contexts();

    // Sales context
    auto& sales = ctx.add_context("sales");
    sales.set_isolated(true);
    sales.add_section("Role", "You are Franklin, a computer sales agent.");
    sales.add_step("determine_use_case")
        .add_section("Task", "Identify the customer's primary use case")
        .add_bullets("Questions", {
            "What will they use the computer for?",
            "Do they play games?",
            "Do they need it for work?"
        })
        .set_step_criteria("Customer has stated: GAMING, WORK, or BALANCED")
        .set_valid_steps({"determine_form_factor"})
        .set_valid_contexts({"tech_support", "manager"});

    sales.add_step("determine_form_factor")
        .add_section("Task", "Laptop or desktop?")
        .set_step_criteria("Customer has chosen LAPTOP or DESKTOP")
        .set_valid_steps({"make_recommendation"})
        .set_valid_contexts({"tech_support", "manager"});

    sales.add_step("make_recommendation")
        .add_section("Task", "Provide a specific recommendation")
        .set_step_criteria("Customer has received a recommendation")
        .set_valid_contexts({"tech_support", "manager"});

    // Tech support context
    auto& tech = ctx.add_context("tech_support");
    tech.set_isolated(true);
    tech.add_section("Role", "You are Rachael, a technical support specialist.");
    tech.add_step("provide_support")
        .add_section("Task", "Help with technical questions")
        .set_step_criteria("Question answered")
        .set_valid_contexts({"sales", "manager"});

    // Manager context
    auto& mgr = ctx.add_context("manager");
    mgr.set_isolated(true);
    mgr.add_section("Role", "You are Dwight, the store manager.");
    mgr.add_enter_filler("en-US", {"Let me connect you with the manager..."});
    mgr.add_step("handle_escalation")
        .add_section("Task", "Resolve the customer's concerns")
        .set_step_criteria("Issue resolved")
        .set_valid_contexts({"sales", "tech_support"});

    // Languages for each persona
    agent.add_language({"English-Franklin", "en-US", "inworld.Mark"});
    agent.add_language({"English-Rachael", "en-US", "inworld.Sarah"});
    agent.add_language({"English-Dwight", "en-US", "inworld.Blake"});

    std::cout << "Contexts demo at http://0.0.0.0:3000/contexts-demo\n";
    agent.run();
}
