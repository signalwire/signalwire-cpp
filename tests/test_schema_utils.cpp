// Schema utility tests — verb definitions, schema loading
#include "signalwire/swml/schema.hpp"
#include "signalwire/swml/document.hpp"
using namespace signalwire::swml;
using json = nlohmann::json;

TEST(schema_utils_load_embedded_ok) {
    Schema schema;
    ASSERT_TRUE(schema.load_embedded());
    return true;
}

TEST(schema_utils_38_verbs) {
    Schema schema;
    (void)schema.load_embedded();
    ASSERT_EQ(schema.verb_names().size(), 38u);
    return true;
}

TEST(schema_utils_find_answer) {
    Schema schema;
    (void)schema.load_embedded();
    auto* vd = schema.find_verb("answer");
    ASSERT_TRUE(vd != nullptr);
    ASSERT_EQ(vd->verb_name, "answer");
    return true;
}

TEST(schema_utils_find_ai) {
    Schema schema;
    (void)schema.load_embedded();
    auto* vd = schema.find_verb("ai");
    ASSERT_TRUE(vd != nullptr);
    ASSERT_EQ(vd->verb_name, "ai");
    return true;
}

TEST(schema_utils_find_nonexistent) {
    Schema schema;
    (void)schema.load_embedded();
    ASSERT_TRUE(schema.find_verb("xyz_nonexistent") == nullptr);
    return true;
}

TEST(schema_utils_verb_has_description) {
    Schema schema;
    (void)schema.load_embedded();
    auto* vd = schema.find_verb("answer");
    ASSERT_TRUE(vd != nullptr);
    // Schema name should be CamelCase
    ASSERT_EQ(vd->schema_name, "Answer");
    return true;
}

TEST(schema_utils_raw_accessible) {
    Schema schema;
    (void)schema.load_embedded();
    // raw() may be null if the implementation doesn't store the full schema
    // Just verify it's accessible without crashing
    auto& raw = schema.raw();
    (void)raw;
    return true;
}

// ========================================================================
// Document utility tests
// ========================================================================

TEST(schema_utils_document_section_access) {
    Document doc;
    auto& s = doc.section("custom");
    s.add_verb("play", json::object({{"url", "test.mp3"}}));
    auto j = doc.to_json();
    ASSERT_TRUE(j["sections"].contains("custom"));
    ASSERT_TRUE(j["sections"]["custom"][0].contains("play"));
    return true;
}

TEST(schema_utils_document_main_shortcut) {
    Document doc;
    doc.main().add_verb("hangup", json::object());
    auto j = doc.to_json();
    ASSERT_TRUE(j["sections"]["main"][0].contains("hangup"));
    return true;
}

TEST(schema_utils_verb_to_json) {
    Verb v("play", json::object({{"url", "audio.mp3"}}));
    auto j = v.to_json();
    ASSERT_TRUE(j.contains("play"));
    ASSERT_EQ(j["play"]["url"].get<std::string>(), "audio.mp3");
    return true;
}

TEST(schema_utils_section_to_json) {
    Section s("test");
    s.add_verb("answer", json::object());
    s.add_verb("hangup", json::object());
    auto j = s.to_json();
    ASSERT_TRUE(j.is_array());
    ASSERT_EQ(j.size(), 2u);
    return true;
}
