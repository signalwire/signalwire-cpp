// DataMap tests

#include "signalwire/datamap/datamap.hpp"

using namespace signalwire::datamap;
using namespace signalwire::swaig;
using json = nlohmann::json;

TEST(datamap_basic_construction) {
    DataMap dm("get_weather");
    auto j = dm.to_swaig_function();
    ASSERT_EQ(j["function"].get<std::string>(), "get_weather");
    ASSERT_TRUE(j.contains("data_map"));
    ASSERT_TRUE(j.contains("parameters"));
    return true;
}

TEST(datamap_purpose) {
    DataMap dm("test");
    dm.purpose("Test function");
    auto j = dm.to_swaig_function();
    ASSERT_EQ(j["description"].get<std::string>(), "Test function");
    return true;
}

TEST(datamap_description_alias) {
    DataMap dm("test");
    dm.description("Alias test");
    auto j = dm.to_swaig_function();
    ASSERT_EQ(j["description"].get<std::string>(), "Alias test");
    return true;
}

TEST(datamap_parameter) {
    DataMap dm("test");
    dm.parameter("city", "string", "City name", true);
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["parameters"]["properties"].contains("city"));
    ASSERT_EQ(j["parameters"]["properties"]["city"]["type"].get<std::string>(), "string");
    ASSERT_TRUE(j["parameters"].contains("required"));
    return true;
}

TEST(datamap_parameter_with_enum) {
    DataMap dm("test");
    dm.parameter("unit", "string", "Temp unit", false, {"celsius", "fahrenheit"});
    auto j = dm.to_swaig_function();
    auto& param = j["parameters"]["properties"]["unit"];
    ASSERT_TRUE(param.contains("enum"));
    ASSERT_EQ(param["enum"].size(), 2u);
    return true;
}

TEST(datamap_webhook) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data");
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"].contains("webhooks"));
    ASSERT_EQ(j["data_map"]["webhooks"].size(), 1u);
    ASSERT_EQ(j["data_map"]["webhooks"][0]["method"].get<std::string>(), "GET");
    return true;
}

TEST(datamap_webhook_with_headers) {
    DataMap dm("test");
    dm.webhook("POST", "https://api.example.com/data",
               json::object({{"Authorization", "Bearer TOKEN"}}));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("headers"));
    return true;
}

TEST(datamap_webhook_body) {
    DataMap dm("test");
    dm.webhook("POST", "https://api.example.com/data");
    dm.body(json::object({{"query", "${args.query}"}}));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("body"));
    return true;
}

TEST(datamap_webhook_params) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data");
    dm.params(json::object({{"q", "${args.query}"}}));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("params"));
    return true;
}

TEST(datamap_body_requires_webhook) {
    DataMap dm("test");
    ASSERT_THROWS(dm.body(json::object({{"key", "value"}})));
    return true;
}

TEST(datamap_params_requires_webhook) {
    DataMap dm("test");
    ASSERT_THROWS(dm.params(json::object({{"key", "value"}})));
    return true;
}

TEST(datamap_output) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data");
    dm.output(FunctionResult("Result: ${response.value}"));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("output"));
    return true;
}

TEST(datamap_fallback_output) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data");
    dm.output(FunctionResult("Primary result"));
    dm.fallback_output(FunctionResult("Fallback result"));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"].contains("output"));
    return true;
}

TEST(datamap_expression) {
    DataMap dm("test");
    dm.expression("${args.command}", "start.*", FunctionResult("Starting"));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"].contains("expressions"));
    ASSERT_EQ(j["data_map"]["expressions"].size(), 1u);
    ASSERT_EQ(j["data_map"]["expressions"][0]["string"].get<std::string>(), "${args.command}");
    ASSERT_EQ(j["data_map"]["expressions"][0]["pattern"].get<std::string>(), "start.*");
    return true;
}

TEST(datamap_expression_with_nomatch) {
    DataMap dm("test");
    FunctionResult nomatch("No match found");
    dm.expression("${args.command}", "start.*", FunctionResult("Starting"), &nomatch);
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["expressions"][0].contains("nomatch_output"));
    return true;
}

TEST(datamap_error_keys_on_webhook) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data");
    dm.error_keys({"error", "fault"});
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("error_keys"));
    return true;
}

TEST(datamap_global_error_keys) {
    DataMap dm("test");
    dm.global_error_keys({"error"});
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"].contains("error_keys"));
    return true;
}

TEST(datamap_foreach) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data");
    dm.foreach(json::object({
        {"input_key", "results"},
        {"output_key", "formatted"},
        {"max", 3},
        {"append", "- ${this.title}\n"}
    }));
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("foreach"));
    return true;
}

TEST(datamap_full_fluent_api) {
    auto j = DataMap("get_weather")
        .purpose("Get weather for a location")
        .parameter("city", "string", "City name", true)
        .parameter("unit", "string", "Temperature unit", false, {"celsius", "fahrenheit"})
        .webhook("GET", "https://api.weather.com/v1?q=${args.city}")
        .output(FunctionResult("Weather in ${args.city}: ${response.temp}"))
        .to_swaig_function();

    ASSERT_EQ(j["function"].get<std::string>(), "get_weather");
    ASSERT_EQ(j["description"].get<std::string>(), "Get weather for a location");
    ASSERT_TRUE(j["parameters"]["properties"].contains("city"));
    ASSERT_TRUE(j["parameters"]["properties"].contains("unit"));
    ASSERT_TRUE(j["data_map"].contains("webhooks"));
    return true;
}

TEST(datamap_multiple_webhooks) {
    DataMap dm("search");
    dm.purpose("Search with fallback");
    dm.webhook("GET", "https://primary.com/search");
    dm.output(FunctionResult("Primary: ${response.result}"));
    dm.webhook("GET", "https://fallback.com/search");
    dm.output(FunctionResult("Fallback: ${response.result}"));
    dm.fallback_output(FunctionResult("All APIs unavailable"));

    auto j = dm.to_swaig_function();
    ASSERT_EQ(j["data_map"]["webhooks"].size(), 2u);
    ASSERT_TRUE(j["data_map"].contains("output")); // fallback
    return true;
}

TEST(datamap_webhook_input_args_as_params) {
    DataMap dm("test");
    dm.webhook("POST", "https://api.example.com/data", json::object(), "", true);
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("input_args_as_params"));
    return true;
}

TEST(datamap_webhook_require_args) {
    DataMap dm("test");
    dm.webhook("GET", "https://api.example.com/data", json::object(), "", false, {"city", "country"});
    auto j = dm.to_swaig_function();
    ASSERT_TRUE(j["data_map"]["webhooks"][0].contains("require_args"));
    ASSERT_EQ(j["data_map"]["webhooks"][0]["require_args"].size(), 2u);
    return true;
}
