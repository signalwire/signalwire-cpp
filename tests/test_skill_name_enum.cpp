// SkillName typed-enum overloads for add_skill/remove_skill/has_skill.
//
// Mirrors the PHP proof (signalwire-php 7f305bc): the typed enum and the bare
// string load the IDENTICAL skill, and has_skill()/remove_skill() accept the
// enum too. Real behavior against a real AgentBase — no mocks. The enum adds
// call-site typo checking; the string overload keeps parity with Python's
// bare str + custom skills.

#include "signalwire/agent/agent_base.hpp"
#include "signalwire/skills/skill_name.hpp"

namespace sw_skill_name = signalwire::skills;
using SkillName = signalwire::skills::SkillName;

// The enum member maps to the canonical wire string a built-in skill reports
// from skill_name(). This is the single normalization point.
TEST(skill_name_enum_maps_to_wire_string) {
    ASSERT_EQ(sw_skill_name::skill_name_value(SkillName::Datetime), std::string("datetime"));
    ASSERT_EQ(sw_skill_name::skill_name_value(SkillName::Math), std::string("math"));
    ASSERT_EQ(sw_skill_name::skill_name_value(SkillName::WebSearch), std::string("web_search"));
    // ADL to_string() agrees with skill_name_value().
    ASSERT_EQ(to_string(SkillName::Datetime), std::string("datetime"));
    return true;
}

// add_skill(SkillName) loads the same skill as add_skill("datetime"); both the
// enum and the string find it via has_skill().
TEST(skill_name_enum_add_loads_identical_skill) {
    signalwire::agent::AgentBase agent;
    agent.add_skill(SkillName::Datetime);
    ASSERT_TRUE(agent.has_skill("datetime"));          // string lookup
    ASSERT_TRUE(agent.has_skill(SkillName::Datetime));  // enum lookup — same skill
    return true;
}

// remove_skill(SkillName) removes the skill that the string add_skill loaded —
// proving the two surfaces operate on the identical underlying name.
TEST(skill_name_enum_remove_matches_string_add) {
    signalwire::agent::AgentBase agent;
    agent.add_skill("datetime");                        // loaded by string
    ASSERT_TRUE(agent.has_skill(SkillName::Datetime));   // enum sees it
    agent.remove_skill(SkillName::Datetime);             // removed by enum
    ASSERT_FALSE(agent.has_skill("datetime"));           // string confirms gone
    return true;
}

// Parity: the bare string still works identically (Python uses str), and the
// enum overload reads back a string-loaded skill.
TEST(skill_name_string_parity) {
    signalwire::agent::AgentBase agent;
    agent.add_skill("math");
    ASSERT_TRUE(agent.has_skill(SkillName::Math));
    // A typo'd custom string is still accepted (open set) but is a different,
    // unrelated entry — it does not collide with the typed built-in.
    agent.add_skill("my_custom_skill");
    ASSERT_TRUE(agent.has_skill("my_custom_skill"));
    ASSERT_FALSE(agent.has_skill(SkillName::WebSearch));
    return true;
}
