// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// PromptObjectModel + Section parity tests. Each Markdown / XML / JSON /
// YAML expectation in this file was captured directly from Python's
// ``signalwire.pom.pom`` so any rendering drift between the two ports is
// caught at compile-test time.
//
// Tagged "[pom]" so callers can run ``./build/run_tests "[pom]"`` to
// exercise just this module.

#include "signalwire/pom/pom.hpp"
#include <string>

// Avoid `using namespace signalwire::pom;` here — tests are concatenated
// into a single TU via test_main.cpp, and other files (notably the SWML
// tests) use ``signalwire::swml::Section`` so a wholesale using-directive
// would create ``Section`` ambiguity. Use the namespace alias instead.
namespace pom_ns = signalwire::pom;
using sw_json = nlohmann::json;

// ===========================================================================
// Section: construction and accessors
// ===========================================================================

TEST(pom_section_default_construct_has_no_title) {
    pom_ns::Section s;
    ASSERT_FALSE(s.title.has_value());
    ASSERT_TRUE(s.body.empty());
    ASSERT_TRUE(s.bullets.empty());
    ASSERT_TRUE(s.subsections.empty());
    ASSERT_FALSE(s.numbered.has_value());
    ASSERT_FALSE(s.numberedBullets);
    return true;
}

TEST(pom_section_construct_with_title) {
    pom_ns::Section s(std::optional<std::string>("Hello"));
    ASSERT_TRUE(s.title.has_value());
    ASSERT_EQ(*s.title, std::string("Hello"));
    return true;
}

TEST(pom_section_add_body_replaces_existing) {
    // Mirrors Python's "Add OR REPLACE the body text" contract.
    pom_ns::Section s(std::optional<std::string>("X"), "initial");
    s.add_body("replacement");
    ASSERT_EQ(s.body, std::string("replacement"));
    return true;
}

TEST(pom_section_add_bullets_appends) {
    pom_ns::Section s(std::optional<std::string>("X"));
    s.add_bullets({"one", "two"});
    s.add_bullets({"three"});
    ASSERT_EQ(s.bullets.size(), 3u);
    ASSERT_EQ(s.bullets[0], std::string("one"));
    ASSERT_EQ(s.bullets[2], std::string("three"));
    return true;
}

TEST(pom_section_add_subsection_returns_reference) {
    pom_ns::Section parent(std::optional<std::string>("P"));
    pom_ns::Section& child = parent.add_subsection("C", "cb");
    child.add_body("cb-replaced");  // mutate via returned reference
    ASSERT_EQ(parent.subsections.size(), 1u);
    ASSERT_EQ(parent.subsections[0].body, std::string("cb-replaced"));
    return true;
}

TEST(pom_section_add_subsection_rejects_empty_title) {
    pom_ns::Section parent(std::optional<std::string>("P"));
    ASSERT_THROWS(parent.add_subsection(""));
    return true;
}

// ===========================================================================
// Section.to_json: key order, omitted-empty fields
// ===========================================================================

TEST(pom_section_to_json_minimal) {
    pom_ns::Section s(std::optional<std::string>("Hello"), "world");
    auto j = s.to_json();
    ASSERT_EQ(j["title"].get<std::string>(), std::string("Hello"));
    ASSERT_EQ(j["body"].get<std::string>(), std::string("world"));
    ASSERT_FALSE(j.contains("bullets"));
    ASSERT_FALSE(j.contains("subsections"));
    ASSERT_FALSE(j.contains("numbered"));
    ASSERT_FALSE(j.contains("numberedBullets"));
    return true;
}

TEST(pom_section_to_json_emits_numbered_only_when_true) {
    pom_ns::Section a(std::optional<std::string>("A"), "b");
    a.numbered = true;
    auto ja = a.to_json();
    ASSERT_TRUE(ja.contains("numbered"));
    ASSERT_TRUE(ja["numbered"].get<bool>());

    // Explicit-false numbered should NOT appear in dict (matches Python).
    pom_ns::Section b(std::optional<std::string>("B"), "y");
    b.numbered = false;
    auto jb = b.to_json();
    ASSERT_FALSE(jb.contains("numbered"));

    // Unset (nullopt) — no numbered key.
    pom_ns::Section c(std::optional<std::string>("C"), "y");
    auto jc = c.to_json();
    ASSERT_FALSE(jc.contains("numbered"));
    return true;
}

TEST(pom_section_to_json_emits_numberedBullets) {
    pom_ns::Section s(std::optional<std::string>("Rules"));
    s.bullets = {"a", "b"};
    s.numberedBullets = true;
    auto j = s.to_json();
    ASSERT_TRUE(j.contains("numberedBullets"));
    ASSERT_TRUE(j["numberedBullets"].get<bool>());
    return true;
}

