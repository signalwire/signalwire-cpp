// ConfigLoader tests (signalwire::core::ConfigLoader)

#include <cstdlib>
#include <fstream>
#include <string>

#include "signalwire/core/config_loader.hpp"

using signalwire::core::ConfigLoader;

namespace {

// Write a config file under a repo-local scratch dir and return its path.
// Avoids /tmp per the harness rules; the dir is derived from the CWD the test
// binary runs in (the build dir), which is writable.
std::string write_temp_config(const std::string& name, const std::string& contents) {
  std::string dir = ".sw-test-tmp";
  std::string path = dir + "/" + name;
  // Best-effort mkdir via std::ofstream failing is caught by the caller's
  // assertions; use system-independent creation.
  std::string mkdir_cmd = "mkdir -p " + dir;
  (void)std::system(mkdir_cmd.c_str());
  std::ofstream f(path);
  f << contents;
  f.close();
  return path;
}

}  // namespace

TEST(config_loader_no_config) {
  // A path list pointing at a non-existent file -> no config loaded.
  ConfigLoader loader(std::vector<std::string>{".sw-test-tmp/does-not-exist-xyz.json"});
  ASSERT_FALSE(loader.has_config());
  ASSERT_FALSE(loader.get_config_file().has_value());
  ASSERT_TRUE(loader.get_config().is_object());
  ASSERT_TRUE(loader.get_config().empty());
  // get() returns the default when nothing loaded.
  ASSERT_EQ(loader.get("a.b.c", nlohmann::json("fallback")), std::string("fallback"));
  return true;
}

TEST(config_loader_loads_json) {
  std::string path = write_temp_config(
      "cl_basic.json", R"({"server": {"port": 8080, "host": "localhost"}, "debug": true})");
  ConfigLoader loader(std::vector<std::string>{path});
  ASSERT_TRUE(loader.has_config());
  ASSERT_TRUE(loader.get_config_file().has_value());
  ASSERT_EQ(*loader.get_config_file(), path);
  ASSERT_EQ(loader.get("server.port"), 8080);
  ASSERT_EQ(loader.get("server.host"), std::string("localhost"));
  ASSERT_EQ(loader.get("debug"), true);
  return true;
}

TEST(config_loader_get_missing_returns_default) {
  std::string path = write_temp_config("cl_missing.json", R"({"a": {"b": 1}})");
  ConfigLoader loader(std::vector<std::string>{path});
  ASSERT_TRUE(loader.get("a.b.c").is_null());
  ASSERT_EQ(loader.get("x.y", nlohmann::json(42)), 42);
  ASSERT_EQ(loader.get("a.b"), 1);
  return true;
}

TEST(config_loader_get_section) {
  std::string path =
      write_temp_config("cl_section.json", R"({"security": {"ssl_enabled": true}, "other": 1})");
  ConfigLoader loader(std::vector<std::string>{path});
  auto section = loader.get_section("security");
  ASSERT_TRUE(section.is_object());
  ASSERT_EQ(section["ssl_enabled"], true);
  // Absent section -> empty object.
  auto absent = loader.get_section("nope");
  ASSERT_TRUE(absent.is_object());
  ASSERT_TRUE(absent.empty());
  return true;
}

TEST(config_loader_var_substitution) {
  setenv("SW_TEST_CL_VAR", "hello", 1);
  std::string path = write_temp_config("cl_subst.json", R"({"greeting": "${SW_TEST_CL_VAR}"})");
  ConfigLoader loader(std::vector<std::string>{path});
  ASSERT_EQ(loader.get("greeting"), std::string("hello"));
  unsetenv("SW_TEST_CL_VAR");
  return true;
}

TEST(config_loader_var_substitution_default) {
  unsetenv("SW_TEST_CL_UNSET");
  std::string path =
      write_temp_config("cl_default.json", R"({"greeting": "${SW_TEST_CL_UNSET|fallbackval}"})");
  ConfigLoader loader(std::vector<std::string>{path});
  ASSERT_EQ(loader.get("greeting"), std::string("fallbackval"));
  return true;
}

TEST(config_loader_substitute_type_coercion) {
  setenv("SW_TEST_BOOL", "true", 1);
  setenv("SW_TEST_INT", "123", 1);
  setenv("SW_TEST_FLOAT", "3.14", 1);
  ConfigLoader loader(std::vector<std::string>{".sw-test-tmp/none.json"});
  // substitute_vars on raw strings coerces to native JSON types.
  ASSERT_EQ(loader.substitute_vars(nlohmann::json("${SW_TEST_BOOL}")), true);
  ASSERT_EQ(loader.substitute_vars(nlohmann::json("${SW_TEST_INT}")), 123);
  auto f = loader.substitute_vars(nlohmann::json("${SW_TEST_FLOAT}"));
  ASSERT_TRUE(f.is_number_float());
  ASSERT_TRUE(f.get<double>() > 3.13 && f.get<double>() < 3.15);
  // A plain string stays a string.
  ASSERT_EQ(loader.substitute_vars(nlohmann::json("plain")), std::string("plain"));
  unsetenv("SW_TEST_BOOL");
  unsetenv("SW_TEST_INT");
  unsetenv("SW_TEST_FLOAT");
  return true;
}

TEST(config_loader_substitute_recurses) {
  setenv("SW_TEST_NESTED", "world", 1);
  ConfigLoader loader(std::vector<std::string>{".sw-test-tmp/none.json"});
  nlohmann::json input = {{"a", "${SW_TEST_NESTED}"},
                          {"b", nlohmann::json::array({"${SW_TEST_NESTED}"})}};
  auto out = loader.substitute_vars(input);
  ASSERT_EQ(out["a"], std::string("world"));
  ASSERT_EQ(out["b"][0], std::string("world"));
  unsetenv("SW_TEST_NESTED");
  return true;
}

TEST(config_loader_substitute_max_depth) {
  ConfigLoader loader(std::vector<std::string>{".sw-test-tmp/none.json"});
  nlohmann::json deep = {{"x", 1}};
  ASSERT_THROWS(loader.substitute_vars(deep, 0));
  return true;
}

TEST(config_loader_merge_with_env) {
  setenv("SWML_TESTMERGE_KEY", "envval", 1);
  std::string path = write_temp_config("cl_merge.json", R"({"existing": "fromconfig"})");
  ConfigLoader loader(std::vector<std::string>{path});
  auto merged = loader.merge_with_env("SWML_");
  ASSERT_EQ(merged["existing"], std::string("fromconfig"));
  // SWML_TESTMERGE_KEY -> testmerge.key nested.
  ASSERT_TRUE(merged.contains("testmerge"));
  ASSERT_EQ(merged["testmerge"]["key"], std::string("envval"));
  unsetenv("SWML_TESTMERGE_KEY");
  return true;
}

TEST(config_loader_find_config_file_none) {
  // No standard config files in a fresh scratch cwd assumption; find with a
  // bogus service name returns nullopt (unless the machine happens to have
  // /etc/swml/config.json — unlikely in CI). Use additional path pointing at a
  // real temp file to assert the positive path deterministically.
  std::string path = write_temp_config("svc_config_probe.json", R"({"ok": 1})");
  auto found = ConfigLoader::find_config_file(std::nullopt, std::vector<std::string>{path});
  ASSERT_TRUE(found.has_value());
  ASSERT_EQ(*found, path);
  return true;
}
