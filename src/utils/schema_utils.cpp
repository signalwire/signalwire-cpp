// schema_utils.cpp — C++ port of signalwire.utils.schema_utils.SchemaUtils.

#include "signalwire/utils/schema_utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
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
    if (i != 0) {
      os << "; ";
    }
    os << errs[i];
  }
  return os.str();
}

namespace {

std::string to_lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
  return s;
}

bool env_boolish(const char* raw) {
  if (raw == nullptr) {
    return false;
  }
  std::string s = to_lower(raw);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  return s == "1" || s == "true" || s == "yes";
}

std::string python_type_annotation(const json& def) {
  if (!def.is_object()) {
    return "Any";
  }
  auto it = def.find("type");
  if (it == def.end() || !it->is_string()) {
    if (def.contains("anyOf") || def.contains("oneOf") || def.contains("$ref")) {
      return "Any";
    }
    return "Any";
  }
  const std::string t = it->get<std::string>();
  if (t == "string") {
    return "str";
  }
  if (t == "integer") {
    return "int";
  }
  if (t == "number") {
    return "float";
  }
  if (t == "boolean") {
    return "bool";
  }
  if (t == "array") {
    std::string item = "Any";
    auto items = def.find("items");
    if (items != def.end()) {
      item = python_type_annotation(*items);
    }
    return std::string("List[") + item + "]";
  }
  if (t == "object") {
    return "Dict[str, Any]";
  }
  return "Any";
}

}  // namespace

SchemaUtils::SchemaUtils(const std::string& schema_path, bool schema_validation)
    : schema_(json::object()),
      schema_path_(schema_path),
      validation_enabled_(schema_validation &&
                          !env_boolish(std::getenv("SWML_SKIP_SCHEMA_VALIDATION"))),
      full_validator_(false) {
  schema_ = load_schema();
  extract_verbs();
  if (validation_enabled_ && !schema_.empty()) {
    init_full_validator();
  }
}

bool SchemaUtils::full_validation_available() const { return full_validator_; }

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
  if (!f.is_open()) {
    return json::object();
  }
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
    } catch (json::parse_error& e) {
      // Embedded schema is malformed; fall through to the file search below.
      static_cast<void>(e);
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
  if (!schema_.contains("$defs") || !schema_["$defs"].is_object()) {
    return;
  }
  const auto& defs = schema_["$defs"];
  if (!defs.contains("SWMLMethod") || !defs["SWMLMethod"].is_object()) {
    return;
  }
  const auto& swml_method = defs["SWMLMethod"];
  if (!swml_method.contains("anyOf") || !swml_method["anyOf"].is_array()) {
    return;
  }
  for (const auto& entry : swml_method["anyOf"]) {
    if (!entry.is_object() || !entry.contains("$ref")) {
      continue;
    }
    if (!entry["$ref"].is_string()) {
      continue;
    }
    const std::string ref = entry["$ref"].get<std::string>();
    const std::string prefix = "#/$defs/";
    if (ref.rfind(prefix, 0) != 0) {
      continue;
    }
    const std::string schema_name = ref.substr(prefix.size());
    if (!defs.contains(schema_name)) {
      continue;
    }
    const auto& defn = defs[schema_name];
    if (!defn.is_object() || !defn.contains("properties")) {
      continue;
    }
    const auto& props = defn["properties"];
    if (!props.is_object() || props.empty()) {
      continue;
    }
    const std::string actual_verb = props.begin().key();
    verbs_[actual_verb] = VerbInfo{actual_verb, schema_name, defn};
  }
}

void SchemaUtils::init_full_validator() {
  // full_validator_ tracks the (still-unwired) whole-DOCUMENT JSON-Schema
  // validator — kept false; validate_document reports "not initialized" and
  // full_validation_available() stays false, matching the reference contract
  // for a port without jsonschema-rs.
  full_validator_ = false;
  // strict_verb_validation_ enables the focused verb-level strict-render checks
  // (validate_verb_full: unknown/misspelled keys, wrong types, required) when
  // the loaded schema is structurally complete (has the document 'sections'
  // property and an extracted verb set). Partial/mocked schemas fall back to the
  // lightweight required-props check so a test stub is never a false reject.
  const bool has_sections = schema_.contains("properties") && schema_["properties"].is_object() &&
                            schema_["properties"].contains("sections");
  strict_verb_validation_ = has_sections && !verbs_.empty();
}

std::vector<std::string> SchemaUtils::get_all_verb_names() const {
  std::vector<std::string> out;
  out.reserve(verbs_.size());
  for (const auto& [k, _] : verbs_) {
    out.push_back(k);
  }
  std::sort(out.begin(), out.end());
  return out;
}

