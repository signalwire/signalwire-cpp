#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <variant>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace signalwire {
namespace contexts {

using json = nlohmann::json;

constexpr int MAX_CONTEXTS = 50;
constexpr int MAX_STEPS_PER_CONTEXT = 100;

// ============================================================================
// GatherQuestion
// ============================================================================

class GatherQuestion {
public:
    GatherQuestion(const std::string& key, const std::string& question,
                   const std::string& type = "string", bool confirm = false,
                   const std::string& prompt = "",
                   const std::vector<std::string>& functions = {});

    json to_json() const;

    const std::string& key() const { return key_; }

private:
    std::string key_;
    std::string question_;
    std::string type_;
    bool confirm_;
    std::string prompt_;
    std::vector<std::string> functions_;
};

// ============================================================================
// GatherInfo
// ============================================================================

class GatherInfo {
public:
    GatherInfo(const std::string& output_key = "",
               const std::string& completion_action = "",
               const std::string& prompt = "");

    GatherInfo& add_question(const std::string& key, const std::string& question,
                              const std::string& type = "string", bool confirm = false,
                              const std::string& prompt = "",
                              const std::vector<std::string>& functions = {});

    json to_json() const;

    bool has_questions() const { return !questions_.empty(); }
    const std::vector<GatherQuestion>& questions() const { return questions_; }
    const std::string& completion_action() const { return completion_action_; }

private:
    std::vector<GatherQuestion> questions_;
    std::string output_key_;
    std::string completion_action_;
    std::string prompt_;
};

// ============================================================================
// Step
// ============================================================================

class Step {
public:
    Step() = default;
    explicit Step(const std::string& name);

    /// Set the step's prompt text directly
    Step& set_text(const std::string& text);

    /// Add a POM section to the step
    Step& add_section(const std::string& title, const std::string& body);

    /// Add a POM section with bullet points
    Step& add_bullets(const std::string& title, const std::vector<std::string>& bullets);

    /// Set step completion criteria
    Step& set_step_criteria(const std::string& criteria);

    /// Set which functions are available ("none" disables all, or list of names)
    Step& set_functions(const std::variant<std::string, std::vector<std::string>>& functions);

    /// Set which steps can be navigated to from this step
    Step& set_valid_steps(const std::vector<std::string>& steps);

    /// Set which contexts can be navigated to from this step
    Step& set_valid_contexts(const std::vector<std::string>& ctxs);

    /// Set whether the conversation should end after this step
    Step& set_end(bool end);

    /// Set whether to skip waiting for user input
    Step& set_skip_user_turn(bool skip);

    /// Set whether to auto-advance to the next step
    Step& set_skip_to_next_step(bool skip);

    /// Enable info gathering on this step
    Step& set_gather_info(const std::string& output_key = "",
                           const std::string& completion_action = "",
                           const std::string& prompt = "");

    /// Add a gather question (set_gather_info must be called first)
    Step& add_gather_question(const std::string& key, const std::string& question,
                               const std::string& type = "string", bool confirm = false,
                               const std::string& prompt = "",
                               const std::vector<std::string>& functions = {});

    /// Clear all sections and text
    Step& clear_sections();

    /// Set reset parameters for context switching
    Step& set_reset_system_prompt(const std::string& sp);
    Step& set_reset_user_prompt(const std::string& up);
    Step& set_reset_consolidate(bool c);
    Step& set_reset_full_reset(bool fr);

    /// Serialize to JSON
    json to_json() const;

    const std::string& name() const { return name_; }
    const std::optional<std::vector<std::string>>& valid_steps() const { return valid_steps_; }
    const std::optional<std::vector<std::string>>& valid_contexts() const { return valid_contexts_; }
    const std::optional<GatherInfo>& gather_info() const { return gather_info_; }

private:
    std::string render_text() const;

    std::string name_;
    std::optional<std::string> text_;
    std::optional<std::string> step_criteria_;
    std::optional<std::variant<std::string, std::vector<std::string>>> functions_;
    std::optional<std::vector<std::string>> valid_steps_;
    std::optional<std::vector<std::string>> valid_contexts_;
    std::vector<json> sections_;
    std::optional<GatherInfo> gather_info_;

    bool end_ = false;
    bool skip_user_turn_ = false;
    bool skip_to_next_step_ = false;

