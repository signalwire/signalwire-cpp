// Copyright (c) 2025 SignalWire — MIT License
// REST: bind an inbound phone number to an SWML webhook (the happy path).
//
// This is the simplest way to route a SignalWire phone number to a backend
// that returns an SWML document per inbound call. You set ``call_handler``
// on the phone number; the server auto-materializes a ``swml_webhook``
// Fabric resource pointing at your URL. You do NOT need to create the
// Fabric webhook resource manually; you do NOT call ``assign_phone_route``.
//
// Set these env vars (or pass them directly to RestClient):
//   SIGNALWIRE_PROJECT_ID   - your SignalWire project ID
//   SIGNALWIRE_API_TOKEN    - your SignalWire API token
//   SIGNALWIRE_SPACE        - your SignalWire space (e.g. example.signalwire.com)
//   PHONE_NUMBER_SID        - SID of a phone number you own (pn-...)
//   SWML_WEBHOOK_URL        - your backend's SWML endpoint

#include <signalwire/rest/rest_client.hpp>
#include <signalwire/rest/phone_call_handler.hpp>
#include <cstdlib>
#include <iostream>
#include <string>

using namespace signalwire::rest;
using json = nlohmann::json;

static std::string env_or_die(const char* key) {
    const char* v = std::getenv(key);
    if (!v || !*v) {
        std::cerr << "Missing required env var: " << key << "\n";
        std::exit(1);
    }
    return v;
}

int main() {
    try {
        const std::string pn_sid      = env_or_die("PHONE_NUMBER_SID");
        const std::string webhook_url = env_or_die("SWML_WEBHOOK_URL");

        auto client = RestClient::from_env();

        // The typed helper — one line:
        std::cout << "Binding " << pn_sid << " to " << webhook_url << " ...\n";
        client.phone_numbers().set_swml_webhook(pn_sid, {.url = webhook_url});

        // The equivalent wire-level form (use this if you need unusual fields):
        //
        // client.phone_numbers().update(pn_sid, {
        //     {"call_handler", to_wire_string(PhoneCallHandler::RelayScript)},
        //     {"call_relay_script_url", webhook_url},
        // });

        // Verify: the server auto-created a swml_webhook Fabric resource.
        auto pn = client.phone_numbers().get(pn_sid);
        std::cout << "  call_handler = " << pn.value("call_handler", "") << "\n";
        std::cout << "  call_relay_script_url = " << pn.value("call_relay_script_url", "") << "\n";
        std::cout << "  calling_handler_resource_id (server-derived) = "
                  << pn.value("calling_handler_resource_id", "") << "\n";

        // To route to something other than an SWML webhook, use:
        //   client.phone_numbers().set_cxml_webhook(sid, {.url = url})            // LAML / Twilio-compat
        //   client.phone_numbers().set_ai_agent(sid, {.agent_id = agent_id})     // AI Agent
        //   client.phone_numbers().set_call_flow(sid, {.flow_id = flow_id})      // Call Flow
        //   client.phone_numbers().set_relay_application(sid, {.name = name})    // Named RELAY app
        //   client.phone_numbers().set_relay_topic(sid, {.topic = topic})        // RELAY topic

    } catch (const SignalWireRestError& e) {
        std::cerr << "Error " << e.status() << ": " << e.what() << "\n";
        return 1;
    }
    return 0;
}
