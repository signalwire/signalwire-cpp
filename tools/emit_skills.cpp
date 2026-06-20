// emit_skills.cpp — the C++ port's SKILL-DUMP program for the cross-port
// SKILL-CONTRACT differ (porting-sdk/scripts/diff_skill_contracts.py).
//
// The sibling of emit_corpus.cpp, for built-in SKILLS rather than
// FunctionResult. For each covered skill it looks up the skill's factory in the
// signalwire::skills::SkillRegistry, instantiates it, runs setup(config) with
// the canonical config from the shared corpus
// (porting-sdk/scripts/skill_contract_corpus.py — the single source of truth),
// collects the tool contracts the skill registers, and prints ONE JSON object
// mapping
//
//     skill-id -> [ { "name": ..., "parameters": {...}, "required"?: [...] }, ... ]
//
// to stdout. The differ runs this program, parses that object, and structurally
// compares each skill's tool contract against the Python reference (which
// registers the same tools). The differ normalises both sides (flat vs wrapped
// params, required list, enum order); this program emits each tool's name +
// parameters verbatim. DESCRIPTIONS are not part of the compared contract.
// Mirrors the Java port's EmitSkills (com.signalwire.sdk.tools.EmitSkills) and
// Go's cmd/emit-skills.
//
// A skill registers tools in one of two shapes, and a skill may use either or
// both:
//   * handler tools — register_tools() returns ToolDefinitions, each carrying a
//     `name` and a `parameters` JSON-Schema map. When that map carries a
//     top-level `required` array it is forwarded so the differ sees it.
//   * DataMap tools — get_datamap_functions() returns the wrapped SWAIG function
//     maps (name under "function", params under "parameters" with their own
//     embedded `required`). The DataMap-based covered skills are weather_api,
//     joke, api_ninjas_trivia, google_maps, datasphere, swml_transfer,
//     play_background_file.
//
// CONTRACT (mirrors the per-port dump contract in the differ's --help):
//   * The id set MUST equal skill_contract_corpus.corpus_ids() (the differ
//     rejects a mismatch).
//   * Only stdout carries the JSON object; all logs/errors go to stderr.
//
// Build: a CMake target `emit_skills` (see CMakeLists.txt). Built in the TEST
// gate; run via the SKILL-CONTRACT gate in scripts/run-ci.sh.

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include "signalwire/swaig/tool_definition.hpp"

using json = nlohmann::json;

namespace {

// One entry of skill_contract_corpus.py's CORPUS: {id, skill, config}.
struct CorpusEntry {
  std::string id;
  std::string skill;
  json config;
};

// Run `python3 <script>`, capturing its stdout into `out`; the child's stderr
// is discarded. Returns the child's exit status (or -1 on a spawn/wait error).
// Uses fork + execvp with an argv vector (NOT a shell) so there is no command
// interpolation — argument values that contain shell metacharacters are passed
// verbatim and never parsed by /bin/sh.
int run_python(const std::string& script, std::string& out) {
  out.clear();
  std::array<int, 2> fds{};
  if (pipe(fds.data()) != 0) {
    return -1;
  }
  const pid_t pid = fork();
  if (pid < 0) {
    close(fds[0]);
    close(fds[1]);
    return -1;
  }
  if (pid == 0) {
    // Child: stdout -> pipe, stderr -> /dev/null, then exec python3 directly.
    dup2(fds[1], STDOUT_FILENO);
    const int devnull = open("/dev/null", O_WRONLY);
    if (devnull >= 0) {
      dup2(devnull, STDERR_FILENO);
    }
    close(fds[0]);
    close(fds[1]);
    const char* argv[] = {"python3", script.c_str(), nullptr};
    execvp("python3", const_cast<char* const*>(argv));
    _exit(127);  // exec failed
  }
  // Parent: read the child's stdout to EOF.
  close(fds[1]);
  std::array<char, 4096> buf{};
  ssize_t n = 0;
  while ((n = read(fds[0], buf.data(), buf.size())) > 0) {
    out.append(buf.data(), static_cast<size_t>(n));
  }
  close(fds[0]);
  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    return -1;
  }
  return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
}

