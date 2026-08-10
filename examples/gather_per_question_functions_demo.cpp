// Copyright (c) 2025 SignalWire — MIT License
//
// Per-Question Function Whitelist Demo (gather_info)
//
// This example exists to teach one specific gotcha: while a step's
// gather_info is asking questions, ALL of the step's other functions
// are forcibly deactivated. The only callable tools during a gather
// question are:
//
//   - `gather_submit` (the native answer-submission tool, always
//     active)
//   - Whatever names you list in that question's `functions` argument
//
// `next_step` and `change_context` are also filtered out — the model
// literally cannot navigate away until the gather completes. This is
// by design: it forces a tight ask → submit → next-question loop.
//
// If a question needs to call out to a tool — for example, to validate
// an email format, geocode a ZIP, or look up something from an
// external service — you must list that tool name in the question's
// `functions` argument. The function is active ONLY for that question.
//
// Below: a customer-onboarding gather flow where each question unlocks
// a different validation tool, and where the step's own non-gather
// tools (escalate_to_human, lookup_existing_account) are LOCKED OUT
// during gather because they aren't whitelisted on any question.
//
// Run this file to see the resulting SWML.

#include <iostream>
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>

using namespace signalwire;

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    agent::AgentBase agent("gather_per_question_functions_demo", "/");

    // Tools that the step would normally have available — but during
    // gather questioning, they're all locked out unless they appear
    // in a question's `functions` whitelist.
    agent.define_tool(
        "validate_email", "Validate that an email address is well-formed and deliverable",
        {{"email", {{"type", "string"}}}}, [](const nlohmann::json&, const nlohmann::json&) {
          return swaig::FunctionResult("valid");
        });

    agent.define_tool("geocode_zip", "Look up the city/state for a US ZIP code",
                      {{"zip", {{"type", "string"}}}},
                      [](const nlohmann::json&, const nlohmann::json&) {
                        return swaig::FunctionResult(R"({"city":"...","state":"..."})");
                      });

    agent.define_tool("check_age_eligibility", "Verify the customer is old enough for the product",
                      {{"age", {{"type", "integer"}}}},
                      [](const nlohmann::json&, const nlohmann::json&) {
                        return swaig::FunctionResult("eligible");
                      });

    // These tools are NOT whitelisted on any gather question. They
    // are registered on the agent and active outside the gather, but
    // during the gather they cannot be called — gather mode locks
    // them out.
    agent.define_tool("escalate_to_human", "Transfer the conversation to a live agent",
                      nlohmann::json::object(), [](const nlohmann::json&, const nlohmann::json&) {
                        return swaig::FunctionResult("transferred");
                      });

    agent.define_tool("lookup_existing_account", "Search for an existing account by email",
                      {{"email", {{"type", "string"}}}},
                      [](const nlohmann::json&, const nlohmann::json&) {
                        return swaig::FunctionResult("not found");
                      });

    // Build a single-context agent with one onboarding step.
    auto& cb = agent.define_contexts();
    auto& ctx = cb.add_context("default");

    auto& onboard = ctx.add_step("onboard");
    onboard
        .set_text(
            "Onboard a new customer by collecting their details. Use "
            "gather_info to ask one question at a time. Each question "
            "may unlock a specific validation tool — only that tool "
            "and gather_submit are callable while answering it.")
        .set_functions(std::vector<std::string>{
            // Outside of the gather (which is the entire step here),
            // these would be available. During the gather they are
            // forcibly hidden in favor of the per-question
            // whitelists.
            "escalate_to_human",
            "lookup_existing_account",
        })
        .set_gather_info("customer", "next_step",
                         "I'll need to collect a few details to set up your "
                         "account. I'll ask one question at a time.");

    // Question 1: email — only validate_email + gather_submit callable.
    onboard.add_gather_question("email", "What's your email address?",
                                /*type*/ "string",
                                /*confirm*/ true,
                                /*prompt*/ "",
                                /*functions*/ {"validate_email"});

    // Question 2: zip — only geocode_zip + gather_submit callable.
    onboard.add_gather_question("zip", "What's your ZIP code?",
                                /*type*/ "string",
                                /*confirm*/ false,
                                /*prompt*/ "",
                                /*functions*/ {"geocode_zip"});

    // Question 3: age — only check_age_eligibility + gather_submit
    // callable.
    onboard.add_gather_question("age", "How old are you?",
                                /*type*/ "integer",
                                /*confirm*/ false,
                                /*prompt*/ "",
                                /*functions*/ {"check_age_eligibility"});

    // Question 4: referral_source — no functions → only gather_submit
    // is callable. The model cannot validate, lookup, escalate —
    // nothing. This is the right pattern when a question needs no
    // tools.
    onboard.add_gather_question("referral_source", "How did you hear about us?");

    // A simple confirmation step the gather auto-advances into.
    ctx.add_step("confirm")
        .set_text(
            "Read the collected info back to the customer and "
            "confirm everything is correct.")
        .set_functions(std::vector<std::string>{})
        .set_end(true);

    auto swml = agent.render_swml();
    std::cout << swml.dump(2) << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
