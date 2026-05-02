// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// PromptObjectModel + Section implementation. See pom.hpp for the contract.
// All rendering is bit-for-bit compatible with Python's signalwire/pom/pom.py
// — the unit tests in tests/test_pom.cpp are derived directly from Python's
// rendered output so any drift here is caught immediately.

#include "signalwire/pom/pom.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace signalwire {
namespace pom {

// ---------------------------------------------------------------------------
// Section
// ---------------------------------------------------------------------------

Section::Section(std::optional<std::string> t,
                 std::string b,
                 std::vector<std::string> bs,
                 std::optional<bool> num,
                 bool numbered_bullets)
    : title(std::move(t)),
      body(std::move(b)),
      bullets(std::move(bs)),
      numbered(num),
      numberedBullets(numbered_bullets) {}

void Section::add_body(const std::string& b) {
    body = b;
}

void Section::add_bullets(const std::vector<std::string>& bs) {
    bullets.insert(bullets.end(), bs.begin(), bs.end());
}

Section& Section::add_subsection(const std::string& t,
                                   const std::string& b,
                                   const std::vector<std::string>& bs,
                                   std::optional<bool> num,
                                   bool numbered_bullets) {
    if (t.empty()) {
        throw std::invalid_argument("Subsections must have a title");
    }
    subsections.emplace_back(std::optional<std::string>(t),
                              b, bs, num, numbered_bullets);
    return subsections.back();
}

namespace {

// Build the section as an ``ordered_json`` so key insertion order is
// preserved on dump. The public ``to_json()`` then converts this back to
// ``json`` for callers that only know about ``nlohmann::json``.
nlohmann::ordered_json section_to_ordered(const Section& sec) {
    nlohmann::ordered_json data = nlohmann::ordered_json::object();
    if (sec.title.has_value()) {
        data["title"] = *sec.title;
    }
    if (!sec.body.empty()) {
        data["body"] = sec.body;
    }
    if (!sec.bullets.empty()) {
        data["bullets"] = sec.bullets;
    }
    if (!sec.subsections.empty()) {
        nlohmann::ordered_json arr = nlohmann::ordered_json::array();
        for (const auto& s : sec.subsections) {
            arr.push_back(section_to_ordered(s));
        }
        data["subsections"] = arr;
    }
    if (sec.numbered.has_value() && *sec.numbered) {
        data["numbered"] = true;
    }
    if (sec.numberedBullets) {
        data["numberedBullets"] = true;
    }
    return data;
}

}  // anonymous namespace

json Section::to_json() const {
    // Build with ordered_json (insertion-order preserving) so the JSON
    // dump path matches Python's key order (title, body, bullets,
    // subsections, numbered, numberedBullets). Convert back to plain
    // ``json`` for the public return type — callers serialise via the
    // ``PromptObjectModel::to_json`` string entry point which uses the
    // ordered builder directly.
    return json::parse(section_to_ordered(*this).dump());
}

namespace {

std::string join_section_number(const std::vector<int>& nums) {
    std::string s;
    for (size_t i = 0; i < nums.size(); ++i) {
        if (i > 0) s += ".";
        s += std::to_string(nums[i]);
    }
    return s;
}

}  // namespace

std::string Section::render_markdown(int level,
                                       const std::vector<int>& section_number) const {
    // Direct port of Section.render_markdown in pom.py. Each "appended line"
    // becomes one element in ``parts`` and we join with "\n" at the end —
    // same contract Python uses (``"\n".join(md)``).
    std::vector<std::string> parts;

    // Title with optional numbering prefix
    if (title.has_value()) {
        std::string prefix;
        if (!section_number.empty()) {
            prefix = join_section_number(section_number) + ". ";
        }
        std::string heading(level, '#');
        parts.push_back(heading + " " + prefix + *title + "\n");
    }

    // Body
    if (!body.empty()) {
        parts.push_back(body + "\n");
    }

    // Bullets
    for (size_t i = 0; i < bullets.size(); ++i) {
        if (numberedBullets) {
            parts.push_back(std::to_string(i + 1) + ". " + bullets[i]);
        } else {
            parts.push_back("- " + bullets[i]);
        }
    }
    if (!bullets.empty()) {
        parts.push_back("");
    }

    // Are any subsections numbered? Used to decide whether the whole
    // sibling group inherits numbering.
    bool any_subsection_numbered = false;
    for (const auto& sub : subsections) {
        if (sub.numbered.has_value() && *sub.numbered) {
            any_subsection_numbered = true;
            break;
        }
    }

    for (size_t i = 0; i < subsections.size(); ++i) {
        const auto& sub = subsections[i];
        std::vector<int> new_section_number;
        int next_level;
        if (title.has_value() || !section_number.empty()) {
            // Only number siblings when this section has a title (or we
            // already inherit a number from above). Match the Python rule:
            // ``if any_subsection_numbered and subsection.numbered is not False``
            bool sub_explicitly_unnumbered =
                sub.numbered.has_value() && !*sub.numbered;
            if (any_subsection_numbered && !sub_explicitly_unnumbered) {
                new_section_number = section_number;
                new_section_number.push_back(static_cast<int>(i + 1));
            } else {
                new_section_number = section_number;
            }
            next_level = level + 1;
        } else {
            // Root section with no title — don't increment anything.
            new_section_number = section_number;
            next_level = level;
        }
        parts.push_back(sub.render_markdown(next_level, new_section_number));
    }

    // Join with "\n" — same as Python.
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "\n";
        out += parts[i];
    }
    return out;
}

