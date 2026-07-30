// Copyright (c) 2025 SignalWire — MIT License
//
// swmlservice_ai_sidecar.cpp
//
// Proves that signalwire::swml::Service can emit the `ai_sidecar` verb,
// register SWAIG tools the sidecar's LLM can call, and dispatch them
// end-to-end — without any AgentBase code path.
//
// The `ai_sidecar` verb runs an AI listener alongside an in-progress call
// (real-time copilot, transcription analyzer, compliance monitor, etc.).
// It is NOT an agent — it does not own the call. So the right host is
// Service, not AgentBase.
//
// Build:
//     cmake -B build && cmake --build build --target example_swmlservice_ai_sidecar
//
// Run:
//     ./build/example_swmlservice_ai_sidecar                                   # defaults
//     ./build/example_swmlservice_ai_sidecar 8080 https://my.host/sales-sidecar
//
// What this serves:
//     GET  /sales-sidecar           → SWML doc with the ai_sidecar verb
//     POST /sales-sidecar/swaig     → SWAIG tool dispatch (used by the sidecar's LLM)
//     POST /sales-sidecar/events    → ai_sidecar lifecycle / transcription events
//
// Drive the SWAIG path through the SDK CLI:
//     bin/swaig-test http://user:pass@localhost:3000/sales-sidecar --list-tools
//     bin/swaig-test http://user:pass@localhost:3000/sales-sidecar
//         --exec lookup_competitor --param competitor=ACME

#include <cstdlib>
#include <iostream>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swml/service.hpp>
#include <string>

#include "httplib.h"

using namespace signalwire;
using json = nlohmann::json;

/// Service subclass that exposes an optional /events route for ai_sidecar
/// lifecycle events. The C++ Service base class doesn't ship a
/// `register_routing_callback` helper the way the Python SWMLService does,
/// so we mount the route through the `register_additional_routes` virtual
/// hook — the same extension point AgentBase uses for /post_prompt etc.
class SalesSidecar : public swml::Service {
 public:
  SalesSidecar() = default;

 protected:
  void register_additional_routes(httplib::Server& server) override {
    // POST /<route>/events — ai_sidecar POSTs each lifecycle / transcription
    // event as JSON. Reply with 200 and any non-redirect body.
    const std::string events_path = route() + "/events";
    server.Post(events_path, [](const httplib::Request& req, httplib::Response& res) {
      json body;
      try {
        body = json::parse(req.body);
      } catch (...) {
        body = json::object();
      }
      const auto type = body.value("type", std::string{"<unknown>"});
      std::cout << "[sidecar event] type=" << type << " body=" << body.dump() << '\n';
      res.set_content("{\"ok\":true}", "application/json");
    });
  }
};

int main(int argc, char** argv) {
  int port = 3000;
  std::string public_url = "https://your-host.example.com/sales-sidecar";
  if (argc > 1) {
    int p = std::atoi(argv[1]);
    if (p > 0) {
      port = p;
    }
  }
  if (argc > 2) {
    public_url = argv[2];
  }

  SalesSidecar svc;
  svc.set_name("sales-sidecar").set_route("/sales-sidecar").set_port(port);

  // 1. Emit any SWML — including ai_sidecar. Service::add_verb accepts
  //    arbitrary verb dicts, so new platform verbs work without an SDK
  //    release. Schema: `prompt` and `lang` are required; SWAIG.defaults
  //    points the sidecar's LLM at this service's /swaig route.
  svc.answer();
  svc.add_verb("main", "ai_sidecar",
               json::object({
                   {"prompt",
                    "You are a real-time sales copilot. Listen to the call and surface "
                    "competitor pricing comparisons when relevant."},
                   {"lang", "en-US"},
                   {"direction", json::array({"remote-caller", "local-caller"})},
                   // Where the sidecar POSTs lifecycle/transcription events.
                   // Optional — drop this key if you don't need an event sink.
                   {"url", public_url + "/events"},
                   // Where the sidecar's LLM POSTs SWAIG tool calls. The Service's
                   // built-in /swaig route is what answers them. Note the UPPERCASE
                   // SWAIG key — the platform schema is case-sensitive here.
                   {"SWAIG", json::object({
                                 {"defaults", json::object({
                                                  {"web_hook_url", public_url + "/swaig"},
                                              })},
                             })},
               }));
  svc.hangup();

  // 2. Register tools the sidecar's LLM can call. Same `define_tool`
  //    you'd use on AgentBase — it lives on Service.
  svc.define_tool(swaig::ToolDefinition{
      /*name=*/"lookup_competitor",
      /*description=*/
      "Look up competitor pricing by company name. The sidecar should call "
      "this whenever the caller mentions a competitor.",
      /*parameters=*/
      json::object({
          {"type", "object"},
          {"properties",
           json::object({
               {"competitor", json::object({
                                  {"type", "string"},
                                  {"description", "The competitor's company name, e.g. 'ACME'."},
                              })},
           })},
          {"required", json::array({"competitor"})},
      }),
      /*handler=*/
      [](const json& args, const json& /*raw*/) -> swaig::FunctionResult {
        const std::string competitor = args.value("competitor", std::string{"<unknown>"});
        return swaig::FunctionResult("Pricing for " + competitor +
                                     ": $99/seat. Our equivalent "
                                     "plan is $79/seat with the same SLA.");
      },
      /*secure=*/false,
  });

  std::cout << "ai_sidecar host service\n"
            << "  URL:    http://0.0.0.0:" << svc.port() << svc.route() << "\n"
            << "  Events: http://0.0.0.0:" << svc.port() << svc.route() << "/events\n"
            << "  Auth:   set SWML_BASIC_AUTH_USER / SWML_BASIC_AUTH_PASSWORD,\n"
            << "          or watch the [INFO] log line printed by serve() for\n"
            << "          the auto-generated user / password.\n"
            << "  Tools:  ";
  for (const auto& n : svc.list_tool_names()) {
    std::cout << n << " ";
  }
  std::cout << "\n\nSWML document:\n" << svc.render_swml().dump(2) << "\n";

  svc.serve();
  return 0;
}
