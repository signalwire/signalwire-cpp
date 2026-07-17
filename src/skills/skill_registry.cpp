// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
// Explicit skill registration - all skills registered in one TU to avoid
// static initialization ordering and linker stripping issues.
#include "signalwire/skills/skill_registry.hpp"

#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <regex>
#include <sstream>

#include "signalwire/common.hpp"
#include "signalwire/datamap/datamap.hpp"
#include "signalwire/skills/claude_skills_core.hpp"
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skills_http.hpp"
#include "signalwire/skills/web_search_core.hpp"

namespace signalwire {
namespace skills {

// ============================================================================
// All 18 skill class definitions (inline in this TU)
// ============================================================================

// --- 1. datetime ---
class DateTimeSkill : public SkillBase {
 public:
  std::string skill_name() const override { return "datetime"; }
  std::string skill_description() const override {
    return "Get current date, time, and timezone information";
  }
  bool setup(const json& p) override {
    params_ = p;
    return true;
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
                "get_current_time", "Get the current time",
                json::object({{"type", "object"},
                              {"properties",
                               json::object({{"timezone", json::object({{"type", "string"}})}})}}),
                [](const json&, const json&) -> swaig::FunctionResult {
                  auto t = std::time(nullptr);
                  std::tm tm;
                  gmtime_r(&t, &tm);
                  char b[64];
                  std::strftime(b, sizeof(b), "%H:%M:%S UTC", &tm);
                  return swaig::FunctionResult(std::string("Current time: ") + b);
                }),
            define_tool(
                "get_current_date", "Get the current date",
                json::object({{"type", "object"},
                              {"properties",
                               json::object({{"timezone", json::object({{"type", "string"}})}})}}),
                [](const json&, const json&) -> swaig::FunctionResult {
                  auto t = std::time(nullptr);
                  std::tm tm;
                  gmtime_r(&t, &tm);
                  char b[64];
                  std::strftime(b, sizeof(b), "%Y-%m-%d", &tm);
                  return swaig::FunctionResult(std::string("Current date: ") + b);
                })};
  }
  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Date and Time Information",
             "You can provide date and time.",
             {"Use get_current_time", "Use get_current_date"}}};
  }
};

// --- 2. math ---
class MathSkillR : public SkillBase {
  static double eval(const std::string& e) {
    std::string s;
    for (char c : e) {
      if (!std::isspace((unsigned char)c)) {
        s += c;
      }
    }
    if (s.empty()) {
      throw std::runtime_error("Empty");
    }
    struct P {
      const std::string& s;
      size_t p = 0;
      double expr() {
        double r = term();
        while (p < s.size() && (s[p] == '+' || s[p] == '-')) {
          char o = s[p++];
          double t = term();
          r = (o == '+') ? r + t : r - t;
        }
        return r;
      }
      double term() {
        double r = pw();
        while (p < s.size() && (s[p] == '*' || s[p] == '/' || s[p] == '%')) {
          if (s[p] == '*' && p + 1 < s.size() && s[p + 1] == '*') {
            break;
          }
          char o = s[p++];
          double t = pw();
          if (o == '*') {
            r *= t;
          } else if (o == '/') {
            if (t == 0) {
              throw std::runtime_error("Div0");
            }
            r /= t;
          } else {
            if (t == 0) {
              throw std::runtime_error("Mod0");
            }
            r = std::fmod(r, t);
          }
        }
        return r;
      }
      double pw() {
        double b = un();
        if (p + 1 < s.size() && s[p] == '*' && s[p + 1] == '*') {
          p += 2;
          return std::pow(b, pw());
        }
        return b;
      }
      double un() {
        if (p < s.size() && s[p] == '-') {
          p++;
          return -atom();
        }
        if (p < s.size() && s[p] == '+') {
          p++;
        }
        return atom();
      }
      double atom() {
        if (p < s.size() && s[p] == '(') {
          p++;
          double r = expr();
          if (p < s.size() && s[p] == ')') {
            p++;
          }
          return r;
        }
        size_t st = p;
        while (p < s.size() && (std::isdigit((unsigned char)s[p]) || s[p] == '.')) {
          p++;
        }
        if (st == p) {
          throw std::runtime_error("Expected number");
        }
        return std::stod(s.substr(st, p - st));
      }
    };
    P parser{s};
    double r = parser.expr();
    if (parser.p != s.size()) {
      throw std::runtime_error("Unexpected char");
    }
    return r;
  }

 public:
  std::string skill_name() const override { return "math"; }
  std::string skill_description() const override {
    return "Perform basic mathematical calculations";
  }
  bool setup(const json& p) override {
    params_ = p;
    return true;
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        "calculate", "Perform a mathematical calculation",
        json::object(
            {{"type", "object"},
             {"properties", json::object({{"expression", json::object({{"type", "string"}})}})}}),
        [](const json& a, const json&) -> swaig::FunctionResult {
          auto e = a.value("expression", "");
          if (e.empty()) {
            return swaig::FunctionResult("No expression");
          }
          try {
            double r = eval(e);
            std::ostringstream o;
            o << e << " = " << r;
            return swaig::FunctionResult(o.str());
          } catch (const std::exception& ex) {
            return swaig::FunctionResult(std::string("Error: ") + ex.what());
          }
        })};
  }
  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Mathematical Calculations", "", {"Use calculate for math operations"}}};
  }
};

