#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace swml {

using json = nlohmann::json;

/// Represents a single SWML verb instance, e.g. {"answer": {"max_duration": 3600}}
struct Verb {
    std::string name;
    json params;

    Verb() = default;
    Verb(std::string n, json p) : name(std::move(n)), params(std::move(p)) {}

    json to_json() const {
        return json::object({{name, params}});
    }
};

/// A named section containing an ordered list of verbs
struct Section {
    std::string name;
    std::vector<Verb> verbs;

    Section() = default;
    explicit Section(std::string n) : name(std::move(n)) {}

    void add_verb(const Verb& verb) {
        verbs.push_back(verb);
    }

    void add_verb(const std::string& verb_name, const json& params) {
        verbs.emplace_back(verb_name, params);
    }

    json to_json() const {
        json arr = json::array();
        for (const auto& v : verbs) {
            arr.push_back(v.to_json());
        }
        return arr;
    }
};

/// A complete SWML document with version and sections
class Document {
public:
    Document() : version_("1.0.0") {
        sections_["main"] = Section("main");
    }

    /// Set the document version
    Document& set_version(const std::string& version) {
        version_ = version;
        return *this;
    }

    /// Get or create a section by name
    Section& section(const std::string& name) {
        auto it = sections_.find(name);
        if (it == sections_.end()) {
            sections_[name] = Section(name);
            section_order_.push_back(name);
        }
        return sections_[name];
    }

    /// Get the main section
    Section& main() {
        return section("main");
    }

    /// Add a verb to the main section
    Document& add_verb(const std::string& verb_name, const json& params) {
        main().add_verb(verb_name, params);
        return *this;
    }

    /// Add a verb to a specific section
    Document& add_verb_to_section(const std::string& section_name,
                                   const std::string& verb_name,
                                   const json& params) {
        section(section_name).add_verb(verb_name, params);
        return *this;
    }

    /// Check if a section exists
    bool has_section(const std::string& name) const {
        return sections_.find(name) != sections_.end();
    }

    /// Render to JSON
    json to_json() const {
        json doc;
        doc["version"] = version_;

        json sections_json;
        // Always render "main" first
        if (auto it = sections_.find("main"); it != sections_.end()) {
            sections_json["main"] = it->second.to_json();
        }
        // Then other sections in insertion order
        for (const auto& name : section_order_) {
            if (name != "main") {
                auto it = sections_.find(name);
                if (it != sections_.end()) {
                    sections_json[name] = it->second.to_json();
                }
            }
        }
        doc["sections"] = sections_json;

        return doc;
    }

    /// Render to JSON string
    std::string to_string(int indent = -1) const {
        return to_json().dump(indent);
    }

private:
    std::string version_;
    std::map<std::string, Section> sections_;
    std::vector<std::string> section_order_;
};

} // namespace swml
} // namespace signalwire
