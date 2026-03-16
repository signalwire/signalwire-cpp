// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
// C wrapper implementation for the SignalWire AI Agents C++ SDK.

#include "signalwire/signalwire_agents_c.h"
#include "signalwire/agent/agent_base.hpp"
#include "signalwire/swaig/function_result.hpp"
#include <string>
#include <cstring>
#include <cstdlib>

using namespace signalwire;

// ========================================================================
// Agent lifecycle
// ========================================================================

sw_agent_t sw_agent_create(const char* name) {
    try {
        auto* agent = new agent::AgentBase(name ? name : "agent");
        return static_cast<sw_agent_t>(agent);
    } catch (...) {
        return nullptr;
    }
}

sw_agent_t sw_agent_create_full(const char* name, const char* route,
                                 const char* host, int port) {
    try {
        auto* agent = new agent::AgentBase(
            name ? name : "agent",
            route ? route : "/",
            host ? host : "0.0.0.0",
            port);
        return static_cast<sw_agent_t>(agent);
    } catch (...) {
        return nullptr;
    }
}

void sw_agent_destroy(sw_agent_t handle) {
    delete static_cast<agent::AgentBase*>(handle);
}

// ========================================================================
// Prompt configuration
// ========================================================================

void sw_agent_set_prompt(sw_agent_t handle, const char* text) {
    if (!handle || !text) return;
    static_cast<agent::AgentBase*>(handle)->set_prompt_text(text);
}

void sw_agent_add_section(sw_agent_t handle, const char* title, const char* body) {
    if (!handle || !title) return;
    static_cast<agent::AgentBase*>(handle)->prompt_add_section(
        title, body ? body : "");
}

void sw_agent_add_section_bullets(sw_agent_t handle, const char* title,
                                   const char** bullets) {
    if (!handle || !title) return;
    std::vector<std::string> bullet_vec;
    if (bullets) {
        for (const char** p = bullets; *p; ++p) {
            bullet_vec.emplace_back(*p);
        }
    }
    static_cast<agent::AgentBase*>(handle)->prompt_add_section(
        title, "", bullet_vec);
}

void sw_agent_set_post_prompt(sw_agent_t handle, const char* text) {
    if (!handle || !text) return;
    static_cast<agent::AgentBase*>(handle)->set_post_prompt(text);
}

// ========================================================================
// Tool registration
// ========================================================================

// Internal: wrap the C callback in a C++ lambda
struct CToolContext {
    sw_tool_handler_t handler;
    void* user_data;
};

void sw_agent_define_tool(sw_agent_t handle, const char* name,
                           const char* description, const char* params_json,
                           sw_tool_handler_t handler, void* user_data) {
    if (!handle || !name || !description || !handler) return;

    auto* agent = static_cast<agent::AgentBase*>(handle);
    nlohmann::json params;
    if (params_json) {
        try {
            params = nlohmann::json::parse(params_json);
        } catch (...) {
            params = nlohmann::json::object({{"type", "object"}, {"properties", nlohmann::json::object()}});
        }
    } else {
        params = nlohmann::json::object({{"type", "object"}, {"properties", nlohmann::json::object()}});
    }

    // Capture handler and user_data by value
    auto ctx = std::make_shared<CToolContext>();
    ctx->handler = handler;
    ctx->user_data = user_data;

    agent->define_tool(name, description, params,
        [ctx](const nlohmann::json& args, const nlohmann::json& raw) -> swaig::FunctionResult {
            std::string args_str = args.dump();
            std::string raw_str = raw.dump();

            sw_function_result_t result_handle = ctx->handler(
                args_str.c_str(), raw_str.c_str(), ctx->user_data);

            if (result_handle) {
                auto* result = static_cast<swaig::FunctionResult*>(result_handle);
                swaig::FunctionResult ret = *result;
                delete result;
                return ret;
            }
            return swaig::FunctionResult("Error: tool handler returned null");
        });
}

