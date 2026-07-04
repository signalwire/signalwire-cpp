// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT

#include "signalwire/core/pom_builder.hpp"

namespace signalwire {
namespace core {

namespace {

// Resolve a TOP-LEVEL section by title (parity with Python's ``_sections``
// dict, which is keyed only by top-level section titles). Deliberately does
// NOT recurse into subsections — unlike ``PromptObjectModel::find_section`` —
// so ``has_section``/``get_section``/auto-vivification only ever see the
// builder's own top-level sections.
pom::Section* find_top_level(pom::PromptObjectModel& model, const std::string& title) {
  for (auto& s : model.sections) {
    if (s.title.has_value() && *s.title == title) {
      return &s;
    }
  }
  return nullptr;
}

const pom::Section* find_top_level(const pom::PromptObjectModel& model, const std::string& title) {
  for (const auto& s : model.sections) {
    if (s.title.has_value() && *s.title == title) {
      return &s;
    }
  }
  return nullptr;
}

}  // namespace

PomBuilder::PomBuilder() = default;

PomBuilder& PomBuilder::add_section(const std::string& title, const std::string& body,
                                    const std::optional<std::vector<std::string>>& bullets,
                                    bool numbered, bool numbered_bullets,
                                    const std::optional<std::vector<json>>& subsections) {
  pom::Section& section =
      pom_.add_section(title, body, bullets.has_value() ? *bullets : std::vector<std::string>{},
                       std::optional<bool>(numbered), numbered_bullets);

  if (subsections.has_value()) {
    for (const auto& sub : *subsections) {
      if (!sub.is_object() || !sub.contains("title")) {
        continue;
      }
      std::string sub_title = sub["title"].get<std::string>();
      std::string sub_body =
          sub.contains("body") && sub["body"].is_string() ? sub["body"].get<std::string>() : "";
      std::vector<std::string> sub_bullets;
      if (sub.contains("bullets") && sub["bullets"].is_array()) {
        for (const auto& b : sub["bullets"]) {
          sub_bullets.push_back(b.get<std::string>());
        }
      }
      section.add_subsection(sub_title, sub_body, sub_bullets);
    }
  }
  return *this;
}

PomBuilder& PomBuilder::add_to_section(const std::string& title,
                                       const std::optional<std::string>& body,
                                       const std::optional<std::string>& bullet,
                                       const std::optional<std::vector<std::string>>& bullets) {
  if (!has_section(title)) {
    add_section(title);
  }
  pom::Section* section = find_top_level(pom_, title);
  if (section == nullptr) {
    return *this;
  }

  if (body.has_value() && !body->empty()) {
    if (!section->body.empty()) {
      section->body = section->body + "\n\n" + *body;
    } else {
      section->body = *body;
    }
  }

  if (bullet.has_value()) {
    section->bullets.push_back(*bullet);
  }

  if (bullets.has_value()) {
    for (const auto& b : *bullets) {
      section->bullets.push_back(b);
    }
  }
  return *this;
}

PomBuilder& PomBuilder::add_subsection(const std::string& parent_title, const std::string& title,
                                       const std::string& body,
                                       const std::optional<std::vector<std::string>>& bullets) {
  if (!has_section(parent_title)) {
    add_section(parent_title);
  }
  pom::Section* parent = find_top_level(pom_, parent_title);
  if (parent == nullptr) {
    return *this;
  }
  parent->add_subsection(title, body, bullets.has_value() ? *bullets : std::vector<std::string>{});
  return *this;
}

bool PomBuilder::has_section(const std::string& title) const {
  return find_top_level(pom_, title) != nullptr;
}

pom::Section* PomBuilder::get_section(const std::string& title) {
  return find_top_level(pom_, title);
}

std::string PomBuilder::render_markdown() const { return pom_.render_markdown(); }

std::string PomBuilder::render_xml() const { return pom_.render_xml(); }

json PomBuilder::to_dict() const { return pom_.to_dict(); }

std::string PomBuilder::to_json() const { return pom_.to_json(); }

PomBuilder PomBuilder::from_sections(const json& sections) {
  PomBuilder builder;
  builder.pom_ = pom::PromptObjectModel::from_json(sections);
  return builder;
}

}  // namespace core
}  // namespace signalwire
