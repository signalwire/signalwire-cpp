// Copyright (c) 2025 SignalWire — MIT License
// Auto-built SWML services: voicemail, IVR, and call transfer.

#include <signalwire/swml/service.hpp>
#include <iostream>

using namespace signalwire;

int main() {
    // --- Voicemail Service ---
    swml::Service voicemail;
    voicemail.set_route("/voicemail");

    voicemail.answer();
    voicemail.play({{"url", "say:Hello, you have reached the voicemail service. Please leave a message after the beep."}});
    voicemail.sleep(1000);
    voicemail.play({{"url", "https://example.com/beep.wav"}});
    voicemail.record({
        {"format", "mp3"},
        {"stereo", false},
        {"beep", false},
        {"max_length", 120},
        {"terminators", "#"},
        {"status_url", "https://example.com/voicemail-status"}
    });
    voicemail.play({{"url", "say:Thank you for your message. Goodbye!"}});
    voicemail.hangup();

    // --- IVR Menu Service ---
    swml::Service ivr;
    ivr.set_route("/ivr");

    ivr.answer();
    ivr.prompt({
        {"play", "say:Press 1 for sales, 2 for support."},
        {"max_digits", 1},
        {"terminators", "#"}
    });
    ivr.transfer({{"dest", "main_menu"}});

    // --- Call Transfer Service ---
    swml::Service transfer;
    transfer.set_route("/transfer");

    transfer.answer();
    transfer.play({{"url", "say:Connecting you with the next available agent."}});
    transfer.connect({
        {"from", "+15551234567"},
        {"timeout", 30},
        {"parallel", nlohmann::json::array({
            {{"to", "+15552223333"}},
            {{"to", "+15554445555"}}
        })}
    });
    transfer.record({{"format", "mp3"}, {"beep", true}, {"max_length", 120}});
    transfer.hangup();

    std::cout << "Voicemail SWML:\n" << voicemail.render_swml().dump(2) << "\n";
    std::cout << "Starting voicemail service at http://0.0.0.0:3000/voicemail\n";
    voicemail.serve();
}
