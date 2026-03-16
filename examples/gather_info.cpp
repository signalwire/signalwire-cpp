// Copyright (c) 2025 SignalWire — MIT License
// GatherInfo demo using contexts/steps with question collection.

#include <signalwire/agent/agent_base.hpp>

using namespace signalwire;

int main() {
    agent::AgentBase agent("gather-info", "/gather-info");

    agent.prompt_add_section("Role", "You are a friendly intake assistant.");

    auto& ctx = agent.define_contexts();
    auto& intake = ctx.add_context("default");

    // Step with gather questions
    intake.add_step("collect_info")
        .add_section("Task", "Collect customer information")
        .set_gather_info("customer_data", "summarize", "Please provide your details.")
        .add_gather_question("name", "What is your full name?")
        .add_gather_question("email", "What is your email address?", "string", true)
        .add_gather_question("phone", "What is your phone number?")
        .add_gather_question("issue", "Please describe your issue.")
        .set_step_criteria("All questions answered")
        .set_valid_steps({"confirm"});

    intake.add_step("confirm")
        .add_section("Task", "Confirm the collected information with the customer")
        .set_step_criteria("Customer confirmed")
        .set_end(true);

    std::cout << "GatherInfo demo at http://0.0.0.0:3000/gather-info\n";
    agent.run();
}
