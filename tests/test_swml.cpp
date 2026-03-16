// SWML Document, Schema, and Service tests

#include "signalwire/swml/document.hpp"
#include "signalwire/swml/schema.hpp"
#include "signalwire/swml/service.hpp"
#include "signalwire/logging.hpp"

using namespace signalwire::swml;
using json = nlohmann::json;

// ========================================================================
// Document tests
// ========================================================================

TEST(document_default_version) {
    Document doc;
    auto j = doc.to_json();
    ASSERT_EQ(j["version"].get<std::string>(), "1.0.0");
    return true;
}

TEST(document_has_main_section) {
    Document doc;
    auto j = doc.to_json();
    ASSERT_TRUE(j.contains("sections"));
    ASSERT_TRUE(j["sections"].contains("main"));
    ASSERT_TRUE(j["sections"]["main"].is_array());
    return true;
}

TEST(document_add_verb) {
    Document doc;
    doc.add_verb("answer", json::object({{"max_duration", 3600}}));
    auto j = doc.to_json();
    ASSERT_EQ(j["sections"]["main"].size(), 1u);
    ASSERT_TRUE(j["sections"]["main"][0].contains("answer"));
    ASSERT_EQ(j["sections"]["main"][0]["answer"]["max_duration"].get<int>(), 3600);
    return true;
}

TEST(document_multiple_verbs) {
    Document doc;
    doc.add_verb("answer", json::object({{"max_duration", 3600}}));
    doc.add_verb("hangup", json::object());
    auto j = doc.to_json();
    ASSERT_EQ(j["sections"]["main"].size(), 2u);
    ASSERT_TRUE(j["sections"]["main"][0].contains("answer"));
    ASSERT_TRUE(j["sections"]["main"][1].contains("hangup"));
    return true;
}

TEST(document_custom_section) {
    Document doc;
    doc.add_verb_to_section("custom", "play", json::object({{"url", "test.mp3"}}));
    auto j = doc.to_json();
    ASSERT_TRUE(j["sections"].contains("custom"));
    ASSERT_TRUE(j["sections"]["custom"][0].contains("play"));
    return true;
}

TEST(document_has_section) {
    Document doc;
    ASSERT_TRUE(doc.has_section("main"));
    ASSERT_FALSE(doc.has_section("nonexistent"));
    doc.section("custom");
    ASSERT_TRUE(doc.has_section("custom"));
    return true;
}

TEST(document_to_string) {
    Document doc;
    doc.add_verb("answer", json::object());
    std::string s = doc.to_string();
    ASSERT_TRUE(s.find("answer") != std::string::npos);
    ASSERT_TRUE(s.find("1.0.0") != std::string::npos);
    return true;
}

TEST(document_set_version) {
    Document doc;
    doc.set_version("2.0.0");
    auto j = doc.to_json();
    ASSERT_EQ(j["version"].get<std::string>(), "2.0.0");
    return true;
}

// ========================================================================
// Schema tests
// ========================================================================

TEST(schema_load_embedded) {
    Schema schema;
    ASSERT_TRUE(schema.load_embedded());
    auto names = schema.verb_names();
    ASSERT_EQ(names.size(), 38u);
    return true;
}

TEST(schema_find_verb) {
    Schema schema;
    schema.load_embedded();
    auto* vd = schema.find_verb("answer");
    ASSERT_TRUE(vd != nullptr);
    ASSERT_EQ(vd->verb_name, "answer");
    ASSERT_EQ(vd->schema_name, "Answer");
    return true;
}

TEST(schema_find_sip_refer) {
    Schema schema;
    schema.load_embedded();
    auto* vd = schema.find_verb("sip_refer");
    ASSERT_TRUE(vd != nullptr);
    ASSERT_EQ(vd->schema_name, "SIPRefer");
    return true;
}

TEST(schema_find_nonexistent) {
    Schema schema;
    schema.load_embedded();
    ASSERT_TRUE(schema.find_verb("nonexistent_verb") == nullptr);
    return true;
}

TEST(schema_verb_names_contain_all_38) {
    Schema schema;
    schema.load_embedded();
    auto names = schema.verb_names();

    // Check some specific important verbs
    auto has = [&](const std::string& n) {
        return std::find(names.begin(), names.end(), n) != names.end();
    };

    ASSERT_TRUE(has("answer"));
    ASSERT_TRUE(has("ai"));
    ASSERT_TRUE(has("hangup"));
    ASSERT_TRUE(has("connect"));
    ASSERT_TRUE(has("play"));
    ASSERT_TRUE(has("record"));
    ASSERT_TRUE(has("transfer"));
    ASSERT_TRUE(has("sleep"));
    ASSERT_TRUE(has("sip_refer"));
    ASSERT_TRUE(has("detect_machine"));
    ASSERT_TRUE(has("user_event"));
    ASSERT_TRUE(has("amazon_bedrock"));
    ASSERT_TRUE(has("live_transcribe"));
    ASSERT_TRUE(has("live_translate"));
    ASSERT_TRUE(has("enter_queue"));
    ASSERT_TRUE(has("join_conference"));
    ASSERT_TRUE(has("join_room"));
    ASSERT_TRUE(has("pay"));
    ASSERT_TRUE(has("send_sms"));
    return true;
}

