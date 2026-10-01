// Post-prompt normalization across the voice and chat engines (signalwire-python
// tests/unit/core/test_post_prompt_normalize.py). post_prompt_data arrives three
// ways; the chat engine also echoes its summary into call_log as a bare
// assistant turn that only a byte comparison against post_prompt_data.raw
// identifies.

#include "signalwire/core/post_prompt.hpp"

namespace ppn_test {

inline const std::string& fenced() {
  static const std::string s =
      "```json\n{\"summary\": \"s\", \"already_answered\": [\"pricing\"]}\n```";
  return s;
}

inline nlohmann::json log() {
  return nlohmann::json::array({
      {{"role", "user"}, {"content", "hi"}},
      {{"role", "assistant"}, {"content", "hello"}},
      {{"role", "system"}, {"content", "the prompt"}},
      {{"role", "system-log"}, {"content", "step trace"}},
      {{"role", "tool"}, {"content", "tool output"}},
      {{"role", "assistant"}, {"content", ""}, {"tool_calls", {{{"id", 1}}}}},
      {{"role", "assistant-manual"}, {"content", "let me look that up"}},
      {{"role", "assistant"}, {"content", "   "}},
      "not even a dict",
  });
}

using Turns = std::vector<std::map<std::string, std::string>>;

}  // namespace ppn_test

namespace pp = signalwire::core::post_prompt;

TEST(post_prompt_parse_flat_keys) {
  ASSERT_EQ(pp::parse_post_prompt_data(nlohmann::json{{"summary", "s"}, {"user_goal", "g"}}),
            (nlohmann::json{{"summary", "s"}, {"user_goal", "g"}}));
  return true;
}

TEST(post_prompt_parse_fenced_raw) {
  ASSERT_EQ(pp::parse_post_prompt_data(nlohmann::json{{"raw", ppn_test::fenced()}}),
            (nlohmann::json{{"summary", "s"}, {"already_answered", {"pricing"}}}));
  return true;
}

TEST(post_prompt_parse_parsed_list_and_dict_wrappers) {
  ASSERT_EQ(
      pp::parse_post_prompt_data(nlohmann::json{{"parsed", {{{"summary", "s3"}}}}, {"raw", "..."}}),
      (nlohmann::json{{"summary", "s3"}}));
  ASSERT_FALSE(pp::parse_post_prompt_data(nlohmann::json{{"parsed", {{{"summary", "s"}}}}})
                   .contains("parsed"));
  ASSERT_EQ(pp::parse_post_prompt_data(nlohmann::json{{"parsed", {{"summary", "s"}}}}),
            (nlohmann::json{{"summary", "s"}}));
  return true;
}

TEST(post_prompt_parse_prose_and_non_object_json) {
  ASSERT_EQ(pp::parse_post_prompt_data(nlohmann::json{{"raw", "They asked about pricing."}}),
            (nlohmann::json{{"summary", "They asked about pricing."}}));
  ASSERT_EQ(pp::parse_post_prompt_data(nlohmann::json{{"raw", "\"just a string\""}}),
            (nlohmann::json{{"summary", "just a string"}}));
  return true;
}

TEST(post_prompt_parse_junk_degrades) {
  for (const auto& junk :
       {nlohmann::json(nullptr), nlohmann::json::object(), nlohmann::json("text"),
        nlohmann::json(42), nlohmann::json::array(), nlohmann::json{{"raw", ""}},
        nlohmann::json{{"raw", "   "}}, nlohmann::json{{"raw", nullptr}}}) {
    ASSERT_EQ(pp::parse_post_prompt_data(junk), nlohmann::json::object());
  }
  return true;
}

TEST(post_prompt_strip_json_fence) {
  ASSERT_EQ(pp::strip_json_fence("```json\n{\"a\":1}\n```"), std::string("{\"a\":1}"));
  ASSERT_EQ(pp::strip_json_fence("```\nplain\n```"), std::string("plain"));
  ASSERT_EQ(pp::strip_json_fence("no fence at all"), std::string("no fence at all"));
  ASSERT_EQ(pp::strip_json_fence(""), std::string(""));
  return true;
}