TEST(pom_section_to_json_with_subsections) {
    pom_ns::Section parent(std::optional<std::string>("Parent"), "pb");
    parent.add_subsection("Child", "cb", {"cb1"});
    auto j = parent.to_json();
    ASSERT_TRUE(j.contains("subsections"));
    ASSERT_EQ(j["subsections"].size(), 1u);
    ASSERT_EQ(j["subsections"][0]["title"].get<std::string>(),
               std::string("Child"));
    ASSERT_EQ(j["subsections"][0]["body"].get<std::string>(), std::string("cb"));
    ASSERT_EQ(j["subsections"][0]["bullets"][0].get<std::string>(),
               std::string("cb1"));
    return true;
}

// ===========================================================================
// PromptObjectModel: basic flow
// ===========================================================================

TEST(pom_empty_pom_has_no_sections) {
    pom_ns::PromptObjectModel pom;
    ASSERT_TRUE(pom.sections.empty());
    return true;
}

TEST(pom_add_section_returns_reference) {
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& s = pom.add_section("Greeting", "Hi");
    ASSERT_EQ(pom.sections.size(), 1u);
    ASSERT_EQ(*s.title, std::string("Greeting"));
    return true;
}

TEST(pom_add_section_with_no_title_after_first_throws) {
    // Mirrors Python: only the FIRST top-level section may have no title.
    pom_ns::PromptObjectModel pom;
    pom.add_section("First", "x");
    ASSERT_THROWS(pom.add_section("", "y"));
    return true;
}

TEST(pom_add_section_first_with_no_title_is_ok) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("", "untitled-body");
    ASSERT_EQ(pom.sections.size(), 1u);
    ASSERT_FALSE(pom.sections[0].title.has_value());
    return true;
}

TEST(pom_find_section_recursive_match) {
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& parent = pom.add_section("Parent", "pb");
    parent.add_subsection("Deep", "db");
    pom_ns::Section* found = pom.find_section("Deep");
    ASSERT_TRUE(found != nullptr);
    ASSERT_EQ(found->body, std::string("db"));
    return true;
}

TEST(pom_find_section_returns_null_when_absent) {
    pom_ns::PromptObjectModel pom;
    ASSERT_TRUE(pom.find_section("nope") == nullptr);
    return true;
}

// ===========================================================================
// Markdown rendering (parity with Python — exact byte match)
// ===========================================================================

TEST(pom_render_markdown_simple_section_body) {
    // Python: "## Hello\n\nHello world\n"
    pom_ns::PromptObjectModel pom;
    pom.add_section("Hello", "Hello world");
    ASSERT_EQ(pom.render_markdown(),
               std::string("## Hello\n\nHello world\n"));
    return true;
}

TEST(pom_render_markdown_section_with_bullets) {
    // Python: "## Rules\n\n- x\n- y\n- z\n"
    pom_ns::PromptObjectModel pom;
    pom.add_section("Rules", "", {"x", "y", "z"});
    ASSERT_EQ(pom.render_markdown(),
               std::string("## Rules\n\n- x\n- y\n- z\n"));
    return true;
}

TEST(pom_render_markdown_numberedBullets) {
    // Python: "## Rules\n\n1. x\n2. y\n"
    pom_ns::PromptObjectModel pom;
    pom.add_section("Rules", "", {"x", "y"}, std::nullopt, /*numbered_bullets=*/true);
    ASSERT_EQ(pom.render_markdown(),
               std::string("## Rules\n\n1. x\n2. y\n"));
    return true;
}

TEST(pom_render_markdown_subsection) {
    // Python: "## Parent\n\npbody\n\n### Child\n\ncbody\n"
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& s = pom.add_section("Parent", "pbody");
    s.add_subsection("Child", "cbody");
    ASSERT_EQ(pom.render_markdown(),
               std::string("## Parent\n\npbody\n\n### Child\n\ncbody\n"));
    return true;
}

TEST(pom_render_markdown_top_level_numbered) {
    // Python: "## 1. A\n\nab\n\n## 2. B\n\nbb\n"
    pom_ns::PromptObjectModel pom;
    pom.add_section("A", "ab", {}, /*numbered=*/true);
    pom.add_section("B", "bb", {}, /*numbered=*/true);
    ASSERT_EQ(pom.render_markdown(),
               std::string("## 1. A\n\nab\n\n## 2. B\n\nbb\n"));
    return true;
}

TEST(pom_render_markdown_nested_numbered) {
    // Python: "## 1. Top\n\ntopb\n\n### 1.1. S1\n\ns1b\n\n### 1.2. S2\n\ns2b\n"
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& s = pom.add_section("Top", "topb", {}, /*numbered=*/true);
    s.add_subsection("S1", "s1b", {}, /*numbered=*/true);
    s.add_subsection("S2", "s2b", {}, /*numbered=*/true);
    ASSERT_EQ(pom.render_markdown(),
               std::string("## 1. Top\n\ntopb\n\n### 1.1. S1\n\ns1b\n\n### 1.2. S2\n\ns2b\n"));
    return true;
}