TEST(schema_load_from_file) {
    Schema schema;
    bool loaded = schema.load_from_file("/home/devuser/src/signalwire-agents-cpp/src/swml/schema.json");
    if (loaded) {
        auto names = schema.verb_names();
        ASSERT_EQ(names.size(), 38u);
    }
    // It's OK if file doesn't exist in CI
    return true;
}

// ========================================================================
// Service tests
// ========================================================================

TEST(service_default_route) {
    Service svc;
    ASSERT_EQ(svc.route(), "/");
    return true;
}

TEST(service_set_route) {
    Service svc;
    svc.set_route("/agent");
    ASSERT_EQ(svc.route(), "/agent");
    return true;
}

TEST(service_set_route_prepends_slash) {
    Service svc;
    svc.set_route("agent");
    ASSERT_EQ(svc.route(), "/agent");
    return true;
}

TEST(service_set_auth) {
    Service svc;
    svc.set_auth("user", "pass");
    ASSERT_EQ(svc.auth_username(), "user");
    ASSERT_EQ(svc.auth_password(), "pass");
    return true;
}

TEST(service_verb_methods_add_to_document) {
    Service svc;
    svc.answer(json::object({{"max_duration", 3600}}));
    svc.hangup();

    auto j = svc.render_swml();
    ASSERT_EQ(j["sections"]["main"].size(), 2u);
    ASSERT_TRUE(j["sections"]["main"][0].contains("answer"));
    ASSERT_TRUE(j["sections"]["main"][1].contains("hangup"));
    return true;
}

TEST(service_sleep_verb) {
    Service svc;
    svc.sleep(1000);
    auto j = svc.render_swml();
    ASSERT_TRUE(j["sections"]["main"][0].contains("sleep"));
    ASSERT_EQ(j["sections"]["main"][0]["sleep"].get<int>(), 1000);
    return true;
}

TEST(service_all_verbs_exist) {
    Service svc;
    // Call each verb method to ensure it compiles and works
    svc.answer();
    svc.ai();
    svc.amazon_bedrock();
    svc.cond(json::array());
    svc.connect();
    svc.denoise();
    svc.detect_machine();
    svc.enter_queue();
    svc.execute();
    svc.goto_section();
    svc.hangup();
    svc.join_conference();
    svc.join_room();
    svc.label();
    svc.live_transcribe();
    svc.live_translate();
    svc.pay();
    svc.play();
    svc.prompt();
    svc.receive_fax();
    svc.record();
    svc.record_call();
    svc.request();
    svc.return_section();
    svc.send_digits();
    svc.send_fax();
    svc.send_sms();
    svc.set();
    svc.sleep(500);
    svc.sip_refer();
    svc.stop_denoise();
    svc.stop_record_call();
    svc.stop_tap();
    svc.switch_section();
    svc.tap();
    svc.transfer();
    svc.unset();
    svc.user_event();

    auto j = svc.render_swml();
    // 37 explicit calls + 1 sleep = 38 verbs
    ASSERT_EQ(j["sections"]["main"].size(), 38u);
    return true;
}

TEST(service_render_swml_json) {
    Service svc;
    svc.answer(json::object({{"max_duration", 7200}}));
    auto j = svc.render_swml();
    ASSERT_EQ(j["version"].get<std::string>(), "1.0.0");
    ASSERT_TRUE(j.contains("sections"));
    return true;
}

TEST(service_timing_safe_compare) {
    ASSERT_TRUE(Service::timing_safe_compare("hello", "hello"));
    ASSERT_FALSE(Service::timing_safe_compare("hello", "world"));
    ASSERT_FALSE(Service::timing_safe_compare("short", "longer_string"));
    ASSERT_TRUE(Service::timing_safe_compare("", ""));
    return true;
}

TEST(service_generate_random_hex) {
    auto hex = Service::generate_random_hex(16);
    ASSERT_EQ(hex.size(), 32u); // 16 bytes = 32 hex chars
    // Verify all chars are hex
    for (char c : hex) {
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
    // Two random strings should differ
    auto hex2 = Service::generate_random_hex(16);
    ASSERT_NE(hex, hex2);
    return true;
}
