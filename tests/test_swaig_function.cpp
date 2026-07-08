// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Behavioral tests for core::SWAIGFunction.
// Python parity: signalwire.core.swaig_function.SWAIGFunction.

#include "signalwire/core/swaig_function.hpp"
#include "signalwire/swaig/function_result.hpp"

using namespace signalwire::core;
using json = nlohmann::json;

namespace {
SwaigFunctionHandler echo_handler = [](const json& args, const json&) -> json {
  return signalwire::swaig::FunctionResult("echo: " + args.value("x", std::string("?"))).to_json();
};
}  // namespace

// ---- ctor / accessors ----

TEST(swaig_function_ctor_defaults) {
  SWAIGFunction fn("greet", echo_handler, "Greets the user");
  ASSERT_EQ(fn.name(), std::string("greet"));
  ASSERT_EQ(fn.description(), std::string("Greets the user"));
  ASSERT_FALSE(fn.secure());
  ASSERT_FALSE(fn.is_external());
  ASSERT_TRUE(fn.required().empty());
  return true;
}

TEST(swaig_function_webhook_makes_external) {
  SWAIGFunction fn("t", echo_handler, "d", json::object(), false, std::nullopt, std::nullopt,
                   std::nullopt, std::optional<std::string>("https://x/hook"));
  ASSERT_TRUE(fn.is_external());
  return true;
}

// ---- call / operator() ----

TEST(swaig_function_call_invokes_handler) {
  SWAIGFunction fn("t", echo_handler, "d");
  json r = fn.call(json::object({{"x", "hi"}}));
  ASSERT_EQ(r["response"].get<std::string>(), std::string("echo: hi"));
  json r2 = fn(json::object({{"x", "yo"}}));
  ASSERT_EQ(r2["response"].get<std::string>(), std::string("echo: yo"));
  return true;
}

// ---- execute (coercion) ----

TEST(swaig_function_execute_function_result_passthrough) {
  SWAIGFunction fn("t", echo_handler, "d");
  json out = fn.execute(json::object({{"x", "hi"}}));
  ASSERT_EQ(out["response"].get<std::string>(), std::string("echo: hi"));
  return true;
}

TEST(swaig_function_execute_string_coerced) {
  SwaigFunctionHandler h = [](const json&, const json&) -> json { return json("plain"); };
  SWAIGFunction fn("t", h, "d");
  json out = fn.execute(json::object());
  ASSERT_EQ(out["response"].get<std::string>(), std::string("plain"));
  return true;
}

TEST(swaig_function_execute_object_without_response) {
  SwaigFunctionHandler h = [](const json&, const json&) -> json {
    return json::object({{"foo", 1}});
  };
  SWAIGFunction fn("t", h, "d");
  json out = fn.execute(json::object());
  ASSERT_EQ(out["response"].get<std::string>(), std::string("Function completed successfully"));
  return true;
}

TEST(swaig_function_execute_object_with_response_passthrough) {
  SwaigFunctionHandler h = [](const json&, const json&) -> json {
    return json::object({{"response", "custom"}, {"action", json::array()}});
  };
  SWAIGFunction fn("t", h, "d");
  json out = fn.execute(json::object());
  ASSERT_EQ(out["response"].get<std::string>(), std::string("custom"));
  return true;
}

TEST(swaig_function_execute_handler_throws_generic_message) {
  SwaigFunctionHandler h = [](const json&, const json&) -> json {
    throw std::runtime_error("boom");
  };
  SWAIGFunction fn("t", h, "d");
  json out = fn.execute(json::object());
  ASSERT_EQ(out["response"].get<std::string>(), std::string(SWAIGFunction::kExecuteErrorResponse));
  return true;
}

// ---- validate_args ----

TEST(swaig_function_validate_no_properties_passes) {
  SWAIGFunction fn("t", echo_handler, "d");
  auto r = fn.validate_args(json::object());
  ASSERT_TRUE(r.valid);
  return true;
}

TEST(swaig_function_validate_required_and_type) {
  json params = {{"type", "object"},
                 {"properties", {{"n", {{"type", "integer"}}}}},
                 {"required", json::array({"n"})}};
  SWAIGFunction fn("t", echo_handler, "d", params);

  // missing required
  auto miss = fn.validate_args(json::object());
  ASSERT_FALSE(miss.valid);

  // wrong type
  auto wrong = fn.validate_args(json::object({{"n", "notint"}}));
  ASSERT_FALSE(wrong.valid);

  // valid
  auto ok = fn.validate_args(json::object({{"n", 5}}));
  ASSERT_TRUE(ok.valid);
  return true;
}

TEST(swaig_function_validate_wraps_loose_properties) {
  // Loose properties (no type/properties envelope): "city" is a bare property.
  json params = {{"city", {{"type", "string"}}}};
  SWAIGFunction fn("t", echo_handler, "d", params, false, std::nullopt, std::nullopt, std::nullopt,
                   std::nullopt, std::vector<std::string>{"city"});
  auto miss = fn.validate_args(json::object());
  ASSERT_FALSE(miss.valid);
  auto ok = fn.validate_args(json::object({{"city", "NYC"}}));
  ASSERT_TRUE(ok.valid);
  return true;
}

// ---- to_swaig ----

TEST(swaig_function_to_swaig_basic) {
  SWAIGFunction fn("greet", echo_handler, "Greets");
  json d = fn.to_swaig("https://host");
  ASSERT_EQ(d["function"].get<std::string>(), std::string("greet"));
  ASSERT_EQ(d["description"].get<std::string>(), std::string("Greets"));
  ASSERT_EQ(d["web_hook_url"].get<std::string>(), std::string("https://host/swaig"));
  // empty params -> {type:object, properties:{}}
  ASSERT_EQ(d["parameters"]["type"].get<std::string>(), std::string("object"));
  return true;
}

TEST(swaig_function_to_swaig_token_call_id) {
  SWAIGFunction fn("greet", echo_handler, "Greets");
  json d = fn.to_swaig("https://host", std::optional<std::string>("TOK"),
                       std::optional<std::string>("CID"));
  ASSERT_EQ(d["web_hook_url"].get<std::string>(),
            std::string("https://host/swaig?token=TOK&call_id=CID"));
  return true;
}

TEST(swaig_function_to_swaig_extra_fields_and_fillers) {
  json fillers = {{"en-US", json::array({"working on it"})}};
  json extra = {{"meta_data_token", "xyz"}};
  SWAIGFunction fn("t", echo_handler, "d", json::object(), false, std::optional<json>(fillers),
                   std::nullopt, std::nullopt, std::nullopt, std::vector<std::string>{}, false,
                   extra);
  json d = fn.to_swaig("https://host");
  ASSERT_TRUE(d.contains("fillers"));
  ASSERT_EQ(d["meta_data_token"].get<std::string>(), std::string("xyz"));
  return true;
}