// ===========================================================================
// XML rendering
// ===========================================================================

TEST(pom_render_xml_simple_section) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("Hello", "Hello world");
    std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<prompt>\n"
        "  <section>\n"
        "    <title>Hello</title>\n"
        "    <body>Hello world</body>\n"
        "  </section>\n"
        "</prompt>";
    ASSERT_EQ(pom.render_xml(), expected);
    return true;
}

TEST(pom_render_xml_with_bullets) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("Rules", "", {"x", "y", "z"});
    std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<prompt>\n"
        "  <section>\n"
        "    <title>Rules</title>\n"
        "    <bullets>\n"
        "      <bullet>x</bullet>\n"
        "      <bullet>y</bullet>\n"
        "      <bullet>z</bullet>\n"
        "    </bullets>\n"
        "  </section>\n"
        "</prompt>";
    ASSERT_EQ(pom.render_xml(), expected);
    return true;
}

TEST(pom_render_xml_with_numberedBullets) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("Rules", "", {"x", "y"}, std::nullopt, /*numbered_bullets=*/true);
    std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<prompt>\n"
        "  <section>\n"
        "    <title>Rules</title>\n"
        "    <bullets>\n"
        "      <bullet id=\"1\">x</bullet>\n"
        "      <bullet id=\"2\">y</bullet>\n"
        "    </bullets>\n"
        "  </section>\n"
        "</prompt>";
    ASSERT_EQ(pom.render_xml(), expected);
    return true;
}

TEST(pom_render_xml_subsection) {
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& s = pom.add_section("Parent", "pb");
    s.add_subsection("Child", "cb");
    std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<prompt>\n"
        "  <section>\n"
        "    <title>Parent</title>\n"
        "    <body>pb</body>\n"
        "    <subsections>\n"
        "      <section>\n"
        "        <title>Child</title>\n"
        "        <body>cb</body>\n"
        "      </section>\n"
        "    </subsections>\n"
        "  </section>\n"
        "</prompt>";
    ASSERT_EQ(pom.render_xml(), expected);
    return true;
}

TEST(pom_render_xml_top_level_numbered) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("A", "ab", {}, /*numbered=*/true);
    pom.add_section("B", "bb", {}, /*numbered=*/true);
    std::string expected =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<prompt>\n"
        "  <section>\n"
        "    <title>1. A</title>\n"
        "    <body>ab</body>\n"
        "  </section>\n"
        "  <section>\n"
        "    <title>2. B</title>\n"
        "    <body>bb</body>\n"
        "  </section>\n"
        "</prompt>";
    ASSERT_EQ(pom.render_xml(), expected);
    return true;
}

// ===========================================================================
// JSON: to_json / from_json round-trips
// ===========================================================================

TEST(pom_to_json_returns_pretty_array) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("Hello", "Hello world");
    std::string expected =
        "[\n"
        "  {\n"
        "    \"title\": \"Hello\",\n"
        "    \"body\": \"Hello world\"\n"
        "  }\n"
        "]";
    ASSERT_EQ(pom.to_json(), expected);
    return true;
}

TEST(pom_from_json_parses_string) {
    std::string js = "[{\"title\": \"X\", \"body\": \"y\"}]";
    pom_ns::PromptObjectModel pom = pom_ns::PromptObjectModel::from_json(js);
    ASSERT_EQ(pom.sections.size(), 1u);
    ASSERT_EQ(*pom.sections[0].title, std::string("X"));
    ASSERT_EQ(pom.sections[0].body, std::string("y"));
    return true;
}

TEST(pom_from_json_round_trip_preserves_bullets_and_subsections) {
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& parent = pom.add_section("Parent", "pb", {"x"});
    parent.add_subsection("Child", "cb", {"cb1"});
    std::string serialised = pom.to_json();
    pom_ns::PromptObjectModel restored = pom_ns::PromptObjectModel::from_json(serialised);
    ASSERT_EQ(restored.sections.size(), 1u);
    ASSERT_EQ(*restored.sections[0].title, std::string("Parent"));
    ASSERT_EQ(restored.sections[0].bullets[0], std::string("x"));
    ASSERT_EQ(restored.sections[0].subsections.size(), 1u);
    ASSERT_EQ(*restored.sections[0].subsections[0].title,
               std::string("Child"));
    ASSERT_EQ(restored.sections[0].subsections[0].bullets[0],
               std::string("cb1"));
    return true;
}

