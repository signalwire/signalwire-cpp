// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Prompt Object Model (POM) — C++ port of signalwire/pom/pom.py.
//
// A structured data format for composing, organizing, and rendering prompt
// instructions for large language models. The POM provides a tree-based
// representation of a prompt document composed of nested sections, each of
// which can include a title, body text, bullet points, and arbitrarily nested
// subsections.
//
// Two classes:
//   * ``signalwire::pom::Section``         — one section in the tree.
//   * ``signalwire::pom::PromptObjectModel`` — the top-level container.
//
// Output formats supported:
//   * JSON  via ``to_json`` / ``from_json`` (delegates to ``nlohmann::json``).
//   * YAML  via ``to_yaml`` / ``from_yaml`` (minimal in-tree YAML I/O — POM
//     content is always a list of dicts whose values are strings, bools, or
//     lists; no anchors, tags, or free-form scalars to handle).
//   * Markdown via ``render_markdown`` — exact byte-parity with Python's
//     ``Section.render_markdown`` / ``PromptObjectModel.render_markdown``.
//   * XML  via ``render_xml`` — exact byte-parity with Python's renderers.
//
// Parity contract: rendered output strings match Python verbatim (including
// trailing newlines, joiners, and section/bullet numbering rules). The C++
// tests in ``tests/test_pom.cpp`` are written from those Python outputs.
#pragma once

#include <optional>
#include <string>
#include <variant>
#include <vector>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace pom {

using json = nlohmann::json;

class PromptObjectModel;  // fwd

/// One section in the Prompt Object Model tree.
///
/// Mirrors Python's ``signalwire.pom.pom.Section``. Fields are public to
/// match the Python attribute access pattern ``section.body``,
/// ``section.bullets``, ``section.subsections``.
class Section {
public:
    /// Section title. Optional only on the very first top-level section
    /// (Python enforces "only the first section can have no title"); for
    /// subsections a title is always required.
    std::optional<std::string> title;

    /// Optional paragraph of body text.
    std::string body;

    /// Optional bullet list.
    std::vector<std::string> bullets;

    /// Nested sections (recursively the same shape).
    std::vector<Section> subsections;

    /// Whether this section participates in section numbering. Three-state:
    ///   * ``std::nullopt`` — not specified (Python ``None``); inherits.
    ///   * ``true``         — explicitly numbered.
    ///   * ``false``        — explicitly opted out of numbering.
    /// Numbering is "all-or-none per sibling group": if any sibling has
    /// ``numbered == true``, every sibling gets numbered unless it
    /// explicitly opts out with ``false``.
    std::optional<bool> numbered;

    /// When true, bullets are rendered as a numbered list (1. 2. 3.) in
    /// markdown and as ``<bullet id="1">`` in XML, instead of dash bullets.
    bool numberedBullets = false;

    Section() = default;

    /// Build a Section. ``title`` is optional; everything else has sensible
    /// defaults so empty Sections can be created and populated incrementally
    /// via ``add_body`` / ``add_bullets`` / ``add_subsection``.
    explicit Section(std::optional<std::string> t,
                     std::string b = "",
                     std::vector<std::string> bs = {},
                     std::optional<bool> num = std::nullopt,
                     bool numbered_bullets = false);

    /// Replace (NOT append) the body text. Mirrors Python's documented
    /// "Add OR REPLACE the body text" contract.
    void add_body(const std::string& b);

    /// Append bullets to the existing list.
    void add_bullets(const std::vector<std::string>& bs);

    /// Add a child subsection. Returns a reference to the newly-created
    /// subsection so callers can chain further mutations.
    /// Throws ``std::invalid_argument`` if ``title`` is empty (Python raises
    /// ``ValueError("Subsections must have a title")``).
    Section& add_subsection(const std::string& title,
                             const std::string& body = "",
                             const std::vector<std::string>& bullets = {},
                             std::optional<bool> numbered = std::nullopt,
                             bool numbered_bullets = false);

    /// Convert the section (and its subtree) to a JSON object. Matches the
    /// Python key order: title, body, bullets, subsections, numbered,
    /// numberedBullets.
    json to_json() const;

    /// Python-compatible alias for to_json — Python exposes ``to_dict``.
    /// Returns the same JSON object.
    json to_dict() const { return to_json(); }