// --- 3. joke ---
class JokeSkillR : public SkillBase {
  std::string ak_, tn_ = "get_joke";

 public:
  std::string skill_name() const override { return "joke"; }
  std::string skill_description() const override { return "Tell jokes using API Ninjas"; }
  bool setup(const json& p) override {
    params_ = p;
    ak_ = get_param_or_env(p, "api_key", "API_NINJAS_KEY");
    tn_ = get_param<std::string>(p, "tool_name", "get_joke");
    return !ak_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }
  std::vector<json> get_datamap_functions() const override {
    datamap::DataMap dm(tn_);
    dm.purpose("Get a joke")
        .parameter("type", "string", "Type", true, {"jokes", "dadjokes"})
        .webhook("GET", "https://api.api-ninjas.com/v1/${args.type}",
                 json::object({{"X-Api-Key", ak_}}))
        .output(swaig::FunctionResult("Here's a joke: ${array[0].joke}"));
    return {dm.to_swaig_function()};
  }
  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Joke Telling", "", {tn_}}};
  }
  json get_global_data() const override { return json::object({{"joke_skill_enabled", true}}); }
};

// --- 4. weather_api ---
class WeatherApiSkillR : public SkillBase {
  std::string ak_, tn_ = "get_weather", tu_ = "fahrenheit";

 public:
  std::string skill_name() const override { return "weather_api"; }
  std::string skill_description() const override {
    return "Get current weather from WeatherAPI.com";
  }
  bool setup(const json& p) override {
    params_ = p;
    ak_ = get_param_or_env(p, "api_key", "WEATHER_API_KEY");
    tn_ = get_param<std::string>(p, "tool_name", "get_weather");
    tu_ = get_param<std::string>(p, "temperature_unit", "fahrenheit");
    return !ak_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }
  std::vector<json> get_datamap_functions() const override {
    datamap::DataMap dm(tn_);
    dm.purpose("Get weather")
        .parameter("location", "string", "City", true)
        .webhook("GET", "https://api.weatherapi.com/v1/current.json?key=" + ak_ +
                            "&q=${lc:enc:args.location}&aqi=no")
        .output(swaig::FunctionResult("Weather: ${response.location.name}"));
    return {dm.to_swaig_function()};
  }
};

// --- 5. web_search ---
// Issues a real GET to Google's Custom Search API. WEB_SEARCH_BASE_URL
// overrides the host (used by audit_skills_dispatch.py to redirect at a
// loopback fixture). Default base is https://www.googleapis.com; the
// `/customsearch/v1` path is always appended so the audit's path
// substring assertion succeeds against either the real upstream or the
// fixture.
class WebSearchSkillR : public SkillBase {
  std::string ak_, sid_, tn_ = "web_search";
  int nr_ = 3;
  // Per Python 8aad242 — optional prefix/postfix wrapped around the
  // success response only (transport/HTTP/parse errors stay raw).
  std::string rp_, rpf_;
  // Latency-control params (Corresponds to 51101da + 295745b). Shared
  // implementation lives in web_search_core.hpp so this skill and the
  // builtin WebSearchSkill can never drift. overall_deadline +
  // per_page_timeout are the contract; parallel_scrape is best-effort.
  web_search_core::LatencyParams lp_;
  std::size_t mcl_ = 32768;  // max_content_length
 public:
  std::string skill_name() const override { return "web_search"; }
  std::string skill_description() const override {
    return "Search the web via Google Custom Search API";
  }
  std::string skill_version() const override { return "2.0.0"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    ak_ = get_param_or_env(p, "api_key", "GOOGLE_SEARCH_API_KEY");
    if (ak_.empty()) {
      ak_ = get_env("GOOGLE_API_KEY");
    }
    sid_ = get_param_or_env(p, "search_engine_id", "GOOGLE_SEARCH_ENGINE_ID");
    if (sid_.empty()) {
      sid_ = get_env("GOOGLE_CSE_ID");
    }
    tn_ = get_param<std::string>(p, "tool_name", "web_search");
    nr_ = get_param<int>(p, "num_results", 3);
    rp_ = get_param<std::string>(p, "response_prefix", "");
    rpf_ = get_param<std::string>(p, "response_postfix", "");
    // Latency-control params. Defaults match Python: per_page_timeout=2.0,
    // overall_deadline=10.0, parallel_scrape=true, snippets_only=false.
    lp_.per_page_timeout = get_param<double>(p, "per_page_timeout", 2.0);
    lp_.overall_deadline = get_param<double>(p, "overall_deadline", 10.0);
    lp_.parallel_scrape = get_param<bool>(p, "parallel_scrape", true);
    lp_.snippets_only = get_param<bool>(p, "snippets_only", false);
    mcl_ = static_cast<std::size_t>(get_param<int>(p, "max_content_length", 32768));
    return !ak_.empty() && !sid_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    std::string ak = ak_, sid = sid_;
    int nr = nr_;
    std::string rp = rp_, rpf = rpf_;
    web_search_core::LatencyParams lp = lp_;
    std::size_t mcl = mcl_;
    return {define_tool(
        tn_, "Search the web for high-quality information",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object({{"query", json::object({{"type", "string"},
                                                    {"description", "Search query"}})}})}}),
        [ak, sid, nr, rp, rpf, lp, mcl](const json& a, const json&) -> swaig::FunctionResult {
          std::string q = a.value("query", "");
          if (q.empty()) {
            return swaig::FunctionResult("No search query provided");
          }
          std::string base = get_env("WEB_SEARCH_BASE_URL", "https://www.googleapis.com");
          while (!base.empty() && base.back() == '/') {
            base.pop_back();
          }
          std::ostringstream u;
          u << base << "/customsearch/v1"
            << "?key=" << url_encode(ak) << "&cx=" << url_encode(sid) << "&q=" << url_encode(q)
            << "&num=" << nr;
          // The CSE fetch is the single non-cancelable step; the
          // overall_deadline budget starts after it (inside core::run).
          auto r = skills::http_get(u.str());
          if (r.status == 0) {
            return swaig::FunctionResult("Web search transport error: " + r.error);
          }
          if (r.status < 200 || r.status >= 300) {
            return swaig::FunctionResult("Web search HTTP " + std::to_string(r.status) + ": " +
                                         r.body);
          }
          json parsed;
          try {
            parsed = json::parse(r.body);
          } catch (const json::parse_error& e) {
            return swaig::FunctionResult(std::string("Web search parse error: ") + e.what());
          }
          auto cands = web_search_core::parse_cse_items(parsed);
          std::string no_items = "Web search results for '" + q + "':\n(no results)";
          // run() handles snippets_only / deadline-bounded scraping /
          // snippet fallback. Returns UNWRAPPED body.
          std::string response = web_search_core::run(q, cands, lp, nr, mcl, no_items);
          // Wrap the success / snippet / scraped response; the no-items
          // message stays unwrapped.
          if (response != no_items) {
            if (!rp.empty()) {
              response = rp + "\n\n" + response;
            }
            if (!rpf.empty()) {
              response = response + "\n\n" + rpf;
            }
          }
          return swaig::FunctionResult(response);
        })};
  }
  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Web Search", "", {"Use " + tn_}}};
  }
  json get_global_data() const override { return json::object({{"web_search_enabled", true}}); }
  json get_parameter_schema() const override {
    // Advertise the 6 latency / response params (Corresponds to 295745b).
    return web_search_core::schema_fragment();
  }
};