std::string Section::render_xml(int indent,
                                  const std::vector<int>& section_number) const {
    std::string indent_str(static_cast<size_t>(indent) * 2, ' ');
    std::vector<std::string> parts;

    parts.push_back(indent_str + "<section>");

    if (title.has_value()) {
        std::string prefix;
        if (!section_number.empty()) {
            prefix = join_section_number(section_number) + ". ";
        }
        parts.push_back(indent_str + "  <title>" + prefix + *title + "</title>");
    }

    if (!body.empty()) {
        parts.push_back(indent_str + "  <body>" + body + "</body>");
    }

    if (!bullets.empty()) {
        parts.push_back(indent_str + "  <bullets>");
        for (size_t i = 0; i < bullets.size(); ++i) {
            if (numberedBullets) {
                parts.push_back(indent_str + "    <bullet id=\"" +
                                std::to_string(i + 1) + "\">" + bullets[i] +
                                "</bullet>");
            } else {
                parts.push_back(indent_str + "    <bullet>" + bullets[i] +
                                "</bullet>");
            }
        }
        parts.push_back(indent_str + "  </bullets>");
    }

    if (!subsections.empty()) {
        parts.push_back(indent_str + "  <subsections>");
        bool any_subsection_numbered = false;
        for (const auto& sub : subsections) {
            if (sub.numbered.has_value() && *sub.numbered) {
                any_subsection_numbered = true;
                break;
            }
        }
        for (size_t i = 0; i < subsections.size(); ++i) {
            const auto& sub = subsections[i];
            std::vector<int> new_section_number;
            if (title.has_value() || !section_number.empty()) {
                bool sub_explicitly_unnumbered =
                    sub.numbered.has_value() && !*sub.numbered;
                if (any_subsection_numbered && !sub_explicitly_unnumbered) {
                    new_section_number = section_number;
                    new_section_number.push_back(static_cast<int>(i + 1));
                } else {
                    new_section_number = section_number;
                }
            } else {
                new_section_number = section_number;
            }
            parts.push_back(sub.render_xml(indent + 2, new_section_number));
        }
        parts.push_back(indent_str + "  </subsections>");
    }

    parts.push_back(indent_str + "</section>");

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "\n";
        out += parts[i];
    }
    return out;
}

// ---------------------------------------------------------------------------
// PromptObjectModel
// ---------------------------------------------------------------------------