TEST(post_prompt_dialogue_keeps_only_real_dialogue) {
  ppn_test::Turns expected = {{{"role", "user"}, {"content", "hi"}},
                              {{"role", "assistant"}, {"content", "hello"}}};
  ASSERT_TRUE(pp::dialogue_turns(ppn_test::log()) == expected);
  return true;
}

TEST(post_prompt_dialogue_drops_echo_only_when_asked) {
  auto with_echo = ppn_test::log();
  with_echo.push_back({{"role", "assistant"}, {"content", ppn_test::fenced()}});
  auto dropped = pp::dialogue_turns(with_echo, pp::dialogue_roles(), ppn_test::fenced());
  ASSERT_EQ(dropped.size(), static_cast<size_t>(2));
  ASSERT_EQ(pp::dialogue_turns(with_echo).size(), static_cast<size_t>(3));
  return true;
}

TEST(post_prompt_dialogue_junk_logs_yield_nothing) {
  for (const auto& junk : {nlohmann::json(nullptr), nlohmann::json::array(),
                           nlohmann::json("nonsense"), nlohmann::json(42)}) {
    ASSERT_TRUE(pp::dialogue_turns(junk).empty());
  }
  return true;
}

TEST(post_prompt_normalize_voice_body) {
  auto r = pp::normalize_post_prompt(
      nlohmann::json{{"conversation_type", "voice"},
                     {"call_id", "c-1"},
                     {"post_prompt_data", {{"parsed", {{{"summary", "v"}}}}}},
                     {"raw_call_log", {{{"role", "user"}, {"content", "hi"}}}}});
  ASSERT_EQ(r.medium, std::string("voice"));
  ASSERT_FALSE(r.conversation_id.has_value());
  ASSERT_EQ(r.summary, (nlohmann::json{{"summary", "v"}}));
  ASSERT_TRUE(r.call_id.has_value() && *r.call_id == "c-1");
  ASSERT_EQ(r.dialogue.size(), static_cast<size_t>(1));
  return true;
}

TEST(post_prompt_normalize_chat_body_drops_echo) {
  auto r = pp::normalize_post_prompt(
      nlohmann::json{{"conversation_type", "chat"},
                     {"conversation_id", "conv-9"},
                     {"post_prompt_data", {{"raw", ppn_test::fenced()}}},
                     {"raw_messages",
                      {{{"role", "user"}, {"content", "hi"}},
                       {{"role", "assistant"}, {"content", ppn_test::fenced()}}}}});
  ASSERT_EQ(r.medium, std::string("chat"));
  ASSERT_TRUE(r.conversation_id.has_value() && *r.conversation_id == "conv-9");
  ASSERT_EQ(r.summary["already_answered"], nlohmann::json({"pricing"}));
  ppn_test::Turns expected = {{{"role", "user"}, {"content", "hi"}}};
  ASSERT_TRUE(r.dialogue == expected);
  return true;
}

TEST(post_prompt_normalize_call_log_key_and_junk_and_raw) {
  ASSERT_EQ(pp::normalize_post_prompt(
                nlohmann::json{{"call_log", {{{"role", "user"}, {"content", "hi"}}}}})
                .dialogue.size(),
            static_cast<size_t>(1));
  for (const auto& junk : {nlohmann::json(nullptr), nlohmann::json("text"), nlohmann::json(42),
                           nlohmann::json::array()}) {
    auto r = pp::normalize_post_prompt(junk);
    ASSERT_EQ(r.medium, std::string(""));
    ASSERT_EQ(r.summary, nlohmann::json::object());
    ASSERT_TRUE(r.dialogue.empty());
  }
  nlohmann::json body{{"conversation_type", "voice"}, {"extra", "kept"}};
  ASSERT_EQ(pp::normalize_post_prompt(body).raw, body);
  return true;
}
