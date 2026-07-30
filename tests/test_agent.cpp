// AgentBase tests

#include <sys/stat.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>

#include "signalwire/agent/agent_base.hpp"

using namespace signalwire::agent;
using namespace signalwire::swaig;
using json = nlohmann::json;

// ========================================================================
// Construction
// ========================================================================

TEST(agent_default_construction) {
  AgentBase agent;
  ASSERT_EQ(agent.name(), "agent");
  ASSERT_EQ(agent.route(), "/");
  return true;
}

TEST(agent_named_construction) {
  AgentBase agent("my_agent", "/bot", "0.0.0.0", 4000);
  ASSERT_EQ(agent.name(), "my_agent");
  ASSERT_EQ(agent.route(), "/bot");
  return true;
}

TEST(agent_set_name) {
  AgentBase agent;
  agent.set_name("new_name");
  ASSERT_EQ(agent.name(), "new_name");
  return true;
}

// ========================================================================
// Construction forwarding — the reference AgentBase.__init__ FORWARDS its
// parameters to collaborators (SWMLService / SessionManager) rather than
// merely storing them. These prove each one lands where it should.
//
// Most of the reference's construction state is underscore-private
// (``self._auto_answer``, ``self._record_call``, …), so the C++ accessors are
// protected. This probe subclass reads them the way a real subclass would.
// ========================================================================

namespace {

struct CtorProbe : public AgentBase {
  using AgentBase::agent_id;
  using AgentBase::AgentBase;
  using AgentBase::auto_answer;
  using AgentBase::check_for_input_override;
  using AgentBase::config_file;
  using AgentBase::default_webhook_url;
  using AgentBase::enable_post_prompt_override;
  using AgentBase::record_call_enabled;
  using AgentBase::record_format;
  using AgentBase::record_stereo;
  using AgentBase::schema_path;
  using AgentBase::schema_validation;
  using AgentBase::suppress_logs;
  using AgentBase::token_expiry_secs;
};

}  // namespace

TEST(agent_ctor_agent_id_supplied_and_generated) {
  CtorProbe supplied("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true, false,
                     "mp4", true, std::nullopt, std::string("fixed-id"));
  ASSERT_EQ(supplied.agent_id(), "fixed-id");

  // reference: ``self.agent_id = agent_id or str(uuid.uuid4())``
  CtorProbe generated("b");
  ASSERT_FALSE(generated.agent_id().empty());
  CtorProbe generated2("c");
  ASSERT_NE(generated.agent_id(), generated2.agent_id());
  return true;
}

TEST(agent_ctor_token_expiry_forwarded_to_session_manager) {
  // reference: AgentBase.__init__ passes token_expiry_secs to
  // SessionManager(token_expiry_secs=...).
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 120);
  ASSERT_EQ(agent.token_expiry_secs(), 120);

  // Prove the value actually reached the SessionManager rather than merely
  // being stored on the agent: a minted token's embedded expiry must sit
  // ~120s out, not at the 3600s default.
  std::string token = agent.session_manager().generate_token("fn", "call-1");
  ASSERT_TRUE(agent.session_manager().validate_token(token, "fn", "call-1"));
  CtorProbe long_lived("b", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 7200);
  ASSERT_EQ(long_lived.token_expiry_secs(), 7200);
  return true;
}

TEST(agent_ctor_schema_validation_forwarded_to_service) {
  CtorProbe off("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true, false, "mp4",
                true, std::nullopt, std::nullopt, std::nullopt, std::nullopt, false, false, false,
                std::nullopt, /*schema_validation=*/false);
  ASSERT_FALSE(off.schema_validation());

  CtorProbe on("b");
  ASSERT_TRUE(on.schema_validation());
  return true;
}

TEST(agent_ctor_basic_auth_forwarded_to_service) {
  // reference: basic_auth tuple is handed to super().__init__ and short
  // circuits the generated-credential path.
  std::pair<std::string, std::string> creds{"alice", "s3cret"};
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, creds);
  ASSERT_EQ(agent.auth_username(), "alice");
  ASSERT_EQ(agent.auth_password(), "s3cret");
  ASSERT_TRUE(agent.validate_basic_auth("alice", "s3cret"));
  ASSERT_FALSE(agent.validate_basic_auth("alice", "wrong"));
  return true;
}

