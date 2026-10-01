// The shallow closed-key check and anyOf/oneOf-shaped verb configs.
//
// The C grammar admits a SWML verb body in four forms — an object, a string, a
// number or an array (mod_infrastructure swml_schema.c
// check_method_type_and_unknown_params: "Allowed types are object, string,
// number, or array") — and the bundled schema.json encodes each form as an arm of
// the verb body's anyOf: the named-parameter OBJECT arm (closed with
// unevaluatedProperties), the positional ARRAY arm, and the bare-scalar
// shorthand arms.
//
// verb_top_level_property_names resolves a verb body to the key set the shallow
// check enforces, under the settled #223 contract (porting-sdk
// docs/legacy-census/DISC-g-d21.md §1.4/§4): EXACTLY ONE CLOSED ARM, ELSE
// DISENGAGE. One closed object arm -> its keys are enforced. Zero -> there is no
// object form to check. Several -> no single key set describes a valid object
// config, so the check disengages instead of unioning the arms (a union admits a
// document mixing keys from two arms that no single arm accepts). The deep
// validator (validate_verb_full) still evaluates every arm.
//
// An earlier resolver bailed on any union node (it carries no `type` of its own)
// and validate_verb_top_level_keys read that as "no key set" and accepted ANY key;
// the forbidden-key tests below are the negative control for that regression.
//
// In THIS port the shallow check is reached through Service::add_verb only for a
// verb with a registered handler (`ai`), but SchemaUtils::validate_verb_top_level_keys
// is PUBLIC API, so these tests drive that entry point directly.

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "signalwire/utils/schema_utils.hpp"

using signalwire::utils::SchemaUtils;