Section PromptObjectModel::build_section(const json& d, bool is_subsection) {
    if (!d.is_object()) {
        throw std::invalid_argument("Each section must be a dictionary.");
    }
    if (d.contains("title") && !d["title"].is_string()) {
        throw std::invalid_argument("'title' must be a string if present.");
    }
    if (d.contains("subsections") && !d["subsections"].is_array()) {
        throw std::invalid_argument("'subsections' must be a list if provided.");
    }
    if (d.contains("bullets") && !d["bullets"].is_array()) {
        throw std::invalid_argument("'bullets' must be a list if provided.");
    }
    if (d.contains("numbered") && !d["numbered"].is_boolean()) {
        throw std::invalid_argument("'numbered' must be a boolean if provided.");
    }
    if (d.contains("numberedBullets") && !d["numberedBullets"].is_boolean()) {
        throw std::invalid_argument(
            "'numberedBullets' must be a boolean if provided.");
    }

    bool has_body = d.contains("body") && d["body"].is_string() &&
                    !d["body"].get<std::string>().empty();
    bool has_bullets = d.contains("bullets") && !d["bullets"].empty();
    bool has_subsections =
        d.contains("subsections") && !d["subsections"].empty();
    if (!has_body && !has_bullets && !has_subsections) {
        throw std::invalid_argument(
            "All sections must have either a non-empty body, "
            "non-empty bullets, or subsections");
    }
    if (is_subsection && !d.contains("title")) {
        throw std::invalid_argument("All subsections must have a title");
    }

    std::optional<std::string> t;
    if (d.contains("title")) t = d["title"].get<std::string>();

    std::string body;
    if (d.contains("body")) body = d["body"].get<std::string>();

    std::vector<std::string> bullets;
    if (d.contains("bullets")) {
        for (const auto& b : d["bullets"]) {
            bullets.push_back(b.get<std::string>());
        }
    }

    std::optional<bool> numbered;
    if (d.contains("numbered")) numbered = d["numbered"].get<bool>();

    bool numbered_bullets = false;
    if (d.contains("numberedBullets")) numbered_bullets = d["numberedBullets"].get<bool>();

    Section section(t, body, bullets, numbered, numbered_bullets);

    if (d.contains("subsections")) {
        for (const auto& sub_json : d["subsections"]) {
            section.subsections.push_back(build_section(sub_json, true));
        }
    }

    return section;
}

PromptObjectModel PromptObjectModel::from_json(const std::string& json_text) {
    json data = json::parse(json_text);
    return from_json(data);
}

PromptObjectModel PromptObjectModel::from_json(const json& data) {
    if (!data.is_array()) {
        throw std::invalid_argument(
            "Top-level POM must be a list of section dictionaries.");
    }
    PromptObjectModel pom;
    for (size_t i = 0; i < data.size(); ++i) {
        json sec = data[i];
        // Mirror Python: only the first section is allowed to have no title.
        // For subsequent sections without a title, Python silently inserts
        // "Untitled Section". Replicate that.
        if (i > 0 && sec.is_object() && !sec.contains("title")) {
            sec["title"] = "Untitled Section";
        }
        pom.sections.push_back(build_section(sec, false));
    }
    return pom;
}

PromptObjectModel PromptObjectModel::from_yaml(const std::string& yaml_text) {
    json parsed = yaml_parse(yaml_text);
    return from_json(parsed);
}

Section& PromptObjectModel::add_section(const std::string& title,
                                         const std::string& body,
                                         const std::vector<std::string>& bullets,
                                         std::optional<bool> numbered,
                                         bool numbered_bullets) {
    if (title.empty() && !sections.empty()) {
        throw std::invalid_argument(
            "Only the first section can have no title");
    }
    std::optional<std::string> t;
    if (!title.empty()) t = title;
    sections.emplace_back(t, body, bullets, numbered, numbered_bullets);
    return sections.back();
}

Section* PromptObjectModel::find_section(const std::string& title) {
    // Recursive helper that returns a pointer into the owned tree.
    std::function<Section*(std::vector<Section>&)> recurse =
        [&](std::vector<Section>& secs) -> Section* {
        for (auto& s : secs) {
            if (s.title.has_value() && *s.title == title) {
                return &s;
            }
            if (auto* hit = recurse(s.subsections)) {
                return hit;
            }
        }
        return nullptr;
    };
    return recurse(sections);
}

const Section* PromptObjectModel::find_section(const std::string& title) const {
    std::function<const Section*(const std::vector<Section>&)> recurse =
        [&](const std::vector<Section>& secs) -> const Section* {
        for (const auto& s : secs) {
            if (s.title.has_value() && *s.title == title) {
                return &s;
            }
            if (auto* hit = recurse(s.subsections)) {
                return hit;
            }
        }
        return nullptr;
    };
    return recurse(sections);
}

json PromptObjectModel::to_dict() const {
    json arr = json::array();
    for (const auto& s : sections) {
        arr.push_back(s.to_json());
    }
    return arr;
}

std::string PromptObjectModel::to_json() const {
    // Build an ``ordered_json`` array directly so the dump output
    // preserves insertion order (matching Python's ``json.dumps`` of an
    // ordered ``dict``).
    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
    for (const auto& s : sections) {
        arr.push_back(section_to_ordered(s));
    }
    return arr.dump(2);
}

// Forward decl: defined inside the anon namespace far below.
std::string yaml_dump_ordered(const nlohmann::ordered_json& value);

