// Copyright (c) 2025 SignalWire — MIT License
//
// swmlservice_swaig_standalone.cpp
//
// Proves that signalwire::swml::Service — by itself, with NO AgentBase —
// can host SWAIG functions and serve them on its own /swaig endpoint.
//
// This is the path you take when you want a SWAIG-callable HTTP service
// that isn't an `<ai>` agent: the SWAIG verb is a generic LLM-tool surface
// and Service is the host. AgentBase is just a Service subclass that *also*
// layers in prompts, AI config, dynamic config, and token validation.
//
// Build (via the project's CMake `examples` target):
//     cmake -B build && cmake --build build --target example_swmlservice_swaig_standalone
//
// Run:
//     ./build/example_swmlservice_swaig_standalone           # default port 3000
//     ./build/example_swmlservice_swaig_standalone 8080      # custom port
//
// Exercise the endpoints (auth credentials are printed on startup):
//     curl -u user:pass http://localhost:3000/standalone        # GET SWML doc
//     curl -u user:pass http://localhost:3000/standalone/swaig
//         -H 'Content-Type: application/json'
//         -d '{"function":"lookup_competitor","argument":{"parsed":[{"competitor":"ACME"}]}}'
//
// Or drive it through the SDK's swaig-test CLI without crafting curl by hand:
//     bin/swaig-test http://user:pass@localhost:3000/standalone --list-tools
//     bin/swaig-test http://user:pass@localhost:3000/standalone
//         --exec lookup_competitor --param competitor=ACME

#include <cstdlib>
#include <iostream>
#include <signalwire/swaig/function_result.hpp>
#include <signalwire/swml/service.hpp>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

int main(int argc, char** argv) {
  int port = 3000;
  if (argc > 1) {
    port = std::atoi(argv[1]);
    if (port <= 0) port = 3000;
  }

  swml::Service svc;
  svc.set_name("standalone-swaig").set_route("/standalone").set_port(port);

  // 1. Build a minimal SWML document. Any verbs are fine — the SWAIG
  //    HTTP surface is independent of what the document contains.
  svc.answer();
  svc.hangup();

  // 2. Register a SWAIG function. `define_tool` lives on Service, not
  //    just AgentBase. The handler receives parsed arguments plus the
  //    raw POST body.
  svc.define_tool(swaig::ToolDefinition{
      /*name=*/"lookup_competitor",
      /*description=*/
      "Look up competitor pricing by company name. Use this when the user "
      "asks how a competitor's price compares to ours.",
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
        return swaig::FunctionResult(competitor + " pricing is $99/seat; we're $79/seat.");
      },
      /*secure=*/false,
  });

  std::cout << "Standalone SWAIG service\n"
            << "  URL:   http://0.0.0.0:" << svc.port() << svc.route() << "\n"
            << "  Auth:  set SWML_BASIC_AUTH_USER / SWML_BASIC_AUTH_PASSWORD,\n"
            << "         or watch the [INFO] log line printed by serve() for\n"
            << "         the auto-generated user / password.\n"
            << "  Tools: ";
  for (const auto& n : svc.list_tool_names()) std::cout << n << " ";
  std::cout << "\n\n"
            << "SWML document:\n"
            << svc.render_swml().dump(2) << "\n";

  svc.serve();
  return 0;
}
