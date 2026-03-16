// Copyright (c) 2025 SignalWire — MIT License
// SWML Service with routing: multiple sections and goto/label verbs.

#include <signalwire/swml/service.hpp>
#include <iostream>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    swml::Service svc;
    svc.set_route("/routed");
    svc.set_port(3000);

    // Main section: answer and route
    svc.answer();
    svc.prompt({
        {"play", "https://example.com/menu.mp3"},
        {"speech", {{"hints", json::array({"sales", "support", "billing"})}}},
        {"digits", {{"max", 1}, {"terminators", "#"}}}
    });

    // Conditional routing
    svc.cond({{
        {{"when", "prompt_value == '1' or prompt_value == 'sales'"},
         {"then", json::array({{{"goto_section", "sales"}}})}}
    }});

    // Named sections
    svc.add_verb("sales", "play", {{"url", "https://example.com/sales.mp3"}});
    svc.add_verb("sales", "ai", {{"prompt", {{"text", "You are a sales agent."}}}});
    svc.add_verb("sales", "hangup", json::object());

    auto swml = svc.render_swml();
    std::cout << "Routed SWML:\n" << swml.dump(2) << "\n\n";
    std::cout << "SWML routing at http://0.0.0.0:3000/routed\n";
    svc.serve();
}
