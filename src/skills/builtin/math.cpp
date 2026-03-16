// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/skills/skill_base.hpp"
#include "signalwire/skills/skill_registry.hpp"
#include <cmath>
#include <sstream>
#include <stack>
#include <cctype>
#include <stdexcept>

namespace signalwire {
namespace skills {

namespace {
// Simple safe expression evaluator (no eval/exec)
double evaluate_expression(const std::string& expr) {
    // Tokenize and evaluate using shunting-yard
    std::string cleaned;
    for (char c : expr) {
        if (!std::isspace(static_cast<unsigned char>(c))) cleaned += c;
    }
    if (cleaned.empty()) throw std::runtime_error("Empty expression");

    // Simple recursive descent parser for +, -, *, /, %, **
    struct Parser {
        const std::string& s;
        size_t pos = 0;

        double parse_expr() {
            double result = parse_term();
            while (pos < s.size() && (s[pos] == '+' || s[pos] == '-')) {
                char op = s[pos++];
                double right = parse_term();
                result = (op == '+') ? result + right : result - right;
            }
            return result;
        }

        double parse_term() {
            double result = parse_power();
            while (pos < s.size() && (s[pos] == '*' || s[pos] == '/' || s[pos] == '%')) {
                if (s[pos] == '*' && pos + 1 < s.size() && s[pos + 1] == '*') break; // ** handled in power
                char op = s[pos++];
                double right = parse_power();
                if (op == '*') result *= right;
                else if (op == '/') {
                    if (right == 0) throw std::runtime_error("Division by zero");
                    result /= right;
                } else {
                    if (right == 0) throw std::runtime_error("Modulo by zero");
                    result = std::fmod(result, right);
                }
            }
            return result;
        }

        double parse_power() {
            double base = parse_unary();
            if (pos + 1 < s.size() && s[pos] == '*' && s[pos + 1] == '*') {
                pos += 2;
                double exp = parse_power(); // right-associative
                return std::pow(base, exp);
            }
            return base;
        }

        double parse_unary() {
            if (pos < s.size() && s[pos] == '-') {
                pos++;
                return -parse_atom();
            }
            if (pos < s.size() && s[pos] == '+') {
                pos++;
            }
            return parse_atom();
        }

        double parse_atom() {
            if (pos < s.size() && s[pos] == '(') {
                pos++; // skip (
                double result = parse_expr();
                if (pos < s.size() && s[pos] == ')') pos++;
                return result;
            }
            // Number
            size_t start = pos;
            while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '.')) pos++;
            if (start == pos) throw std::runtime_error("Expected number");
            return std::stod(s.substr(start, pos - start));
        }
    };

    Parser p{cleaned};
    double result = p.parse_expr();
    if (p.pos != cleaned.size()) throw std::runtime_error("Unexpected character");
    return result;
}
} // anonymous namespace

class MathSkill : public SkillBase {
public:
    std::string skill_name() const override { return "math"; }
    std::string skill_description() const override { return "Perform basic mathematical calculations"; }
    bool supports_multiple_instances() const override { return false; }

    bool setup(const json& params) override { params_ = params; return true; }

    std::vector<swaig::ToolDefinition> register_tools() override {
        return {define_tool(
            "calculate",
            "Perform a mathematical calculation with basic operations (+, -, *, /, %, **)",
            json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"expression", json::object({
                        {"type", "string"},
                        {"description", "Mathematical expression to evaluate"}
                    })}
                })},
                {"required", json::array({"expression"})}
            }),
            [](const json& args, const json&) -> swaig::FunctionResult {
                std::string expr = args.value("expression", "");
                if (expr.empty()) {
                    return swaig::FunctionResult("No expression provided");
                }
                try {
                    double result = evaluate_expression(expr);
                    std::ostringstream oss;
                    oss << expr << " = " << result;
                    return swaig::FunctionResult(oss.str());
                } catch (const std::exception& e) {
                    return swaig::FunctionResult(std::string("Error: ") + e.what());
                }
            }
        )};
    }

    std::vector<SkillPromptSection> get_prompt_sections() const override {
        return {{
            "Mathematical Calculations",
            "",
            {"Use calculate to perform math operations", "Supports +, -, *, /, %, ** operators"}
        }};
    }
};

REGISTER_SKILL(MathSkill)

} // namespace skills
} // namespace signalwire
