// Copyright (c) 2025 SignalWire — MIT License
// Auto-vivified SWML service: voicemail, IVR, and call transfer.

#include <signalwire/swml/swml_service.hpp>

using namespace signalwire;

int main() {
    // --- Voicemail Service ---
    swml::SWMLService voicemail("voicemail", "/voicemail");

    voicemail.add_answer_verb();
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
    voicemail.add_hangup_verb();

    // --- IVR Menu Service ---
    swml::SWMLService ivr("ivr", "/ivr");

    ivr.add_answer_verb();
    ivr.add_section("main_menu");
    ivr.add_verb_to_section("main_menu", "prompt", {
        {"play", "say:Press 1 for sales, 2 for support."},
        {"max_digits", 1},
        {"terminators", "#"}
    });
    ivr.add_verb("transfer", {{"dest", "main_menu"}});

    // --- Call Transfer Service ---
    swml::SWMLService transfer("transfer", "/transfer");

    transfer.add_answer_verb();
    transfer.add_verb("play", {{"url", "say:Connecting you with the next available agent."}});
    transfer.add_verb("connect", {
        {"from", "+15551234567"},
        {"timeout", 30},
        {"parallel", nlohmann::json::array({
            {{"to", "+15552223333"}},
            {{"to", "+15554445555"}}
        })}
    });
    transfer.add_verb("record", {{"format", "mp3"}, {"beep", true}, {"max_length", 120}});
    transfer.add_hangup_verb();

    std::cout << "Starting voicemail service at http://0.0.0.0:3000/voicemail\n";
    voicemail.run();
}