char** sw_agent_list_tools(sw_agent_t handle) {
    if (!handle) return nullptr;
    auto tools = static_cast<agent::AgentBase*>(handle)->list_tools();
    char** arr = static_cast<char**>(std::malloc((tools.size() + 1) * sizeof(char*)));
    if (!arr) return nullptr;
    for (size_t i = 0; i < tools.size(); ++i) {
        arr[i] = static_cast<char*>(std::malloc(tools[i].size() + 1));
        std::strcpy(arr[i], tools[i].c_str());
    }
    arr[tools.size()] = nullptr;
    return arr;
}

void sw_free_string_array(char** arr) {
    if (!arr) return;
    for (char** p = arr; *p; ++p) {
        std::free(*p);
    }
    std::free(arr);
}

// ========================================================================
// AI configuration
// ========================================================================

void sw_agent_set_param(sw_agent_t handle, const char* key, const char* value_json) {
    if (!handle || !key || !value_json) return;
    try {
        auto val = nlohmann::json::parse(value_json);
        static_cast<agent::AgentBase*>(handle)->set_param(key, val);
    } catch (...) {}
}

void sw_agent_set_global_data(sw_agent_t handle, const char* json_str) {
    if (!handle || !json_str) return;
    try {
        auto data = nlohmann::json::parse(json_str);
        static_cast<agent::AgentBase*>(handle)->set_global_data(data);
    } catch (...) {}
}

void sw_agent_add_hint(sw_agent_t handle, const char* hint) {
    if (!handle || !hint) return;
    static_cast<agent::AgentBase*>(handle)->add_hint(hint);
}

void sw_agent_set_auth(sw_agent_t handle, const char* username, const char* password) {
    if (!handle || !username || !password) return;
    static_cast<agent::AgentBase*>(handle)->set_auth(username, password);
}

// ========================================================================
// Skills
// ========================================================================

void sw_agent_add_skill(sw_agent_t handle, const char* skill_name,
                          const char* params_json) {
    if (!handle || !skill_name) return;
    nlohmann::json params;
    if (params_json) {
        try {
            params = nlohmann::json::parse(params_json);
        } catch (...) {
            params = nlohmann::json::object();
        }
    }
    static_cast<agent::AgentBase*>(handle)->add_skill(skill_name, params);
}

// ========================================================================
// Server
// ========================================================================

void sw_agent_run(sw_agent_t handle) {
    if (!handle) return;
    static_cast<agent::AgentBase*>(handle)->run();
}

void sw_agent_serve(sw_agent_t handle) {
    if (!handle) return;
    static_cast<agent::AgentBase*>(handle)->serve();
}

void sw_agent_stop(sw_agent_t handle) {
    if (!handle) return;
    static_cast<agent::AgentBase*>(handle)->stop();
}

// ========================================================================
// SWML rendering
// ========================================================================

char* sw_agent_render_swml(sw_agent_t handle) {
    if (!handle) return nullptr;
    auto swml = static_cast<agent::AgentBase*>(handle)->render_swml();
    std::string str = swml.dump(2);
    char* result = static_cast<char*>(std::malloc(str.size() + 1));
    if (result) std::strcpy(result, str.c_str());
    return result;
}

void sw_free_string(char* str) {
    std::free(str);
}

// ========================================================================
// FunctionResult
// ========================================================================

sw_function_result_t sw_result_create(const char* response) {
    try {
        auto* result = new swaig::FunctionResult(response ? response : "");
        return static_cast<sw_function_result_t>(result);
    } catch (...) {
        return nullptr;
    }
}

void sw_result_add_action(sw_function_result_t handle, const char* action_name,
                           const char* action_data_json) {
    if (!handle || !action_name) return;
    auto* result = static_cast<swaig::FunctionResult*>(handle);
    nlohmann::json data;
    if (action_data_json) {
        try {
            data = nlohmann::json::parse(action_data_json);
        } catch (...) {
            data = action_data_json;
        }
    }
    result->add_action(action_name, data);
}

char* sw_result_to_json(sw_function_result_t handle) {
    if (!handle) return nullptr;
    auto str = static_cast<swaig::FunctionResult*>(handle)->to_string(2);
    char* result = static_cast<char*>(std::malloc(str.size() + 1));
    if (result) std::strcpy(result, str.c_str());
    return result;
}

void sw_result_destroy(sw_function_result_t handle) {
    delete static_cast<swaig::FunctionResult*>(handle);
}
