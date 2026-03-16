#include "signalwire/swml/schema.hpp"
#include "signalwire/logging.hpp"
#include <fstream>
#include <sstream>

namespace signalwire {
namespace swml {

// Hardcoded known verbs (schema name -> actual verb name).
// Used as a fallback when the schema file is not available.
static const std::vector<std::pair<std::string, std::string>> KNOWN_VERBS = {
    {"Answer",          "answer"},
    {"AI",              "ai"},
    {"AmazonBedrock",   "amazon_bedrock"},
    {"Cond",            "cond"},
    {"Connect",         "connect"},
    {"Denoise",         "denoise"},
    {"DetectMachine",   "detect_machine"},
    {"EnterQueue",      "enter_queue"},
    {"Execute",         "execute"},
    {"Goto",            "goto"},
    {"Hangup",          "hangup"},
    {"JoinConference",  "join_conference"},
    {"JoinRoom",        "join_room"},
    {"Label",           "label"},
    {"LiveTranscribe",  "live_transcribe"},
    {"LiveTranslate",   "live_translate"},
    {"Pay",             "pay"},
    {"Play",            "play"},
    {"Prompt",          "prompt"},
    {"ReceiveFax",      "receive_fax"},
    {"Record",          "record"},
    {"RecordCall",      "record_call"},
    {"Request",         "request"},
    {"Return",          "return"},
    {"SendDigits",      "send_digits"},
    {"SendFax",         "send_fax"},
    {"SendSMS",         "send_sms"},
    {"Set",             "set"},
    {"Sleep",           "sleep"},
    {"SIPRefer",        "sip_refer"},
    {"StopDenoise",     "stop_denoise"},
    {"StopRecordCall",  "stop_record_call"},
    {"StopTap",         "stop_tap"},
    {"Switch",          "switch"},
    {"Tap",             "tap"},
    {"Transfer",        "transfer"},
    {"Unset",           "unset"},
    {"UserEvent",       "user_event"},
};

const std::string& get_embedded_schema() {
    static std::string empty;
    return empty;
}

bool Schema::load_from_string(const std::string& schema_json) {
    try {
        schema_ = json::parse(schema_json);
        extract_verbs();
        return true;
    } catch (const json::parse_error& e) {
        get_logger().error(std::string("Failed to parse schema JSON: ") + e.what());
        return false;
    }
}

bool Schema::load_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        get_logger().warn("Could not open schema file: " + path);
        return false;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    return load_from_string(buffer.str());
}

bool Schema::load_embedded() {
    // Use the known verbs table so verb methods work without the full schema.
    verbs_.clear();
    verb_index_.clear();

    for (const auto& [schema_name, verb_name] : KNOWN_VERBS) {
        VerbDefinition vd;
        vd.schema_name = schema_name;
        vd.verb_name = verb_name;
        vd.properties = json::object();
        vd.description = verb_name + " verb";

        verb_index_[verb_name] = verbs_.size();
        verbs_.push_back(std::move(vd));
    }

    return true;
}

void Schema::extract_verbs() {
    verbs_.clear();
    verb_index_.clear();

    if (!schema_.contains("$defs")) {
        get_logger().warn("Schema missing $defs, using known verbs");
        load_embedded();
        return;
    }

    auto& defs = schema_["$defs"];

    if (!defs.contains("SWMLMethod") || !defs["SWMLMethod"].contains("anyOf")) {
        get_logger().warn("SWMLMethod missing anyOf, using known verbs");
        load_embedded();
        return;
    }

    for (const auto& entry : defs["SWMLMethod"]["anyOf"]) {
        if (!entry.contains("$ref")) continue;

        std::string ref = entry["$ref"].get<std::string>();
        auto slash = ref.rfind('/');
        if (slash == std::string::npos) continue;

        std::string schema_name = ref.substr(slash + 1);
        if (!defs.contains(schema_name)) continue;

        auto& def = defs[schema_name];
        if (!def.contains("properties")) continue;

        auto& props = def["properties"];
        if (props.empty()) continue;

        // The actual verb name is the first key in properties
        std::string verb_name = props.begin().key();
        json verb_props = props.begin().value();

        VerbDefinition vd;
        vd.schema_name = schema_name;
        vd.verb_name = verb_name;
        vd.properties = verb_props;

        if (verb_props.contains("description")) {
            vd.description = verb_props["description"].get<std::string>();
        } else if (def.contains("description")) {
            vd.description = def["description"].get<std::string>();
        }

        verb_index_[verb_name] = verbs_.size();
        verbs_.push_back(std::move(vd));
    }

    get_logger().debug("Extracted " + std::to_string(verbs_.size()) + " verb definitions from schema");
}

const VerbDefinition* Schema::find_verb(const std::string& verb_name) const {
    auto it = verb_index_.find(verb_name);
    if (it != verb_index_.end()) {
        return &verbs_[it->second];
    }
    return nullptr;
}

std::vector<std::string> Schema::verb_names() const {
    std::vector<std::string> names;
    names.reserve(verbs_.size());
    for (const auto& v : verbs_) {
        names.push_back(v.verb_name);
    }
    return names;
}

} // namespace swml
} // namespace signalwire
