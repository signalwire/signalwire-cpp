// Copyright (c) 2025 SignalWire — MIT License
// DataSphere serverless skill from environment variables.
// Required: DATASPHERE_DOCUMENT_ID
// Optional: DATASPHERE_COUNT, DATASPHERE_DISTANCE, DATASPHERE_TAGS

#include <signalwire/agent/agent_base.hpp>
#include <cstdlib>

using namespace signalwire;
using json = nlohmann::json;

std::string require_env(const char* name) {
    const char* val = std::getenv(name);
    if (!val || std::string(val).empty()) {
        std::cerr << "Error: Required environment variable " << name << " is not set.\n";
        std::exit(1);
    }
    return val;
}

int main() {
    std::string document_id = require_env("DATASPHERE_DOCUMENT_ID");

    int count = 3;
    if (auto v = std::getenv("DATASPHERE_COUNT")) count = std::atoi(v);

    double distance = 4.0;
    if (auto v = std::getenv("DATASPHERE_DISTANCE")) distance = std::atof(v);

    agent::AgentBase agent("datasphere-serverless-env", "/datasphere-env");

    agent.prompt_add_section("Role",
        "You are a knowledge assistant with access to a document library via "
        "serverless DataSphere.");

    agent.add_language({"English", "en-US", "inworld.Mark"});
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    agent.add_skill("datetime", {});
    agent.add_skill("math", {});

    json config = {
        {"document_id", document_id},
        {"count", count},
        {"distance", distance}
    };

    if (auto tags = std::getenv("DATASPHERE_TAGS")) {
        // Simple comma-split
        json tag_array = json::array();
        std::string t(tags);
        size_t pos = 0;
        while ((pos = t.find(',')) != std::string::npos) {
            tag_array.push_back(t.substr(0, pos));
            t.erase(0, pos + 1);
        }
        if (!t.empty()) tag_array.push_back(t);
        config["tags"] = tag_array;
    }

    agent.add_skill("datasphere", config);

    std::cout << "DataSphere Serverless Environment Demo\n";
    std::cout << "  Document: " << document_id << "\n";
    std::cout << "  Count: " << count << ", Distance: " << distance << "\n";
    agent.run();
}
