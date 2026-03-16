#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace swml {

using json = nlohmann::json;

/// Represents a verb definition extracted from the SWML schema
struct VerbDefinition {
    std::string schema_name;   // e.g., "SIPRefer"
    std::string verb_name;     // e.g., "sip_refer"
    json properties;           // The verb's parameter schema
    std::string description;
};

/// Schema loader that extracts verb definitions from schema.json
class Schema {
public:
    Schema() = default;

    /// Load schema from a JSON string
    bool load_from_string(const std::string& schema_json);

    /// Load schema from a file path
    bool load_from_file(const std::string& path);

    /// Load the embedded schema
    bool load_embedded();

    /// Get all verb definitions
    const std::vector<VerbDefinition>& verb_definitions() const { return verbs_; }

    /// Get a specific verb definition by verb name
    const VerbDefinition* find_verb(const std::string& verb_name) const;

    /// Get all verb names
    std::vector<std::string> verb_names() const;

    /// Get the raw schema JSON
    const json& raw() const { return schema_; }

private:
    void extract_verbs();

    json schema_;
    std::vector<VerbDefinition> verbs_;
    std::map<std::string, size_t> verb_index_;
};

/// Get the embedded schema JSON string
const std::string& get_embedded_schema();

} // namespace swml
} // namespace signalwire
