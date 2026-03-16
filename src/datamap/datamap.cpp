// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/datamap/datamap.hpp"

namespace signalwire {
namespace datamap {

DataMap::DataMap(const std::string& function_name)
    : function_name_(function_name) {}

DataMap& DataMap::purpose(const std::string& desc) {
    purpose_ = desc;
    return *this;
}

DataMap& DataMap::description(const std::string& desc) {
    return purpose(desc);
}

DataMap& DataMap::parameter(const std::string& name, const std::string& param_type,
                             const std::string& desc, bool required,
                             const std::vector<std::string>& enum_values) {
    if (!parameters_.contains("properties")) {
        parameters_["type"] = "object";
        parameters_["properties"] = json::object();
    }

    json param;
    param["type"] = param_type;
    param["description"] = desc;
    if (!enum_values.empty()) {
        param["enum"] = enum_values;
    }

    parameters_["properties"][name] = param;

    if (required) {
        if (!parameters_.contains("required")) {
            parameters_["required"] = json::array();
        }
        parameters_["required"].push_back(name);
    }

    return *this;
}

DataMap& DataMap::expression(const std::string& test_value, const std::string& pattern,
                              const swaig::FunctionResult& output_result,
                              const swaig::FunctionResult* nomatch_output) {
    json expr;
    expr["string"] = test_value;
    expr["pattern"] = pattern;
    expr["output"] = output_result.to_json();
    if (nomatch_output) {
        expr["nomatch_output"] = nomatch_output->to_json();
    }
    expressions_.push_back(expr);
    return *this;
}

DataMap& DataMap::webhook(const std::string& method, const std::string& url,
                           const json& headers, const std::string& form_param,
                           bool input_args_as_params,
                           const std::vector<std::string>& require_args) {
    json wh;
    wh["method"] = method;
    wh["url"] = url;
    if (!headers.empty()) wh["headers"] = headers;
    if (!form_param.empty()) wh["form_param"] = form_param;
    if (input_args_as_params) wh["input_args_as_params"] = true;
    if (!require_args.empty()) wh["require_args"] = require_args;
    webhooks_.push_back(wh);
    return *this;
}

DataMap& DataMap::webhook_expressions(const std::vector<json>& expressions) {
    if (!webhooks_.empty()) {
        webhooks_.back()["expressions"] = expressions;
    }
    return *this;
}

DataMap& DataMap::body(const json& data) {
    if (webhooks_.empty()) {
        throw std::runtime_error("Must add webhook before setting body");
    }
    webhooks_.back()["body"] = data;
    return *this;
}

DataMap& DataMap::params(const json& data) {
    if (webhooks_.empty()) {
        throw std::runtime_error("Must add webhook before setting params");
    }
    webhooks_.back()["params"] = data;
    return *this;
}

DataMap& DataMap::foreach(const json& foreach_config) {
    if (!webhooks_.empty()) {
        webhooks_.back()["foreach"] = foreach_config;
    }
    return *this;
}

DataMap& DataMap::output(const swaig::FunctionResult& result) {
    if (!webhooks_.empty()) {
        webhooks_.back()["output"] = result.to_json();
    } else {
        output_ = result.to_json();
    }
    return *this;
}

DataMap& DataMap::fallback_output(const swaig::FunctionResult& result) {
    output_ = result.to_json();
    return *this;
}

DataMap& DataMap::error_keys(const std::vector<std::string>& keys) {
    if (!webhooks_.empty()) {
        webhooks_.back()["error_keys"] = keys;
    } else {
        error_keys_ = keys;
    }
    return *this;
}

DataMap& DataMap::global_error_keys(const std::vector<std::string>& keys) {
    error_keys_ = keys;
    return *this;
}

json DataMap::to_swaig_function() const {
    json func;
    func["function"] = function_name_;
    if (!purpose_.empty()) func["description"] = purpose_;

    if (!parameters_.empty()) {
        func["parameters"] = parameters_;
    } else {
        func["parameters"] = json::object({
            {"type", "object"},
            {"properties", json::object()}
        });
    }

    json dm;
    if (!expressions_.empty()) {
        dm["expressions"] = expressions_;
    }
    if (!webhooks_.empty()) {
        dm["webhooks"] = webhooks_;
    }
    if (!output_.is_null()) {
        dm["output"] = output_;
    }
    if (!error_keys_.empty()) {
        dm["error_keys"] = error_keys_;
    }

    func["data_map"] = dm;
    return func;
}

} // namespace datamap
} // namespace signalwire