std::string PromptObjectModel::to_yaml() const {
    // Build with ordered_json so the dump iterates keys in insertion
    // order (matching PyYAML's preservation of dict order).
    nlohmann::ordered_json arr = nlohmann::ordered_json::array();
    for (const auto& s : sections) {
        arr.push_back(section_to_ordered(s));
    }
    return yaml_dump_ordered(arr);
}

std::string PromptObjectModel::render_markdown() const {
    bool any_section_numbered = false;
    for (const auto& s : sections) {
        if (s.numbered.has_value() && *s.numbered) {
            any_section_numbered = true;
            break;
        }
    }

    std::vector<std::string> parts;
    int section_counter = 0;
    for (const auto& s : sections) {
        std::vector<int> section_number;
        if (s.title.has_value()) {
            section_counter += 1;
            // Python: ``if any_section_numbered and section.numbered != False``
            bool explicitly_unnumbered = s.numbered.has_value() && !*s.numbered;
            if (any_section_numbered && !explicitly_unnumbered) {
                section_number.push_back(section_counter);
            }
        }
        parts.push_back(s.render_markdown(2, section_number));
    }

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "\n";
        out += parts[i];
    }
    return out;
}

std::string PromptObjectModel::render_xml() const {
    std::vector<std::string> parts;
    parts.push_back("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    parts.push_back("<prompt>");

    bool any_section_numbered = false;
    for (const auto& s : sections) {
        if (s.numbered.has_value() && *s.numbered) {
            any_section_numbered = true;
            break;
        }
    }

    int section_counter = 0;
    for (const auto& s : sections) {
        std::vector<int> section_number;
        if (s.title.has_value()) {
            section_counter += 1;
            bool explicitly_unnumbered = s.numbered.has_value() && !*s.numbered;
            if (any_section_numbered && !explicitly_unnumbered) {
                section_number.push_back(section_counter);
            }
        }
        parts.push_back(s.render_xml(1, section_number));
    }

    parts.push_back("</prompt>");

    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) out += "\n";
        out += parts[i];
    }
    return out;
}

void PromptObjectModel::add_pom_as_subsection(
    const std::string& target_title, const PromptObjectModel& pom_to_add) {
    Section* target = find_section(target_title);
    if (!target) {
        throw std::invalid_argument("No section with title '" + target_title +
                                     "' found.");
    }
    add_pom_as_subsection(*target, pom_to_add);
}

void PromptObjectModel::add_pom_as_subsection(
    Section& target, const PromptObjectModel& pom_to_add) {
    for (const auto& s : pom_to_add.sections) {
        target.subsections.push_back(s);
    }
}

// ---------------------------------------------------------------------------
// YAML reader / writer (POM-shaped subset)
// ---------------------------------------------------------------------------
//
// The POM YAML shape is narrow:
//
//   - Document top-level: a sequence of mappings.
//   - Mapping value types: strings, booleans, sequences-of-strings,
//     sequences-of-mappings (for ``subsections``).
//
// We do not need to support: anchors, tags, multi-doc, flow style, complex
// keys, multi-line block scalars beyond ``|`` literal blocks (which the POM
// data shape avoids by design — body is single-line in tests).
//
// Implementation: single-pass indentation tracker. Each line is either:
//   (a) "- " block-list start, optionally followed by "key: value" inline.
//   (b) "key: value" mapping entry.
//   (c) "key:" (empty) followed by a block-list / block-mapping below.
//   (d) blank line — ignored.
//
// Indentation determines container nesting.
//
// Quoting: we honour double-quoted strings (with simple escape sequences)
// and single-quoted strings (no escapes per YAML 1.2 spec). Unquoted
// scalars are interpreted as bool (``true`` / ``false``) when the literal
// matches, otherwise as plain strings.