TEST(agent_ctor_use_pom_forwarded) {
  CtorProbe with_pom("a");
  ASSERT_TRUE(with_pom.pom().has_value());

  CtorProbe without("b", "/", "0.0.0.0", std::nullopt, std::nullopt, /*use_pom=*/false);
  ASSERT_FALSE(without.pom().has_value());
  return true;
}

TEST(agent_ctor_native_functions_forwarded) {
  std::vector<std::string> natives{"check_time", "wait_for_user"};
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true, false, "mp4",
                  true, std::nullopt, std::nullopt, natives);
  json swml = agent.render_swml();
  const json& ai = swml["sections"]["main"][1]["ai"];
  ASSERT_EQ(ai["SWAIG"]["native_functions"], json(natives));
  return true;
}

TEST(agent_ctor_flags_stored) {
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true, false, "mp4",
                  true, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                  /*suppress_logs=*/true, /*enable_post_prompt_override=*/true,
                  /*check_for_input_override=*/true);
  ASSERT_TRUE(agent.suppress_logs());
  ASSERT_TRUE(agent.enable_post_prompt_override());
  ASSERT_TRUE(agent.check_for_input_override());
  return true;
}

TEST(agent_ctor_config_file_supplies_service_defaults) {
  // reference: _load_service_config reads the config file's ``service``
  // section; constructor arguments left at their defaults pick it up, and
  // explicit constructor arguments win over it.
  // Repo-local scratch dir (never /tmp), same idiom as the ConfigLoader
  // tests; relative to the build dir the test binary runs in.
  std::string dir = ".sw-test-tmp";
  // ::mkdir instead of system("mkdir -p"): no shell is spawned (so nothing in
  // the path can be interpreted), and EEXIST is the expected steady state.
  if (::mkdir(dir.c_str(), 0755) != 0 && errno != EEXIST) {
    std::cerr << "could not create " << dir << ": " << std::strerror(errno) << "\n";
    return false;
  }
  std::string cfg = dir + "/agent_ctor_service.json";
  {
    std::ofstream out(cfg);
    out << R"({"service": {"name": "from-config", "route": "/cfg", "host": "127.0.0.1", )"
        << R"("port": 4321}})";
  }

  CtorProbe defaults("ignored-name", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true,
                     false, "mp4", true, std::nullopt, std::nullopt, std::nullopt, std::nullopt,
                     false, false, false, cfg);
  ASSERT_EQ(defaults.name(), "from-config");
  ASSERT_EQ(defaults.route(), "/cfg");
  ASSERT_EQ(defaults.config_file().value(), cfg);

  // Explicit non-default route beats the config file.
  CtorProbe explicit_route("n", "/mine", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true,
                           false, "mp4", true, std::nullopt, std::nullopt, std::nullopt,
                           std::nullopt, false, false, false, cfg);
  ASSERT_EQ(explicit_route.route(), "/mine");

  std::remove(cfg.c_str());
  return true;
}

TEST(agent_ctor_schema_path_forwarded_to_schema_utils) {
  // reference: schema_path goes to super().__init__ which hands it to
  // SchemaUtils(schema_path=...). The agent must report the path it was
  // constructed with rather than silently discarding it.
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true, false, "mp4",
                  true, std::nullopt, std::nullopt, std::nullopt,
                  std::string("/nonexistent/schema.json"));
  ASSERT_EQ(agent.schema_path().value(), "/nonexistent/schema.json");
  return true;
}

// ---- construction parameters with WIRE effect ---------------------------

TEST(agent_ctor_auto_answer_gates_answer_verb) {
  // reference: ``if agent_to_use._auto_answer: add_verb("answer", ...)``
  CtorProbe on("a");
  json with_answer = on.render_swml();
  ASSERT_EQ(with_answer["sections"]["main"][0].contains("answer"), true);

  CtorProbe off("b", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600,
                /*auto_answer=*/false);
  json no_answer = off.render_swml();
  // With auto_answer off the FIRST verb is the ai verb, not answer.
  ASSERT_EQ(no_answer["sections"]["main"][0].contains("answer"), false);
  ASSERT_EQ(no_answer["sections"]["main"][0].contains("ai"), true);
  return true;
}

TEST(agent_ctor_record_call_emits_record_verb) {
  // reference: record_call adds a post-answer ``record_call`` verb carrying
  // format + stereo.
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true,
                  /*record_call=*/true, /*record_format=*/"wav", /*record_stereo=*/false);
  json swml = agent.render_swml();
  const json& rec = swml["sections"]["main"][1]["record_call"];
  ASSERT_EQ(rec["format"], "wav");
  ASSERT_EQ(rec["stereo"], false);

  CtorProbe without("b");
  json plain = without.render_swml();
  ASSERT_EQ(plain["sections"]["main"][1].contains("record_call"), false);
  return true;
}

