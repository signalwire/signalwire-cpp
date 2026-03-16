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

    /// Set function purpose/description
    DataMap& purpose(const std::string& desc);

    /// Alias for purpose()
    DataMap& description(const std::string& desc);

    /// Add a function parameter
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
    json to_swaig_function() const;

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
