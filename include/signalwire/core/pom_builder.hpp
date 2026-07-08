// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// PomBuilder — standalone builder for structured POM prompts.
//
// C++ port of the Python reference
// ``signalwire.core.pom_builder.PomBuilder`` (cross-checked against the Java
// ``com.signalwire.sdk.core.PomBuilder``). A flexible wrapper around the
// existing ``signalwire::pom::PromptObjectModel`` (see
// ``include/signalwire/pom/pom.hpp``) that allows dynamic creation of sections
// on demand, adding content to existing sections, nesting subsections, and
// rendering to Markdown or XML. There are no predefined section types. All
// mutator methods return ``*this`` for fluent chaining.
//
// Section lookup: rather than caching ``Section*`` (which vector growth would
// invalidate), sections are resolved by title through
// ``PromptObjectModel::find_section`` on each access — that recursive search
// resolves top-level sections, matching the Python ``_sections`` map for the
// operations this builder performs (all keyed by top-level section title).
#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

#include "signalwire/pom/pom.hpp"

namespace signalwire {
namespace core {

using json = nlohmann::json;

class PomBuilder {
 public:
  /// Initialize a new POM builder with an empty POM.
  PomBuilder();

  /// Add a new section to the POM.
  /// @param title            Section title.
  /// @param body             Optional body text.
  /// @param bullets          Optional list of bullet points.
  /// @param numbered         Whether to number this section (tri-state).
  /// @param numbered_bullets Whether bullets render as a numbered list.
  /// @param subsections      Optional list of subsection descriptor objects
  ///                         (keys: ``title``, ``body``, ``bullets``).
  /// @return ``*this`` for chaining.
  PomBuilder& add_section(const std::string& title, const std::string& body = "",
                          const std::optional<std::vector<std::string>>& bullets = std::nullopt,
                          bool numbered = false, bool numbered_bullets = false,
                          const std::optional<std::vector<json>>& subsections = std::nullopt);

  /// Add content to an existing section, creating it if it doesn't exist
  /// (auto-vivification).
  /// @param body    Appended to any existing body (separated by a blank line).
  /// @param bullet  A single bullet to append (optional).
  /// @param bullets A list of bullets to append (optional).
  /// @return ``*this`` for chaining.
  PomBuilder& add_to_section(const std::string& title,
                             const std::optional<std::string>& body = std::nullopt,
                             const std::optional<std::string>& bullet = std::nullopt,
                             const std::optional<std::vector<std::string>>& bullets = std::nullopt);

  /// Add a subsection to an existing section, creating the parent if needed
  /// (auto-vivification).
  /// @return ``*this`` for chaining.
  PomBuilder& add_subsection(const std::string& parent_title, const std::string& title,
                             const std::string& body = "",
                             const std::optional<std::vector<std::string>>& bullets = std::nullopt);

  /// Check if a section with the given title exists.
  [[nodiscard]] bool has_section(const std::string& title) const;

  /// Get a section by title, or ``nullptr`` if not found. The pointer is into
  /// the owned POM tree; it is invalidated by any subsequent mutation of this
  /// builder that grows the section list (same contract as
  /// ``std::vector::data()``).
  [[nodiscard]] pom::Section* get_section(const std::string& title);

  /// Render the POM as Markdown.
  [[nodiscard]] std::string render_markdown() const;

  /// Render the POM as XML.
  [[nodiscard]] std::string render_xml() const;

  /// Convert the POM to a list (JSON array) of section objects.
  [[nodiscard]] json to_dict() const;

  /// Convert the POM to a JSON string.
  [[nodiscard]] std::string to_json() const;

  /// Access the underlying PromptObjectModel.
  [[nodiscard]] pom::PromptObjectModel& pom() { return pom_; }
  [[nodiscard]] const pom::PromptObjectModel& pom() const { return pom_; }

  /// Create a PomBuilder from a list (JSON array) of section objects.
  [[nodiscard]] static PomBuilder from_sections(const json& sections);

 private:
  pom::PromptObjectModel pom_;
};

}  // namespace core
}  // namespace signalwire