    std::optional<std::string> reset_system_prompt_;
    std::optional<std::string> reset_user_prompt_;
    bool reset_consolidate_ = false;
    bool reset_full_reset_ = false;
};

// ============================================================================
// Context
// ============================================================================

class Context {
public:
    Context() = default;
    explicit Context(const std::string& name);

    /// Add a new step to this context (returns reference for chaining)
    Step& add_step(const std::string& name,
                   const std::string& task = "",
                   const std::vector<std::string>& bullets = {},
                   const std::string& criteria = "",
                   const std::optional<std::variant<std::string, std::vector<std::string>>>& functions = std::nullopt,
                   const std::vector<std::string>& valid_steps = {});

    /// Get an existing step by name
    Step* get_step(const std::string& name);

    /// Remove a step
    Context& remove_step(const std::string& name);

    /// Move a step to a specific position
    Context& move_step(const std::string& name, int position);

    /// Set valid contexts for navigation
    Context& set_valid_contexts(const std::vector<std::string>& ctxs);

    /// Set valid steps for all steps in this context
    Context& set_valid_steps(const std::vector<std::string>& steps);

    /// Set post prompt override
    Context& set_post_prompt(const std::string& pp);

    /// Set system prompt (for context switching)
    Context& set_system_prompt(const std::string& sp);

    /// Set consolidate
    Context& set_consolidate(bool c);

    /// Set full reset
    Context& set_full_reset(bool fr);

    /// Set user prompt
    Context& set_user_prompt(const std::string& up);

    /// Set isolated mode
    Context& set_isolated(bool isolated);

    /// Set prompt text directly
    Context& set_prompt(const std::string& prompt);

    /// Add a POM section to the context prompt
    Context& add_section(const std::string& title, const std::string& body);

    /// Add a POM section with bullets to the context prompt
    Context& add_bullets(const std::string& title, const std::vector<std::string>& bullets);

    /// Add a POM section to the system prompt
    Context& add_system_section(const std::string& title, const std::string& body);

    /// Add a POM section with bullets to the system prompt
    Context& add_system_bullets(const std::string& title, const std::vector<std::string>& bullets);

    /// Set enter fillers
    Context& set_enter_fillers(const json& fillers);

    /// Set exit fillers
    Context& set_exit_fillers(const json& fillers);

    /// Add enter filler for a specific language
    Context& add_enter_filler(const std::string& lang, const std::vector<std::string>& fillers);

    /// Add exit filler for a specific language
    Context& add_exit_filler(const std::string& lang, const std::vector<std::string>& fillers);

    /// Serialize to JSON
    json to_json() const;

    const std::string& name() const { return name_; }
    bool has_steps() const { return !steps_.empty(); }
    const std::map<std::string, Step>& steps() const { return steps_; }
    const std::vector<std::string>& step_order() const { return step_order_; }
    const std::optional<std::vector<std::string>>& valid_contexts() const { return valid_contexts_; }

private:
    std::optional<std::string> render_prompt() const;
    std::optional<std::string> render_system_prompt() const;

    std::string name_;
    std::map<std::string, Step> steps_;
    std::vector<std::string> step_order_;
    std::optional<std::vector<std::string>> valid_contexts_;
    std::optional<std::vector<std::string>> valid_steps_;

    std::optional<std::string> post_prompt_;
    std::optional<std::string> system_prompt_;
    std::vector<json> system_prompt_sections_;
    bool consolidate_ = false;
    bool full_reset_ = false;
    std::optional<std::string> user_prompt_;
    bool isolated_ = false;

    std::optional<std::string> prompt_text_;
    std::vector<json> prompt_sections_;

    json enter_fillers_;
    json exit_fillers_;
};

// ============================================================================
// ContextBuilder
// ============================================================================

class ContextBuilder {
public:
    ContextBuilder() = default;

    /// Add a new context
    Context& add_context(const std::string& name);

    /// Get an existing context
    Context* get_context(const std::string& name);

    /// Validate all contexts
    void validate() const;

    /// Serialize all contexts to JSON
    json to_json() const;

    bool has_contexts() const { return !contexts_.empty(); }

private:
    std::map<std::string, Context> contexts_;
    std::vector<std::string> context_order_;
};

} // namespace contexts
} // namespace signalwire
