// schema_utils.hpp — C++ port of signalwire.utils.schema_utils.SchemaUtils.
//
// Loads the SWML JSON Schema, extracts verb metadata, and validates
// either a single verb config or a complete SWML document.  Validation
// is lightweight (verb existence + required-property check) by default;
// full JSON Schema validation can be wired in via
// nlohmann/json-schema-validator by extending init_full_validator.
//
// Construction rules mirror Python:
//
//   - Pass schema_path = "" to use the embedded schema.json.
//   - schema_validation = false disables validation
//     (validate_verb returns (true, []) for every call).
//   - The env var SWML_SKIP_SCHEMA_VALIDATION=1/true/yes also disables
//     validation regardless of the constructor argument.

#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace signalwire {
namespace utils {

using json = nlohmann::json;

/// SchemaValidationError — C++ port of
/// signalwire.utils.schema_utils.SchemaValidationError.
class SchemaValidationError : public std::runtime_error {
public:
    SchemaValidationError(std::string verb_name, std::vector<std::string> errors)
        : std::runtime_error(build_message(verb_name, errors)),
          verb_name_(std::move(verb_name)),
          errors_(std::move(errors)) {}

    const std::string& verb_name() const { return verb_name_; }
    const std::vector<std::string>& errors() const { return errors_; }

private:
    static std::string build_message(const std::string& vn,
                                     const std::vector<std::string>& errs);
    std::string verb_name_;
    std::vector<std::string> errors_;
};

/// Verb metadata extracted from the schema.
struct VerbInfo {
    std::string name;
    std::string schema_name;
    json definition;
};

/// SchemaUtils — C++ port of
/// signalwire.utils.schema_utils.SchemaUtils.
class SchemaUtils {
public:
    /// Construct a SchemaUtils.  Mirrors Python's
    /// `SchemaUtils(schema_path=None, schema_validation=True)`.
    /// Pass schema_path = "" to use the embedded schema.
    SchemaUtils(const std::string& schema_path = "", bool schema_validation = true);

    /// Whether full JSON Schema validation is wired up.
    /// Mirrors Python's full_validation_available property.
    bool full_validation_available() const;

    /// Read and parse the JSON Schema. Mirrors Python's load_schema().
    json load_schema();

    /// Sorted list of all known verb names.
    /// Mirrors Python's get_all_verb_names().
    std::vector<std::string> get_all_verb_names() const;

    /// The properties[verb_name] block for a verb, or empty when
    /// unknown. Mirrors Python's get_verb_properties(verb_name).
    json get_verb_properties(const std::string& verb_name) const;

    /// The required list for a verb, or empty when unknown / not
    /// specified. Mirrors Python's get_verb_required_properties(verb_name).
    std::vector<std::string> get_verb_required_properties(const std::string& verb_name) const;

    /// Parameter-definition block used by code-gen tooling.
    /// Mirrors Python's get_verb_parameters(verb_name).
    json get_verb_parameters(const std::string& verb_name) const;

    /// Validate a verb config against the schema.
    /// Mirrors Python's validate_verb(verb_name, verb_config).
    /// Returns (valid, errors) — Python's Tuple[bool, List[str]].
    std::pair<bool, std::vector<std::string>>
    validate_verb(const std::string& verb_name, const json& verb_config) const;

    /// Validate a complete SWML document.
    /// Mirrors Python's validate_document(document). Returns
    /// (false, ["Schema validator not initialized"]) when no full
    /// validator is wired in.
    std::pair<bool, std::vector<std::string>>
    validate_document(const json& document) const;

    /// Generate a Python-style method signature string for a verb.
    /// Mirrors Python's generate_method_signature(verb_name).
    std::string generate_method_signature(const std::string& verb_name) const;

    /// Generate a Python-style method body string for a verb.
    /// Mirrors Python's generate_method_body(verb_name).
    std::string generate_method_body(const std::string& verb_name) const;

private:
    void extract_verbs();
    void init_full_validator();
    std::pair<bool, std::vector<std::string>>
    validate_verb_full(const std::string& verb_name, const json& verb_config) const;
    std::pair<bool, std::vector<std::string>>
    validate_verb_lightweight(const std::string& verb_name, const json& verb_config) const;

    json schema_;
    std::string schema_path_;
    bool validation_enabled_;
    std::map<std::string, VerbInfo> verbs_;
    bool full_validator_;
};

}  // namespace utils
}  // namespace signalwire