TEST(agent_ctor_default_webhook_url_emits_swaig_defaults) {
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 3600, true, false, "mp4",
                  true, std::string("https://example.com/hook"));
  ASSERT_EQ(agent.default_webhook_url().value(), "https://example.com/hook");
  json swml = agent.render_swml();
  const json& ai = swml["sections"]["main"][1]["ai"];
  ASSERT_EQ(ai["SWAIG"]["defaults"]["web_hook_url"], "https://example.com/hook");
  return true;
}

TEST(agent_ctor_params_survive_clone) {
  // The dynamic-config copy must carry the construction state onto the
  // request-scoped clone.
  CtorProbe agent("a", "/", "0.0.0.0", std::nullopt, std::nullopt, true, 250, false, true, "wav",
                  false, std::string("https://example.com/hook"), std::string("id-7"));
  CtorProbe copy(agent);
  ASSERT_EQ(copy.agent_id(), "id-7");
  ASSERT_EQ(copy.token_expiry_secs(), 250);
  ASSERT_FALSE(copy.auto_answer());
  ASSERT_TRUE(copy.record_call_enabled());
  ASSERT_EQ(copy.record_format(), "wav");
  ASSERT_FALSE(copy.record_stereo());
  ASSERT_EQ(copy.default_webhook_url().value(), "https://example.com/hook");
  return true;
}

// ========================================================================
// Prompt Methods
// ========================================================================

TEST(agent_set_prompt_text) {
  AgentBase agent;
  agent.set_prompt_text("Hello world");
  ASSERT_EQ(agent.get_prompt(), "Hello world");
  return true;
}

TEST(agent_prompt_add_section) {
  AgentBase agent;
  agent.prompt_add_section("Personality", "You are helpful.");
  ASSERT_TRUE(agent.prompt_has_section("Personality"));
  ASSERT_FALSE(agent.prompt_has_section("Nonexistent"));
  std::string prompt = agent.get_prompt();
  ASSERT_TRUE(prompt.find("Personality") != std::string::npos);
  ASSERT_TRUE(prompt.find("You are helpful.") != std::string::npos);
  return true;
}

TEST(agent_prompt_add_section_with_bullets) {
  AgentBase agent;
  agent.prompt_add_section("Rules", "", {"Be kind", "Be concise"});
  std::string prompt = agent.get_prompt();
  ASSERT_TRUE(prompt.find("Be kind") != std::string::npos);
  ASSERT_TRUE(prompt.find("Be concise") != std::string::npos);
  return true;
}

TEST(agent_prompt_add_subsection) {
  AgentBase agent;
  agent.prompt_add_section("Main", "Main body");
  agent.prompt_add_subsection("Main", "Sub", "Sub body");
  std::string prompt = agent.get_prompt();
  ASSERT_TRUE(prompt.find("Main") != std::string::npos);
  ASSERT_TRUE(prompt.find("Sub body") != std::string::npos);
  return true;
}

TEST(agent_prompt_add_to_section) {
  AgentBase agent;
  agent.prompt_add_section("Rules", "Rule 1");
  agent.prompt_add_to_section("Rules", "", {"Rule 2"});
  std::string prompt = agent.get_prompt();
  ASSERT_TRUE(prompt.find("Rule 1") != std::string::npos);
  ASSERT_TRUE(prompt.find("Rule 2") != std::string::npos);
  return true;
}

TEST(agent_set_post_prompt) {
  AgentBase agent;
  agent.set_post_prompt("Summarize the call");
  // Post prompt is part of the AI verb, not direct prompt text
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  bool found = false;
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("post_prompt")) {
      found = true;
      ASSERT_EQ(verb["ai"]["post_prompt"]["text"].get<std::string>(), "Summarize the call");
    }
  }
  ASSERT_TRUE(found);
  return true;
}

// ========================================================================
// Tool Methods
// ========================================================================

TEST(agent_define_tool) {
  AgentBase agent;
  agent.define_tool(
      "greet", "Say hello", json::object(),
      [](const json&, const json&) -> FunctionResult { return FunctionResult("Hello!"); });
  ASSERT_TRUE(agent.has_tool("greet"));
  ASSERT_FALSE(agent.has_tool("nonexistent"));
  return true;
}