TEST(pom_from_json_rejects_subsection_without_title) {
    // Subsections must have a title — Python raises ValueError; we throw
    // std::invalid_argument.
    std::string bad =
        "[{\"title\": \"P\", \"subsections\": [{\"body\": \"missing-title\"}]}]";
    ASSERT_THROWS(pom_ns::PromptObjectModel::from_json(bad));
    return true;
}

TEST(pom_from_json_rejects_section_without_content) {
    // Python rule: every section needs either body, bullets, or subsections.
    std::string bad = "[{\"title\": \"empty\"}]";
    ASSERT_THROWS(pom_ns::PromptObjectModel::from_json(bad));
    return true;
}

// ===========================================================================
// YAML: round-trip + dump shape
// ===========================================================================

TEST(pom_to_yaml_produces_pyyaml_shape) {
    // Python: '- title: Greeting\n  body: Hello\n  bullets:\n  - x\n  - y\n'
    pom_ns::PromptObjectModel pom;
    pom.add_section("Greeting", "Hello", {"x", "y"});
    std::string expected =
        "- title: Greeting\n"
        "  body: Hello\n"
        "  bullets:\n"
        "  - x\n"
        "  - y\n";
    ASSERT_EQ(pom.to_yaml(), expected);
    return true;
}

TEST(pom_from_yaml_round_trip) {
    pom_ns::PromptObjectModel pom;
    pom.add_section("Greeting", "Hello", {"x", "y"});
    std::string y = pom.to_yaml();
    pom_ns::PromptObjectModel restored = pom_ns::PromptObjectModel::from_yaml(y);
    ASSERT_EQ(restored.sections.size(), 1u);
    ASSERT_EQ(*restored.sections[0].title, std::string("Greeting"));
    ASSERT_EQ(restored.sections[0].body, std::string("Hello"));
    ASSERT_EQ(restored.sections[0].bullets.size(), 2u);
    ASSERT_EQ(restored.sections[0].bullets[0], std::string("x"));
    ASSERT_EQ(restored.sections[0].bullets[1], std::string("y"));
    return true;
}

TEST(pom_from_yaml_with_subsections_round_trip) {
    pom_ns::PromptObjectModel pom;
    pom_ns::Section& s = pom.add_section("Parent", "pb");
    s.add_subsection("Child", "cb", {"x"});
    std::string y = pom.to_yaml();
    pom_ns::PromptObjectModel restored = pom_ns::PromptObjectModel::from_yaml(y);
    ASSERT_EQ(restored.sections.size(), 1u);
    ASSERT_EQ(restored.sections[0].subsections.size(), 1u);
    ASSERT_EQ(*restored.sections[0].subsections[0].title,
               std::string("Child"));
    ASSERT_EQ(restored.sections[0].subsections[0].bullets[0],
               std::string("x"));
    return true;
}

// ===========================================================================
// add_pom_as_subsection
// ===========================================================================

TEST(pom_add_pom_as_subsection_by_title) {
    pom_ns::PromptObjectModel a;
    a.add_section("Host", "hbody");
    pom_ns::PromptObjectModel b;
    b.add_section("Guest", "gbody");
    a.add_pom_as_subsection("Host", b);
    ASSERT_EQ(a.sections[0].subsections.size(), 1u);
    ASSERT_EQ(*a.sections[0].subsections[0].title, std::string("Guest"));
    return true;
}

TEST(pom_add_pom_as_subsection_missing_target_throws) {
    pom_ns::PromptObjectModel a;
    a.add_section("Host", "x");
    pom_ns::PromptObjectModel b;
    b.add_section("Guest", "y");
    ASSERT_THROWS(a.add_pom_as_subsection("NoSuchSection", b));
    return true;
}

// ===========================================================================
// AgentBase.pom() → PromptObjectModel parity
// ===========================================================================

#include "signalwire/agent/agent_base.hpp"

TEST(pom_agent_base_pom_returns_prompt_object_model) {
    signalwire::agent::AgentBase agent;
    agent.prompt_add_section("Greeting", "Hi", {"x", "y"});
    auto maybe_pom = agent.pom();
    ASSERT_TRUE(maybe_pom.has_value());
    ASSERT_EQ(maybe_pom->sections.size(), 1u);
    ASSERT_EQ(*maybe_pom->sections[0].title, std::string("Greeting"));
    ASSERT_EQ(maybe_pom->sections[0].bullets.size(), 2u);
    // Returned model is renderable directly (parity check).
    ASSERT_EQ(maybe_pom->render_markdown(),
               std::string("## Greeting\n\nHi\n\n- x\n- y\n"));
    return true;
}

TEST(pom_agent_base_pom_nullopt_when_use_pom_false) {
    signalwire::agent::AgentBase agent;
    agent.set_use_pom(false);
    auto maybe_pom = agent.pom();
    ASSERT_FALSE(maybe_pom.has_value());
    return true;
}
