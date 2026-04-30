// schema_utils.cpp — C++ port of signalwire.utils.schema_utils.SchemaUtils.

#include "signalwire/utils/schema_utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>

#include "signalwire/swml/schema.hpp"

namespace signalwire {
namespace utils {

std::string SchemaValidationError::build_message(const std::string& vn,
                                                 const std::vector<std::string>& errs) {
    std::ostringstream os;
    os << "Schema validation failed for '" << vn << "': ";
    for (size_t i = 0; i < errs.size(); ++i) {
        if (i != 0) os << "; ";
        os << errs[i];
    }
    return os.str();
}

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

bool env_boolish(const char* raw) {
    if (raw == nullptr) return false;
    std::string s = to_lower(raw);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s == "1" || s == "true" || s == "yes";
}

std::string python_type_annotation(const json& def) {
    if (!def.is_object()) return "Any";
    auto it = def.find("type");
    if (it == def.end() || !it->is_string()) {
        if (def.contains("anyOf") || def.contains("oneOf") || def.contains("$ref")) {
            return "Any";
        }
        return "Any";
    }
    const std::string t = it->get<std::string>();
    if (t == "string")  return "str";
    if (t == "integer") return "int";
    if (t == "number")  return "float";
    if (t == "boolean") return "bool";
    if (t == "array") {
        std::string item = "Any";
        auto items = def.find("items");
        if (items != def.end()) item = python_type_annotation(*items);
        return std::string("List[") + item + "]";
    }
    if (t == "object") return "Dict[str, Any]";
    return "Any";
}

}  // namespace

SchemaUtils::SchemaUtils(const std::string& schema_path, bool schema_validation)
    : schema_(json::object()),
      schema_path_(schema_path),
      validation_enabled_(schema_validation && !env_boolish(std::getenv("SWML_SKIP_SCHEMA_VALIDATION"))),
      full_validator_(false) {
    schema_ = load_schema();
    extract_verbs();
    if (validation_enabled_ && !schema_.empty()) {
        init_full_validator();
    }
}

bool SchemaUtils::full_validation_available() const {
    return full_validator_;
}

namespace {
// Candidate schema.json locations.  Mirrors Python's
// _get_default_schema_path manual-search ladder: package/cwd/parent.
std::vector<std::string> default_schema_candidates() {
    std::vector<std::string> out;
#ifdef PROJECT_SOURCE_DIR
    out.push_back(std::string(PROJECT_SOURCE_DIR) + "/src/swml/schema.json");
#endif
    out.push_back("./schema.json");
    out.push_back("./src/swml/schema.json");
    out.push_back("../src/swml/schema.json");
    return out;
}

json read_json_file(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return json::object();
    std::stringstream ss;
    ss << f.rdbuf();
    try {
        return json::parse(ss.str());
    } catch (json::parse_error&) {
        return json::object();
    }
}
}  // namespace

json SchemaUtils::load_schema() {
    if (!schema_path_.empty()) {
        return read_json_file(schema_path_);
    }
    // Default: try the embedded source first, then fall back to the
    // candidate schema.json file paths.  Mirrors Python's
    // _get_default_schema_path manual-search behaviour.
    const std::string& embedded = swml::get_embedded_schema();
    if (!embedded.empty()) {
        try {
            return json::parse(embedded);
        } catch (json::parse_error&) {
            // fall through to file search
        }
    }
    for (const auto& candidate : default_schema_candidates()) {
        json result = read_json_file(candidate);
        if (!result.empty()) {
            return result;
        }
    }
    return json::object();
}

void SchemaUtils::extract_verbs() {
    if (!schema_.contains("$defs") || !schema_["$defs"].is_object()) return;
    const auto& defs = schema_["$defs"];
    if (!defs.contains("SWMLMethod") || !defs["SWMLMethod"].is_object()) return;
    const auto& swml_method = defs["SWMLMethod"];
    if (!swml_method.contains("anyOf") || !swml_method["anyOf"].is_array()) return;
    for (const auto& entry : swml_method["anyOf"]) {
        if (!entry.is_object() || !entry.contains("$ref")) continue;
        if (!entry["$ref"].is_string()) continue;
        const std::string ref = entry["$ref"].get<std::string>();
        const std::string prefix = "#/$defs/";
        if (ref.rfind(prefix, 0) != 0) continue;
        const std::string schema_name = ref.substr(prefix.size());
        if (!defs.contains(schema_name)) continue;
        const auto& defn = defs[schema_name];
        if (!defn.is_object() || !defn.contains("properties")) continue;
        const auto& props = defn["properties"];
        if (!props.is_object() || props.empty()) continue;
        const std::string actual_verb = props.begin().key();
        verbs_[actual_verb] = VerbInfo{actual_verb, schema_name, defn};
    }
}

void SchemaUtils::init_full_validator() {
    // Reserved for full-validator wiring (nlohmann/json-schema-validator).
    full_validator_ = false;
}

std::vector<std::string> SchemaUtils::get_all_verb_names() const {
    std::vector<std::string> out;
    out.reserve(verbs_.size());
    for (const auto& [k, _] : verbs_) out.push_back(k);
    std::sort(out.begin(), out.end());
    return out;
}

json SchemaUtils::get_verb_properties(const std::string& verb_name) const {
    auto it = verbs_.find(verb_name);
    if (it == verbs_.end()) return json::object();
    const auto& defn = it->second.definition;
    if (!defn.contains("properties") || !defn["properties"].is_object()) return json::object();
    const auto& outer = defn["properties"];
    if (!outer.contains(verb_name) || !outer[verb_name].is_object()) return json::object();
    return outer[verb_name];
}

std::vector<std::string> SchemaUtils::get_verb_required_properties(const std::string& verb_name) const {
    json inner = get_verb_properties(verb_name);
    if (!inner.contains("required") || !inner["required"].is_array()) return {};
    std::vector<std::string> out;
    for (const auto& v : inner["required"]) {
        if (v.is_string()) out.push_back(v.get<std::string>());
    }
    return out;
}

json SchemaUtils::get_verb_parameters(const std::string& verb_name) const {
    json inner = get_verb_properties(verb_name);
    if (!inner.contains("properties") || !inner["properties"].is_object()) return json::object();
    return inner["properties"];
}

std::pair<bool, std::vector<std::string>>
SchemaUtils::validate_verb(const std::string& verb_name, const json& verb_config) const {
    if (!validation_enabled_) {
        return {true, {}};
    }
    if (verbs_.find(verb_name) == verbs_.end()) {
        return {false, {std::string("Unknown verb: ") + verb_name}};
    }
    if (full_validator_) {
        return validate_verb_full(verb_name, verb_config);
    }
    return validate_verb_lightweight(verb_name, verb_config);
}

std::pair<bool, std::vector<std::string>>
SchemaUtils::validate_verb_full(const std::string& verb_name, const json& verb_config) const {
    // Reserved for full-validator wiring; falls back to lightweight check.
    return validate_verb_lightweight(verb_name, verb_config);
}

std::pair<bool, std::vector<std::string>>
SchemaUtils::validate_verb_lightweight(const std::string& verb_name, const json& verb_config) const {
    std::vector<std::string> errors;
    for (const auto& prop : get_verb_required_properties(verb_name)) {
        bool present = verb_config.is_object() && verb_config.contains(prop);
        if (!present) {
            errors.push_back("Missing required property '" + prop +
                             "' for verb '" + verb_name + "'");
        }
    }
    return {errors.empty(), errors};
}

std::pair<bool, std::vector<std::string>>
SchemaUtils::validate_document(const json& /*document*/) const {
    if (!full_validator_) {
        return {false, {"Schema validator not initialized"}};
    }
    // Reserved for full-validator wiring.
    return {true, {}};
}

std::string SchemaUtils::generate_method_signature(const std::string& verb_name) const {
    json params = get_verb_parameters(verb_name);
    std::set<std::string> required;
    for (auto& r : get_verb_required_properties(verb_name)) required.insert(r);

    std::vector<std::string> param_keys;
    if (params.is_object()) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            param_keys.push_back(it.key());
        }
    }
    std::sort(param_keys.begin(), param_keys.end());

    std::vector<std::string> parts;
    parts.push_back("self");
    for (const auto& name : param_keys) {
        std::string t = python_type_annotation(params[name]);
        if (required.count(name)) {
            parts.push_back(name + ": " + t);
        } else {
            parts.push_back(name + ": Optional[" + t + "] = None");
        }
    }
    parts.push_back("**kwargs");

    std::ostringstream os;
    os << "def " << verb_name << "(";
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) os << ", ";
        os << parts[i];
    }
    os << ") -> bool:\n";
    os << "\"\"\"\n        Add the " << verb_name << " verb to the current document\n        \n";
    for (const auto& name : param_keys) {
        std::string desc;
        const auto& d = params[name];
        if (d.is_object() && d.contains("description") && d["description"].is_string()) {
            desc = d["description"].get<std::string>();
            std::replace(desc.begin(), desc.end(), '\n', ' ');
            // trim
            while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.front()))) desc.erase(desc.begin());
            while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.back()))) desc.pop_back();
        }
        os << "        Args:\n            " << name << ": " << desc << "\n";
    }
    os << "        \n        Returns:\n            True if the verb was added successfully, False otherwise\n        \"\"\"\n";
    return os.str();
}

std::string SchemaUtils::generate_method_body(const std::string& verb_name) const {
    json params = get_verb_parameters(verb_name);
    std::vector<std::string> param_keys;
    if (params.is_object()) {
        for (auto it = params.begin(); it != params.end(); ++it) {
            param_keys.push_back(it.key());
        }
    }
    std::sort(param_keys.begin(), param_keys.end());

    std::ostringstream os;
    os << "        # Prepare the configuration\n        config = {}";
    for (const auto& name : param_keys) {
        os << "\n        if " << name << " is not None:";
        os << "\n            config['" << name << "'] = " << name;
    }
    os << "\n        # Add any additional parameters from kwargs";
    os << "\n        for key, value in kwargs.items():";
    os << "\n            if value is not None:";
    os << "\n                config[key] = value";
    os << "\n";
    os << "\n        # Add the " << verb_name << " verb";
    os << "\n        return self.add_verb('" << verb_name << "', config)";
    return os.str();
}

}  // namespace utils
}  // namespace signalwire