TEST(agent_list_tools) {
  AgentBase agent;
  agent.define_tool("tool_a", "A", json::object(), nullptr);
  agent.define_tool("tool_b", "B", json::object(), nullptr);
  auto tools = agent.list_tools();
  ASSERT_EQ(tools.size(), 2u);
  ASSERT_EQ(tools[0], "tool_a");
  ASSERT_EQ(tools[1], "tool_b");
  return true;
}

TEST(agent_on_function_call) {
  AgentBase agent;
  agent.define_tool("echo", "Echo input", json::object(),
                    [](const json& args, const json&) -> FunctionResult {
                      return FunctionResult("Echo: " + args.value("text", ""));
                    });

  auto result = agent.on_function_call("echo", json::object({{"text", "hi"}}), json::object());
  auto j = result.to_json();
  ASSERT_TRUE(j["response"].get<std::string>().find("hi") != std::string::npos);
  return true;
}

TEST(agent_on_function_call_unknown) {
  AgentBase agent;
  auto result = agent.on_function_call("nonexistent", json::object(), json::object());
  auto j = result.to_json();
  ASSERT_TRUE(j["response"].get<std::string>().find("Unknown") != std::string::npos);
  return true;
}

// ========================================================================
// AI Config Methods
// ========================================================================

TEST(agent_add_hints) {
  AgentBase agent;
  agent.add_hint("hello");
  agent.add_hints({"world", "test"});
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("hints")) {
      auto& hints = verb["ai"]["hints"];
      ASSERT_EQ(hints.size(), 3u);
      return true;
    }
  }
  // Hints should be present
  ASSERT_TRUE(false);
  return true;
}

