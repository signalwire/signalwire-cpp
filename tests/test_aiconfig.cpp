// AI Config mixin tests — hints, languages, pronunciations, params,
// global data, native functions, fillers, debug events, function includes

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using json = nlohmann::json;

// Helper to find the AI verb in rendered SWML
static json find_ai_verb(const json& swml) {
    auto& main = swml["sections"]["main"];
    for (const auto& verb : main) {
        if (verb.contains("ai")) return verb["ai"];
    }
    return json::object();
}

// ========================================================================
// Hints
// ========================================================================

TEST(aiconfig_add_single_hint) {
    AgentBase agent;
    agent.add_hint("hello");
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("hints"));
    ASSERT_EQ(ai["hints"].size(), 1u);
    ASSERT_EQ(ai["hints"][0].get<std::string>(), "hello");
    return true;
}

TEST(aiconfig_add_multiple_hints) {
    AgentBase agent;
    agent.add_hints({"hello", "world", "test"});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["hints"].size(), 3u);
    return true;
}

TEST(aiconfig_add_pattern_hint) {
    AgentBase agent;
    agent.add_pattern_hint("^[0-9]+$");
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["hints"].size(), 1u);
    ASSERT_EQ(ai["hints"][0].get<std::string>(), "^[0-9]+$");
    return true;
}

TEST(aiconfig_no_hints_not_in_swml) {
    AgentBase agent;
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai.contains("hints"));
    return true;
}

// ========================================================================
// Languages
// ========================================================================

TEST(aiconfig_add_language) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "rachel", "", ""});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("languages"));
    ASSERT_EQ(ai["languages"].size(), 1u);
    ASSERT_EQ(ai["languages"][0]["name"].get<std::string>(), "English");
    ASSERT_EQ(ai["languages"][0]["code"].get<std::string>(), "en-US");
    ASSERT_EQ(ai["languages"][0]["voice"].get<std::string>(), "rachel");
    return true;
}

TEST(aiconfig_set_languages_replaces) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "rachel", "", ""});
    agent.set_languages({
        {"Spanish", "es-ES", "isabella", "", ""},
        {"French", "fr-FR", "jean", "", ""}
    });
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["languages"].size(), 2u);
    ASSERT_EQ(ai["languages"][0]["name"].get<std::string>(), "Spanish");
    ASSERT_EQ(ai["languages"][1]["name"].get<std::string>(), "French");
    return true;
}

TEST(aiconfig_language_with_engine) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "alloy", "openai", ""});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["languages"][0]["engine"].get<std::string>(), "openai");
    return true;
}

// ========================================================================
// Per-language params (Python parity: 029ca6f, TestPerLanguageParams)
// ========================================================================
// The Python reference adds add_language(params=...) +
// set_language_params(code, params) + get_language_params(code).
// C++ keeps add_language as a single LanguageConfig overload — params
// is now a field on the struct (json), and the two new accessors are
// pure-additions on AgentBase. SWML key stays snake_case ("params").

TEST(aiconfig_add_language_with_params_attaches_params) {
    AgentBase agent;
    LanguageConfig lc{"English", "en-US", "josh", "elevenlabs", "",
                      json::object({{"stability", 0.5}, {"similarity_boost", 0.75}})};
    agent.add_language(lc);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai["languages"][0].contains("params"));
    ASSERT_EQ(ai["languages"][0]["params"]["stability"].get<double>(), 0.5);
    ASSERT_EQ(ai["languages"][0]["params"]["similarity_boost"].get<double>(), 0.75);
    return true;
}

TEST(aiconfig_add_language_without_params_omits_key) {
    AgentBase agent;
    agent.add_language({"French", "fr-FR", "fr-FR-Neural2-A", "", ""});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai["languages"][0].contains("params"));
    return true;
}

TEST(aiconfig_add_language_with_empty_params_omits_key) {
    AgentBase agent;
    LanguageConfig lc{"French", "fr-FR", "v", "", "", json::object()};
    agent.add_language(lc);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai["languages"][0].contains("params"));
    return true;
}

TEST(aiconfig_get_language_params_returns_set_dict) {
    AgentBase agent;
    LanguageConfig lc{"English", "en-US", "v", "", "", json::object({{"a", 1}})};
    agent.add_language(lc);
    auto got = agent.get_language_params("en-US");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ((*got)["a"].get<int>(), 1);
    return true;
}

TEST(aiconfig_get_language_params_returns_nullopt_when_unset) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "v", "", ""});
    auto got = agent.get_language_params("en-US");
    ASSERT_FALSE(got.has_value());
    return true;
}

TEST(aiconfig_get_language_params_returns_nullopt_for_unknown_code) {
    AgentBase agent;
    auto got = agent.get_language_params("zh-CN");
    ASSERT_FALSE(got.has_value());
    return true;
}

