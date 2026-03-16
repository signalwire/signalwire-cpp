// Copyright (c) 2025 SignalWire — MIT License
// Low-level SWML Service: build SWML documents directly with verbs.

#include <signalwire/swml/service.hpp>
#include <iostream>

using namespace signalwire;

int main() {
    swml::Service svc;
    svc.set_route("/swml-service");
    svc.set_port(3000);

    // Build a simple IVR flow
    svc.answer({{"max_duration", 3600}});
    svc.play({{"url", "https://example.com/greeting.mp3"}});
    svc.ai({
        {"prompt", {{"text", "You are a helpful assistant."}}},
        {"post_prompt", {{"text", "Summarize the conversation."}}}
    });
    svc.hangup();

    // Print the document
    auto swml = svc.render_swml();
    std::cout << "SWML Document:\n" << swml.dump(2) << "\n\n";

    std::cout << "SWML Service at http://0.0.0.0:3000/swml-service\n";
    svc.serve();
}
