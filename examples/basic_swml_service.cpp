// Copyright (c) 2025 SignalWire — MIT License
//
// basic_swml_service.cpp
//
// Demonstrates using signalwire::swml::Service directly to build and serve
// SWML documents without any AI components. Mirrors Python's
// `examples/basic_swml_service.py`. This shows the SDK's "SWML-only" lane:
// build a document with verbs (answer, play, record_call, hangup), serve
// it on an HTTP endpoint, and let the SignalWire platform drive the call.
//
// The example builds three example flows (voicemail, IVR, transfer) and
// switches between them via the FLOW env var. All three are pure SWML —
// no AI verb, no SWAIG handlers — so they match what Python's basic
// service script proves: SWMLService is independently usable.
//
// Build:
//     cmake --build build --target example_basic_swml_service
//
// Run (default = voicemail):
//     ./build/example_basic_swml_service
//     FLOW=ivr ./build/example_basic_swml_service
//     FLOW=transfer ./build/example_basic_swml_service
//
// Probe:
//     curl -u user:pass http://localhost:3000/swml

#include <cstdlib>
#include <iostream>
#include <signalwire/swml/service.hpp>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

static void build_voicemail(swml::Service& svc) {
  svc.answer();
  svc.play(json{{"url", "https://cdn.signalwire.com/voicemail/greeting.mp3"}});
  svc.record(json{
      {"format", "mp4"},
      {"stereo", true},
      {"max_length", 120},
  });
  svc.hangup();
}

static void build_ivr(swml::Service& svc) {
  svc.answer();
  svc.play(json{{"url", "https://cdn.signalwire.com/ivr/menu.mp3"}});
  svc.send_digits(json{{"digits", "1"}});  // demo placeholder
  svc.transfer(json{{"dest", "+15555555555"}});
  svc.hangup();
}

static void build_transfer(swml::Service& svc) {
  svc.answer();
  svc.play(json{{"url", "https://cdn.signalwire.com/transfer/please_hold.mp3"}});
  svc.transfer(json{{"dest", "sip:agent@example.com"}});
}

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    swml::Service svc;
    svc.set_name("basic-swml").set_route("/swml");

    const char* flow = std::getenv("FLOW");
    std::string which = flow ? flow : "voicemail";
    if (which == "ivr") {
      build_ivr(svc);
    } else if (which == "transfer") {
      build_transfer(svc);
    } else {
      build_voicemail(svc);
    }

    std::cout << "Basic SWMLService — flow: " << which << "\n";
    std::cout << "  URL:  http://0.0.0.0:" << svc.port() << svc.route() << "\n";
    std::cout << "  Auth: set SWML_BASIC_AUTH_USER / SWML_BASIC_AUTH_PASSWORD,\n"
              << "        or watch the [INFO] log line for auto-generated creds.\n\n";
    std::cout << "Document:\n" << svc.render_swml().dump(2) << "\n\n";

    svc.serve();
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