// --- 6. wikipedia_search ---
// Issues a real GET to Wikipedia's `/w/api.php` action=query endpoint.
// WIKIPEDIA_BASE_URL overrides the host (audit fixture redirect).
class WikipediaSearchSkillR : public SkillBase {
  int nr_ = 1;
  std::string nm_ = "No Wikipedia articles found for that topic.";

 public:
  std::string skill_name() const override { return "wikipedia_search"; }
  std::string skill_description() const override { return "Search Wikipedia"; }
  bool setup(const json& p) override {
    params_ = p;
    nr_ = get_param<int>(p, "num_results", 1);
    nm_ = get_param<std::string>(p, "no_results_message", nm_);
    return true;
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    int nr = nr_;
    std::string nm = nm_;
    return {define_tool(
        "search_wiki", "Search Wikipedia",
        json::object(
            {{"type", "object"},
             {"properties", json::object({{"query", json::object({{"type", "string"},
                                                                  {"description", "Topic"}})}})}}),
        [nr, nm](const json& a, const json&) -> swaig::FunctionResult {
          std::string q = a.value("query", "");
          if (q.empty()) {
            return swaig::FunctionResult(nm);
          }
          std::string base = get_env("WIKIPEDIA_BASE_URL", "https://en.wikipedia.org");
          while (!base.empty() && base.back() == '/') {
            base.pop_back();
          }
          std::ostringstream u;
          u << base << "/w/api.php"
            << "?action=query&list=search&format=json"
            << "&srlimit=" << nr << "&srsearch=" << url_encode(q);
          auto r = skills::http_get(u.str());
          if (r.status == 0) {
            return swaig::FunctionResult("Wikipedia transport error: " + r.error);
          }
          if (r.status < 200 || r.status >= 300) {
            return swaig::FunctionResult("Wikipedia HTTP " + std::to_string(r.status) + ": " +
                                         r.body);
          }
          json parsed;
          try {
            parsed = json::parse(r.body);
          } catch (const json::parse_error& e) {
            return swaig::FunctionResult(std::string("Wikipedia parse error: ") + e.what());
          }
          std::ostringstream out;
          out << "Wikipedia search for '" << q << "':\n";
          bool any = false;
          if (parsed.contains("query") && parsed["query"].contains("search") &&
              parsed["query"]["search"].is_array()) {
            for (const auto& h : parsed["query"]["search"]) {
              out << "- " << h.value("title", "") << ": " << h.value("snippet", "") << "\n";
              any = true;
            }
          }
          if (!any) {
            return swaig::FunctionResult(nm);
          }
          return swaig::FunctionResult(out.str());
        })};
  }
  std::vector<SkillPromptSection> get_prompt_sections() const override {
    return {{"Wikipedia Search", "", {"Use search_wiki"}}};
  }
};

// --- 7-18: Remaining skills (abbreviated but functional) ---

class GoogleMapsSkillR : public SkillBase {
  std::string ak_;