json SchemaUtils::get_verb_properties(const std::string& verb_name) const {
  auto it = verbs_.find(verb_name);
  if (it == verbs_.end()) {
    return json::object();
  }
  const auto& defn = it->second.definition;
  if (!defn.contains("properties") || !defn["properties"].is_object()) {
    return json::object();
  }
  const auto& outer = defn["properties"];
  if (!outer.contains(verb_name) || !outer[verb_name].is_object()) {
    return json::object();
  }
  return outer[verb_name];
}

std::vector<std::string> SchemaUtils::get_verb_required_properties(
    const std::string& verb_name) const {
  json inner = get_verb_properties(verb_name);
  if (!inner.contains("required") || !inner["required"].is_array()) {
    return {};
  }
  std::vector<std::string> out;
  for (const auto& v : inner["required"]) {
    if (v.is_string()) {
      out.push_back(v.get<std::string>());
    }
  }
  return out;
}

json SchemaUtils::get_verb_parameters(const std::string& verb_name) const {
  json inner = get_verb_properties(verb_name);
  if (!inner.contains("properties") || !inner["properties"].is_object()) {
    return json::object();
  }
  return inner["properties"];
}

std::pair<bool, std::vector<std::string>> SchemaUtils::validate_verb(
    const std::string& verb_name, const json& verb_config) const {
  if (!validation_enabled_) {
    return {true, {}};
  }
  if (verbs_.find(verb_name) == verbs_.end()) {
    return {false, {std::string("Unknown verb: ") + verb_name}};
  }
  if (strict_verb_validation_) {
    return validate_verb_full(verb_name, verb_config);
  }
  return validate_verb_lightweight(verb_name, verb_config);
}