// Locate porting-sdk/scripts/skill_contract_corpus.py via $PORTING_SDK /
// $PORTING_SDK_PATH or the sibling ../porting-sdk (the adjacency convention),
// run it, and return its CORPUS entries.
std::vector<CorpusEntry> load_corpus() {
  std::vector<std::string> bases;
  for (const char* var : {"PORTING_SDK", "PORTING_SDK_PATH"}) {
    const char* v = std::getenv(var);
    if (v != nullptr && v[0] != '\0') {
      bases.emplace_back(v);
    }
  }
  bases.emplace_back("../porting-sdk");

  for (const auto& base : bases) {
    std::string script = base + "/scripts/skill_contract_corpus.py";
    // Probe existence cheaply via the python invocation itself; if the file is
    // missing python3 exits nonzero and we fall through to the next base.
    std::string out;
    int rc = run_python(script, out);
    if (rc != 0 || out.empty()) {
      continue;
    }
    json parsed = json::parse(out, nullptr, /*allow_exceptions=*/false);
    if (parsed.is_discarded() || !parsed.contains("corpus")) {
      continue;
    }
    std::vector<CorpusEntry> entries;
    for (const auto& e : parsed["corpus"]) {
      CorpusEntry entry;
      entry.id = e.value("id", "");
      entry.skill = e.value("skill", "");
      entry.config = e.contains("config") && !e["config"].is_null() ? e["config"] : json::object();
      entries.push_back(std::move(entry));
    }
    return entries;
  }
  throw std::runtime_error(
      "cannot locate porting-sdk/scripts/skill_contract_corpus.py "
      "(set PORTING_SDK / PORTING_SDK_PATH or clone porting-sdk adjacent)");
}

// Instantiate one covered skill with the corpus config, run setup, and collect
// the {name, parameters, required?} contracts it registers from BOTH
// register_tools() (handler tools) and get_datamap_functions() (DataMap tools).
json contracts_for(const CorpusEntry& entry) {
  auto& registry = signalwire::skills::SkillRegistry::instance();
  std::unique_ptr<signalwire::skills::SkillBase> skill = registry.create(entry.skill);
  if (!skill) {
    throw std::runtime_error("no registered factory for covered skill \"" + entry.skill + "\"");
  }
  if (!skill->setup(entry.config)) {
    throw std::runtime_error("skill \"" + entry.skill +
                             "\" setup returned false with the corpus config — "
                             "config drift between the corpus and the port.");
  }

  json contracts = json::array();

  // Handler tools: name + parameters (JSON-Schema map). Forward a top-level
  // `required` array if the parameters map carries one (the differ reads it
  // either from the wrapped schema or from this top-level key).
  for (const auto& tool : skill->register_tools()) {
    json c = json::object();
    c["name"] = tool.name;
    c["parameters"] =
        (!tool.parameters.is_null() && !tool.parameters.empty()) ? tool.parameters : json::object();
    if (tool.parameters.is_object() && tool.parameters.contains("required") &&
        tool.parameters["required"].is_array()) {
      c["required"] = tool.parameters["required"];
    }
    contracts.push_back(std::move(c));
  }

  // DataMap tools: the wrapped SWAIG function map — name under "function",
  // params (with their own embedded `required`) under "parameters".
  for (const auto& fn : skill->get_datamap_functions()) {
    json c = json::object();
    c["name"] = fn.contains("function") ? fn["function"] : json(nullptr);
    c["parameters"] = fn.contains("parameters") && !fn["parameters"].is_null() ? fn["parameters"]
                                                                               : json::object();
    contracts.push_back(std::move(c));
  }

  return contracts;
}

}  // namespace

int main() {
  try {
    // Force linkage of the built-in skill self-registration TUs: the skills
    // live in the static signalwire library and their REGISTER_SKILL statics
    // would otherwise be dropped when nothing in this program references them.
    signalwire::skills::ensure_builtin_skills_registered();

    std::vector<CorpusEntry> corpus = load_corpus();
    json result = json::object();
    for (const auto& entry : corpus) {
      result[entry.id] = contracts_for(entry);
    }
    std::cout << result.dump() << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "emit_skills: " << e.what() << "\n";
    return 1;
  }
}