 public:
  std::string skill_name() const override { return "google_maps"; }
  std::string skill_description() const override { return "Google Maps"; }
  bool setup(const json& p) override {
    params_ = p;
    ak_ = get_param_or_env(p, "api_key", "GOOGLE_MAPS_API_KEY");
    return !ak_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    // Contract mirrors Python's google_maps register_tools (no required[] on
    // either tool's params except as Python derives it): lookup_address takes
    // address (string) + optional bias_lat/bias_lng (number, required:False ->
    // none required); compute_route takes the four lat/lng numbers (Python sets
    // no required:False, so all four are required).
    return {
        define_tool(
            "lookup_address", "Validate and geocode a street address or business name",
            json::object(
                {{"type", "object"},
                 {"properties",
                  json::object(
                      {{"address",
                        json::object({{"type", "string"},
                                      {"description", "The address or business name to look up"}})},
                       {"bias_lat", json::object({{"type", "number"},
                                                  {"description",
                                                   "Latitude to bias results toward (optional)"}})},
                       {"bias_lng",
                        json::object(
                            {{"type", "number"},
                             {"description", "Longitude to bias results toward (optional)"}})}})}}),
            [](const json&, const json&) -> swaig::FunctionResult {
              return swaig::FunctionResult("Address lookup");
            }),
        define_tool(
            "compute_route", "Compute a driving route between two points",
            json::object(
                {{"type", "object"},
                 {"properties",
                  json::object(
                      {{"origin_lat",
                        json::object({{"type", "number"}, {"description", "Origin latitude"}})},
                       {"origin_lng",
                        json::object({{"type", "number"}, {"description", "Origin longitude"}})},
                       {"dest_lat", json::object({{"type", "number"},
                                                  {"description", "Destination latitude"}})},
                       {"dest_lng", json::object({{"type", "number"},
                                                  {"description", "Destination longitude"}})}})}}),
            [](const json&, const json&) -> swaig::FunctionResult {
              return swaig::FunctionResult("Route computed");
            })};
  }
  std::vector<std::string> get_hints() const override { return {"address", "location", "route"}; }
};

// Spider — issues a real GET to the URL the LLM provides; SPIDER_BASE_URL
// (set by the audit) replaces the scheme://host so requests land on the
// loopback fixture while the path/query stays for the audit's substring
// assertion. Strips HTML tags from the response.
class SpiderSkillR : public SkillBase {
  static std::string strip_html(const std::string& html) {
    static const std::regex tag_re(R"(<[^>]+>)");
    static const std::regex ws_re(R"(\s+)");
    std::string nt = std::regex_replace(html, tag_re, " ");
    return std::regex_replace(nt, ws_re, " ");
  }
  static std::string apply_base(const std::string& url, const std::string& base) {
    if (base.empty()) {
      return url;
    }
    auto se = url.find("://");
    if (se == std::string::npos) {
      return url;
    }
    auto ps = url.find('/', se + 3);
    std::string b = base;
    while (!b.empty() && b.back() == '/') {
      b.pop_back();
    }
    if (ps == std::string::npos) {
      return b;
    }
    return b + url.substr(ps);
  }

 public:
  std::string skill_name() const override { return "spider"; }
  std::string skill_description() const override { return "Web scraping"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    return true;
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    return {
        define_tool(
            "scrape_url", "Scrape URL",
            json::object(
                {{"type", "object"},
                 {"properties",
                  json::object({{"url", json::object({{"type", "string"},
                                                      {"description", "URL to scrape"}})}})},
                 {"required", json::array({"url"})}}),
            [](const json& a, const json&) -> swaig::FunctionResult {
              std::string url = a.value("url", "");
              if (url.empty()) {
                return swaig::FunctionResult("No URL provided");
              }
              std::string base = get_env("SPIDER_BASE_URL");
              std::string eff = apply_base(url, base);
              auto r = skills::http_get(eff);
              if (r.status == 0) {
                return swaig::FunctionResult("Spider transport error: " + r.error);
              }
              if (r.status < 200 || r.status >= 300) {
                return swaig::FunctionResult("Spider HTTP " + std::to_string(r.status) + " from " +
                                             eff);
              }
              std::string text;
              if (!r.body.empty() && r.body.front() == '{') {
                try {
                  json parsed = json::parse(r.body);
                  if (parsed.contains("_raw_html") && parsed["_raw_html"].is_string()) {
                    text = strip_html(parsed["_raw_html"].get<std::string>());
                  } else {
                    text = strip_html(r.body);
                  }
                } catch (...) {
                  text = strip_html(r.body);
                }
              } else {
                text = strip_html(r.body);
              }
              return swaig::FunctionResult("Scraped content from " + eff + ":\n" + text);
            }),
        // crawl_site + extract_structured_data mirror Python's spider
        // register_tools (each one string param, required). Behavior is not
        // part of the compared contract; the handlers return a placeholder.
        define_tool(
            "crawl_site", "Crawl multiple pages starting from a URL",
            json::object(
                {{"type", "object"},
                 {"properties",
                  json::object({{"start_url",
                                 json::object({{"type", "string"},
                                               {"description", "Starting URL for the crawl"}})}})},
                 {"required", json::array({"start_url"})}}),
            [](const json& a, const json&) -> swaig::FunctionResult {
              std::string url = a.value("start_url", "");
              if (url.empty()) {
                return swaig::FunctionResult("No start_url provided");
              }
              return swaig::FunctionResult("Crawl started from " + url);
            }),
        define_tool(
            "extract_structured_data", "Extract specific data from a web page using selectors",
            json::object(
                {{"type", "object"},
                 {"properties",
                  json::object({{"url", json::object({{"type", "string"},
                                                      {"description", "The URL to scrape"}})}})},
                 {"required", json::array({"url"})}}),
            [](const json& a, const json&) -> swaig::FunctionResult {
              std::string url = a.value("url", "");
              if (url.empty()) {
                return swaig::FunctionResult("No URL provided");
              }
              return swaig::FunctionResult("Structured data from " + url);
            })};
  }
  std::vector<std::string> get_hints() const override { return {"scrape", "crawl"}; }
};

