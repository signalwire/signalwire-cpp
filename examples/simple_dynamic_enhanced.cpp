// Copyright (c) 2025 SignalWire — MIT License
// Enhanced dynamic agent adapting based on request parameters:
//   ?vip=true, ?department=sales, ?customer_id=X, ?language=es

#include <signalwire/agent/agent_base.hpp>
#include <algorithm>

using namespace signalwire;
using json = nlohmann::json;

static std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

int main() {
    agent::AgentBase a("dynamic-enhanced", "/dynamic-enhanced");

    a.set_dynamic_config_callback([](
        const std::map<std::string, std::string>& qp,
        const json& body,
        const std::map<std::string, std::string>& headers,
        agent::AgentBase& ephemeral) {

        (void)body; (void)headers;

        auto get = [&qp](const std::string& key, const std::string& def) {
            auto it = qp.find(key);
            return (it != qp.end()) ? it->second : def;
        };

        bool is_vip = to_lower(get("vip", "")) == "true";
        std::string department = to_lower(get("department", "general"));
        std::string customer_id = get("customer_id", "");
        std::string lang = to_lower(get("language", "en"));

        // Voice and language
        std::string voice = is_vip ? "inworld.Sarah" : "inworld.Mark";
        if (lang == "es") {
            ephemeral.add_language({"Spanish", "es-ES", voice});
        } else {
            ephemeral.add_language({"English", "en-US", voice});
        }

        // AI parameters
        ephemeral.set_params({
            {"end_of_speech_timeout", is_vip ? 300 : 500},
            {"attention_timeout", is_vip ? 20000 : 15000}
        });

        // Global data
        json global = {
            {"department", department},
            {"service_level", is_vip ? "vip" : "standard"}
        };
        if (!customer_id.empty()) global["customer_id"] = customer_id;
        ephemeral.set_global_data(global);

        // Role prompt
        std::string role = customer_id.empty()
            ? "You are a professional customer service representative."
            : "You are a customer service rep helping customer " + customer_id + ".";
        if (is_vip) role += " This is a VIP customer who receives priority service.";
        ephemeral.prompt_add_section("Role", role);

        // Department expertise
        if (department == "sales") {
            ephemeral.prompt_add_section("Sales Expertise", "You specialize in sales:", {
                "Present product features and benefits",
                "Handle pricing questions",
                "Process orders and upgrades"
            });
            ephemeral.add_hints({"pricing", "enterprise", "upgrade"});
        } else if (department == "billing") {
            ephemeral.prompt_add_section("Billing Expertise", "You specialize in billing:", {
                "Explain statements and charges",
                "Process payment arrangements",
                "Handle dispute resolution"
            });
            ephemeral.add_hints({"invoice", "payment", "charges"});
        } else {
            ephemeral.prompt_add_section("Support Guidelines", "Follow these principles:", {
                "Listen carefully to customer needs",
                "Provide accurate information",
                "Escalate complex issues when appropriate"
            });
            ephemeral.add_hints({"support", "troubleshoot", "help"});
        }

        if (is_vip) {
            ephemeral.prompt_add_section("VIP Standards", "Premium service:", {
                "Provide immediate attention",
                "Offer exclusive options",
                "Ensure complete satisfaction"
            });
        }

        // Common tool
        ephemeral.define_tool("check_order", "Check order status",
            {{"type", "object"}, {"properties", {
                {"order_number", {{"type", "string"}, {"description", "Order number"}}}
            }}},
            [](const json& args, const json& raw) -> swaig::FunctionResult {
                (void)raw;
                std::string num = args.value("order_number", "unknown");
                return swaig::FunctionResult(
                    "Order " + num + " is being processed. Ships in 2 business days.");
            });
    });

    std::cout << "Enhanced Dynamic Agent at http://0.0.0.0:3000/dynamic-enhanced\n";
    std::cout << "  ?vip=true          Premium voice + faster response\n";
    std::cout << "  ?department=sales  Sales expertise\n";
    std::cout << "  ?customer_id=X     Personalized experience\n";
    std::cout << "  ?language=es       Spanish\n";
    a.run();
}
