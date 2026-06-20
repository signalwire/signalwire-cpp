// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#include "signalwire/contexts/contexts.hpp"

#include <algorithm>

namespace signalwire {
namespace contexts {

const std::set<std::string>& reserved_native_tool_names() {
  static const std::set<std::string> kReserved{
      "next_step",
      "change_context",
      "gather_submit",
  };
  return kReserved;
}

// ============================================================================
// GatherQuestion
// ============================================================================

GatherQuestion::GatherQuestion(const std::string& key, const std::string& question,
                               const std::string& type, bool confirm, const std::string& prompt,
                               const std::vector<std::string>& functions)
    : key_(key),
      question_(question),
      type_(type),
      confirm_(confirm),
      prompt_(prompt),
      functions_(functions) {}

json GatherQuestion::to_json() const {
  json j;
  j["key"] = key_;
  j["question"] = question_;
  j["type"] = type_;
  if (confirm_) { j["confirm"] = true;
}
  if (!prompt_.empty()) { j["prompt"] = prompt_;
}
  if (!functions_.empty()) { j["functions"] = functions_;
}
  return j;
}

// ============================================================================
// GatherInfo
// ============================================================================

GatherInfo::GatherInfo(const std::string& output_key, const std::string& completion_action,
                       const std::string& prompt)
    : output_key_(output_key), completion_action_(completion_action), prompt_(prompt) {}

GatherInfo& GatherInfo::add_question(const std::string& key, const std::string& question,
                                     const std::string& type, bool confirm,
                                     const std::string& prompt,
                                     const std::vector<std::string>& functions) {
  questions_.emplace_back(key, question, type, confirm, prompt, functions);
  return *this;
}

json GatherInfo::to_json() const {
  json j;
  if (!output_key_.empty()) { j["output_key"] = output_key_;
}
  if (!completion_action_.empty()) { j["completion_action"] = completion_action_;
}
  if (!prompt_.empty()) { j["prompt"] = prompt_;
}
  if (!questions_.empty()) {
    j["questions"] = json::array();
    for (const auto& q : questions_) {
      j["questions"].push_back(q.to_json());
    }
  }
  return j;
}

// ============================================================================
// Step
// ============================================================================

Step::Step(const std::string& name) : name_(name) {}

Step& Step::set_text(const std::string& text) {
  text_ = text;
  return *this;
}

Step& Step::add_section(const std::string& title, const std::string& body) {
  json s;
  s["title"] = title;
  s["body"] = body;
  sections_.push_back(s);
  return *this;
}

Step& Step::add_bullets(const std::string& title, const std::vector<std::string>& bullets) {
  json s;
  s["title"] = title;
  s["bullets"] = bullets;
  sections_.push_back(s);
  return *this;
}

Step& Step::set_step_criteria(const std::string& criteria) {
  step_criteria_ = criteria;
  return *this;
}

Step& Step::set_functions(const std::variant<std::string, std::vector<std::string>>& functions) {
  functions_ = functions;
  return *this;
}

Step& Step::set_valid_steps(const std::vector<std::string>& steps) {
  valid_steps_ = steps;
  return *this;
}

Step& Step::set_valid_contexts(const std::vector<std::string>& ctxs) {
  valid_contexts_ = ctxs;
  return *this;
}

Step& Step::set_end(bool end) {
  end_ = end;
  return *this;
}
Step& Step::set_skip_user_turn(bool skip) {
  skip_user_turn_ = skip;
  return *this;
}
Step& Step::set_skip_to_next_step(bool skip) {
  skip_to_next_step_ = skip;
  return *this;
}

Step& Step::set_gather_info(const std::string& output_key, const std::string& completion_action,
                            const std::string& prompt) {
  gather_info_ = GatherInfo(output_key, completion_action, prompt);
  return *this;
}

Step& Step::add_gather_question(const std::string& key, const std::string& question,
                                const std::string& type, bool confirm, const std::string& prompt,
                                const std::vector<std::string>& functions) {
  if (gather_info_) {
    gather_info_->add_question(key, question, type, confirm, prompt, functions);
  }
  return *this;
}

Step& Step::clear_sections() {
  sections_.clear();
  text_.reset();
  return *this;
}

Step& Step::set_reset_system_prompt(const std::string& sp) {
  reset_system_prompt_ = sp;
  return *this;
}
Step& Step::set_reset_user_prompt(const std::string& up) {
  reset_user_prompt_ = up;
  return *this;
}
Step& Step::set_reset_consolidate(bool c) {
  reset_consolidate_ = c;
  return *this;
}
Step& Step::set_reset_full_reset(bool fr) {
  reset_full_reset_ = fr;
  return *this;
}

std::string Step::render_text() const {
  if (text_) { return *text_;
}
  if (sections_.empty()) { return "";
}
  // Render POM sections as text
  std::string result;
  for (const auto& s : sections_) {
    if (s.contains("title")) {
      result += "## " + s["title"].get<std::string>() + "\n";
    }
    if (s.contains("body")) {
      result += s["body"].get<std::string>() + "\n";
    }
    if (s.contains("bullets")) {
      for (const auto& b : s["bullets"]) {
        result += "- " + b.get<std::string>() + "\n";
      }
    }
    result += "\n";
  }
  return result;
}

json Step::to_json() const {
  json j;
  j["name"] = name_;

  std::string text = render_text();
  if (!text.empty()) { j["text"] = text;
}

  if (!sections_.empty()) {
    j["pom"] = sections_;
  }

  if (step_criteria_) { j["step_criteria"] = *step_criteria_;
}
  if (end_) { j["end"] = true;
}
  if (skip_user_turn_) { j["skip_user_turn"] = true;
}
  if (skip_to_next_step_) { j["skip_to_next_step"] = true;
}

  if (functions_) {
    if (std::holds_alternative<std::string>(*functions_)) {
      j["functions"] = std::get<std::string>(*functions_);
    } else {
      j["functions"] = std::get<std::vector<std::string>>(*functions_);
    }
  }

  if (valid_steps_) { j["valid_steps"] = *valid_steps_;
}
  if (valid_contexts_) { j["valid_contexts"] = *valid_contexts_;
}

  if (gather_info_ && gather_info_->has_questions()) {
    j["gather_info"] = gather_info_->to_json();
  }

  json reset;
  if (reset_system_prompt_) { reset["system_prompt"] = *reset_system_prompt_;
}
  if (reset_user_prompt_) { reset["user_prompt"] = *reset_user_prompt_;
}
  if (reset_consolidate_) { reset["consolidate"] = true;
}
  if (reset_full_reset_) { reset["full_reset"] = true;
}
  if (!reset.empty()) { j["reset"] = reset;
}

  return j;
}

// ============================================================================
// Context
// ============================================================================

Context::Context(const std::string& name) : name_(name) {}

Step& Context::add_step(
    const std::string& name, const std::string& task, const std::vector<std::string>& bullets,
    const std::string& criteria,
    const std::optional<std::variant<std::string, std::vector<std::string>>>& functions,
    const std::vector<std::string>& valid_steps_val) {
  if (steps_.size() >= static_cast<size_t>(MAX_STEPS_PER_CONTEXT)) {
    throw std::runtime_error("Maximum steps per context exceeded");
  }

  Step step(name);
  if (!task.empty()) { step.add_section("Task", task);
}
  if (!bullets.empty()) { step.add_bullets("Instructions", bullets);
}
  if (!criteria.empty()) { step.set_step_criteria(criteria);
}
  if (functions) { step.set_functions(*functions);
}
  if (!valid_steps_val.empty()) { step.set_valid_steps(valid_steps_val);
}

  steps_[name] = std::move(step);
  step_order_.push_back(name);
  return steps_[name];
}

Step* Context::get_step(const std::string& name) {
  auto it = steps_.find(name);
  return it != steps_.end() ? &it->second : nullptr;
}

Context& Context::remove_step(const std::string& name) {
  steps_.erase(name);
  step_order_.erase(std::remove(step_order_.begin(), step_order_.end(), name), step_order_.end());
  return *this;
}

Context& Context::move_step(const std::string& name, int position) {
  auto it = std::find(step_order_.begin(), step_order_.end(), name);
  if (it != step_order_.end()) {
    step_order_.erase(it);
    if (position < 0 || position >= static_cast<int>(step_order_.size())) {
      step_order_.push_back(name);
    } else {
      step_order_.insert(step_order_.begin() + position, name);
    }
  }
  return *this;
}

Context& Context::set_initial_step(const std::string& step_name) {
  initial_step_ = step_name;
  return *this;
}

Context& Context::set_valid_contexts(const std::vector<std::string>& ctxs) {
  valid_contexts_ = ctxs;
  return *this;
}

Context& Context::set_valid_steps(const std::vector<std::string>& steps) {
  valid_steps_ = steps;
  return *this;
}

Context& Context::set_post_prompt(const std::string& pp) {
  post_prompt_ = pp;
  return *this;
}
Context& Context::set_system_prompt(const std::string& sp) {
  system_prompt_ = sp;
  return *this;
}
Context& Context::set_consolidate(bool c) {
  consolidate_ = c;
  return *this;
}
Context& Context::set_full_reset(bool fr) {
  full_reset_ = fr;
  return *this;
}
Context& Context::set_user_prompt(const std::string& up) {
  user_prompt_ = up;
  return *this;
}
Context& Context::set_isolated(bool isolated) {
  isolated_ = isolated;
  return *this;
}

Context& Context::set_prompt(const std::string& prompt) {
  prompt_text_ = prompt;
  return *this;
}

Context& Context::add_section(const std::string& title, const std::string& body) {
  prompt_sections_.push_back(json::object({{"title", title}, {"body", body}}));
  return *this;
}

Context& Context::add_bullets(const std::string& title, const std::vector<std::string>& bullets) {
  prompt_sections_.push_back(json::object({{"title", title}, {"bullets", bullets}}));
  return *this;
}

Context& Context::add_system_section(const std::string& title, const std::string& body) {
  system_prompt_sections_.push_back(json::object({{"title", title}, {"body", body}}));
  return *this;
}

Context& Context::add_system_bullets(const std::string& title,
                                     const std::vector<std::string>& bullets) {
  system_prompt_sections_.push_back(json::object({{"title", title}, {"bullets", bullets}}));
  return *this;
}

Context& Context::set_enter_fillers(const json& fillers) {
  enter_fillers_ = fillers;
  return *this;
}
Context& Context::set_exit_fillers(const json& fillers) {
  exit_fillers_ = fillers;
  return *this;
}

Context& Context::add_enter_filler(const std::string& lang,
                                   const std::vector<std::string>& fillers) {
  if (enter_fillers_.is_null()) { enter_fillers_ = json::object();
}
  enter_fillers_[lang] = fillers;
  return *this;
}

Context& Context::add_exit_filler(const std::string& lang,
                                  const std::vector<std::string>& fillers) {
  if (exit_fillers_.is_null()) { exit_fillers_ = json::object();
}
  exit_fillers_[lang] = fillers;
  return *this;
}

std::optional<std::string> Context::render_prompt() const {
  if (prompt_text_) { return *prompt_text_;
}
  if (prompt_sections_.empty()) { return std::nullopt;
}
  std::string result;
  for (const auto& s : prompt_sections_) {
    if (s.contains("title")) { result += "## " + s["title"].get<std::string>() + "\n";
}
    if (s.contains("body")) { result += s["body"].get<std::string>() + "\n";
}
    if (s.contains("bullets")) {
      for (const auto& b : s["bullets"]) { result += "- " + b.get<std::string>() + "\n";
}
    }
    result += "\n";
  }
  return result;
}

std::optional<std::string> Context::render_system_prompt() const {
  if (system_prompt_) { return *system_prompt_;
}
  if (system_prompt_sections_.empty()) { return std::nullopt;
}
  std::string result;
  for (const auto& s : system_prompt_sections_) {
    if (s.contains("title")) { result += "## " + s["title"].get<std::string>() + "\n";
}
    if (s.contains("body")) { result += s["body"].get<std::string>() + "\n";
}
    if (s.contains("bullets")) {
      for (const auto& b : s["bullets"]) { result += "- " + b.get<std::string>() + "\n";
}
    }
    result += "\n";
  }
  return result;
}

json Context::to_json() const {
  json j;

  auto prompt = render_prompt();
  if (prompt) { j["prompt"] = *prompt;
}

  auto sys_prompt = render_system_prompt();
  if (sys_prompt) { j["system_prompt"] = *sys_prompt;
}

  if (consolidate_) { j["consolidate"] = true;
}
  if (full_reset_) { j["full_reset"] = true;
}
  if (user_prompt_) { j["user_prompt"] = *user_prompt_;
}
  if (isolated_) { j["isolated"] = true;
}
  if (post_prompt_) { j["post_prompt"] = *post_prompt_;
}

  if (!enter_fillers_.is_null()) { j["enter_fillers"] = enter_fillers_;
}
  if (!exit_fillers_.is_null()) { j["exit_fillers"] = exit_fillers_;
}

  if (initial_step_) { j["initial_step"] = *initial_step_;
}
  if (valid_contexts_) { j["valid_contexts"] = *valid_contexts_;
}
  if (valid_steps_) { j["valid_steps"] = *valid_steps_;
}

  if (!steps_.empty()) {
    j["steps"] = json::array();
    for (const auto& name : step_order_) {
      auto it = steps_.find(name);
      if (it != steps_.end()) {
        j["steps"].push_back(it->second.to_json());
      }
    }
  }

  return j;
}

// ============================================================================
// ContextBuilder
// ============================================================================

ContextBuilder& ContextBuilder::reset() {
  contexts_.clear();
  context_order_.clear();
  return *this;
}

Context& ContextBuilder::add_context(const std::string& name) {
  if (contexts_.size() >= static_cast<size_t>(MAX_CONTEXTS)) {
    throw std::runtime_error("Maximum number of contexts exceeded");
  }
  contexts_.emplace(name, Context(name));
  context_order_.push_back(name);
  return contexts_.at(name);
}

Context* ContextBuilder::get_context(const std::string& name) {
  auto it = contexts_.find(name);
  return it != contexts_.end() ? &it->second : nullptr;
}

ContextBuilder& ContextBuilder::attach_tool_name_supplier(
    std::function<std::vector<std::string>()> supplier) {
  tool_name_supplier_ = std::move(supplier);
  return *this;
}

void ContextBuilder::validate() const {
  if (contexts_.empty()) {
    throw std::runtime_error("At least one context must be defined");
  }

  if (contexts_.size() == 1) {
    if (contexts_.begin()->first != "default") {
      throw std::runtime_error("When using a single context, it must be named 'default'");
    }
  }

  // Each context must have at least one step.
  for (const auto& [name, ctx] : contexts_) {
    if (!ctx.has_steps()) {
      throw std::runtime_error("Context '" + name + "' must have at least one step");
    }
  }

  // Validate initial_step references a real step in the context.
  for (const auto& [name, ctx] : contexts_) {
    if (ctx.initial_step().has_value()) {
      const auto& is = *ctx.initial_step();
      if (ctx.steps().find(is) == ctx.steps().end()) {
        std::vector<std::string> available;
        for (const auto& [k, _] : ctx.steps()) { available.push_back(k);
}
        std::sort(available.begin(), available.end());
        std::string avail_str = "[";
        for (std::size_t i = 0; i < available.size(); ++i) {
          if (i > 0) { avail_str += ", ";
}
          avail_str += "'" + available[i] + "'";
        }
        avail_str += "]";
        throw std::runtime_error("Context '" + name + "' has initial_step='" + is +
                                 "' but that step does not exist. Available steps: " + avail_str);
      }
    }
  }

  // Validate gather_info completion_action references.
  for (const auto& [ctx_name, ctx] : contexts_) {
    const auto& order = ctx.step_order();
    const auto& steps = ctx.steps();
    for (std::size_t i = 0; i < order.size(); ++i) {
      const auto& step_name = order[i];
      auto it = steps.find(step_name);
      if (it == steps.end()) { continue;
}
      const auto& gi_opt = it->second.gather_info();
      if (!gi_opt.has_value()) { continue;
}
      const auto& action = gi_opt->completion_action();
      if (action.empty()) { continue;
}

      if (action == "next_step") {
        if (i + 1 >= order.size()) {
          throw std::runtime_error("Step '" + step_name + "' in context '" + ctx_name +
                                   "' has gather_info completion_action='next_step' but "
                                   "it is the last step in the context. Either "
                                   "(1) add another step after '" +
                                   step_name +
                                   "', "
                                   "(2) set completion_action to the name of an "
                                   "existing step in this context to jump to it, or "
                                   "(3) leave completion_action empty (default) to "
                                   "stay in '" +
                                   step_name +
                                   "' after gathering "
                                   "completes.");
        }
      } else if (steps.find(action) == steps.end()) {
        std::vector<std::string> available;
        for (const auto& [k, _] : steps) { available.push_back(k);
}
        std::sort(available.begin(), available.end());
        std::string avail_str = "[";
        for (std::size_t j = 0; j < available.size(); ++j) {
          if (j > 0) { avail_str += ", ";
}
          avail_str += "'" + available[j] + "'";
        }
        avail_str += "]";
        throw std::runtime_error("Step '" + step_name + "' in context '" + ctx_name +
                                 "' has gather_info completion_action='" + action + "' but '" +
                                 action +
                                 "' is not a step in this context. "
                                 "Valid options: 'next_step' (advance to the next "
                                 "sequential step), empty string (stay in the current "
                                 "step), or one of " +
                                 avail_str + ".");
      }
    }
  }

  // Validate that user-defined tools do not collide with reserved native
  // tool names. The runtime auto-injects next_step / change_context /
  // gather_submit when contexts/steps are present, so user tools sharing
  // those names would never be called.
  if (tool_name_supplier_) {
    const auto& reserved = reserved_native_tool_names();
    const auto registered = tool_name_supplier_();
    std::vector<std::string> colliding;
    for (const auto& name : registered) {
      if (reserved.count(name) > 0) {
        colliding.push_back(name);
      }
    }
    if (!colliding.empty()) {
      std::sort(colliding.begin(), colliding.end());
      colliding.erase(std::unique(colliding.begin(), colliding.end()), colliding.end());

      auto format_list = [](const std::vector<std::string>& v) {
        std::string s = "[";
        for (std::size_t j = 0; j < v.size(); ++j) {
          if (j > 0) { s += ", ";
}
          s += "'" + v[j] + "'";
        }
        s += "]";
        return s;
      };
      std::vector<std::string> reserved_sorted(reserved.begin(), reserved.end());
      std::sort(reserved_sorted.begin(), reserved_sorted.end());

      throw std::runtime_error("Tool name(s) " + format_list(colliding) +
                               " collide with "
                               "reserved native tools auto-injected by contexts/steps. "
                               "The names " +
                               format_list(reserved_sorted) +
                               " are "
                               "reserved and cannot be used for user-defined SWAIG tools "
                               "when contexts/steps are in use. Rename your tool(s) to "
                               "avoid the collision.");
    }
  }
}

json ContextBuilder::to_json() const {
  json j = json::object();
  for (const auto& name : context_order_) {
    auto it = contexts_.find(name);
    if (it != contexts_.end()) {
      j[name] = it->second.to_json();
    }
  }
  return j;
}

}  // namespace contexts
}  // namespace signalwire