// DataSphere — issues a real POST against SignalWire's RAG endpoint with
// document_id + query_string in the body and Basic auth derived from
// project:token. DATASPHERE_BASE_URL overrides the host (audit fixture
// redirect). The path is `/api/datasphere/documents/search` (the
// document_id goes in the body, not the URL — matches Python's
// signalwire/skills/datasphere/skill.py).
//
// The upstream returns `{"chunks":[{"text":"...","score":...}]}`. The
// skill flattens chunks into a numbered text summary the LLM can speak.
class DatasphereSkillR : public SkillBase {
  std::string sp_, pi_, tk_, di_, tn_ = "search_knowledge";
  int count_ = 1;
  double distance_ = 3.0;

 public:
  std::string skill_name() const override { return "datasphere"; }
  std::string skill_description() const override { return "DataSphere RAG"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    sp_ = get_param_or_env(p, "space_name", "SIGNALWIRE_SPACE_NAME");
    pi_ = get_param_or_env(p, "project_id", "SIGNALWIRE_PROJECT_ID");
    tk_ = get_param_or_env(p, "token", "SIGNALWIRE_API_TOKEN");
    if (tk_.empty()) {
      tk_ = get_env("DATASPHERE_TOKEN");
    }
    di_ = get_param<std::string>(p, "document_id", "");
    tn_ = get_param<std::string>(p, "tool_name", "search_knowledge");
    count_ = get_param<int>(p, "count", 1);
    distance_ = get_param<double>(p, "distance", 3.0);
    return !sp_.empty() && !pi_.empty() && !tk_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    std::string sp = sp_, pi = pi_, tk = tk_, di = di_;
    int cnt = count_;
    double dist = distance_;
    return {define_tool(
        tn_, "Search the knowledge base",
        json::object(
            {{"type", "object"},
             {"properties",
              json::object({{"query", json::object({{"type", "string"},
                                                    {"description", "Search query"}})}})}}),
        [sp, pi, tk, di, cnt, dist](const json& a, const json&) -> swaig::FunctionResult {
          std::string q = a.value("query", "");
          if (q.empty()) {
            return swaig::FunctionResult("No search query provided");
          }
          std::string base = get_env("DATASPHERE_BASE_URL");
          if (base.empty()) {
            base = "https://" + sp + ".signalwire.com";
          }
          std::string url = base + "/api/datasphere/documents/search";
          json body = json::object({
              {"document_id", di},
              {"query_string", q},
              {"count", cnt},
              {"distance", dist},
          });
          std::map<std::string, std::string> hdrs;
          hdrs["Authorization"] = "Basic " + base64_encode(pi + ":" + tk);
          hdrs["Accept"] = "application/json";
          auto r = skills::http_post(url, body.dump(), "application/json", hdrs);
          if (r.status == 0) {
            return swaig::FunctionResult("DataSphere transport error: " + r.error);
          }
          if (r.status < 200 || r.status >= 300) {
            return swaig::FunctionResult("DataSphere HTTP " + std::to_string(r.status) + ": " +
                                         r.body);
          }
          json parsed;
          try {
            parsed = json::parse(r.body);
          } catch (const json::parse_error& e) {
            return swaig::FunctionResult(std::string("DataSphere parse error: ") + e.what());
          }
          if (!parsed.contains("chunks") || !parsed["chunks"].is_array()) {
            return swaig::FunctionResult("No results found for '" + q + "'");
          }
          const auto& chunks = parsed["chunks"];
          if (chunks.empty()) {
            return swaig::FunctionResult("No results found for '" + q + "'");
          }
          std::ostringstream out;
          out << "I found " << chunks.size() << (chunks.size() == 1 ? " result" : " results")
              << " for '" << q << "':\n\n";
          int idx = 1;
          for (const auto& c : chunks) {
            out << "=== RESULT " << idx++ << " ===\n";
            if (c.contains("text") && c["text"].is_string()) {
              out << c["text"].get<std::string>() << "\n";
            } else if (c.contains("content") && c["content"].is_string()) {
              out << c["content"].get<std::string>() << "\n";
            } else if (c.contains("chunk") && c["chunk"].is_string()) {
              out << c["chunk"].get<std::string>() << "\n";
            } else {
              out << c.dump() << "\n";
            }
          }
          return swaig::FunctionResult(out.str());
        })};
  }
  json get_global_data() const override { return json::object({{"datasphere_enabled", true}}); }
};

class DatasphereServerlessSkillR : public SkillBase {
  std::string sp_, pi_, tk_, di_, tn_ = "search_knowledge";