namespace {

// --------- YAML lexer helpers ---------------------------------------------

struct Line {
    int indent;          // count of leading spaces (tabs not allowed here)
    std::string text;    // content with leading spaces stripped, trailing ws kept
    int line_no;
};

std::vector<Line> split_lines(const std::string& src) {
    std::vector<Line> out;
    int line_no = 0;
    size_t i = 0;
    while (i < src.size()) {
        line_no += 1;
        size_t start = i;
        while (i < src.size() && src[i] != '\n') ++i;
        std::string raw = src.substr(start, i - start);
        if (i < src.size()) ++i;  // consume newline

        // strip CR
        if (!raw.empty() && raw.back() == '\r') raw.pop_back();

        // skip pure-whitespace / comment-only lines (but record indent of
        // non-blank lines only)
        std::string content_test = raw;
        size_t first_nonspace = 0;
        while (first_nonspace < content_test.size() &&
               content_test[first_nonspace] == ' ') {
            ++first_nonspace;
        }
        if (first_nonspace == content_test.size()) {
            continue;  // blank
        }
        if (content_test[first_nonspace] == '#') {
            continue;  // comment
        }
        // strip inline comment (heuristic: " #" outside quotes)
        bool in_dq = false, in_sq = false;
        size_t cut = std::string::npos;
        for (size_t k = first_nonspace; k < content_test.size(); ++k) {
            char c = content_test[k];
            if (c == '"' && !in_sq) in_dq = !in_dq;
            else if (c == '\'' && !in_dq) in_sq = !in_sq;
            else if (c == '#' && !in_dq && !in_sq && k > 0 &&
                     content_test[k - 1] == ' ') {
                cut = k;
                break;
            }
        }
        if (cut != std::string::npos) {
            content_test = content_test.substr(0, cut);
            // trim trailing spaces
            while (!content_test.empty() && content_test.back() == ' ')
                content_test.pop_back();
        }

        Line L;
        L.indent = static_cast<int>(first_nonspace);
        L.text = content_test.substr(first_nonspace);
        L.line_no = line_no;
        out.push_back(std::move(L));
    }
    return out;
}

// Trim leading/trailing whitespace
std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

// Parse a YAML scalar value (RHS of "key: VALUE", or list item value).
// Handles double-quoted, single-quoted, true/false, plain string.
json parse_scalar(const std::string& raw) {
    std::string s = trim(raw);
    if (s.empty()) return json(nullptr);

    // Double-quoted
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        std::string body = s.substr(1, s.size() - 2);
        std::string out;
        for (size_t i = 0; i < body.size(); ++i) {
            char c = body[i];
            if (c == '\\' && i + 1 < body.size()) {
                char n = body[++i];
                switch (n) {
                    case 'n': out += '\n'; break;
                    case 't': out += '\t'; break;
                    case 'r': out += '\r'; break;
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    default: out += n; break;
                }
            } else {
                out += c;
            }
        }
        return out;
    }

    // Single-quoted (no escapes per YAML; '' is a literal ')
    if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
        std::string body = s.substr(1, s.size() - 2);
        std::string out;
        for (size_t i = 0; i < body.size(); ++i) {
            if (body[i] == '\'' && i + 1 < body.size() && body[i + 1] == '\'') {
                out += '\'';
                ++i;
            } else {
                out += body[i];
            }
        }
        return out;
    }

    // Booleans
    if (s == "true" || s == "True" || s == "TRUE") return true;
    if (s == "false" || s == "False" || s == "FALSE") return false;
    if (s == "null" || s == "Null" || s == "NULL" || s == "~") return nullptr;

    // Integers (POM doesn't use them directly, but be permissive)
    bool is_int = !s.empty();
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') start = 1;
    if (start == s.size()) is_int = false;
    for (size_t i = start; i < s.size() && is_int; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) is_int = false;
    }
    if (is_int) {
        try {
            return std::stoll(s);
        } catch (...) {
            // fall through to plain string
        }
    }

    // Plain unquoted string
    return s;
}

// Forward decls
json parse_block(const std::vector<Line>& lines, size_t& idx, int parent_indent);

