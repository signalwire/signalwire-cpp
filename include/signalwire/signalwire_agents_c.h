/* Copyright (c) 2025 SignalWire
 * SPDX-License-Identifier: MIT
 *
 * C wrapper for the SignalWire AI Agents C++ SDK.
 * Provides an extern "C" API for FFI from C, Python, Ruby, etc.
 */

#ifndef SIGNALWIRE_AGENTS_C_H
#define SIGNALWIRE_AGENTS_C_H

#ifdef __cplusplus
extern "C" {
#endif

/* Opaque handle types */
typedef void* sw_agent_t;
typedef void* sw_function_result_t;

/* Tool handler callback: receives args JSON string and raw_data JSON string,
 * must return a sw_function_result_t (caller takes ownership). */
typedef sw_function_result_t (*sw_tool_handler_t)(const char* args_json,
                                                   const char* raw_data_json,
                                                   void* user_data);

/* ======================================================================== */
/* Agent lifecycle                                                          */
/* ======================================================================== */

/** Create a new agent with the given name.
 *  @param name  Display name for the agent (UTF-8).
 *  @return Opaque agent handle, or NULL on failure. */
sw_agent_t sw_agent_create(const char* name);

/** Create a new agent with name, route, host, and port. */
sw_agent_t sw_agent_create_full(const char* name, const char* route,
                                 const char* host, int port);

/** Destroy an agent and free all associated resources. */
void sw_agent_destroy(sw_agent_t agent);

/* ======================================================================== */
/* Prompt configuration                                                     */
/* ======================================================================== */

/** Set the raw prompt text (replaces POM). */
void sw_agent_set_prompt(sw_agent_t agent, const char* text);

/** Add a POM section with title and body. */
void sw_agent_add_section(sw_agent_t agent, const char* title, const char* body);

/** Add a POM section with title and bullet points.
 *  @param bullets  NULL-terminated array of C strings. */
void sw_agent_add_section_bullets(sw_agent_t agent, const char* title,
                                   const char** bullets);

/** Set the post-prompt text for summary generation. */
void sw_agent_set_post_prompt(sw_agent_t agent, const char* text);

/* ======================================================================== */
/* Tool (SWAIG function) registration                                       */
/* ======================================================================== */

/** Define a tool (SWAIG function) on the agent.
 *  @param name         Function name.
 *  @param description  Human-readable description.
 *  @param params_json  JSON string of the parameter schema (or NULL for none).
 *  @param handler      Callback invoked when the AI calls this function.
 *  @param user_data    Opaque pointer passed to the handler. */
void sw_agent_define_tool(sw_agent_t agent, const char* name,
                           const char* description, const char* params_json,
                           sw_tool_handler_t handler, void* user_data);

/** List tool names. Returns a NULL-terminated array of strings.
 *  Caller must free the array and each string with sw_free_string_array(). */
char** sw_agent_list_tools(sw_agent_t agent);

/** Free a string array returned by sw_agent_list_tools(). */
void sw_free_string_array(char** arr);

/* ======================================================================== */
/* AI configuration                                                         */
/* ======================================================================== */

/** Set a single AI parameter (value is a JSON string). */
void sw_agent_set_param(sw_agent_t agent, const char* key, const char* value_json);

/** Set global data (JSON object string). */
void sw_agent_set_global_data(sw_agent_t agent, const char* json_str);

/** Add a speech recognition hint. */
void sw_agent_add_hint(sw_agent_t agent, const char* hint);

/** Set basic auth credentials. */
void sw_agent_set_auth(sw_agent_t agent, const char* username, const char* password);

/* ======================================================================== */
/* Skills                                                                   */
/* ======================================================================== */

/** Add a skill by name with optional params (JSON string, or NULL). */
void sw_agent_add_skill(sw_agent_t agent, const char* skill_name,
                          const char* params_json);

/* ======================================================================== */
/* Server                                                                   */
/* ======================================================================== */

/** Start the agent HTTP server (blocking). */
void sw_agent_run(sw_agent_t agent);

/** Start the agent HTTP server (blocking, same as run). */
void sw_agent_serve(sw_agent_t agent);

/** Stop the agent HTTP server. */
void sw_agent_stop(sw_agent_t agent);

/* ======================================================================== */
/* SWML rendering                                                           */
/* ======================================================================== */

/** Render the agent's SWML document as a JSON string.
 *  Caller must free the returned string with sw_free_string(). */
char* sw_agent_render_swml(sw_agent_t agent);

/** Free a string returned by sw_agent_render_swml(). */
void sw_free_string(char* str);

/* ======================================================================== */
/* FunctionResult                                                           */
/* ======================================================================== */

/** Create a new FunctionResult with a response string. */
sw_function_result_t sw_result_create(const char* response);

/** Add an action to a FunctionResult (action_data is a JSON string). */
void sw_result_add_action(sw_function_result_t result, const char* action_name,
                           const char* action_data_json);

/** Serialize a FunctionResult to a JSON string.
 *  Caller must free the returned string with sw_free_string(). */
char* sw_result_to_json(sw_function_result_t result);

/** Destroy a FunctionResult. */
void sw_result_destroy(sw_function_result_t result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* SIGNALWIRE_AGENTS_C_H */