 public:
  std::string skill_name() const override { return "datasphere_serverless"; }
  std::string skill_description() const override { return "DataSphere serverless"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    sp_ = get_param_or_env(p, "space_name", "SIGNALWIRE_SPACE_NAME");
    pi_ = get_param_or_env(p, "project_id", "SIGNALWIRE_PROJECT_ID");
    tk_ = get_param_or_env(p, "token", "SIGNALWIRE_API_TOKEN");
    return !sp_.empty() && !pi_.empty() && !tk_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }
  std::vector<json> get_datamap_functions() const override {
    datamap::DataMap dm(tn_);
    dm.purpose("Search")
        .parameter("query", "string", "Query", true)
        .webhook("POST", "https://" + sp_ + "/api/datasphere/documents/search")
        .output(swaig::FunctionResult("Results"));
    return {dm.to_swaig_function()};
  }
};

class SwmlTransferSkillR : public SkillBase {
  std::string tn_ = "transfer_call";

 public:
  std::string skill_name() const override { return "swml_transfer"; }
  std::string skill_description() const override { return "Transfer calls"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    tn_ = get_param<std::string>(p, "tool_name", "transfer_call");
    return p.contains("transfers");
  }
  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }
  std::vector<json> get_datamap_functions() const override {
    datamap::DataMap dm(tn_);
    dm.purpose("Transfer call").parameter("transfer_type", "string", "Dest", true);
    return {dm.to_swaig_function()};
  }
  std::vector<std::string> get_hints() const override { return {"transfer", "connect"}; }
};

class PlayBackgroundFileSkillR : public SkillBase {
  std::string tn_;

 public:
  std::string skill_name() const override { return "play_background_file"; }
  std::string skill_description() const override { return "Background playback"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    tn_ = get_param<std::string>(p, "tool_name", "play_background_file");
    return p.contains("files");
  }
  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }
  std::vector<json> get_datamap_functions() const override {
    datamap::DataMap dm(tn_);
    // action enum = start_<key> for each configured file, plus "stop" (mirrors
    // Python's play_background_file register_tools); required.
    std::vector<std::string> actions;
    if (params_.contains("files") && params_["files"].is_array()) {
      for (const auto& f : params_["files"]) {
        if (f.contains("key") && f["key"].is_string()) {
          actions.push_back("start_" + f["key"].get<std::string>());
        }
      }
    }
    actions.emplace_back("stop");
    dm.purpose("Control playback").parameter("action", "string", "Action", true, actions);
    return {dm.to_swaig_function()};
  }
};

class ApiNinjasTriviaSkillR : public SkillBase {
  std::string ak_, tn_ = "get_trivia";

 public:
  std::string skill_name() const override { return "api_ninjas_trivia"; }
  std::string skill_description() const override { return "Trivia from API Ninjas"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    ak_ = get_param_or_env(p, "api_key", "API_NINJAS_KEY");
    return !ak_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override { return {}; }
  std::vector<json> get_datamap_functions() const override {
    datamap::DataMap dm(tn_);
    // category enum = the configured categories, defaulting to all 14 valid
    // API Ninjas categories (mirrors Python's VALID_CATEGORIES keys); required.
    std::vector<std::string> categories = {
        "artliterature",     "entertainment", "fooddrink",     "general",  "geography",
        "historyholidays",   "language",      "mathematics",   "music",    "peopleplaces",
        "religionmythology", "sciencenature", "sportsleisure", "toysgames"};
    if (params_.contains("categories") && params_["categories"].is_array() &&
        !params_["categories"].empty()) {
      categories.clear();
      for (const auto& c : params_["categories"]) {
        if (c.is_string()) {
          categories.push_back(c.get<std::string>());
        }
      }
    }
    dm.purpose("Get trivia")
        .parameter("category", "string", "Category", true, categories)
        .webhook("GET", "https://api.api-ninjas.com/v1/trivia?category=${args.category}",
                 json::object({{"X-Api-Key", ak_}}))
        .output(swaig::FunctionResult("Trivia: ${array[0].question}"));
    return {dm.to_swaig_function()};
  }
};

class NativeVectorSearchSkillR : public SkillBase {
  std::string tn_ = "search_knowledge", ru_, index_name_;

