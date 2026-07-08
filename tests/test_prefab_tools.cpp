// Prefab tool-method behavioural tests.
//
// Each prefab exposes reference SWAIG tool handlers / callbacks as real
// member methods. These tests exercise the methods directly (the behaviour
// that matters), and confirm the matching tools are registered.
#include "signalwire/prefabs/prefabs.hpp"

using namespace signalwire::prefabs;
using json = nlohmann::json;

namespace {
std::string resp(const signalwire::swaig::FunctionResult& r) {
    return r.to_json().value("response", "");
}
}  // namespace

// ── ConciergeAgent ─────────────────────────────────────────────────

TEST(prefab_tools_concierge_registers_tools) {
    ConciergeAgent agent;
    agent.set_amenities({
        json{{"name", "Pool"}, {"location", "Floor 2"}, {"description", "Rooftop pool"}}
    });
    ASSERT_TRUE(agent.has_tool("check_availability"));
    ASSERT_TRUE(agent.has_tool("get_directions"));
    return true;
}

TEST(prefab_tools_concierge_check_availability_found) {
    ConciergeAgent agent;
    agent.set_venue_name("Grand");
    agent.set_amenities({json{{"name", "Spa"}, {"description", "Relaxing spa"}}});
    auto r = agent.check_availability(json{{"amenity", "spa"}, {"date", "2026-07-05"}}, json::object());
    ASSERT_TRUE(resp(r).find("available on 2026-07-05") != std::string::npos);
    return true;
}

TEST(prefab_tools_concierge_check_availability_not_found) {
    ConciergeAgent agent;
    agent.set_venue_name("Grand");
    agent.set_amenities({json{{"name", "Spa"}}});
    auto r = agent.check_availability(json{{"amenity", "Golf"}}, json::object());
    ASSERT_TRUE(resp(r).find("don't offer Golf") != std::string::npos);
    ASSERT_TRUE(resp(r).find("Spa") != std::string::npos);
    return true;
}

TEST(prefab_tools_concierge_get_directions) {
    ConciergeAgent agent;
    agent.set_amenities({json{{"name", "Pool"}, {"location", "Floor 2"}}});
    auto found = agent.get_directions(json{{"location", "pool"}}, json::object());
    ASSERT_TRUE(resp(found).find("Floor 2") != std::string::npos);

    auto missing = agent.get_directions(json{{"location", "Rooftop Bar"}}, json::object());
    ASSERT_TRUE(resp(missing).find("front desk") != std::string::npos);
    return true;
}

TEST(prefab_tools_concierge_on_summary) {
    ConciergeAgent agent;
    bool called = false;
    agent.on_summary([&](const json&, const json&) { called = true; });
    // Chaining returns *this; nothing to assert beyond registration success.
    (void)called;
    return true;
}

// ── FAQBotAgent ────────────────────────────────────────────────────

TEST(prefab_tools_faqbot_registers_search_faqs) {
    FAQBotAgent agent;
    agent.set_faqs({json{{"question", "What are your hours?"}, {"answer", "9-5"}}});
    ASSERT_TRUE(agent.has_tool("search_faqs"));
    return true;
}

TEST(prefab_tools_faqbot_search_faqs_ranks_matches) {
    FAQBotAgent agent;
    agent.set_faqs({
        json{{"question", "What are your hours?"}, {"answer", "9-5"},
             {"categories", json::array({"general"})}},
        json{{"question", "How do I reset my password?"}, {"answer", "Click reset"}},
        json{{"question", "Where are you located?"}, {"answer", "NYC"}}
    });
    auto r = agent.search_faqs(json{{"query", "hours"}}, json::object());
    ASSERT_TRUE(resp(r).find("most relevant FAQs") != std::string::npos);
    ASSERT_TRUE(resp(r).find("What are your hours?") != std::string::npos);
    return true;
}

TEST(prefab_tools_faqbot_search_faqs_no_match) {
    FAQBotAgent agent;
    agent.set_faqs({json{{"question", "What are your hours?"}, {"answer", "9-5"}}});
    auto r = agent.search_faqs(json{{"query", "zzzznomatch"}}, json::object());
    ASSERT_EQ(resp(r), "No matching FAQs found.");
    return true;
}