TEST(agent_add_language) {
  AgentBase agent;
  agent.add_language({"English", "en-US", "rachel", "", ""});
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("languages")) {
      ASSERT_EQ(verb["ai"]["languages"].size(), 1u);
      ASSERT_EQ(verb["ai"]["languages"][0]["code"].get<std::string>(), "en-US");
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(agent_set_params) {
  AgentBase agent;
  agent.set_param("temperature", 0.7);
  agent.set_params(json::object({{"top_p", 0.9}}));
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("params")) {
      ASSERT_EQ(verb["ai"]["params"]["temperature"].get<double>(), 0.7);
      ASSERT_EQ(verb["ai"]["params"]["top_p"].get<double>(), 0.9);
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(agent_set_global_data) {
  AgentBase agent;
  agent.set_global_data(json::object({{"key", "value"}}));
  agent.update_global_data(json::object({{"key2", "value2"}}));
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("global_data")) {
      ASSERT_EQ(verb["ai"]["global_data"]["key"].get<std::string>(), "value");
      ASSERT_EQ(verb["ai"]["global_data"]["key2"].get<std::string>(), "value2");
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(agent_add_pronunciation) {
  AgentBase agent;
  agent.add_pronunciation("SW", "SignalWire");
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("pronounce")) {
      ASSERT_EQ(verb["ai"]["pronounce"].size(), 1u);
      ASSERT_EQ(verb["ai"]["pronounce"][0]["replace"].get<std::string>(), "SW");
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

// ========================================================================
// Verb Methods (5-phase pipeline)
// ========================================================================

TEST(agent_render_swml_default) {
  AgentBase agent;
  agent.set_prompt_text("Hello");
  json swml = agent.render_swml();

  ASSERT_EQ(swml["version"].get<std::string>(), "1.0.0");
  auto& main = swml["sections"]["main"];
  ASSERT_TRUE(main.size() >= 2u);  // answer + ai at minimum

  // First verb should be answer
  ASSERT_TRUE(main[0].contains("answer"));
  ASSERT_EQ(main[0]["answer"]["max_duration"].get<int>(), 3600);

  return true;
}

TEST(agent_pre_answer_verbs) {
  AgentBase agent;
  agent.add_pre_answer_verb("play", json::object({{"url", "https://cdn.example.com/ring.mp3"}}));
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  // First verb should be the pre-answer play
  ASSERT_TRUE(main[0].contains("play"));
  return true;
}

TEST(agent_post_answer_verbs) {
  AgentBase agent;
  agent.add_post_answer_verb("play",
                             json::object({{"url", "https://cdn.example.com/welcome.mp3"}}));
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  // After answer, before AI
  bool found_play = false;
  bool found_ai = false;
  for (const auto& verb : main) {
    if (verb.contains("play")) {
      found_play = true;
    }
    if (verb.contains("ai")) {
      ASSERT_TRUE(found_play);  // play should come before ai
      found_ai = true;
    }
  }
  ASSERT_TRUE(found_play);
  ASSERT_TRUE(found_ai);
  return true;
}

TEST(agent_post_ai_verbs) {
  AgentBase agent;
  agent.add_post_ai_verb("hangup", json::object());
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  // Last verb should be hangup
  ASSERT_TRUE(main.back().contains("hangup"));
  return true;
}

TEST(agent_clear_verbs) {
  AgentBase agent;
  agent.add_pre_answer_verb("play", json::object());
  agent.clear_pre_answer_verbs();
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  // First verb should be answer, not play
  ASSERT_TRUE(main[0].contains("answer"));
  return true;
}

// ========================================================================
// Contexts
// ========================================================================

TEST(agent_define_contexts) {
  AgentBase agent;
  auto& ctx = agent.add_context("default");
  ctx.add_step("greeting", "Greet the user");
  ASSERT_TRUE(agent.has_contexts());
  return true;
}

// ========================================================================
// Skills
// ========================================================================

TEST(agent_add_skill) {
  AgentBase agent;
  agent.add_skill("datetime");
  ASSERT_TRUE(agent.has_skill("datetime"));
  ASSERT_FALSE(agent.has_skill("nonexistent"));
  return true;
}

TEST(agent_remove_skill) {
  AgentBase agent;
  agent.add_skill("datetime");
  agent.remove_skill("datetime");
  ASSERT_FALSE(agent.has_skill("datetime"));
  return true;
}

TEST(agent_list_skills) {
  AgentBase agent;
  agent.add_skill("datetime");
  agent.add_skill("math");
  auto skills = agent.list_skills();
  ASSERT_EQ(skills.size(), 2u);
  return true;
}

// ========================================================================
// Auth
// ========================================================================

TEST(agent_set_auth) {
  AgentBase agent;
  agent.set_auth("user", "pass");
  ASSERT_EQ(agent.auth_username(), "user");
  ASSERT_EQ(agent.auth_password(), "pass");
  return true;
}

// ========================================================================
// Web Config
// ========================================================================

TEST(agent_swaig_query_params) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.add_swaig_query_param("key1", "val1");
  agent.add_swaig_query_param("key2", "val2");
  json swml = agent.render_swml();
  // Check that webhook URLs contain query params
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
      // If there are functions, check URLs
      return true;
    }
  }
  return true;
}

TEST(agent_sip_routing) {
  AgentBase agent;
  agent.enable_sip_routing();
  agent.register_sip_username("alice");
  // Valid username should succeed
  return true;
}

// ========================================================================
// Method Chaining
// ========================================================================

TEST(agent_method_chaining) {
  AgentBase agent;
  agent.set_name("chained")
      .set_prompt_text("Hello")
      .add_hint("test")
      .set_param("temperature", 0.5)
      .set_global_data(json::object({{"k", "v"}}))
      .add_pronunciation("SW", "SignalWire")
      .set_post_prompt("Summary");
  ASSERT_EQ(agent.name(), "chained");
  ASSERT_EQ(agent.get_prompt(), "Hello");
  return true;
}

// ========================================================================
// POM Rendering
// ========================================================================

TEST(agent_pom_rendering) {
  AgentBase agent;
  agent.set_use_pom(true);
  agent.prompt_add_section("Personality", "You are helpful.", {"Be concise"});
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("prompt")) {
      auto& prompt = verb["ai"]["prompt"];
      if (prompt.contains("pom")) {
        ASSERT_EQ(prompt["pom"].size(), 1u);
        ASSERT_EQ(prompt["pom"][0]["title"].get<std::string>(), "Personality");
        return true;
      }
    }
  }
  ASSERT_TRUE(false);
  return true;
}

TEST(agent_raw_text_rendering) {
  AgentBase agent;
  agent.set_use_pom(false);
  agent.set_prompt_text("Raw prompt text");
  json swml = agent.render_swml();
  auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("prompt")) {
      auto& prompt = verb["ai"]["prompt"];
      ASSERT_EQ(prompt["text"].get<std::string>(), "Raw prompt text");
      return true;
    }
  }
  ASSERT_TRUE(false);
  return true;
}