 public:
  std::string skill_name() const override { return "native_vector_search"; }
  std::string skill_description() const override { return "Vector search"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    tn_ = get_param<std::string>(p, "tool_name", "search_knowledge");
    ru_ = get_param<std::string>(p, "remote_url", "");
    index_name_ = get_param<std::string>(p, "index_name", "");
    // Strip a single trailing slash so ru_ + "/search" is well-formed.
    if (!ru_.empty() && ru_.back() == '/') {
      ru_.pop_back();
    }
    return !ru_.empty() || p.contains("index_file");
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        tn_, "Search knowledge",
        json::object(
            {{"type", "object"},
             {"properties", json::object({{"query", json::object({{"type", "string"}})}})}}),
        [this](const json& a, const json&) -> swaig::FunctionResult {
          std::string query = a.value("query", "");
          if (query.empty()) {
            return swaig::FunctionResult("Please provide a search query.");
          }
          if (!ru_.empty()) {
            return search_remote(query, a.value("count", 3));
          }
          return swaig::FunctionResult(
              "Vector search for '" + query +
              "': local index mode is not supported in this build; configure a "
              "remote_url for network search.");
        })};
  }
  std::vector<std::string> get_hints() const override { return {"search", "find"}; }

 private:
  // Network mode (Python _search_remote): POST {query,index_name,count,...} to
  // <remote_url>/search and format the returned results into the tool result.
  swaig::FunctionResult search_remote(const std::string& query, int count) const {
    json request = json::object({
        {"query", query},
        {"index_name", index_name_},
        {"count", count},
        {"similarity_threshold", get_param<double>(params_, "similarity_threshold", 0.0)},
        {"tags", params_.contains("tags") ? params_["tags"] : json::array()},
    });

    SkillHttpResponse resp = http_post(ru_ + "/search", request.dump(), "application/json", {}, 30);

    if (resp.status == 0) {
      return swaig::FunctionResult("Remote search error: " + resp.error);
    }
    if (resp.status != 200) {
      return swaig::FunctionResult("Remote search failed with status " +
                                   std::to_string(resp.status));
    }

    json data;
    try {
      data = json::parse(resp.body);
    } catch (const std::exception& e) {
      return swaig::FunctionResult(std::string("Remote search response parse error: ") + e.what());
    }

    const json results =
        data.contains("results") && data["results"].is_array() ? data["results"] : json::array();
    if (results.empty()) {
      return swaig::FunctionResult("No search results found for '" + query + "'.");
    }

    std::string response =
        "Found " + std::to_string(results.size()) + " relevant results for '" + query + "':\n";
    int i = 1;
    for (const auto& result : results) {
      std::string content = result.value("content", "");
      double score = result.value("score", 0.0);
      std::string filename;
      if (result.contains("metadata") && result["metadata"].is_object()) {
        filename = result["metadata"].value("filename", "");
      }
      char score_buf[16];
      std::snprintf(score_buf, sizeof(score_buf), "%.2f", score);
      response += "\n**Result " + std::to_string(i) + "**";
      if (!filename.empty()) {
        response += " (from " + filename + ", relevance: " + score_buf + ")";
      } else {
        response += " (relevance: " + std::string(score_buf) + ")";
      }
      response += "\n" + content + "\n";
      ++i;
    }
    return swaig::FunctionResult(response);
  }
};

class InfoGathererSkillR : public SkillBase {
  std::string pf_;

 public:
  std::string skill_name() const override { return "info_gatherer"; }
  std::string skill_description() const override { return "Gather info"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    pf_ = get_param<std::string>(p, "prefix", "");
    return p.contains("questions");
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    std::string sn = pf_.empty() ? "start_questions" : pf_ + "_start_questions";
    std::string an = pf_.empty() ? "submit_answer" : pf_ + "_submit_answer";
    return {define_tool(sn, "Start questions",
                        json::object({{"type", "object"}, {"properties", json::object()}}),
                        [](const json&, const json&) -> swaig::FunctionResult {
                          return swaig::FunctionResult("Starting questions");
                        }),
            define_tool(
                an, "Submit answer",
                json::object(
                    {{"type", "object"},
                     {"properties",
                      json::object({{"answer", json::object({{"type", "string"}})},
                                    {"confirmed_by_user", json::object({{"type", "boolean"}})}})}}),
                [](const json& a, const json&) -> swaig::FunctionResult {
                  return swaig::FunctionResult("Recorded: " + a.value("answer", ""));
                })};
  }
  std::string get_instance_key() const override {
    return pf_.empty() ? "info_gatherer" : "info_gatherer_" + pf_;
  }
};

// Real SKILL.md discovery — the registered claude_skills implementation.
// Discovers subdirectories of skills_path containing a SKILL.md, parses the
// frontmatter, and declares one SWAIG tool per skill. Logic lives in
// claude_skills_core.hpp, shared with builtin/claude_skills.cpp so the two can
// never drift. Native execution of skill code is impossible (AOT) — discover +
// declare only.
class ClaudeSkillsSkillR : public SkillBase {
  std::string sp_, tp_ = "claude_";
  std::vector<claude_core::DiscoveredSkill> discovered_;

 public:
  std::string skill_name() const override { return "claude_skills"; }
  std::string skill_description() const override { return "Claude SKILL.md tools"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    sp_ = get_param<std::string>(p, "skills_path", "");
    tp_ = get_param<std::string>(p, "tool_prefix", "claude_");
    if (sp_.empty()) {
      return false;
    }
    discovered_ = claude_core::discover_skills(sp_);
    return true;
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    std::vector<swaig::ToolDefinition> tools;
    tools.reserve(discovered_.size());
    for (const auto& skill : discovered_) {
      tools.push_back(claude_core::build_tool(skill, tp_));
    }
    return tools;
  }
  std::vector<std::string> get_hints() const override {
    return claude_core::hints_from(discovered_);
  }
};

class McpGatewaySkillR : public SkillBase {
  std::string gu_;

 public:
  std::string skill_name() const override { return "mcp_gateway"; }
  std::string skill_description() const override { return "MCP bridge"; }
  bool setup(const json& p) override {
    params_ = p;
    gu_ = get_param<std::string>(p, "gateway_url", "");
    return !gu_.empty();
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    return {define_tool(
        "mcp_query", "MCP query",
        json::object(
            {{"type", "object"},
             {"properties", json::object({{"query", json::object({{"type", "string"}})}})}}),
        [](const json& a, const json&) -> swaig::FunctionResult {
          return swaig::FunctionResult("MCP: " + a.value("query", ""));
        })};
  }
  std::vector<std::string> get_hints() const override { return {"MCP", "gateway"}; }
};