namespace anyof_test {

// A union-shaped verb in the bundled schema with exactly one closed object arm:
// the verb, a legitimate config that must keep passing, and every key of that
// arm (probed key-by-key below, since the resolver is private).
struct UnionVerb {
  std::string verb;
  nlohmann::json legit;
  std::vector<std::string> arm_keys;
};

inline std::vector<UnionVerb> union_verbs() {
  return {
      {"sleep", nlohmann::json{{"duration", 5000}}, {"duration"}},
      {"play",
       nlohmann::json{{"url", "https://example.test/a.mp3"}},
       {"auto_answer", "loop", "say_gender", "say_language", "say_voice", "status_url", "url",
        "urls", "volume"}},
      {"send_sms",
       nlohmann::json{
           {"to_number", "+15551110000"}, {"from_number", "+15552220000"}, {"body", "hi"}},
       {"body", "from_number", "media", "region", "status_callback", "tags", "to_number"}},
      {"answer", nlohmann::json{{"max_duration", 60}}, {"max_duration"}},
      {"hangup", nlohmann::json{{"reason", "busy"}}, {"reason"}},
      {"label", nlohmann::json{{"label", "start"}}, {"label"}},
  };
}

inline bool errors_mention(const std::vector<std::string>& errors, const std::string& needle) {
  for (const auto& e : errors) {
    if (e.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

// Write a minimal schema whose one verb `v` has the given body, so the contract's
// arm-counting rule can be pinned against a body SHAPE the test controls (the
// bundled schema has no verb with two closed object arms today, so without a
// fixture the multi-arm branch would be proven by nothing).
inline std::string write_fixture_schema(const nlohmann::json& body, const std::string& tag) {
  nlohmann::json schema = {
      {"$defs",
       {{"SWMLMethod", {{"anyOf", nlohmann::json::array({{{"$ref", "#/$defs/V"}}})}}},
        {"V", {{"type", "object"}, {"properties", {{"v", body}}}}}}},
  };
  auto path = std::filesystem::temp_directory_path() /
              ("sw-cpp-anyof-" + tag + "-" + std::to_string(std::rand()) + ".json");
  std::ofstream(path) << schema.dump();
  return path.string();
}

inline nlohmann::json closed_arm(const std::vector<std::string>& keys) {
  nlohmann::json props = nlohmann::json::object();
  for (const auto& k : keys) {
    props[k] = {{"type", "string"}};
  }
  return {{"type", "object"},
          {"properties", props},
          {"unevaluatedProperties", {{"not", nlohmann::json::object()}}}};
}

}  // namespace anyof_test

// Forbidden-key direction, and the negative control for the union-node bail: a
// key in the verb's single closed object arm is enforced through the anyOf.
TEST(schema_anyof_union_verbs_reject_key_in_no_branch) {
  SchemaUtils su("", true);
  // Reports EVERY failing verb, not just the first.
  bool ok = true;
  for (const auto& tc : anyof_test::union_verbs()) {
    nlohmann::json cfg = tc.legit;
    cfg["zzz_not_a_real_key"] = 1;
    auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, cfg);
    if (valid) {
      std::cerr << "  FAIL: " << tc.verb
                << ": a key in no arm was ACCEPTED -- the closed-key check is "
                   "disengaged on this union-shaped config\n";
      ok = false;
      continue;
    }
    // The rejection must name the offending key, not merely fail.
    if (!anyof_test::errors_mention(errors, "zzz_not_a_real_key")) {
      std::cerr << "  FAIL: " << tc.verb << ": rejection must name the offending key\n";
      ok = false;
    }
  }
  return ok;
}

// The other direction — legitimate documents keep passing.
TEST(schema_anyof_union_verbs_accept_legitimate_config) {
  SchemaUtils su("", true);
  for (const auto& tc : anyof_test::union_verbs()) {
    auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, tc.legit);
    if (!valid) {
      std::cerr << "  FAIL: " << tc.verb << ": legitimate config rejected\n";
      return false;
    }
  }
  return true;
}

// Every key of the closed object arm is accepted (a key is known iff a config
// carrying only it is accepted, which enumerates the resolved set).
TEST(schema_anyof_closed_arm_keys_all_accepted) {
  SchemaUtils su("", true);
  bool ok = true;
  for (const auto& tc : anyof_test::union_verbs()) {
    for (const auto& key : tc.arm_keys) {
      nlohmann::json cfg = nlohmann::json::object();
      cfg[key] = "x";
      auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, cfg);
      if (!valid) {
        std::cerr << "  FAIL: " << tc.verb << ": arm key '" << key << "' was rejected\n";
        ok = false;
      }
    }
  }
  return ok;
}

// The resolved key set is EXACTLY the closed arm's keys — nothing extra crept in.
// Probed by asserting a deliberately-adjacent misspelling of each key is rejected.
TEST(schema_anyof_closed_arm_set_is_exact) {
  SchemaUtils su("", true);
  bool ok = true;
  for (const auto& tc : anyof_test::union_verbs()) {
    for (const auto& key : tc.arm_keys) {
      nlohmann::json cfg = nlohmann::json::object();
      cfg[key + "_zz"] = "x";
      auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, cfg);
      if (valid) {
        std::cerr << "  FAIL: " << tc.verb << ": '" << key
                  << "_zz' is in no arm yet was ACCEPTED\n";
        ok = false;
        break;  // one report per verb is enough
      }
    }
  }
  return ok;
}

// Shapes that genuinely have no closed key-set, pinned so the check is not read
// as "always enforce something": set (an OPEN variable bag), unset (no object
// arm), cond / return (not objects). For these the check must PASS.
TEST(schema_anyof_non_enumerable_configs_stay_disengaged) {
  SchemaUtils su("", true);
  for (const std::string& verb : {"set", "unset", "cond", "return"}) {
    nlohmann::json cfg = nlohmann::json{{"anything_at_all", 1}};
    auto [valid, errors] = su.validate_verb_top_level_keys(verb, cfg);
    if (!valid) {
      std::cerr << "  FAIL: " << verb
                << " has no closed key-set in the schema; the shallow check must stay "
                   "disengaged rather than invent one\n";
      return false;
    }
  }
  return true;
}