// Parse a block-sequence (each line starts with "- ") at the given indent.
// Each item may inline a "key: value" pair, in which case the same indent
// (or deeper) continuation lines belong to that mapping.
json parse_sequence(const std::vector<Line>& lines, size_t& idx, int seq_indent) {
    json arr = json::array();
    while (idx < lines.size()) {
        const Line& L = lines[idx];
        if (L.indent < seq_indent) break;
        if (L.indent > seq_indent) {
            throw std::invalid_argument(
                "YAML parse error: unexpected indent at line " +
                std::to_string(L.line_no));
        }
        if (L.text.size() < 2 || L.text[0] != '-' ||
            (L.text.size() > 1 && L.text[1] != ' ' && L.text[1] != '\0')) {
            // Not a sequence item at this level — break.
            break;
        }

        // Strip "- " prefix
        std::string item_first = L.text.substr(L.text.size() >= 2 ? 2 : 1);
        // The "- " consumes 2 columns; subsequent inline content's effective
        // indent is seq_indent + 2.
        int item_inline_indent = seq_indent + 2;

        // If item_first looks like "key: value" (or "key:"), treat it as a
        // mapping with one inline key, plus possible subsequent continuation
        // lines at deeper indent.
        idx += 1;

        std::string trimmed_first = item_first;
        // strip trailing spaces
        while (!trimmed_first.empty() &&
               (trimmed_first.back() == ' ' || trimmed_first.back() == '\t')) {
            trimmed_first.pop_back();
        }

        bool is_mapping_inline = false;
        // Detect "key:" or "key: value" without colon-in-quoted-string corner case
        // (POM doesn't put colons inside titles).
        size_t colon_pos = std::string::npos;
        bool in_dq = false, in_sq = false;
        for (size_t k = 0; k < trimmed_first.size(); ++k) {
            char c = trimmed_first[k];
            if (c == '"' && !in_sq) in_dq = !in_dq;
            else if (c == '\'' && !in_dq) in_sq = !in_sq;
            else if (c == ':' && !in_dq && !in_sq) {
                if (k + 1 == trimmed_first.size() ||
                    trimmed_first[k + 1] == ' ') {
                    colon_pos = k;
                    is_mapping_inline = true;
                    break;
                }
            }
        }

        if (is_mapping_inline) {
            json m = json::object();
            std::string key = trim(trimmed_first.substr(0, colon_pos));
            std::string val_part = (colon_pos + 1 < trimmed_first.size())
                                       ? trimmed_first.substr(colon_pos + 1)
                                       : "";
            val_part = trim(val_part);
            if (val_part.empty()) {
                // Empty value — three valid follow-ons:
                //   * Block-mapping at deeper indent (> item_inline_indent)
                //   * Block-sequence at SAME indent (PyYAML compact style)
                //   * Nothing — null value.
                if (idx < lines.size()) {
                    const Line& Next = lines[idx];
                    if (Next.indent > item_inline_indent) {
                        m[key] = parse_block(lines, idx, item_inline_indent);
                    } else if (Next.indent == item_inline_indent &&
                               !Next.text.empty() && Next.text[0] == '-') {
                        m[key] = parse_sequence(lines, idx, item_inline_indent);
                    } else {
                        m[key] = nullptr;
                    }
                } else {
                    m[key] = nullptr;
                }
            } else {
                m[key] = parse_scalar(val_part);
            }

            // Continue reading more "key: value" lines at item_inline_indent
            while (idx < lines.size() && lines[idx].indent == item_inline_indent) {
                const Line& L2 = lines[idx];
                if (L2.text.empty() || L2.text[0] == '-') break;
                size_t cp2 = std::string::npos;
                bool dq2 = false, sq2 = false;
                for (size_t k = 0; k < L2.text.size(); ++k) {
                    char c = L2.text[k];
                    if (c == '"' && !sq2) dq2 = !dq2;
                    else if (c == '\'' && !dq2) sq2 = !sq2;
                    else if (c == ':' && !dq2 && !sq2) {
                        if (k + 1 == L2.text.size() || L2.text[k + 1] == ' ') {
                            cp2 = k;
                            break;
                        }
                    }
                }
                if (cp2 == std::string::npos) break;
                std::string k2 = trim(L2.text.substr(0, cp2));
                std::string v2 = (cp2 + 1 < L2.text.size())
                                     ? trim(L2.text.substr(cp2 + 1))
                                     : "";
                idx += 1;
                if (v2.empty()) {
                    if (idx < lines.size()) {
                        const Line& Next = lines[idx];
                        if (Next.indent > item_inline_indent) {
                            m[k2] = parse_block(lines, idx, item_inline_indent);
                        } else if (Next.indent == item_inline_indent &&
                                   !Next.text.empty() && Next.text[0] == '-') {
                            m[k2] = parse_sequence(lines, idx, item_inline_indent);
                        } else {
                            m[k2] = nullptr;
                        }
                    } else {
                        m[k2] = nullptr;
                    }
                } else {
                    m[k2] = parse_scalar(v2);
                }
            }

            arr.push_back(m);
        } else {
            // Plain scalar item ("- value")
            arr.push_back(parse_scalar(item_first));
        }
    }
    return arr;
}