namespace {

// A focused recursive JSON-Schema validator for the strict-render contract.
// Handles the keyword subset the SWML verb schemas use: $ref (into $defs),
// anyOf / oneOf unions, type (incl. string `pattern`), object `properties` with
// closure (additionalProperties:false / unevaluatedProperties disallowed) +
// required, and array `items`. Enough to reject unknown/misspelled keys and
// wrong-typed values (an integer field given a string, a SWMLVar-pattern string
// given a plain string) — the surface the strict-render corpus covers — without
// pulling in a full JSON-Schema engine. Unknown keywords are permissive (they
// don't reject), so a shape the subset doesn't model is never a false reject.

bool type_keyword_accepts(const std::string& t, const json& value) {
  if (t == "string") {
    return value.is_string();
  }
  if (t == "integer") {
    return value.is_number_integer() || value.is_number_unsigned();
  }
  if (t == "number") {
    return value.is_number();
  }
  if (t == "boolean") {
    return value.is_boolean();
  }
  if (t == "array") {
    return value.is_array();
  }
  if (t == "object") {
    return value.is_object();
  }
  if (t == "null") {
    return value.is_null();
  }
  return true;  // unknown keyword — don't reject
}

// Resolve a single-level $ref ("#/$defs/Name") against the schema root. Returns
// by value (the referenced sub-schema, or the fragment itself when it is not a
// resolvable $ref) — the fragments are small and a value return sidesteps any
// dangling-reference hazard when the caller passes a temporary.
json deref(const json& schema_root, const json& frag) {
  if (frag.is_object() && frag.contains("$ref") && frag["$ref"].is_string()) {
    const std::string ref = frag["$ref"].get<std::string>();
    const std::string name = ref.substr(ref.find_last_of('/') + 1);
    if (schema_root.contains("$defs") && schema_root["$defs"].contains(name)) {
      return schema_root["$defs"][name];
    }
  }
  return frag;
}

// Does `value` validate against schema fragment `frag` (resolving $ref against
// schema_root)? Recurses through unions, object properties, and array items.
bool schema_validate(const json& schema_root, const json& frag, const json& value) {
  if (!frag.is_object()) {
    return true;  // `true`-schema / no constraint
  }
  if (frag.contains("$ref")) {
    return schema_validate(schema_root, deref(schema_root, frag), value);
  }

  for (const char* union_key : {"anyOf", "oneOf"}) {
    auto it = frag.find(union_key);
    if (it != frag.end() && it->is_array()) {
      for (const auto& alt : *it) {
        if (schema_validate(schema_root, alt, value)) {
          return true;  // matches a branch (oneOf treated as anyOf — for
                        // strict-render "matches at least one" suffices)
        }
      }
      return false;
    }
  }

  // type keyword
  auto tit = frag.find("type");
  if (tit != frag.end()) {
    bool type_ok = false;
    if (tit->is_string()) {
      type_ok = type_keyword_accepts(tit->get<std::string>(), value);
    } else if (tit->is_array()) {
      for (const auto& t : *tit) {
        if (t.is_string() && type_keyword_accepts(t.get<std::string>(), value)) {
          type_ok = true;
          break;
        }
      }
    } else {
      type_ok = true;
    }
    if (!type_ok) {
      return false;
    }
  }

  // string pattern (e.g. SWMLVar's ${..}/%{..} constraint)
  if (value.is_string()) {
    auto pit = frag.find("pattern");
    if (pit != frag.end() && pit->is_string()) {
      try {
        std::regex re(pit->get<std::string>());
        if (!std::regex_search(value.get<std::string>(), re)) {
          return false;
        }
      } catch (const std::regex_error& e) {
        // Unsupported pattern syntax in std::regex — treat as "no pattern
        // constraint we can enforce" rather than rejecting a possibly-valid
        // value on our own regex-engine limitation.
        static_cast<void>(e);
      }
    }
  }

  // object: known-key closure + property schemas + required
  if (value.is_object()) {
    const bool closes =
        frag.value("additionalProperties", json()) == json(false) ||
        frag.value("unevaluatedProperties", json()) == json(false) ||
        frag.value("unevaluatedProperties", json()) == json{{"not", json::object()}};
    const json props = frag.contains("properties") && frag["properties"].is_object()
                           ? frag["properties"]
                           : json::object();
    for (auto field = value.begin(); field != value.end(); ++field) {
      if (props.contains(field.key())) {
        if (!schema_validate(schema_root, props[field.key()], field.value())) {
          return false;
        }
      } else if (closes) {
        return false;  // unknown/misspelled key on a closed object
      }
    }
    auto rit = frag.find("required");
    if (rit != frag.end() && rit->is_array()) {
      for (const auto& r : *rit) {
        if (r.is_string() && !value.contains(r.get<std::string>())) {
          return false;
        }
      }
    }
  }

  // array: validate each item against `items`
  if (value.is_array()) {
    auto iit = frag.find("items");
    if (iit != frag.end() && iit->is_object()) {
      for (const auto& elem : value) {
        if (!schema_validate(schema_root, *iit, elem)) {
          return false;
        }
      }
    }
  }

  return true;
}

// The verb config object schema ("answer" -> the {type:object,properties:...}
// under Answer.properties.answer), following a single $ref (AI -> AIObject).
// Returns an empty object when it can't be resolved.
json resolve_verb_body(const json& schema, const json& verb_definition,
                       const std::string& verb_name) {
  if (!verb_definition.is_object() || !verb_definition.contains("properties")) {
    return json::object();
  }
  const auto& props = verb_definition["properties"];
  if (!props.is_object() || !props.contains(verb_name)) {
    return json::object();
  }
  json body = props[verb_name];
  if (body.is_object() && body.contains("$ref") && body["$ref"].is_string()) {
    const std::string ref = body["$ref"].get<std::string>();
    const std::string name = ref.substr(ref.find_last_of('/') + 1);
    if (schema.contains("$defs") && schema["$defs"].contains(name)) {
      body = schema["$defs"][name];
    }
  }
  return body.is_object() ? body : json::object();
}

bool body_closes(const json& body) {
  if (body.value("additionalProperties", json()) == json(false)) {
    return true;
  }
  auto it = body.find("unevaluatedProperties");
  if (it != body.end()) {
    if (*it == json(false)) {
      return true;
    }
    if (*it == json{{"not", json::object()}}) {
      return true;
    }
  }
  return false;
}

}  // namespace

std::optional<std::set<std::string>> SchemaUtils::verb_top_level_property_names(
    const std::string& verb_name) const {
  auto it = verbs_.find(verb_name);
  if (it == verbs_.end()) {
    return std::nullopt;
  }
  json body = resolve_verb_body(schema_, it->second.definition, verb_name);
  if (!body.is_object() || body.value("type", "") != "object") {
    return std::nullopt;
  }
  auto pit = body.find("properties");
  if (pit == body.end() || !pit->is_object()) {
    return std::nullopt;
  }
  // Only meaningful as a closed-key check when the schema itself closes the
  // object (additionalProperties:false or unevaluatedProperties disallowed).
  if (!body_closes(body)) {
    return std::nullopt;
  }
  std::set<std::string> keys;
  for (auto p = pit->begin(); p != pit->end(); ++p) {
    keys.insert(p.key());
  }
  return keys;
}

