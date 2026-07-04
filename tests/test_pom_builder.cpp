// PomBuilder tests (signalwire::core::PomBuilder)

#include <string>

#include "signalwire/core/pom_builder.hpp"

using signalwire::core::PomBuilder;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
  return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(pom_builder_add_section_and_render_markdown) {
  PomBuilder b;
  b.add_section("Role", "You are a helpful assistant.");
  ASSERT_TRUE(b.has_section("Role"));

  std::string md = b.render_markdown();
  // Level-2 heading + body text.
  ASSERT_TRUE(contains(md, "## Role"));
  ASSERT_TRUE(contains(md, "You are a helpful assistant."));
  return true;
}

TEST(pom_builder_add_section_with_bullets) {
  PomBuilder b;
  std::vector<std::string> bullets = {"first", "second"};
  b.add_section("Rules", "", bullets);
  std::string md = b.render_markdown();
  ASSERT_TRUE(contains(md, "## Rules"));
  ASSERT_TRUE(contains(md, "- first"));
  ASSERT_TRUE(contains(md, "- second"));
  return true;
}

TEST(pom_builder_fluent_chaining) {
  PomBuilder b;
  // Each mutator returns *this.
  b.add_section("A", "abody").add_section("B", "bbody");
  ASSERT_TRUE(b.has_section("A"));
  ASSERT_TRUE(b.has_section("B"));
  return true;
}

TEST(pom_builder_add_to_section_autovivifies) {
  PomBuilder b;
  ASSERT_FALSE(b.has_section("New"));
  b.add_to_section("New", std::string("body text"));
  ASSERT_TRUE(b.has_section("New"));
  std::string md = b.render_markdown();
  ASSERT_TRUE(contains(md, "## New"));
  ASSERT_TRUE(contains(md, "body text"));
  return true;
}

TEST(pom_builder_add_to_section_appends_body) {
  PomBuilder b;
  b.add_section("S", "one");
  b.add_to_section("S", std::string("two"));
  auto* section = b.get_section("S");
  ASSERT_NE(section, nullptr);
  // Python appends with a blank line separator.
  ASSERT_EQ(section->body, std::string("one\n\ntwo"));
  return true;
}

TEST(pom_builder_add_to_section_bullets) {
  PomBuilder b;
  b.add_section("S", "body");
  b.add_to_section("S", std::nullopt, std::string("single"));
  std::vector<std::string> more = {"x", "y"};
  b.add_to_section("S", std::nullopt, std::nullopt, more);
  auto* section = b.get_section("S");
  ASSERT_NE(section, nullptr);
  ASSERT_EQ(section->bullets.size(), 3u);
  ASSERT_EQ(section->bullets[0], std::string("single"));
  ASSERT_EQ(section->bullets[1], std::string("x"));
  ASSERT_EQ(section->bullets[2], std::string("y"));
  return true;
}

TEST(pom_builder_add_subsection_autovivifies_parent) {
  PomBuilder b;
  ASSERT_FALSE(b.has_section("Parent"));
  b.add_subsection("Parent", "Child", "child body");
  ASSERT_TRUE(b.has_section("Parent"));
  auto* parent = b.get_section("Parent");
  ASSERT_NE(parent, nullptr);
  ASSERT_EQ(parent->subsections.size(), 1u);
  ASSERT_TRUE(parent->subsections[0].title.has_value());
  ASSERT_EQ(*parent->subsections[0].title, std::string("Child"));
  // Subsection renders at a deeper heading level (### ).
  std::string md = b.render_markdown();
  ASSERT_TRUE(contains(md, "### Child"));
  ASSERT_TRUE(contains(md, "child body"));
  return true;
}

TEST(pom_builder_has_section_false_for_subsection_title) {
  PomBuilder b;
  b.add_subsection("Parent", "Child", "body");
  // Child is a subsection, not a top-level section -> has_section is false
  // (parity with Python's _sections dict, keyed by top-level title only).
  ASSERT_TRUE(b.has_section("Parent"));
  ASSERT_FALSE(b.has_section("Child"));
  ASSERT_EQ(b.get_section("Child"), nullptr);
  return true;
}

TEST(pom_builder_get_section_missing) {
  PomBuilder b;
  ASSERT_EQ(b.get_section("nope"), nullptr);
  return true;
}

TEST(pom_builder_render_xml) {
  PomBuilder b;
  b.add_section("Role", "You are helpful.");
  std::string xml = b.render_xml();
  ASSERT_TRUE(contains(xml, "<?xml"));
  ASSERT_TRUE(contains(xml, "<prompt>"));
  ASSERT_TRUE(contains(xml, "Role"));
  ASSERT_TRUE(contains(xml, "You are helpful."));
  return true;
}

TEST(pom_builder_to_dict_and_to_json) {
  PomBuilder b;
  b.add_section("Role", "body", std::vector<std::string>{"b1"});
  auto dict = b.to_dict();
  ASSERT_TRUE(dict.is_array());
  ASSERT_EQ(dict.size(), 1u);
  ASSERT_EQ(dict[0]["title"], std::string("Role"));
  ASSERT_EQ(dict[0]["body"], std::string("body"));
  ASSERT_EQ(dict[0]["bullets"][0], std::string("b1"));

  std::string js = b.to_json();
  ASSERT_TRUE(contains(js, "Role"));
  // to_json is a valid JSON array string.
  auto reparsed = nlohmann::json::parse(js);
  ASSERT_TRUE(reparsed.is_array());
  return true;
}

TEST(pom_builder_add_section_with_subsections_arg) {
  PomBuilder b;
  nlohmann::json sub = {
      {"title", "Sub"}, {"body", "subbody"}, {"bullets", nlohmann::json::array({"sb1"})}};
  b.add_section("Main", "mainbody", std::nullopt, false, false, std::vector<nlohmann::json>{sub});
  auto* main = b.get_section("Main");
  ASSERT_NE(main, nullptr);
  ASSERT_EQ(main->subsections.size(), 1u);
  ASSERT_EQ(*main->subsections[0].title, std::string("Sub"));
  ASSERT_EQ(main->subsections[0].body, std::string("subbody"));
  ASSERT_EQ(main->subsections[0].bullets[0], std::string("sb1"));
  return true;
}

TEST(pom_builder_from_sections) {
  nlohmann::json sections = nlohmann::json::array();
  sections.push_back({{"title", "One"}, {"body", "onebody"}});
  sections.push_back({{"title", "Two"}, {"body", "twobody"}});
  PomBuilder b = PomBuilder::from_sections(sections);
  ASSERT_TRUE(b.has_section("One"));
  ASSERT_TRUE(b.has_section("Two"));
  std::string md = b.render_markdown();
  ASSERT_TRUE(contains(md, "## One"));
  ASSERT_TRUE(contains(md, "onebody"));
  ASSERT_TRUE(contains(md, "## Two"));
  return true;
}

TEST(pom_builder_pom_accessor) {
  PomBuilder b;
  b.add_section("X", "xbody");
  // The underlying PromptObjectModel is accessible and consistent.
  ASSERT_EQ(b.pom().sections.size(), 1u);
  ASSERT_EQ(*b.pom().sections[0].title, std::string("X"));
  return true;
}