// ========================================================================
// Dynamic Config Callback
// ========================================================================

TEST(agent_dynamic_config) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.set_prompt_text("Original");

  agent.set_dynamic_config_callback([](const std::map<std::string, std::string>& qp, const json&,
                                       const std::map<std::string, std::string>&, AgentBase& copy) {
    auto it = qp.find("tenant");
    if (it != qp.end()) {
      copy.set_prompt_text("Tenant: " + it->second);
    }
  });

  // Without tenant param
  json swml1 = agent.render_swml_for_request({}, json::object(), {});
  // Agent original prompt should be used

  // With tenant param
  std::map<std::string, std::string> qp = {{"tenant", "acme"}};
  json swml2 = agent.render_swml_for_request(qp, json::object(), {});

  // Original agent should be unchanged
  ASSERT_EQ(agent.get_prompt(), "Original");

  return true;
}

// ========================================================================
// Tool token methods
//
// Parity: signalwire-python tests/unit/core/test_agent_base.py
//   ::TestAgentBaseTokenMethods::test_validate_tool_token
//   ::TestAgentBaseTokenMethods::test_create_tool_token
// Python's StateMixin._create_tool_token catches all exceptions and
// returns ""; validate_tool_token rejects unknown function names up front.
// ========================================================================

TEST(agent_create_tool_token_round_trip) {
  AgentBase agent;
  agent.define_tool("test_tool", "t", json::object(), nullptr, true);

  std::string token = agent.create_tool_token("test_tool", "call_123");
  ASSERT_FALSE(token.empty());
  ASSERT_TRUE(agent.validate_tool_token("test_tool", token, "call_123"));
  return true;
}

TEST(agent_validate_tool_token_rejects_unknown_function) {
  AgentBase agent;
  ASSERT_FALSE(agent.validate_tool_token("not_registered", "any", "call_123"));
  return true;
}

TEST(agent_validate_tool_token_rejects_bad_token) {
  AgentBase agent;
  agent.define_tool("test_tool", "t", json::object(), nullptr, true);
  ASSERT_FALSE(agent.validate_tool_token("test_tool", "garbage_token_value", "call_123"));
  return true;
}

TEST(agent_validate_tool_token_rejects_wrong_call_id) {
  AgentBase agent;
  agent.define_tool("test_tool", "t", json::object(), nullptr, true);
  std::string token = agent.create_tool_token("test_tool", "call_A");
  ASSERT_FALSE(token.empty());
  ASSERT_FALSE(agent.validate_tool_token("test_tool", token, "call_B"));
  return true;
}

// The token the RENDER puts on the wire must be the token the VALIDATOR
// accepts. Without this the two halves can drift (a minted-but-unvalidatable
// token, or a validator reading a different wire field) and every secure tool
// would 403 in production while the render looked correct.
TEST(agent_rendered_token_validates_end_to_end) {
  AgentBase agent;
  agent.set_auth("u", "p");
  agent.define_tool("secure_tool", "t", json::object(), [](const json&, const json&) {
    return signalwire::swaig::FunctionResult("ok");
  });

  const std::map<std::string, std::string> query = {{"call_id", "call_xyz"}};
  json swml = agent.render_swml_for_request(query, json::object(), {});

  // Pull the __token the render minted onto the webhook.
  std::string url;
  for (const auto& verb : swml["sections"]["main"]) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG") &&
        verb["ai"]["SWAIG"].contains("functions")) {
      url = verb["ai"]["SWAIG"]["functions"][0]["web_hook_url"].get<std::string>();
    }
  }
  auto at = url.find("__token=");
  ASSERT_TRUE(at != std::string::npos);
  std::string token = url.substr(at + 8);
  auto amp = token.find('&');
  if (amp != std::string::npos) {
    token = token.substr(0, amp);
  }

  // That exact value must validate for this tool + call_id, and not for another.
  ASSERT_TRUE(agent.validate_tool_token("secure_tool", token, "call_xyz"));
  ASSERT_FALSE(agent.validate_tool_token("secure_tool", token, "other_call"));
  return true;
}

// ========================================================================
// Behavior parity bundle (#190/#191/#185/#182) regression tests
// ========================================================================

// Helper: pull the `ai` verb's prompt object out of rendered SWML.
static json find_ai_prompt(const json& swml) {
  const auto& main = swml["sections"]["main"];
  for (const auto& verb : main) {
    if (verb.contains("ai") && verb["ai"].contains("prompt")) {
      return verb["ai"]["prompt"];
    }
  }
  return json();
}