// #223: EXACTLY ONE closed arm engages — pinned against a fixture body shaped
// like the bundled schema's (closed object arm + positional array arm + a bare
// scalar arm).
TEST(schema_anyof_contract_single_closed_arm_engages) {
  nlohmann::json body = {
      {"anyOf",
       nlohmann::json::array({anyof_test::closed_arm({"a", "b"}),
                              {{"type", "array"}},
                              {{"allOf", nlohmann::json::array({{{"type", "string"}}})}}})}};
  auto path = anyof_test::write_fixture_schema(body, "one");
  SchemaUtils su(path, true);
  std::remove(path.c_str());
  auto [ok_valid, ok_errors] = su.validate_verb_top_level_keys("v", nlohmann::json{{"a", "x"}});
  ASSERT_TRUE(ok_valid);
  auto [bad_valid, bad_errors] =
      su.validate_verb_top_level_keys("v", nlohmann::json{{"a", "x"}, {"zz", "y"}});
  ASSERT_FALSE(bad_valid);
  ASSERT_TRUE(anyof_test::errors_mention(bad_errors, "zz"));
  return true;
}

// #223: MORE THAN ONE closed arm disengages — never the union of the arms. Under a
// union, {"a", "c"} (a key from each arm, a document NO single arm accepts) would
// pass while {"zz"} failed; under the contract the shallow check enforces nothing.
TEST(schema_anyof_contract_multiple_closed_arms_disengage) {
  nlohmann::json body = {{"anyOf", nlohmann::json::array({anyof_test::closed_arm({"a", "b"}),
                                                          anyof_test::closed_arm({"c"})})}};
  auto path = anyof_test::write_fixture_schema(body, "two");
  SchemaUtils su(path, true);
  std::remove(path.c_str());
  auto [valid, errors] = su.validate_verb_top_level_keys("v", nlohmann::json{{"zz", "y"}});
  ASSERT_TRUE(valid);
  ASSERT_TRUE(errors.empty());
  return true;
}

// #223: ZERO closed arms (only non-object forms) disengage.
TEST(schema_anyof_contract_no_closed_arm_disengages) {
  nlohmann::json body = {
      {"anyOf", nlohmann::json::array({{{"type", "string"}}, {{"type", "array"}}})}};
  auto path = anyof_test::write_fixture_schema(body, "zero");
  SchemaUtils su(path, true);
  std::remove(path.c_str());
  auto [valid, errors] = su.validate_verb_top_level_keys("v", nlohmann::json{{"zz", "y"}});
  ASSERT_TRUE(valid);
  return true;
}

// Guards the handler-verb path — `ai`, the one verb Service::add_verb routes to
// the shallow check — a misspelled top-level key is rejected.
TEST(schema_anyof_ref_following_still_resolves_ai) {
  SchemaUtils su("", true);

  nlohmann::json bad{{"prompt", nlohmann::json{{"text", "hi"}}}, {"temperatur", 0.5}};
  auto [bad_valid, bad_errors] = su.validate_verb_top_level_keys("ai", bad);
  ASSERT_FALSE(bad_valid);
  ASSERT_TRUE(anyof_test::errors_mention(bad_errors, "temperatur"));

  nlohmann::json good{{"prompt", nlohmann::json{{"text", "hi"}}}};
  auto [good_valid, good_errors] = su.validate_verb_top_level_keys("ai", good);
  ASSERT_TRUE(good_valid);
  return true;
}

// An unknown verb is still rejected (the resolver is never consulted for one).
TEST(schema_anyof_unknown_verb_still_rejected) {
  SchemaUtils su("", true);
  auto [valid, errors] =
      su.validate_verb_top_level_keys("xyz_not_a_verb", nlohmann::json::object());
  ASSERT_FALSE(valid);
  return true;
}