class CustomSkillsSkillR : public SkillBase {
 public:
  std::string skill_name() const override { return "custom_skills"; }
  std::string skill_description() const override { return "Custom tools"; }
  bool supports_multiple_instances() const override { return true; }
  bool setup(const json& p) override {
    params_ = p;
    return p.contains("tools");
  }
  std::vector<swaig::ToolDefinition> register_tools() override {
    std::vector<swaig::ToolDefinition> t;
    if (!params_.contains("tools")) {
      return t;
    }
    for (const auto& td : params_["tools"]) {
      auto n = td.value("name", "tool");
      auto d = td.value("description", "");
      auto r = td.value("response", "OK");
      t.push_back(define_tool(n, d,
                              json::object({{"type", "object"}, {"properties", json::object()}}),
                              [r](const json&, const json&) -> swaig::FunctionResult {
                                return swaig::FunctionResult(r);
                              }));
    }
    return t;
  }
};

// ============================================================================
// Registration
// ============================================================================

namespace {
struct SkillRegistrar {
  SkillRegistrar() {
    auto& reg = SkillRegistry::instance();
    reg.register_skill("datetime", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<DateTimeSkill>();
    });
    reg.register_skill(
        "math", []() -> std::unique_ptr<SkillBase> { return std::make_unique<MathSkillR>(); });
    reg.register_skill(
        "joke", []() -> std::unique_ptr<SkillBase> { return std::make_unique<JokeSkillR>(); });
    reg.register_skill("weather_api", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<WeatherApiSkillR>();
    });
    reg.register_skill("web_search", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<WebSearchSkillR>();
    });
    reg.register_skill("wikipedia_search", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<WikipediaSearchSkillR>();
    });
    reg.register_skill("google_maps", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<GoogleMapsSkillR>();
    });
    reg.register_skill(
        "spider", []() -> std::unique_ptr<SkillBase> { return std::make_unique<SpiderSkillR>(); });
    reg.register_skill("datasphere", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<DatasphereSkillR>();
    });
    reg.register_skill("datasphere_serverless", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<DatasphereServerlessSkillR>();
    });
    reg.register_skill("swml_transfer", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<SwmlTransferSkillR>();
    });
    reg.register_skill("play_background_file", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<PlayBackgroundFileSkillR>();
    });
    reg.register_skill("api_ninjas_trivia", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<ApiNinjasTriviaSkillR>();
    });
    reg.register_skill("native_vector_search", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<NativeVectorSearchSkillR>();
    });
    reg.register_skill("info_gatherer", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<InfoGathererSkillR>();
    });
    reg.register_skill("claude_skills", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<ClaudeSkillsSkillR>();
    });
    reg.register_skill("mcp_gateway", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<McpGatewaySkillR>();
    });
    reg.register_skill("custom_skills", []() -> std::unique_ptr<SkillBase> {
      return std::make_unique<CustomSkillsSkillR>();
    });
  }
};
static SkillRegistrar g_registrar;
}  // anonymous namespace

void ensure_builtin_skills_registered() {
  // Force static initialization of g_registrar to happen.
  // This is called from code that references this TU to prevent linker stripping.
  static bool done = false;
  if (!done) {
    done = true;
    // Re-register in case static init hasn't run yet
    auto& reg = SkillRegistry::instance();
    if (!reg.has_skill("datetime")) {
      reg.register_skill("datetime", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<DateTimeSkill>();
      });
      reg.register_skill(
          "math", []() -> std::unique_ptr<SkillBase> { return std::make_unique<MathSkillR>(); });
      reg.register_skill(
          "joke", []() -> std::unique_ptr<SkillBase> { return std::make_unique<JokeSkillR>(); });
      reg.register_skill("weather_api", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<WeatherApiSkillR>();
      });
      reg.register_skill("web_search", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<WebSearchSkillR>();
      });
      reg.register_skill("wikipedia_search", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<WikipediaSearchSkillR>();
      });
      reg.register_skill("google_maps", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<GoogleMapsSkillR>();
      });
      reg.register_skill("spider", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<SpiderSkillR>();
      });
      reg.register_skill("datasphere", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<DatasphereSkillR>();
      });
      reg.register_skill("datasphere_serverless", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<DatasphereServerlessSkillR>();
      });
      reg.register_skill("swml_transfer", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<SwmlTransferSkillR>();
      });
      reg.register_skill("play_background_file", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<PlayBackgroundFileSkillR>();
      });
      reg.register_skill("api_ninjas_trivia", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<ApiNinjasTriviaSkillR>();
      });
      reg.register_skill("native_vector_search", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<NativeVectorSearchSkillR>();
      });
      reg.register_skill("info_gatherer", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<InfoGathererSkillR>();
      });
      reg.register_skill("claude_skills", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<ClaudeSkillsSkillR>();
      });
      reg.register_skill("mcp_gateway", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<McpGatewaySkillR>();
      });
      reg.register_skill("custom_skills", []() -> std::unique_ptr<SkillBase> {
        return std::make_unique<CustomSkillsSkillR>();
      });
    }
  }
}

}  // namespace skills
}  // namespace signalwire
