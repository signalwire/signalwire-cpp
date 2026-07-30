// Copyright (c) 2025 SignalWire — MIT License
//
// Step Function Inheritance Demo
//
// This example exists to teach one specific gotcha: the per-step
// `functions` whitelist INHERITS from the previous step when omitted.
//
// Why this matters
// ----------------
// A common mistake when building multi-step agents is to assume each
// step starts with a fresh tool set. It does not. The runtime only
// resets the active set when a step explicitly declares its `functions`
// field. If you forget set_functions() on a later step, the previous
// step's tools quietly remain available.
//
// This file shows four step-shaped patterns side by side:
//
//   1. step_lookup   — explicitly whitelists `lookup_account`
//   2. step_inherit  — has NO set_functions() call. Inherits
//                      step_lookup's whitelist, so `lookup_account` is
//                      still callable here. This is rarely what you
//                      want.
//   3. step_explicit — explicitly whitelists `process_payment`. The
//                      previously inherited `lookup_account` is now
//                      disabled, and only `process_payment` is active.
//   4. step_disabled — explicitly disables ALL user functions with an
//                      empty vector (or "none"). Internal tools like
//                      next_step still work.
//
// Best practice
// -------------
// Call set_functions() on EVERY step that should differ from the
// previous one. Treat omission as an explicit decision to inherit, not
// a default.
//
// Run this file to see the rendered SWML — there are no real webhook
// endpoints behind the tools, this is purely a documentation example.

#include <iostream>
#include <signalwire/agent/agent_base.hpp>
#include <signalwire/swaig/function_result.hpp>

using namespace signalwire;

int main() {
  agent::AgentBase agent("step_function_inheritance_demo", "/");

  // Register three SWAIG tools so we have something to whitelist.
  // In a real agent these would call out to webhooks; here they're
  // stubs.
  agent.define_tool("lookup_account", "Look up customer account details by account number",
                    {{"account_number", {{"type", "string"}}}},
                    [](const nlohmann::json&, const nlohmann::json&) {
                      return swaig::FunctionResult("looked up");
                    });

  agent.define_tool("process_payment", "Process a payment for the current customer",
                    {{"amount", {{"type", "number"}}}},
                    [](const nlohmann::json&, const nlohmann::json&) {
                      return swaig::FunctionResult("payment processed");
                    });

  agent.define_tool(
      "send_receipt", "Email a receipt to the customer", {{"email", {{"type", "string"}}}},
      [](const nlohmann::json&, const nlohmann::json&) { return swaig::FunctionResult("sent"); });

  // Build the contexts.
  auto& cb = agent.define_contexts();
  auto& ctx = cb.add_context("default");

  // -- Step 1: explicit whitelist --
  // `lookup_account` is the only tool active in this step.
  ctx.add_step("step_lookup")
      .set_text(
          "Greet the customer and ask for their account number. "
          "Use lookup_account to fetch their details.")
      .set_functions(std::vector<std::string>{"lookup_account"})
      .set_valid_steps({"step_inherit"});

  // -- Step 2: NO set_functions() call → inheritance --
  // Because we didn't call set_functions(), this step inherits the
  // active set from step_lookup. `lookup_account` is STILL callable
  // here, even though we never asked for it. Most of the time this
  // is a bug. To break the inheritance, call set_functions() with
  // an explicit list (even if it's empty).
  ctx.add_step("step_inherit")
      .set_text(
          "Confirm the customer's identity. (No set_functions() "
          "here, so lookup_account is still active — this is the "
          "inheritance trap.)")
      .set_valid_steps({"step_explicit"});

  // -- Step 3: explicit replacement --
  // Whitelist replaces the inherited set. lookup_account is now
  // inactive; only process_payment is active.
  ctx.add_step("step_explicit")
      .set_text(
          "Take the customer's payment. Use process_payment. "
          "lookup_account is no longer available.")
      .set_functions(std::vector<std::string>{"process_payment"})
      .set_valid_steps({"step_disabled"});

  // -- Step 4: explicit disable-all --
  // Pass an empty vector (or "none") to lock out every user-defined
  // tool. Internal navigation tools (next_step) are unaffected.
  ctx.add_step("step_disabled")
      .set_text(
          "Thank the customer and wrap up. No tools are needed "
          "here, so we lock everything down with "
          "set_functions(std::vector<std::string>{}).")
      .set_functions(std::vector<std::string>{})
      .set_end(true);

  // Render and pretty-print the resulting SWML so you can see
  // exactly which steps have a `functions` key in the output and
  // which don't.
  auto swml = agent.render_swml();
  std::cout << swml.dump(2) << std::endl;
  return 0;
}