TEST(aiconfig_set_language_params_replaces_existing) {
    AgentBase agent;
    LanguageConfig lc{"English", "en-US", "v", "", "", json::object({{"a", 1}})};
    agent.add_language(lc);
    agent.set_language_params("en-US", json::object({{"b", 2}}));
    auto got = agent.get_language_params("en-US");
    ASSERT_TRUE(got.has_value());
    ASSERT_FALSE(got->contains("a"));
    ASSERT_EQ((*got)["b"].get<int>(), 2);
    return true;
}

TEST(aiconfig_set_language_params_adds_when_unset) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "v", "", ""});
    agent.set_language_params("en-US", json::object({{"c", 3}}));
    auto got = agent.get_language_params("en-US");
    ASSERT_TRUE(got.has_value());
    ASSERT_EQ((*got)["c"].get<int>(), 3);
    return true;
}

TEST(aiconfig_set_language_params_empty_object_removes_key) {
    AgentBase agent;
    LanguageConfig lc{"English", "en-US", "v", "", "", json::object({{"a", 1}})};
    agent.add_language(lc);
    agent.set_language_params("en-US", json::object());
    auto got = agent.get_language_params("en-US");
    ASSERT_FALSE(got.has_value());
    // And the wire shape doesn't carry the key any more either.
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai["languages"][0].contains("params"));
    return true;
}

TEST(aiconfig_set_language_params_unknown_code_is_noop) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "v", "", ""});
    agent.set_language_params("zh-CN", json::object({{"a", 1}}));
    // The known language remains untouched.
    auto got = agent.get_language_params("en-US");
    ASSERT_FALSE(got.has_value());
    return true;
}

TEST(aiconfig_set_language_params_returns_self_for_chaining) {
    AgentBase agent;
    agent.add_language({"English", "en-US", "v", "", ""});
    AgentBase& ret = agent.set_language_params("en-US", json::object({{"a", 1}}));
    ASSERT_TRUE(&ret == &agent);
    return true;
}

// ========================================================================
// Multilingual (Mode B) — set_multilingual(config) -> top-level "multilingual"
// ========================================================================

TEST(aiconfig_set_multilingual_emits_wire_key) {
    AgentBase agent;
    json cfg = json::object({
        {"languages", json::array({"en", "es"})},
        {"start_language", "en"},
        {"min_switch_words", 2},
    });
    agent.set_multilingual(cfg);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("multilingual"));
    ASSERT_EQ(ai["multilingual"], cfg);
    return true;
}

TEST(aiconfig_no_multilingual_not_in_swml) {
    AgentBase agent;
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai.contains("multilingual"));
    return true;
}

TEST(aiconfig_set_multilingual_empty_object_ignored) {
    AgentBase agent;
    agent.set_multilingual(json::object());
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai.contains("multilingual"));
    return true;
}

TEST(aiconfig_set_multilingual_non_object_ignored) {
    AgentBase agent;
    agent.set_multilingual(json("not-a-dict"));
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai.contains("multilingual"));
    return true;
}

TEST(aiconfig_set_multilingual_coexists_with_languages) {
    // multilingual and languages both emit; server prefers multilingual.
    AgentBase agent;
    agent.add_language({"English", "en-US", "rachel", "", ""});
    agent.set_multilingual(json::object({{"start_language", "en"}}));
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("languages"));
    ASSERT_TRUE(ai.contains("multilingual"));
    return true;
}

TEST(aiconfig_set_multilingual_returns_self_for_chaining) {
    AgentBase agent;
    AgentBase& ret = agent.set_multilingual(json::object({{"start_language", "en"}}));
    ASSERT_TRUE(&ret == &agent);
    return true;
}

// ========================================================================
// Pronunciations
// ========================================================================

TEST(aiconfig_add_pronunciation) {
    AgentBase agent;
    agent.add_pronunciation("SW", "SignalWire");
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("pronounce"));
    ASSERT_EQ(ai["pronounce"].size(), 1u);
    ASSERT_EQ(ai["pronounce"][0]["replace"].get<std::string>(), "SW");
    ASSERT_EQ(ai["pronounce"][0]["with"].get<std::string>(), "SignalWire");
    return true;
}

TEST(aiconfig_add_pronunciation_ignore_case) {
    AgentBase agent;
    agent.add_pronunciation("api", "A.P.I.", true);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["pronounce"][0]["ignore_case"].get<bool>(), true);
    return true;
}