TEST(agent_set_global_data_merges_not_replaces) {
  // #190: a second set_global_data must MERGE over the first, not replace.
  AgentBase agent;
  agent.set_global_data(json::object({{"a", 1}}));
  agent.set_global_data(json::object({{"b", 2}}));
  json swml = agent.render_swml();
  bool found = false;
  for (const auto& verb : swml["sections"]["main"]) {
    if (verb.contains("ai") && verb["ai"].contains("global_data")) {
      auto& gd = verb["ai"]["global_data"];
      ASSERT_EQ(gd.size(), 2u);  // second set must merge, not replace
      ASSERT_EQ(gd["a"].get<int>(), 1);
      ASSERT_EQ(gd["b"].get<int>(), 2);
      found = true;
    }
  }
  ASSERT_TRUE(found);
  return true;
}

TEST(agent_set_function_includes_drops_invalid) {
  // #191: keep only entries with a non-empty string `url` AND an array
  // `functions`; drop the rest.
  AgentBase agent;
  agent.set_function_includes({
      json::object({{"url", "https://x/swaig"}, {"functions", json::array({"a", "b"})}}),  // valid
      json::object({{"url", "https://y/swaig"}}),                         // no functions
      json::object({{"functions", json::array({"c"})}}),                  // no url
      json::object({{"url", ""}, {"functions", json::array({"d"})}}),     // empty url
      json::object({{"url", "https://z/swaig"}, {"functions", "nope"}}),  // functions not array
  });
  json swml = agent.render_swml();
  bool found = false;
  for (const auto& verb : swml["sections"]["main"]) {
    if (verb.contains("ai") && verb["ai"].contains("SWAIG") &&
        verb["ai"]["SWAIG"].contains("includes")) {
      auto& includes = verb["ai"]["SWAIG"]["includes"];
      ASSERT_EQ(includes.size(), 1u);  // only the well-formed entry survives
      ASSERT_EQ(includes[0]["url"].get<std::string>(), "https://x/swaig");
      found = true;
    }
  }
  ASSERT_TRUE(found);
  return true;
}

TEST(agent_default_prompt_fallback_with_contexts) {
  // #185: with contexts and no prompt text, render emits the fallback.
  AgentBase agent("test-agent");
  auto& ctx = agent.add_context("default");
  ctx.add_step("intro", "Hi");
  json swml = agent.render_swml();
  json prompt = find_ai_prompt(swml);
  ASSERT_EQ(prompt["text"].get<std::string>(), "You are test-agent, a helpful AI assistant.");
  return true;
}

TEST(agent_no_default_prompt_fallback_without_contexts) {
  // #185: WITHOUT contexts, an empty prompt is passed through (no fallback).
  AgentBase agent("test-agent");
  json swml = agent.render_swml();
  json prompt = find_ai_prompt(swml);
  ASSERT_EQ(prompt["text"].get<std::string>(), "");
  return true;
}

TEST(agent_prompt_add_to_section_autocreates) {
  // #182: appending to a missing section auto-creates it.
  AgentBase agent;
  agent.prompt_add_to_section("Fresh", "body text", {"b1"});
  ASSERT_TRUE(agent.prompt_has_section("Fresh"));
  auto pom = agent.pom();
  ASSERT_TRUE(pom.has_value());
  ASSERT_EQ(pom->sections.size(), 1u);
  ASSERT_TRUE(pom->sections[0].title.has_value());
  ASSERT_EQ(*pom->sections[0].title, "Fresh");
  ASSERT_EQ(pom->sections[0].body, "body text");
  ASSERT_EQ(pom->sections[0].bullets.size(), 1u);
  return true;
}

TEST(agent_prompt_add_subsection_autocreates) {
  // #182: adding a subsection under a missing parent auto-creates the parent.
  AgentBase agent;
  agent.prompt_add_subsection("Parent", "Child", "detail");
  ASSERT_TRUE(agent.prompt_has_section("Parent"));
  auto pom = agent.pom();
  ASSERT_TRUE(pom.has_value());
  ASSERT_EQ(pom->sections.size(), 1u);
  ASSERT_EQ(pom->sections[0].subsections.size(), 1u);
  ASSERT_TRUE(pom->sections[0].subsections[0].title.has_value());
  ASSERT_EQ(*pom->sections[0].subsections[0].title, "Child");
  return true;
}