TEST(prefab_tools_faqbot_search_faqs_category_boost) {
    FAQBotAgent agent;
    agent.set_faqs({
        json{{"question", "Billing question"}, {"answer", "..."},
             {"categories", json::array({"billing"})}}
    });
    auto r = agent.search_faqs(json{{"query", "billing"}, {"category", "billing"}}, json::object());
    ASSERT_TRUE(resp(r).find("Billing question") != std::string::npos);
    return true;
}

// ── InfoGathererAgent ──────────────────────────────────────────────

TEST(prefab_tools_info_gatherer_registers_tools) {
    InfoGathererAgent agent;
    ASSERT_TRUE(agent.has_tool("start_questions"));
    ASSERT_TRUE(agent.has_tool("submit_answer"));
    return true;
}

TEST(prefab_tools_info_gatherer_start_questions) {
    InfoGathererAgent agent;
    json raw{{"global_data",
              {{"questions", json::array({json{{"key_name", "name"},
                                               {"question_text", "What is your name?"}}})},
               {"question_index", 0},
               {"answers", json::array()}}}};
    auto r = agent.start_questions(json::object(), raw);
    ASSERT_TRUE(resp(r).find("What is your name?") != std::string::npos);
    return true;
}

TEST(prefab_tools_info_gatherer_start_questions_empty) {
    InfoGathererAgent agent;
    auto r = agent.start_questions(json::object(), json{{"global_data", json::object()}});
    ASSERT_TRUE(resp(r).find("don't have any questions") != std::string::npos);
    return true;
}

TEST(prefab_tools_info_gatherer_submit_answer_advances) {
    InfoGathererAgent agent;
    json raw{{"global_data",
              {{"questions", json::array({
                    json{{"key_name", "name"}, {"question_text", "Name?"}},
                    json{{"key_name", "email"}, {"question_text", "Email?"}}})},
               {"question_index", 0},
               {"answers", json::array()}}}};
    auto r = agent.submit_answer(json{{"answer", "Alice"}}, raw);
    // Advances to the next question.
    ASSERT_TRUE(resp(r).find("Email?") != std::string::npos);
    // The action stashes the recorded answer + new index in global_data.
    json out = r.to_json();
    bool has_update = false;
    if (out.contains("action")) {
        for (const auto& a : out["action"]) {
            if (a.contains("set_global_data")) has_update = true;
        }
    }
    ASSERT_TRUE(has_update);
    return true;
}

TEST(prefab_tools_info_gatherer_submit_answer_completes) {
    InfoGathererAgent agent;
    json raw{{"global_data",
              {{"questions", json::array({json{{"key_name", "name"},
                                               {"question_text", "Name?"}}})},
               {"question_index", 0},
               {"answers", json::array()}}}};
    auto r = agent.submit_answer(json{{"answer", "Bob"}}, raw);
    ASSERT_TRUE(resp(r).find("All questions have been answered") != std::string::npos);
    return true;
}

TEST(prefab_tools_info_gatherer_on_swml_request_static_no_override) {
    InfoGathererAgent agent;
    agent.set_questions({json{{"key_name", "k"}, {"question_text", "q?"}}});
    json override = agent.on_swml_request(json::object(), json::object(), json::object());
    // Static mode -> null (no override).
    ASSERT_TRUE(override.is_null());
    return true;
}

TEST(prefab_tools_info_gatherer_on_swml_request_dynamic_callback) {
    InfoGathererAgent agent;
    agent.set_question_callback([](const json&, const json&, const json&) {
        return std::vector<json>{json{{"key_name", "color"},
                                      {"question_text", "Favourite colour?"}}};
    });
    json override = agent.on_swml_request(json::object(), json::object(), json::object());
    ASSERT_TRUE(override.contains("global_data"));
    auto& gd = override["global_data"];
    ASSERT_EQ(gd["questions"].size(), 1u);
    ASSERT_EQ(gd["questions"][0]["key_name"], "color");
    ASSERT_EQ(gd["question_index"], 0);
    return true;
}

