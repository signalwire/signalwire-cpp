// The shallow closed-key check and anyOf/oneOf-shaped verb configs.
//
// verb_top_level_property_names used to test `body.value("type", "") != "object"`
// on the verb's config node and return std::nullopt otherwise. A union node
// (`{"anyOf": [...]}`) carries no `type` of its own, so that test failed and the
// resolver bailed — and validate_verb_top_level_keys reads std::nullopt as "no
// key-set to enforce" and reports valid for ANY key. The check did not report a
// problem; it stopped checking and reported success, which is strictly worse
// than failing.
//
// Five verbs in the SHIPPED schema.json are union-shaped: connect and play
// (oneOf of $refs), send_sms (anyOf of $refs), sleep (anyOf of an
// object-with-duration / integer / SWMLVar), and unset (anyOf of string /
// array-of-string). Four of the five have object branches whose keys are
// perfectly enumerable, and the shallow check accepted arbitrary keys for all
// four. Engaged verbs go 30 -> 34.
//
// In THIS port the defect is LATENT rather than live through Service::add_verb:
// that entry point routes a verb to the shallow resolver only when the verb has a
// registered handler, and `ai` is the only registered handler (a plain closed
// object, not a union) — everything else goes to the deep validator. But
// SchemaUtils::validate_verb_top_level_keys is PUBLIC API, so a caller reaching
// it directly got the disengaged behaviour, and any future handler registration
// for a union-shaped verb would make it live. These tests drive that public
// entry point.
//
// The semantic: a config satisfying a union satisfies SOME branch, so the known
// keys are the UNION of the object branches' keys, and a key belonging to no
// branch belongs to no valid document. Non-object branches contribute nothing —
// they constrain the config to not be an object at all, a different question.
// unset has no object branch, so it correctly stays disengaged.

#include <string>
#include <vector>

#include "signalwire/utils/schema_utils.hpp"

using signalwire::utils::SchemaUtils;

namespace anyof_test {

// A verb config the shipped schema expresses as an anyOf/oneOf: the verb, a
// legitimate config that must keep passing, and the number of keys the resolved
// union must contain (probed key-by-key below, since the resolver is private).
struct UnionVerb {
  std::string verb;
  nlohmann::json legit;
  // Every key the union must ACCEPT. For connect these span all four
  // ConnectDevice branches, which differ only in their discriminating key — an
  // INTERSECTION would reject three of the four.
  std::vector<std::string> branch_keys;
};

inline std::vector<UnionVerb> union_verbs() {
  return {
      {"sleep", nlohmann::json{{"duration", 5000}}, {"duration"}},
      {"play",
       nlohmann::json{{"url", "https://example.test/a.mp3"}},
       {"url", "urls", "volume", "auto_answer", "say_voice", "say_language", "say_gender",
        "status_url"}},
      {"send_sms",
       nlohmann::json{
           {"to_number", "+15551110000"}, {"from_number", "+15552220000"}, {"body", "hi"}},
       {"body", "media", "to_number", "from_number", "region", "tags"}},
      {"connect",
       nlohmann::json{{"to", "sip:alice@example.test"}},
       {"to",
        "serial",
        "parallel",
        "serial_parallel",
        "from",
        "headers",
        "codecs",
        "timeout",
        "max_duration",
        "session_timeout",
        "confirm",
        "confirm_timeout",
        "ringback",
        "encryption",
        "webrtc_media",
        "call_state_url",
        "call_state_events",
        "result",
        "username",
        "password",
        "answer_on_bridge",
        "transfer_after_bridge"}},
  };
}

inline bool errors_mention(const std::vector<std::string>& errors, const std::string& needle) {
  for (const auto& e : errors) {
    if (e.find(needle) != std::string::npos) return true;
  }
  return false;
}

}  // namespace anyof_test

// Forbidden-key direction, and the direct negative control: every one of these
// was ACCEPTED before the fix, because the resolver returned std::nullopt on a
// union node and the check disengaged.
TEST(schema_anyof_union_verbs_reject_key_in_no_branch) {
  SchemaUtils su("", true);
  // Reports EVERY failing verb, not just the first — the negative control is
  // per-verb, so a run must show which of the four are disengaged.
  bool ok = true;
  for (const auto& tc : anyof_test::union_verbs()) {
    nlohmann::json cfg = tc.legit;
    cfg["zzz_not_a_real_key"] = 1;
    auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, cfg);
    if (valid) {
      std::cerr << "  FAIL: " << tc.verb
                << ": a key present in no branch was ACCEPTED -- the closed-key "
                   "check is disengaged on this union-shaped config\n";
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

// The other direction — the fix must not start rejecting valid documents.
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

// Every key of every object branch must be accepted, which is what distinguishes
// a UNION from an intersection or from picking one branch. Probing key-by-key
// also enumerates the resolved set through the public API: a key is known iff a
// config carrying only it is accepted.
TEST(schema_anyof_union_is_union_not_intersection) {
  SchemaUtils su("", true);
  bool ok = true;
  for (const auto& tc : anyof_test::union_verbs()) {
    for (const auto& key : tc.branch_keys) {
      nlohmann::json cfg = nlohmann::json::object();
      cfg[key] = "x";
      auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, cfg);
      if (!valid) {
        std::cerr << "  FAIL: " << tc.verb << ": branch key '" << key
                  << "' fell out of the union\n";
        ok = false;
      }
    }
  }
  return ok;
}

// The resolved key set is EXACTLY the union of the object branches' keys —
// nothing extra crept in. Probed through the public API by asserting a
// deliberately-adjacent misspelling of each branch key is rejected.
TEST(schema_anyof_union_set_is_exact) {
  SchemaUtils su("", true);
  bool ok = true;
  for (const auto& tc : anyof_test::union_verbs()) {
    for (const auto& key : tc.branch_keys) {
      nlohmann::json cfg = nlohmann::json::object();
      cfg[key + "_zz"] = "x";
      auto [valid, errors] = su.validate_verb_top_level_keys(tc.verb, cfg);
      if (valid) {
        std::cerr << "  FAIL: " << tc.verb << ": '" << key
                  << "_zz' is in no branch yet was ACCEPTED\n";
        ok = false;
        break;  // one report per verb is enough
      }
    }
  }
  return ok;
}

// Shapes that genuinely have no closed key-set, pinned so the fix is not read as
// "always enforce something":
//
//   - set   -- an OPEN object (unevaluatedProperties:{} with no `not`, zero
//              declared properties): a free-form variable bag by design.
//   - unset -- a union with NO object branch (string | array-of-string).
//   - cond / label / return -- array / string / untyped, not objects at all.
//
// For these the check must be a NO-OP (pass), not a rejection.
TEST(schema_anyof_non_enumerable_configs_stay_disengaged) {
  SchemaUtils su("", true);
  for (const std::string& verb : {"set", "unset", "cond", "label", "return"}) {
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

// Guards the shape the resolver already handled — a single $ref (ai -> AIObject)
// — since the fix rewrote that path into the shared recursive resolver.
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