std::pair<bool, std::vector<std::string>> SchemaUtils::validate_verb_top_level_keys(
    const std::string& verb_name, const json& verb_config) const {
  if (!validation_enabled_) {
    return {true, {}};
  }
  if (verbs_.find(verb_name) == verbs_.end()) {
    return {false, {std::string("Unknown verb: ") + verb_name}};
  }
  auto known = verb_top_level_property_names(verb_name);
  if (!known.has_value()) {
    // No enumerable closed key-set — nothing shallow to enforce.
    return {true, {}};
  }
  if (!verb_config.is_object()) {
    return {true, {}};
  }
  std::vector<std::string> unknown;
  for (auto it = verb_config.begin(); it != verb_config.end(); ++it) {
    if (known->find(it.key()) == known->end()) {
      unknown.push_back(it.key());
    }
  }
  if (!unknown.empty()) {
    std::sort(unknown.begin(), unknown.end());
    std::string msg = "Unknown/misspelled key(s) [";
    for (size_t i = 0; i < unknown.size(); ++i) {
      if (i != 0) {
        msg += ", ";
      }
      msg += "'";
      msg += unknown[i];
      msg += "'";
    }
    msg += "] for verb '";
    msg += verb_name;
    msg += "'";
    return {false, {msg}};
  }
  return {true, {}};
}

std::pair<bool, std::vector<std::string>> SchemaUtils::validate_verb_full(
    const std::string& verb_name, const json& verb_config) const {
  // Focused strict-render validation for standard (non-handler) verbs: validate
  // the verb config against its schema body (resolving $ref, handling
  // anyOf/oneOf branches, object closure + property types + required, string
  // patterns, array items) via the recursive schema_validate below. This is the
  // C++ analog of the reference's jsonschema-rs pass for the surface the
  // strict-render contract covers (verb existence, unknown/misspelled keys,
  // wrong-typed values, missing required). A verb whose body cannot be resolved
  // to an actual schema falls back to the lightweight required-props check so a
  // partial/mocked schema is never a false reject.
  auto it = verbs_.find(verb_name);
  if (it == verbs_.end()) {
    return {false, {std::string("Unknown verb: ") + verb_name}};
  }
  json body = resolve_verb_body(schema_, it->second.definition, verb_name);
  if (!body.is_object() ||
      (!body.contains("properties") && !body.contains("oneOf") && !body.contains("anyOf"))) {
    // Not a resolvable object/union schema — fall back to lightweight.
    return validate_verb_lightweight(verb_name, verb_config);
  }
  if (schema_validate(schema_, body, verb_config)) {
    return {true, {}};
  }
  std::string msg = "Schema validation error for '";
  msg += verb_name;
  msg += "': config does not match schema";
  return {false, {msg}};
}

std::pair<bool, std::vector<std::string>> SchemaUtils::validate_verb_lightweight(
    const std::string& verb_name, const json& verb_config) const {
  std::vector<std::string> errors;
  for (const auto& prop : get_verb_required_properties(verb_name)) {
    bool present = verb_config.is_object() && verb_config.contains(prop);
    if (!present) {
      std::string msg = "Missing required property '";
      msg += prop;
      msg += "' for verb '";
      msg += verb_name;
      msg += "'";
      errors.push_back(msg);
    }
  }
  return {errors.empty(), errors};
}

std::pair<bool, std::vector<std::string>> SchemaUtils::validate_document(
    const json& /*document*/) const {
  if (!full_validator_) {
    return {false, {"Schema validator not initialized"}};
  }
  // Reserved for full-validator wiring.
  return {true, {}};
}

std::string SchemaUtils::generate_method_signature(const std::string& verb_name) const {
  json params = get_verb_parameters(verb_name);
  std::set<std::string> required;
  for (auto& r : get_verb_required_properties(verb_name)) {
    required.insert(r);
  }

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
      std::string part = name;
      part += ": ";
      part += t;
      parts.push_back(part);
    } else {
      std::string part = name;
      part += ": Optional[";
      part += t;
      part += "] = None";
      parts.push_back(part);
    }
  }
  parts.push_back("**kwargs");

  std::ostringstream os;
  os << "def " << verb_name << "(";
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i != 0) {
      os << ", ";
    }
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
      while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.front()))) {
        desc.erase(desc.begin());
      }
      while (!desc.empty() && std::isspace(static_cast<unsigned char>(desc.back()))) {
        desc.pop_back();
      }
    }
    os << "        Args:\n            " << name << ": " << desc << "\n";
  }
  os << "        \n        Returns:\n            True if the verb was added successfully, False "
        "otherwise\n        \"\"\"\n";
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
