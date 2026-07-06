// type_inference — infer_schema / create_typed_handler_wrapper
// (Python: signalwire.core.agent.tools.type_inference). C++ infers the SWAIG
// parameter schema from the typed ParameterSchema params-builder (Python has
// lambda reflection; C++ does not), and wraps a typed ToolHandler to the
// standard (args, raw_data) calling convention.

#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include "signalwire/swaig/function_result.hpp"
#include "signalwire/swaig/parameter_schema.hpp"
#include "signalwire/swaig/type_inference.hpp"

using json = nlohmann::json;
namespace sw_ti = signalwire::swaig::type_inference;
namespace sw_swaig = signalwire::swaig;

TEST(infer_schema_from_typed_builder) {
  sw_swaig::ParameterSchema schema;
  schema.string("service", "The service to book")
      .integer("count", "How many")
      .required({"service"});

  auto [parameters, required, description, is_typed, has_raw_data] =
      sw_ti::infer_schema(schema);

  // parameters is the properties map (name -> property).
  ASSERT_TRUE(parameters.is_object());
  ASSERT_TRUE(parameters.contains("service"));
  ASSERT_EQ(parameters["service"]["type"], "string");
  ASSERT_EQ(parameters["service"]["description"], "The service to book");
  ASSERT_TRUE(parameters.contains("count"));
  ASSERT_EQ(parameters["count"]["type"], "integer");

  // required is the required-names list.
  ASSERT_EQ(required.size(), static_cast<size_t>(1));
  ASSERT_EQ(required[0], std::string("service"));

  // No description supplied -> nullopt.
  ASSERT_FALSE(description.has_value());

  // Typed builder with properties -> is_typed true, no raw_data property.
  ASSERT_TRUE(is_typed);
  ASSERT_FALSE(has_raw_data);
  return true;
}

TEST(infer_schema_description_passthrough) {
  sw_swaig::ParameterSchema schema;
  schema.string("q");

  auto result = sw_ti::infer_schema(schema, std::optional<std::string>("Book a service"));
  ASSERT_TRUE(std::get<2>(result).has_value());
  ASSERT_EQ(*std::get<2>(result), std::string("Book a service"));
  ASSERT_TRUE(std::get<3>(result));  // is_typed
  return true;
}

TEST(infer_schema_raw_data_excluded_but_flagged) {
  // A raw_data property in the builder is the SWAIG raw channel: it is excluded
  // from parameters/required but reported via has_raw_data.
  sw_swaig::ParameterSchema schema;
  schema.string("name").string("raw_data").required({"name", "raw_data"});

  auto [parameters, required, description, is_typed, has_raw_data] =
      sw_ti::infer_schema(schema);

  ASSERT_TRUE(parameters.contains("name"));
  ASSERT_FALSE(parameters.contains("raw_data"));
  ASSERT_EQ(required.size(), static_cast<size_t>(1));
  ASSERT_EQ(required[0], std::string("name"));
  ASSERT_TRUE(is_typed);
  ASSERT_TRUE(has_raw_data);
  static_cast<void>(description);
  return true;
}

TEST(infer_schema_empty_builder_is_untyped) {
  sw_swaig::ParameterSchema schema;  // no properties declared
  auto [parameters, required, description, is_typed, has_raw_data] =
      sw_ti::infer_schema(schema);
  ASSERT_TRUE(parameters.empty());
  ASSERT_TRUE(required.empty());
  ASSERT_FALSE(is_typed);
  ASSERT_FALSE(has_raw_data);
  static_cast<void>(description);
  return true;
}

TEST(create_typed_handler_wrapper_forwards_args) {
  json seen_args;
  json seen_raw;
  sw_swaig::ToolHandler inner = [&](const json& args, const json& raw) {
    seen_args = args;
    seen_raw = raw;
    return sw_swaig::FunctionResult("ok");
  };

  // has_raw_data = false: the wrapper passes an empty object for the raw
  // channel even when the caller supplies raw data.
  auto wrapper = sw_ti::create_typed_handler_wrapper(inner, false);
  json args = {{"service", "haircut"}};
  json raw = {{"call_id", "xyz"}};
  auto res = wrapper(args, raw);
  ASSERT_EQ(seen_args["service"], "haircut");
  ASSERT_TRUE(seen_raw.is_object());
  ASSERT_TRUE(seen_raw.empty());
  ASSERT_EQ(res.to_json()["response"], "ok");
  return true;
}

TEST(create_typed_handler_wrapper_forwards_raw_when_declared) {
  json seen_raw;
  sw_swaig::ToolHandler inner = [&](const json& /*args*/, const json& raw) {
    seen_raw = raw;
    return sw_swaig::FunctionResult("ok");
  };

  auto wrapper = sw_ti::create_typed_handler_wrapper(inner, true);
  json raw = {{"call_id", "xyz"}};
  wrapper(json::object(), raw);
  ASSERT_EQ(seen_raw["call_id"], "xyz");
  return true;
}

TEST(create_typed_handler_wrapper_normalizes_non_object_args) {
  json seen_args;
  sw_swaig::ToolHandler inner = [&](const json& args, const json& /*raw*/) {
    seen_args = args;
    return sw_swaig::FunctionResult("ok");
  };
  auto wrapper = sw_ti::create_typed_handler_wrapper(inner, false);
  // A null args value is normalized to an empty object.
  wrapper(json(nullptr), json::object());
  ASSERT_TRUE(seen_args.is_object());
  ASSERT_TRUE(seen_args.empty());
  return true;
}
