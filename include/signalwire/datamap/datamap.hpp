#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include "signalwire/swaig/function_result.hpp"

namespace signalwire {
namespace datamap {

using json = nlohmann::json;

/// Fluent builder for SWAIG data_map tools (server-side, no webhook needed).
/// Every setter returns *this for chaining.
class DataMap {
public:
    explicit DataMap(const std::string& function_name);

    /// Set the LLM-facing tool description (the "purpose"). PROMPT
    /// ENGINEERING, not developer documentation.
    ///
    /// The description string is rendered into the OpenAI tool schema
    /// `description` field on every LLM turn. The model reads it to
    /// decide WHEN to call this tool. A vague purpose() is the #1
    /// cause of "the model has the right tool but doesn't call it"
    /// failures with data-map tools.
    ///
    /// Bad vs good:
    ///   BAD : .purpose("weather api")
    ///   GOOD: .purpose("Get the current weather conditions and "
    ///                  "forecast for a specific city. Use this "
    ///                  "whenever the user asks about weather, "
    ///                  "temperature, rain, or similar conditions in "
    ///                  "a named location.")
    DataMap& purpose(const std::string& desc);

    /// Alias for purpose(). Sets the LLM-facing tool description.
    /// This string is read by the model to decide WHEN to call this
    /// tool. See purpose() for bad-vs-good examples.
    DataMap& description(const std::string& desc);

    /// Add a parameter to this data-map tool — the `desc` is
    /// LLM-FACING.
    ///
    /// Each parameter description is rendered into the OpenAI tool
    /// schema under parameters.properties.<name>.description and sent
    /// to the model. The model uses it to decide HOW to fill in the
    /// argument from user speech. It is prompt engineering, not
    /// developer FYI.
    ///
    /// Bad vs good:
    ///   BAD : .parameter("city", "string", "the city")
    ///   GOOD: .parameter("city", "string",
    ///             "The name of the city to get weather for, e.g. "
    ///             "'San Francisco'. Ask the user if they did not "
    ///             "provide one. Include the state or country if the "
    ///             "city name is ambiguous.")
    DataMap& parameter(const std::string& name, const std::string& param_type,
                       const std::string& desc, bool required = false,
                       const std::vector<std::string>& enum_values = {});

    /// Add an expression pattern for pattern-based responses
    DataMap& expression(const std::string& test_value, const std::string& pattern,
                        const swaig::FunctionResult& output,
                        const swaig::FunctionResult* nomatch_output = nullptr);

    /// Add a webhook API call
    DataMap& webhook(const std::string& method, const std::string& url,
                     const json& headers = json::object(),
                     const std::string& form_param = "",
                     bool input_args_as_params = false,
                     const std::vector<std::string>& require_args = {});

    /// Add expressions that run after the most recent webhook
    DataMap& webhook_expressions(const std::vector<json>& expressions);

    /// Set request body for the last added webhook
    DataMap& body(const json& data);

    /// Set request params for the last added webhook (alias for body)
    DataMap& params(const json& data);

    /// Set foreach configuration for the last webhook
    DataMap& foreach(const json& foreach_config);

    /// Set output for the most recent webhook
    DataMap& output(const swaig::FunctionResult& result);

    /// Set a fallback output at the top level
    DataMap& fallback_output(const swaig::FunctionResult& result);

    /// Set error keys for the most recent webhook or top-level
    DataMap& error_keys(const std::vector<std::string>& keys);

    /// Set top-level error keys
    DataMap& global_error_keys(const std::vector<std::string>& keys);

    /// Convert to a SWAIG function definition JSON
    /// [[nodiscard]]: the built tool definition is the output of the builder;
    /// dropping it discards the whole DataMap. (The fluent DataMap& setters
    /// above are intentionally NOT nodiscard.)
    [[nodiscard]] json to_swaig_function() const;

private:
    std::string function_name_;
    std::string purpose_;
    json parameters_ = json::object();
    std::vector<json> expressions_;
    std::vector<json> webhooks_;
    json output_;
    std::vector<std::string> error_keys_;
};

} // namespace datamap
} // namespace signalwire
