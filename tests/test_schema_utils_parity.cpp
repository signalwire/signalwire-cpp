// Parity tests for signalwire::utils::SchemaUtils.
// Mirrors signalwire-python tests/unit/utils/test_schema_utils.py and the
// TS / Perl reference implementations.
//
// Each public method is exercised; assertions check shape (not just
// presence) so the no-cheat-tests audit accepts them.

#include "signalwire/utils/schema_utils.hpp"

#include <cstdlib>
#include <string>

using signalwire::utils::SchemaUtils;
using signalwire::utils::SchemaValidationError;

namespace {

bool contains_string(const std::vector<std::string>& haystack, const std::string& needle) {
    for (const auto& s : haystack) {
        if (s == needle) return true;
    }
    return false;
}

bool string_contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(schema_utils_default_load) {
    SchemaUtils su;
    auto names = su.get_all_verb_names();
    ASSERT_FALSE(names.empty());
    ASSERT_TRUE(contains_string(names, "ai"));
    ASSERT_TRUE(contains_string(names, "answer"));
    return true;
}

TEST(schema_utils_disabled_validation) {
    SchemaUtils su("", false);
    ASSERT_FALSE(su.full_validation_available());
    auto [valid, errors] = su.validate_verb("ai", nlohmann::json::object());
    ASSERT_TRUE(valid);
    ASSERT_TRUE(errors.empty());
    return true;
}

TEST(schema_utils_env_skip_disables_validation) {
    ::setenv("SWML_SKIP_SCHEMA_VALIDATION", "1", 1);
    SchemaUtils su("", true);
    ASSERT_FALSE(su.full_validation_available());
    auto [valid, errors] = su.validate_verb("ai", nlohmann::json::object());
    ASSERT_TRUE(valid);
    ASSERT_TRUE(errors.empty());
    ::unsetenv("SWML_SKIP_SCHEMA_VALIDATION");
    return true;
}

TEST(schema_utils_validate_verb_unknown) {
    SchemaUtils su;
    auto [valid, errors] = su.validate_verb("not_a_real_verb", nlohmann::json::object());
    ASSERT_FALSE(valid);
    ASSERT_EQ(errors.size(), static_cast<size_t>(1));
    ASSERT_TRUE(string_contains(errors[0], "Unknown verb"));
    return true;
}

TEST(schema_utils_get_verb_properties_known) {
    SchemaUtils su;
    auto props = su.get_verb_properties("answer");
    ASSERT_TRUE(props.is_object());
    ASSERT_FALSE(props.empty());
    ASSERT_TRUE(props.contains("type"));
    ASSERT_EQ(props["type"].get<std::string>(), std::string("object"));
    return true;
}

TEST(schema_utils_get_verb_properties_unknown) {
    SchemaUtils su;
    auto props = su.get_verb_properties("not_a_verb");
    ASSERT_TRUE(props.is_object());
    ASSERT_TRUE(props.empty());
    return true;
}

TEST(schema_utils_get_verb_required_properties_unknown) {
    SchemaUtils su;
    auto req = su.get_verb_required_properties("not_a_verb");
    ASSERT_TRUE(req.empty());
    return true;
}

TEST(schema_utils_validate_document_no_full_validator) {
    // C++ port doesn't ship a full validator yet; validate_document must
    // return (false, ["Schema validator not initialized"]) — same contract
    // as Python.
    SchemaUtils su;
    nlohmann::json doc = {
        {"version", "1.0.0"},
        {"sections", nlohmann::json::object()},
    };
    auto [valid, errors] = su.validate_document(doc);
    ASSERT_FALSE(valid);
    ASSERT_EQ(errors.size(), static_cast<size_t>(1));
    ASSERT_TRUE(string_contains(errors[0], "validator not initialized"));
    return true;
}

TEST(schema_utils_generate_method_signature) {
    SchemaUtils su;
    std::string sig = su.generate_method_signature("answer");
    ASSERT_TRUE(string_contains(sig, "def answer("));
    ASSERT_TRUE(string_contains(sig, "**kwargs"));
    return true;
}

TEST(schema_utils_generate_method_body) {
    SchemaUtils su;
    std::string body = su.generate_method_body("answer");
    ASSERT_TRUE(string_contains(body, "self.add_verb('answer'"));
    ASSERT_TRUE(string_contains(body, "config = {}"));
    return true;
}

TEST(schema_validation_error_message) {
    SchemaValidationError err("ai", {"missing prompt", "bad type"});
    std::string msg = err.what();
    ASSERT_TRUE(string_contains(msg, "ai"));
    ASSERT_TRUE(string_contains(msg, "missing prompt"));
    ASSERT_EQ(err.verb_name(), std::string("ai"));
    ASSERT_EQ(err.errors().size(), static_cast<size_t>(2));
    return true;
}