// Parse a block-mapping at the given indent (caller has already sliced past
// any preceding "key:" parent). Stops when the next line drops below
// ``map_indent`` or starts a new sequence at the SAME indent (PyYAML
// "compact" block-sequence style — see parse_block_after_key for the
// special-case lookahead used when a key has an empty value).
json parse_mapping(const std::vector<Line>& lines, size_t& idx, int map_indent) {
    json obj = json::object();
    while (idx < lines.size()) {
        const Line& L = lines[idx];
        if (L.indent < map_indent) break;
        if (L.indent > map_indent) {
            throw std::invalid_argument(
                "YAML parse error: unexpected over-indent at line " +
                std::to_string(L.line_no));
        }
        if (!L.text.empty() && L.text[0] == '-') break;

        size_t colon_pos = std::string::npos;
        bool dq = false, sq = false;
        for (size_t k = 0; k < L.text.size(); ++k) {
            char c = L.text[k];
            if (c == '"' && !sq) dq = !dq;
            else if (c == '\'' && !dq) sq = !sq;
            else if (c == ':' && !dq && !sq) {
                if (k + 1 == L.text.size() || L.text[k + 1] == ' ') {
                    colon_pos = k;
                    break;
                }
            }
        }
        if (colon_pos == std::string::npos) {
            throw std::invalid_argument(
                "YAML parse error: expected mapping key at line " +
                std::to_string(L.line_no));
        }
        std::string key = trim(L.text.substr(0, colon_pos));
        std::string val_part = (colon_pos + 1 < L.text.size())
                                   ? L.text.substr(colon_pos + 1)
                                   : "";
        val_part = trim(val_part);
        idx += 1;
        if (val_part.empty()) {
            // Two valid follow-ons after "key:" with empty value:
            //   1. Block-mapping at deeper indent (> map_indent).
            //   2. Block-sequence at SAME indent (PyYAML default — see
            //      ``yaml.dump(..., indent=2, default_flow_style=False)``
            //      output for sequences-of-strings inside a mapping).
            if (idx < lines.size()) {
                const Line& Next = lines[idx];
                if (Next.indent > map_indent) {
                    obj[key] = parse_block(lines, idx, map_indent);
                } else if (Next.indent == map_indent && !Next.text.empty() &&
                           Next.text[0] == '-') {
                    obj[key] = parse_sequence(lines, idx, map_indent);
                } else {
                    obj[key] = nullptr;
                }
            } else {
                obj[key] = nullptr;
            }
        } else {
            obj[key] = parse_scalar(val_part);
        }
    }
    return obj;
}

json parse_block(const std::vector<Line>& lines, size_t& idx, int parent_indent) {
    if (idx >= lines.size()) return nullptr;
    const Line& L = lines[idx];
    int my_indent = L.indent;
    if (my_indent <= parent_indent) {
        // Nothing inside this block.
        return nullptr;
    }
    if (!L.text.empty() && L.text[0] == '-') {
        return parse_sequence(lines, idx, my_indent);
    }
    return parse_mapping(lines, idx, my_indent);
}

// --------- YAML emitter helpers --------------------------------------------

bool needs_quoting(const std::string& s) {
    if (s.empty()) return true;
    // Any of these YAML special leading chars / reserved values force quoting.
    static const std::string special = "[]{},*&!|>'\"%@`#";
    if (special.find(s.front()) != std::string::npos) return true;
    if (s.front() == ' ' || s.back() == ' ') return true;
    if (s.find(": ") != std::string::npos) return true;
    if (s.back() == ':') return true;
    if (s.find('\n') != std::string::npos) return true;
    // Reserved "boolean-shaped" values
    if (s == "true" || s == "True" || s == "TRUE" ||
        s == "false" || s == "False" || s == "FALSE" ||
        s == "null" || s == "Null" || s == "NULL" ||
        s == "yes" || s == "no" || s == "Yes" || s == "No" ||
        s == "~") {
        return true;
    }
    // Pure-numeric strings
    bool numeric = true;
    size_t start = 0;
    if (s[0] == '-' || s[0] == '+') start = 1;
    if (start == s.size()) numeric = false;
    for (size_t i = start; i < s.size() && numeric; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) numeric = false;
    }
    return numeric;
}

std::string quote_string(const std::string& s) {
    // PyYAML's default_flow_style=False prefers single quotes when the string
    // contains no special escapes. We use single quotes to match.
    std::string out = "'";
    for (char c : s) {
        if (c == '\'') out += "''";
        else out += c;
    }
    out += "'";
    return out;
}

// Templated emit so we can dump either ``nlohmann::json`` (std::map-backed
// — used for input from the public yaml_dump entry point) or
// ``nlohmann::ordered_json`` (insertion-order — used internally by
// ``PromptObjectModel::to_yaml``). Same structural code applies; only the
// underlying iteration order differs.

template <typename J>
void emit_value(std::string& out, const J& v, int indent_level);