TEST(aiconfig_set_pronunciations_replaces) {
    AgentBase agent;
    agent.add_pronunciation("A", "Alpha");
    agent.set_pronunciations({{"B", "Bravo", false}, {"C", "Charlie", false}});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["pronounce"].size(), 2u);
    ASSERT_EQ(ai["pronounce"][0]["replace"].get<std::string>(), "B");
    return true;
}

// ========================================================================
// Parameters
// ========================================================================

TEST(aiconfig_set_param) {
    AgentBase agent;
    agent.set_param("temperature", 0.7);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["params"]["temperature"].get<double>(), 0.7);
    return true;
}

TEST(aiconfig_set_params_multiple) {
    AgentBase agent;
    agent.set_params(json::object({
        {"temperature", 0.3}, {"top_p", 0.95}, {"barge_confidence", 0.8}
    }));
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["params"]["temperature"].get<double>(), 0.3);
    ASSERT_EQ(ai["params"]["top_p"].get<double>(), 0.95);
    ASSERT_EQ(ai["params"]["barge_confidence"].get<double>(), 0.8);
    return true;
}

TEST(aiconfig_set_param_override) {
    AgentBase agent;
    agent.set_param("temperature", 0.5);
    agent.set_param("temperature", 0.9);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["params"]["temperature"].get<double>(), 0.9);
    return true;
}

TEST(aiconfig_no_params_not_in_swml) {
    AgentBase agent;
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai.contains("params"));
    return true;
}

// ========================================================================
// Global data
// ========================================================================

TEST(aiconfig_set_global_data) {
    AgentBase agent;
    agent.set_global_data(json::object({{"key1", "value1"}}));
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["global_data"]["key1"].get<std::string>(), "value1");
    return true;
}

TEST(aiconfig_update_global_data_merges) {
    AgentBase agent;
    agent.set_global_data(json::object({{"a", 1}}));
    agent.update_global_data(json::object({{"b", 2}}));
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["global_data"]["a"].get<int>(), 1);
    ASSERT_EQ(ai["global_data"]["b"].get<int>(), 2);
    return true;
}

TEST(aiconfig_update_global_data_overwrites_key) {
    AgentBase agent;
    agent.set_global_data(json::object({{"key", "old"}}));
    agent.update_global_data(json::object({{"key", "new"}}));
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_EQ(ai["global_data"]["key"].get<std::string>(), "new");
    return true;
}

// ========================================================================
// Native functions
// ========================================================================

TEST(aiconfig_set_native_functions) {
    AgentBase agent;
    agent.set_auth("u", "p");
    agent.set_native_functions({"check_for_input", "debug_info"});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai["SWAIG"].contains("native_functions"));
    ASSERT_EQ(ai["SWAIG"]["native_functions"].size(), 2u);
    ASSERT_EQ(ai["SWAIG"]["native_functions"][0].get<std::string>(), "check_for_input");
    return true;
}

// ========================================================================
// Internal fillers
// ========================================================================

TEST(aiconfig_set_internal_fillers) {
    AgentBase agent;
    json fillers = json::object({
        {"en-US", json::array({"Let me think...", "One moment..."})},
        {"es-ES", json::array({"Un momento..."})}
    });
    agent.set_internal_fillers(fillers);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("fillers"));
    ASSERT_EQ(ai["fillers"]["en-US"].size(), 2u);
    return true;
}

TEST(aiconfig_add_internal_filler) {
    AgentBase agent;
    agent.add_internal_filler("en-US", {"Hold on...", "Working..."});
    agent.add_internal_filler("fr-FR", {"Un instant..."});
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("fillers"));
    ASSERT_EQ(ai["fillers"]["en-US"].size(), 2u);
    ASSERT_EQ(ai["fillers"]["fr-FR"].size(), 1u);
    return true;
}

// ========================================================================
// Debug events
// ========================================================================

TEST(aiconfig_enable_debug_events) {
    AgentBase agent;
    agent.enable_debug_events(true);
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_TRUE(ai.contains("debug_events"));
    ASSERT_EQ(ai["debug_events"].get<bool>(), true);
    return true;
}

TEST(aiconfig_debug_events_off_by_default) {
    AgentBase agent;
    json swml = agent.render_swml();
    auto ai = find_ai_verb(swml);
    ASSERT_FALSE(ai.contains("debug_events"));
    return true;
}

// ========================================================================
// Callbacks
// ========================================================================

TEST(aiconfig_on_summary_callback_set) {
    AgentBase agent;
    bool called = false;
    agent.on_summary([&](const json&, const json&) { called = true; });
    // Callback is stored; no crash
    ASSERT_FALSE(called); // Not fired without a request
    return true;
}

TEST(aiconfig_on_debug_event_callback_set) {
    AgentBase agent;
    bool called = false;
    agent.on_debug_event([&](const json&) { called = true; });
    ASSERT_FALSE(called);
    return true;
}
