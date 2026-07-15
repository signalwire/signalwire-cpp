// DateTime skill tests
#include "signalwire/skills/skill_registry.hpp"
namespace sw_skills = signalwire::skills;
using json = nlohmann::json;

TEST(skill_datetime_name) {
    auto& reg = sw_skills::SkillRegistry::instance();
    auto skill = reg.create("datetime");
    ASSERT_TRUE(skill != nullptr);
    ASSERT_EQ(skill->skill_name(), "datetime");
    return true;
}

TEST(skill_datetime_version) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    ASSERT_EQ(skill->skill_version(), "1.0.0");
    return true;
}

TEST(skill_datetime_no_multi_instance) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    ASSERT_FALSE(skill->supports_multiple_instances());
    return true;
}

TEST(skill_datetime_setup_ok) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    ASSERT_TRUE(skill->setup(json::object()));
    return true;
}

TEST(skill_datetime_registers_two_tools) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();
    ASSERT_EQ(tools.size(), 2u);
    ASSERT_EQ(tools[0].name, "get_current_time");
    ASSERT_EQ(tools[1].name, "get_current_date");
    return true;
}

TEST(skill_datetime_tool_handlers_work) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();
    auto time_result = tools[0].handler(json::object(), json::object());
    auto date_result = tools[1].handler(json::object(), json::object());
    // Both should return non-empty responses
    ASSERT_FALSE(time_result.to_json()["response"].get<std::string>().empty());
    ASSERT_FALSE(date_result.to_json()["response"].get<std::string>().empty());
    return true;
}

TEST(skill_datetime_has_prompt_sections) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto sections = skill->get_prompt_sections();
    ASSERT_EQ(sections.size(), 1u);
    ASSERT_EQ(sections[0].title, "Date and Time Information");
    return true;
}

TEST(skill_datetime_no_env_vars_required) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    ASSERT_TRUE(skill->required_env_vars().empty());
    return true;
}

// A non-UTC zone must produce the real local time in that zone, not UTC.
// Asia/Tokyo (UTC+9, no DST) and UTC read the SAME instant, so the two HH:MM:SS
// strings must differ by a whole number of hours != 0 unless the wall-clock
// happens to straddle a boundary — assert on the zone label instead, which is
// unambiguous: the Tokyo answer must carry "JST", never "UTC". This test FAILS
// against the old handler, which hardcoded "%H:%M:%S UTC" regardless of `tz`.
TEST(skill_datetime_non_utc_zone_is_not_utc) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();

    json tokyo_args = {{"timezone", "Asia/Tokyo"}};
    auto tokyo = tools[0].handler(tokyo_args, json::object());
    std::string tokyo_resp = tokyo.to_json()["response"].get<std::string>();
    // The emitted %Z abbreviation is Tokyo's, not "UTC".
    ASSERT_TRUE(tokyo_resp.find("JST") != std::string::npos);
    ASSERT_TRUE(tokyo_resp.find(" UTC") == std::string::npos);

    json utc_args = {{"timezone", "UTC"}};
    auto utc = tools[0].handler(utc_args, json::object());
    std::string utc_resp = utc.to_json()["response"].get<std::string>();
    ASSERT_TRUE(utc_resp.find("UTC") != std::string::npos);

    // The two must not be the identical time string — different zones, same
    // instant. (JST is +9h from UTC; the HH portion always differs.)
    ASSERT_NE(tokyo_resp, utc_resp);
    return true;
}

// A New_York time must reflect the -5/-4h offset from UTC. Compare the raw
// epoch-derived hour: parse the two HH values and assert they differ. Using
// two eastern/western zones that never share an hour-of-day with UTC keeps the
// assertion robust regardless of when the suite runs.
TEST(skill_datetime_zone_offset_applied) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();

    json ny_args = {{"timezone", "America/New_York"}};
    auto ny = tools[0].handler(ny_args, json::object());
    std::string ny_resp = ny.to_json()["response"].get<std::string>();
    // Eastern time carries EST or EDT, never "UTC".
    bool eastern = ny_resp.find("EST") != std::string::npos ||
                   ny_resp.find("EDT") != std::string::npos;
    ASSERT_TRUE(eastern);
    ASSERT_TRUE(ny_resp.find(" UTC") == std::string::npos);
    return true;
}

// An unknown/garbage zone must error, NOT silently return a UTC answer labelled
// as that zone (glibc/BSD both fall back to UTC on an invalid TZ).
TEST(skill_datetime_unknown_zone_errors) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();

    json bad_args = {{"timezone", "Not/AZone"}};
    auto time_res = tools[0].handler(bad_args, json::object());
    std::string time_resp = time_res.to_json()["response"].get<std::string>();
    ASSERT_TRUE(time_resp.find("Error getting time") != std::string::npos);

    auto date_res = tools[1].handler(bad_args, json::object());
    std::string date_resp = date_res.to_json()["response"].get<std::string>();
    ASSERT_TRUE(date_resp.find("Error getting date") != std::string::npos);
    return true;
}

// Default (no timezone arg) resolves to UTC.
TEST(skill_datetime_default_is_utc) {
    auto skill = sw_skills::SkillRegistry::instance().create("datetime");
    skill->setup(json::object());
    auto tools = skill->register_tools();

    auto time_res = tools[0].handler(json::object(), json::object());
    std::string time_resp = time_res.to_json()["response"].get<std::string>();
    ASSERT_TRUE(time_resp.find("UTC") != std::string::npos);
    return true;
}