TEST(prefab_tools_info_gatherer_on_swml_request_dynamic_fallback) {
    InfoGathererAgent agent;
    // No static questions, no callback -> fallback questions.
    json override = agent.on_swml_request(json::object(), json::object(), json::object());
    ASSERT_TRUE(override.contains("global_data"));
    ASSERT_EQ(override["global_data"]["questions"].size(), 2u);
    return true;
}

// ── ReceptionistAgent ──────────────────────────────────────────────

TEST(prefab_tools_receptionist_on_summary) {
    ReceptionistAgent agent;
    agent.on_summary([](const json&, const json&) {});
    return true;
}

// ── SurveyAgent ────────────────────────────────────────────────────

TEST(prefab_tools_survey_registers_tools) {
    SurveyAgent agent;
    agent.set_questions({json{{"id", "q1"}, {"text", "Rate us"}, {"type", "rating"}, {"scale", 5}}});
    ASSERT_TRUE(agent.has_tool("validate_response"));
    ASSERT_TRUE(agent.has_tool("log_response"));
    return true;
}

TEST(prefab_tools_survey_validate_rating) {
    SurveyAgent agent;
    agent.set_questions({json{{"id", "q1"}, {"text", "Rate us"}, {"type", "rating"}, {"scale", 5}}});
    auto ok = agent.validate_response(json{{"question_id", "q1"}, {"response", "3"}}, json::object());
    ASSERT_TRUE(resp(ok).find("is valid") != std::string::npos);
    auto bad = agent.validate_response(json{{"question_id", "q1"}, {"response", "9"}}, json::object());
    ASSERT_TRUE(resp(bad).find("between 1 and 5") != std::string::npos);
    auto nan = agent.validate_response(json{{"question_id", "q1"}, {"response", "abc"}}, json::object());
    ASSERT_TRUE(resp(nan).find("between 1 and 5") != std::string::npos);
    return true;
}

TEST(prefab_tools_survey_validate_multiple_choice) {
    SurveyAgent agent;
    agent.set_questions({json{{"id", "q2"}, {"text", "Pick"}, {"type", "multiple_choice"},
                              {"options", json::array({"Red", "Blue"})}}});
    auto ok = agent.validate_response(json{{"question_id", "q2"}, {"response", "blue"}}, json::object());
    ASSERT_TRUE(resp(ok).find("is valid") != std::string::npos);
    auto bad = agent.validate_response(json{{"question_id", "q2"}, {"response", "green"}}, json::object());
    ASSERT_TRUE(resp(bad).find("Invalid choice") != std::string::npos);
    return true;
}

TEST(prefab_tools_survey_validate_yes_no) {
    SurveyAgent agent;
    agent.set_questions({json{{"id", "q3"}, {"text", "OK?"}, {"type", "yes_no"}}});
    auto ok = agent.validate_response(json{{"question_id", "q3"}, {"response", "Yes"}}, json::object());
    ASSERT_TRUE(resp(ok).find("is valid") != std::string::npos);
    auto bad = agent.validate_response(json{{"question_id", "q3"}, {"response", "maybe"}}, json::object());
    ASSERT_TRUE(resp(bad).find("'yes' or 'no'") != std::string::npos);
    return true;
}

TEST(prefab_tools_survey_validate_unknown_question) {
    SurveyAgent agent;
    agent.set_questions({json{{"id", "q1"}, {"text", "T"}, {"type", "yes_no"}}});
    auto r = agent.validate_response(json{{"question_id", "nope"}, {"response", "x"}}, json::object());
    ASSERT_TRUE(resp(r).find("not found") != std::string::npos);
    return true;
}

TEST(prefab_tools_survey_log_response) {
    SurveyAgent agent;
    agent.set_questions({json{{"id", "q1"}, {"text", "How satisfied?"}, {"type", "rating"}}});
    auto r = agent.log_response(json{{"question_id", "q1"}, {"response", "5"}}, json::object());
    ASSERT_TRUE(resp(r).find("How satisfied?") != std::string::npos);
    ASSERT_TRUE(resp(r).find("recorded") != std::string::npos);
    return true;
}

TEST(prefab_tools_survey_on_summary) {
    SurveyAgent agent;
    agent.on_summary([](const json&, const json&) {});
    return true;
}