template <typename J>
void emit_mapping(std::string& out, const J& obj, int indent_level,
                  bool inline_first) {
    // PyYAML default emits each key on its own line. When ``inline_first`` is
    // true, the first key is emitted inline (after a "- " sequence marker)
    // without any leading indent — mirroring PyYAML's "compact" style.
    bool first = true;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (first && inline_first) {
            // Already at column right after "- "
        } else {
            out += std::string(static_cast<size_t>(indent_level) * 2, ' ');
        }
        out += it.key();
        out += ":";
        const auto& v = it.value();
        if (v.is_array() || v.is_object()) {
            // PyYAML default_flow_style=False emits sequences-of-strings
            // at the SAME indent as the parent key, not deeper. For
            // sequences-of-mappings the same rule applies. Mappings
            // however go to indent+1.
            if (v.is_array() && v.empty()) {
                out += " []\n";
            } else if (v.is_object() && v.empty()) {
                out += " {}\n";
            } else {
                out += "\n";
                int next_indent = v.is_array() ? indent_level : (indent_level + 1);
                if (v.is_array()) {
                    emit_value(out, v, next_indent);
                } else {
                    emit_value(out, v, next_indent);
                }
            }
        } else {
            out += " ";
            emit_value(out, v, indent_level + 1);
        }
        first = false;
    }
}

template <typename J>
void emit_sequence(std::string& out, const J& arr, int indent_level) {
    // PyYAML default_flow_style=False emits each item on its own line:
    //   - item
    // For sequences-of-mappings, the dash is at the parent indent and the
    // first key sits on the same line.
    for (const auto& item : arr) {
        out += std::string(static_cast<size_t>(indent_level) * 2, ' ');
        out += "-";
        if (item.is_object() && !item.empty()) {
            out += " ";
            emit_mapping(out, item, indent_level + 1, true);
        } else if (item.is_array() && !item.empty()) {
            out += "\n";
            emit_sequence(out, item, indent_level + 1);
        } else {
            out += " ";
            emit_value(out, item, indent_level + 1);
        }
    }
}

template <typename J>
void emit_value(std::string& out, const J& v, int indent_level) {
    if (v.is_null()) {
        out += "null\n";
        return;
    }
    if (v.is_boolean()) {
        out += v.template get<bool>() ? "true\n" : "false\n";
        return;
    }
    if (v.is_number_integer()) {
        out += std::to_string(v.template get<long long>());
        out += "\n";
        return;
    }
    if (v.is_number()) {
        std::ostringstream oss;
        oss << v.template get<double>();
        out += oss.str();
        out += "\n";
        return;
    }
    if (v.is_string()) {
        const std::string& s = v.template get_ref<const std::string&>();
        if (needs_quoting(s)) {
            out += quote_string(s);
        } else {
            out += s;
        }
        out += "\n";
        return;
    }
    if (v.is_array()) {
        if (v.empty()) {
            out += "[]\n";
            return;
        }
        emit_sequence(out, v, indent_level);
        return;
    }
    if (v.is_object()) {
        if (v.empty()) {
            out += "{}\n";
            return;
        }
        emit_mapping(out, v, indent_level, false);
        return;
    }
}

template <typename J>
std::string yaml_dump_impl(const J& value) {
    std::string out;
    if (value.is_array()) {
        if (value.empty()) {
            return "[]\n";
        }
        emit_sequence(out, value, 0);
        return out;
    }
    if (value.is_object()) {
        if (value.empty()) {
            return "{}\n";
        }
        emit_mapping(out, value, 0, false);
        return out;
    }
    emit_value(out, value, 0);
    return out;
}

}  // anonymous namespace

json yaml_parse(const std::string& yaml_text) {
    auto lines = split_lines(yaml_text);
    if (lines.empty()) return json::array();
    size_t idx = 0;
    int top_indent = lines[0].indent;
    if (!lines[0].text.empty() && lines[0].text[0] == '-') {
        return parse_sequence(lines, idx, top_indent);
    }
    return parse_mapping(lines, idx, top_indent);
}

std::string yaml_dump(const json& value) {
    return yaml_dump_impl(value);
}

// Non-template dispatch entry for ordered_json — defined at file scope so
// PromptObjectModel::to_yaml can forward-declare it without dragging in
// the templated impl. ``ordered_json`` shares the same external API as
// ``json`` so the templated impl handles it transparently.
std::string yaml_dump_ordered(const nlohmann::ordered_json& value) {
    return yaml_dump_impl(value);
}

}  // namespace pom
}  // namespace signalwire
