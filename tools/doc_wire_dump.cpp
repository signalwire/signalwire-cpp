// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// doc_wire_dump.cpp
//
// The DOC-WIRE (§2.1) runner. porting-sdk's scripts/doc_wire.py spawns the shared
// mock_signalwire in FLAG mode (unknown query params / body keys are journaled as
// `wire_violations` instead of 400'd), exports MOCK_SIGNALWIRE_PORT, and runs this
// program. We point a RestClient at that mock and REPLAY the REST call SHAPES the
// documentation shows (README / rest/README / rest/docs / rest/examples) — the same
// query-param maps and request bodies a reader would copy — so the mock records the
// exact wire each documented call emits. doc_wire.py then reads the journal and fails
// if any documented call sent a key the canonical spec doesn't define (e.g. the old
// `area_code` vs the spec's `areacode`).
//
// This is a WIRE-SHAPE probe, not a behavioral test: we only need each documented
// call to REACH the mock so its request is journaled. A non-2xx response (the mock's
// canned fixtures may 404 an id we invent) is fine — the request was still sent and
// journaled — so REST errors are swallowed per-call; the program only fails (non-zero
// exit) if it cannot reach the mock at all.

#include <signalwire/common.hpp>
#include <signalwire/rest/rest_client.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

using namespace signalwire;
using json = nlohmann::json;

namespace {

std::string env_or(const char* name, const std::string& fallback = "") {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : fallback;
}

// Resolve the mock base URL the gate handed us. doc_wire.py exports both
// SIGNALWIRE_MOCK_URL (full URL) and MOCK_SIGNALWIRE_PORT (port only); prefer the
// explicit URL, fall back to the port on loopback.
std::string mock_base_url() {
    std::string url = env_or("SIGNALWIRE_MOCK_URL");
    if (!url.empty()) return url;
    std::string port = env_or("MOCK_SIGNALWIRE_PORT");
    if (port.empty()) {
        std::cerr << "doc_wire_dump: neither SIGNALWIRE_MOCK_URL nor "
                     "MOCK_SIGNALWIRE_PORT is set\n";
        std::exit(2);
    }
    return "http://127.0.0.1:" + port;
}

// Send one documented call; swallow a REST error (the request is still journaled).
// The label is only for a debug trace on stderr.
template <typename Fn>
void probe(const char* label, Fn&& fn) {
    try {
        (void)fn();
    } catch (const std::exception& e) {
        std::cerr << "doc_wire_dump: [" << label << "] " << e.what()
                  << " (ignored — request was journaled)\n";
    }
}

}  // namespace

int main() {
    if (!std::getenv("SIGNALWIRE_LOG_MODE")) {
        ::setenv("SIGNALWIRE_LOG_MODE", "off", 1);
    }
    const std::string base = mock_base_url();
    rest::RestClient client =
        rest::RestClient::with_base_url(base, "doc-wire-project", "doc-wire-token");

    // ---- Phone numbers: search query params (README / rest/README / namespaces) ----
    probe("phone_numbers.search",
          [&] { return client.phone_numbers().search({{"areacode", "512"}}); });
    probe("phone_numbers.search+type", [&] {
        return client.phone_numbers().search(
            {{"areacode", "512"}, {"number_type", "local"}, {"max_results", "3"}});
    });
    probe("phone_numbers.list",
          [&] { return client.phone_numbers().list({{"name", "Main"}}); });

    // ---- Fabric AI agents: create body (rest_manage_resources / api_reference) ----
    probe("fabric.ai_agents.create", [&] {
        return client.fabric().ai_agents.create(
            {{"name", "Demo Support Bot"},
             {"prompt", {{"text", "You are a friendly support agent."}}}});
    });
    probe("fabric.ai_agents.list", [&] { return client.fabric().ai_agents.list(); });

    // ---- Calling: dial body (README quickstart / rest examples) ----
    probe("calling.dial", [&] {
        rest::generated::Calling::DialParams p;
        p.from = "+15559876543";
        p.to = "+15551234567";
        p.url = "https://example.com/handler";
        return client.calling().dial(p);
    });

    // ---- Datasphere: document search body (README / namespaces) ----
    probe("datasphere.documents.search", [&] {
        rest::generated::DatasphereDocuments::SearchParams p;
        p.query_string = "billing policy";
        p.count = 5;
        return client.datasphere().documents.search(p);
    });
    probe("datasphere.documents.create", [&] {
        return client.datasphere().documents.create(
            {{"url", "https://example.com/doc.pdf"}, {"tags", {"support"}}});
    });

    // ---- Video rooms: create body (namespaces) ----
    probe("video.rooms.create", [&] {
        return client.video().rooms.create(
            {{"name", "standup"}, {"max_members", 10}});
    });

    // ---- Queues: create body (namespaces) ----
    probe("queues.create",
          [&] { return client.queues().create({{"name", "Support"}}); });

    // ---- Registry brands: create body (namespaces 10DLC) ----
    probe("registry.brands.create", [&] {
        return client.registry().brands.create(
            {{"name", "My Brand"}, {"ein", "12-3456789"}});
    });

    std::cout << "doc_wire_dump: replayed documented REST call shapes against " << base
              << "\n";
    return 0;
}
