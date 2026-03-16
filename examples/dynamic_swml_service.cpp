// Copyright (c) 2025 SignalWire — MIT License
// Dynamic SWML service: different documents per request.

#include <signalwire/swml/service.hpp>
#include <iostream>

using namespace signalwire;
using json = nlohmann::json;

int main() {
    swml::Service svc;
    svc.set_route("/dynamic-swml");
    svc.set_port(3000);

    // Base document
    svc.answer();
    svc.ai({
        {"prompt", {{"text", "You are a helpful assistant. "
                              "Customize via query params: ?persona=sales"}}},
        {"post_prompt", {{"text", "Summarize in JSON."}}}
    });
    svc.hangup();

    std::cout << "Base SWML:\n" << svc.render_swml().dump(2) << "\n\n";
    std::cout << "Dynamic SWML Service at http://0.0.0.0:3000/dynamic-swml\n";
    svc.serve();
}
