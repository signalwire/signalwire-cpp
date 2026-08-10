// Copyright (c) 2025 SignalWire — MIT License
// Webhook-based DataSphere skill from environment variables.
// Compare with datasphere_serverless_env.cpp for the serverless approach.
// Required: DATASPHERE_DOCUMENT_ID

#include <cstdlib>
#include <iostream>
#include <signalwire/agent/agent_base.hpp>
#include <stdexcept>
#include <string>

using namespace signalwire;

std::string require_env(const char* name) {
  const char* val = std::getenv(name);
  if (!val || std::string(val).empty()) {
    std::cerr << "Error: Required environment variable " << name << " is not set.\n";
    std::exit(1);
  }
  return val;
}

int main() {
  // exception-escape guard: main() must not let an exception escape
  // (that is std::terminate, with no message). Report and exit nonzero.
  try {
    std::string document_id = require_env("DATASPHERE_DOCUMENT_ID");

    int count = 3;
    if (auto v = std::getenv("DATASPHERE_COUNT")) {
      // atoi() cannot report a bad value -- it returns 0, which would
      // silently ask for zero results. Parse it properly.
      try {
        count = std::stoi(v);
      } catch (const std::exception&) {
        std::cerr << "DATASPHERE_COUNT=\"" << v << "\" is not a number; using " << count << "\n";
      }
    }

    double distance = 4.0;
    if (auto v = std::getenv("DATASPHERE_DISTANCE")) {
      try {
        distance = std::stod(v);
      } catch (const std::exception&) {
        std::cerr << "DATASPHERE_DISTANCE=\"" << v << "\" is not a number; using " << distance
                  << "\n";
      }
    }

    agent::AgentBase agent("datasphere-webhook-env", "/datasphere-webhook");

    agent.prompt_add_section(
        "Role", "You are a knowledge assistant using webhook-based DataSphere for retrieval.");

    agent.add_language({"English", "en-US", "inworld.Mark"});
    agent.set_params({{"ai_model", "gpt-4.1-nano"}});

    agent.add_skill("datetime", {});
    agent.add_skill("math", {});

    agent.add_skill("datasphere", {{"document_id", document_id},
                                   {"count", count},
                                   {"distance", distance},
                                   {"mode", "webhook"}});

    std::cout << "DataSphere Webhook Environment Demo\n";
    std::cout << "  Document: " << document_id << "\n";
    std::cout << "  Execution: Webhook-based (traditional)\n\n";
    std::cout << "  Webhook:    Full control, custom error handling\n";
    std::cout << "  Serverless: No webhooks, lower latency, executes on SignalWire\n";
    agent.run();
  } catch (const std::exception& e) {
    std::cerr << "fatal: " << e.what() << "\n";
    return 1;
  }
}