    /// Render this section + subtree as Markdown. ``level`` is the heading
    /// level for this section (default 2 = ``## ``); ``section_number`` is
    /// the parent path that will prefix this section's title (e.g.
    /// ``{1, 2}`` -> ``"1.2. "``); empty means "no numbering".
    std::string render_markdown(int level = 2,
                                 const std::vector<int>& section_number = {}) const;

    /// Render this section + subtree as XML. ``indent`` is the number of
    /// 2-space indents to use; ``section_number`` follows the same rule as
    /// ``render_markdown``.
    std::string render_xml(int indent = 0,
                            const std::vector<int>& section_number = {}) const;
};


/// Top-level container of an ordered list of sections.
class PromptObjectModel {
public:
    std::vector<Section> sections;
    bool debug = false;

    PromptObjectModel() = default;
    explicit PromptObjectModel(bool debug_flag) : debug(debug_flag) {}

    /// Build a POM from a JSON string.
    /// Throws ``nlohmann::json::parse_error`` on malformed JSON, and
    /// ``std::invalid_argument`` on shape violations (missing required
    /// fields, wrong types, etc.).
    static PromptObjectModel from_json(const std::string& json_text);
    /// Build a POM directly from an already-parsed ``json`` value.
    static PromptObjectModel from_json(const json& data);
    /// Build a POM from a YAML string (minimal POM-shaped subset only).
    static PromptObjectModel from_yaml(const std::string& yaml_text);

    /// Append a new top-level section. ``title`` may be empty *only* for
    /// the very first section (Python enforces "Only the first section can
    /// have no title"); subsequent calls without a title throw
    /// ``std::invalid_argument``.
    Section& add_section(const std::string& title = "",
                          const std::string& body = "",
                          const std::vector<std::string>& bullets = {},
                          std::optional<bool> numbered = std::nullopt,
                          bool numbered_bullets = false);

    /// Recursively search for a section by title. Returns a pointer to the
    /// owned section so callers can mutate it; returns ``nullptr`` when
    /// nothing matches. Pointer is invalidated by any subsequent mutation
    /// of the POM that grows ``sections`` or ``subsections`` (caller's
    /// responsibility — same contract as ``std::vector::data()``).
    Section* find_section(const std::string& title);
    const Section* find_section(const std::string& title) const;

    /// Whole-tree JSON serializer. Returns a pretty-printed (indent=2)
    /// JSON array string, matching Python's ``json.dumps(..., indent=2)``.
    std::string to_json() const;

    /// Whole-tree YAML serializer. Returns a YAML document representing
    /// the JSON-equivalent list-of-dicts structure.
    std::string to_yaml() const;

    /// Whole-tree dict view (a ``json`` array). Identical content to
    /// ``to_json``, returned as a parsed ``json`` value.
    json to_dict() const;

    /// Render entire POM as Markdown.
    std::string render_markdown() const;

    /// Render entire POM as XML (with ``<?xml ... ?>`` prolog and a
    /// ``<prompt>`` root element).
    std::string render_xml() const;

    /// Add every top-level section of ``pom_to_add`` as a subsection of
    /// the section identified by ``target_title``. Throws
    /// ``std::invalid_argument`` when no matching section exists.
    void add_pom_as_subsection(const std::string& target_title,
                                const PromptObjectModel& pom_to_add);

    /// Add every top-level section of ``pom_to_add`` as a subsection of
    /// the given ``target`` Section. Caller owns ``target``.
    void add_pom_as_subsection(Section& target,
                                const PromptObjectModel& pom_to_add);

private:
    static Section build_section(const json& d, bool is_subsection);
};


// ---------------------------------------------------------------------------
// YAML support — narrow internal API. Callers should use the
// PromptObjectModel::from_yaml / to_yaml entry points; these helpers are
// exposed only for unit-testing and tooling.
// ---------------------------------------------------------------------------

/// Parse a YAML document (POM-shaped subset only) into a json value.
/// Supports list-of-dicts at top level; values may be strings, booleans,
/// or lists of either. Throws ``std::invalid_argument`` on malformed input.
json yaml_parse(const std::string& yaml_text);

/// Emit a json value as YAML. Inverse of ``yaml_parse``. Pretty output
/// matches the shape PyYAML's ``yaml.dump(..., default_flow_style=False,
/// sort_keys=False)`` produces for the POM shape.
std::string yaml_dump(const json& value);

}  // namespace pom
}  // namespace signalwire
