#pragma once

#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace signalwire {

// Forward declaration for the friendship below: AgentBase is the only intended
// caller of ContextBuilder's private attach_tool_name_supplier() wiring seam.
// Declared rather than #included to keep contexts.hpp free of an agent/ include
// (agent_base.hpp already includes this header, so including it back would be
// circular).
namespace agent {
class AgentBase;
}

namespace contexts {

using json = nlohmann::json;

constexpr int MAX_CONTEXTS = 50;
constexpr int MAX_STEPS_PER_CONTEXT = 100;

/// Reserved tool names auto-injected by the runtime when contexts/steps are
/// present. User-defined SWAIG tools must not collide with these names:
///   - next_step / change_context are injected when valid_steps or
///     valid_contexts is set so the model can navigate the flow.
///   - gather_submit is injected while a step's gather_info is collecting
///     answers.
/// ContextBuilder::validate() rejects any agent that registers a user tool
/// sharing one of these names.
[[nodiscard]] const std::set<std::string>& reserved_native_tool_names();

// ============================================================================
// GatherQuestion
// ============================================================================

/// One question in a step's ``gather_info`` questionnaire.
///
/// Each question names the ``key`` its answer is stored under, the
/// ``question`` text put to the caller, and the answer ``type``. Optional
/// per-question overrides narrow the behaviour for this question only:
/// ``confirm`` reads the answer back for confirmation, ``prompt`` replaces the
/// gather-level prompt, ``functions`` limits which SWAIG tools are callable
/// while it is being answered, and ``isolated`` is tri-state — unset inherits
/// the ``GatherInfo`` default, a set value overrides it.
///
/// Immutable after construction; ``to_json`` emits only the fields that differ
/// from their default, so a bare (key, question) pair renders as a minimal
/// object.
class GatherQuestion {
 public:
  GatherQuestion(const std::string& key, const std::string& question,
                 const std::string& type = "string", bool confirm = false,
                 const std::optional<std::string>& prompt = std::nullopt,
                 const std::vector<std::string>& functions = {},
                 const std::optional<bool>& isolated = std::nullopt);

  [[nodiscard]] json to_json() const;

  // Every construction parameter is readable back here.
  [[nodiscard]] const std::string& key() const { return key_; }
  /// The question text put to the caller.
  [[nodiscard]] const std::string& question() const { return question_; }
  /// Answer type ("string" by default); emitted to SWML only when it differs
  /// from the default.
  [[nodiscard]] const std::string& type() const { return type_; }
  /// Whether the answer is read back for confirmation; emitted only when true.
  [[nodiscard]] bool confirm() const { return confirm_; }
  /// Per-question prompt override; ``nullopt`` when absent, and emitted only
  /// when set to a non-empty string.
  [[nodiscard]] const std::optional<std::string>& prompt() const { return prompt_; }
  /// SWAIG functions available while this question is being answered; emitted
  /// only when non-empty.
  [[nodiscard]] const std::vector<std::string>& functions() const { return functions_; }
  /// Tri-state; ``nullopt`` inherits the gather_info default, a set value
  /// overrides it (emitted even when false).
  [[nodiscard]] const std::optional<bool>& isolated() const { return isolated_; }

 private:
  std::string key_;
  std::string question_;
  std::string type_;
  bool confirm_;
  std::optional<std::string> prompt_;
  std::vector<std::string> functions_;
  // Tri-state: nullopt means "inherit the gather_info default".
  std::optional<bool> isolated_;
};

// ============================================================================
// GatherInfo
// ============================================================================

/// A step's structured data-collection block — the ``gather_info`` object.
///
/// Attaching one to a ``Step`` turns that step into a questionnaire: the
/// runtime injects the reserved ``gather_submit`` tool (see
/// ``reserved_native_tool_names``) and walks the caller through each
/// ``GatherQuestion`` in order, writing the collected answers under
/// ``output_key``. ``completion_action`` says what happens once every question
/// is answered, ``prompt`` supplies the gather-wide prompt each question may
/// override, and ``isolated`` is the default the questions inherit when they
/// do not set their own.
///
/// ``add_question`` appends and returns ``*this`` for fluent chaining.
class GatherInfo {
 public:
  GatherInfo(const std::optional<std::string>& output_key = std::nullopt,
             const std::optional<std::string>& completion_action = std::nullopt,
             const std::optional<std::string>& prompt = std::nullopt, bool isolated = false);

  GatherInfo& add_question(const std::string& key, const std::string& question,
                           const std::string& type = "string", bool confirm = false,
                           const std::optional<std::string>& prompt = std::nullopt,
                           const std::vector<std::string>& functions = {},
                           const std::optional<bool>& isolated = std::nullopt);

  [[nodiscard]] json to_json() const;

  [[nodiscard]] bool has_questions() const { return !questions_.empty(); }
  [[nodiscard]] const std::vector<GatherQuestion>& questions() const { return questions_; }
  [[nodiscard]] const std::optional<std::string>& completion_action() const {
    return completion_action_;
  }

 private:
  std::vector<GatherQuestion> questions_;
  std::optional<std::string> output_key_;
  std::optional<std::string> completion_action_;
  std::optional<std::string> prompt_;
  bool isolated_ = false;
};

// ============================================================================
// Step
// ============================================================================

/// One stage of a context's guided flow — the unit the runtime advances
/// through.
///
/// A step carries its own prompt (raw text via ``set_text``, or POM sections
/// via ``add_section``/``add_bullets``) and the rules for leaving it:
/// ``set_step_criteria`` states when it is complete, ``set_valid_steps`` /
/// ``set_valid_contexts`` declare where the model may navigate next (which is
/// what causes the reserved ``next_step`` / ``change_context`` tools to be
/// injected), and ``set_end`` marks it terminal for the flow.
///
/// Three behaviours are easy to get wrong and are documented on their setters:
///   * ``set_functions`` — the active tool set is INHERITED from the previous
///     step unless a step declares its own;
///   * ``set_end(true)`` — exits step mode, it does NOT end the call;
///   * ``set_gather_info`` — while a gather is running, every other tool
///     except ``gather_submit`` and the question's own ``functions`` is
///     deactivated, including the navigation tools.
///
/// All mutators return ``*this`` for fluent chaining; ``to_json`` emits the
/// step object embedded in the SWML contexts structure.
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

  /// Set which non-internal functions are callable while this step is
  /// active.
  ///
  /// IMPORTANT — inheritance behavior:
  ///   If you do NOT call this method, the step inherits whichever
  ///   function set was active on the previous step (or the previous
  ///   context's last step). The server-side runtime only resets the
  ///   active set when a step explicitly declares its `functions`
  ///   field. This is the most common source of bugs in multi-step
  ///   agents: forgetting set_functions on a later step lets the
  ///   previous step's tools leak through. Best practice is to call
  ///   set_functions explicitly on every step that should differ from
  ///   the previous one.
  ///
  /// Keep the per-step active set small: LLM tool selection accuracy
  /// degrades noticeably past ~7-8 simultaneously-active tools per
  /// call. Use per-step whitelisting to partition large tool
  /// collections.
  ///
  /// Internal functions (e.g. gather_submit, hangup hook) are ALWAYS
  /// protected and cannot be deactivated by this whitelist. The
  /// native navigation tools next_step and change_context are
  /// injected automatically when set_valid_steps / set_valid_contexts
  /// is used; they are not affected by this list.
  ///
  /// @param functions One of:
  ///   - std::vector<std::string> — whitelist of allowed names
  ///   - empty std::vector — disable all user functions
  ///   - std::string "none" — synonym for the empty vector
  Step& set_functions(const std::variant<std::string, std::vector<std::string>>& functions);

  /// Set which steps can be navigated to from this step
  Step& set_valid_steps(const std::vector<std::string>& steps);

  /// Set which contexts can be navigated to from this step
  Step& set_valid_contexts(const std::vector<std::string>& ctxs);

  /// Mark this step as terminal for the step flow.
  ///
  /// IMPORTANT: end=true does NOT end the conversation or hang up
  /// the call. It exits step mode entirely after this step executes
  /// — clearing the steps list, current step index, valid_steps, and
  /// valid_contexts. The agent keeps running, but operates only
  /// under the base system prompt and the context-level prompt; no
  /// more step instructions are injected and no more next_step tool
  /// is offered.
  ///
  /// To actually end the call, call a hangup tool or define a
  /// hangup hook.
  Step& set_end(bool end);

  /// Set whether to skip waiting for user input
  Step& set_skip_user_turn(bool skip);

  /// Set whether to auto-advance to the next step
  Step& set_skip_to_next_step(bool skip);

  /// Control what the model still sees when this step is entered.
  ///
  /// The mode applies at the moment this step is entered and governs
  /// everything that came before it. It does not affect this step's own
  /// turns, which accumulate fresh. Nothing is deleted: the call log keeps
  /// every message.
  ///
  /// @param history One of:
  ///   - "keep": clear nothing. Every prior step's instructions and
  ///     dialogue stay visible to the model.
  ///   - "default": hide the prior step instructions, keep the
  ///     user/assistant dialogue. This is the behavior when unset.
  ///   - "hide": hide the prior instructions AND pull the prior dialogue
  ///     out of the model's context. Pair with a ${step_history.*}
  ///     reference in this step's text to choose what comes back.
  ///
  /// Throws std::invalid_argument if history is not one of the three modes.
  Step& set_history(const std::string& history);

  /// Enable info gathering on this step.
  ///
  /// @param isolated Default for every question in this gather. When true,
  ///   a question is asked with the sibling Q&A hidden from the model, so it
  ///   must ask rather than derive the answer from an earlier one. A
  ///   question's own isolated overrides this. The hidden turns remain in
  ///   the call log.
  Step& set_gather_info(const std::optional<std::string>& output_key = std::nullopt,
                        const std::optional<std::string>& completion_action = std::nullopt,
                        const std::optional<std::string>& prompt = std::nullopt,
                        bool isolated = false);

  /// Add a gather question (set_gather_info must be called first).
  ///
  /// IMPORTANT — gather mode locks function access:
  ///   While the model is asking gather questions, the runtime
  ///   forcibly deactivates ALL of the step's other functions. The
  ///   only callable tools during a gather question are:
  ///
  ///     - gather_submit (the native answer-submission tool)
  ///     - Whatever names you pass in this question's `functions`
  ///       argument
  ///
  ///   next_step and change_context are also filtered out — the
  ///   model cannot navigate away until the gather completes. This
  ///   is by design: it forces a tight ask → submit → next-question
  ///   loop.
  ///
  ///   If a question needs to call out to a tool (e.g. validate an
  ///   email, geocode a ZIP), list that tool name in this question's
  ///   `functions` argument. Functions listed here are active ONLY
  ///   for this question.
  ///   Pass this question's isolated to override the gather's isolated
  ///   default: true hides the sibling Q&A while this question is asked;
  ///   false keeps it visible even in an isolated gather; nullopt (default)
  ///   inherits the gather's setting.
  Step& add_gather_question(const std::string& key, const std::string& question,
                            const std::string& type = "string", bool confirm = false,
                            const std::optional<std::string>& prompt = std::nullopt,
                            const std::vector<std::string>& functions = {},
                            const std::optional<bool>& isolated = std::nullopt);

  /// Clear all sections and text
  Step& clear_sections();

  /// Set reset parameters for context switching
  Step& set_reset_system_prompt(const std::string& sp);
  Step& set_reset_user_prompt(const std::string& up);
  Step& set_reset_consolidate(bool c);
  Step& set_reset_full_reset(bool fr);

  /// Serialize to JSON
  [[nodiscard]] json to_json() const;

  const std::string& name() const { return name_; }
  [[nodiscard]] const std::optional<std::variant<std::string, std::vector<std::string>>>&
  functions() const {
    return functions_;
  }
  [[nodiscard]] const std::optional<std::vector<std::string>>& valid_steps() const {
    return valid_steps_;
  }
  [[nodiscard]] const std::optional<std::vector<std::string>>& valid_contexts() const {
    return valid_contexts_;
  }
  [[nodiscard]] const std::optional<GatherInfo>& gather_info() const { return gather_info_; }

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

  // Visibility of everything that came before this step (nullopt = unset).
  std::optional<std::string> history_;

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

/// A named, ordered collection of ``Step``s — one mode of an agent's workflow.
///
/// A ``ContextBuilder`` owns one or more contexts and exactly one is active at
/// a time; ``set_valid_contexts`` declares which others the model may switch
/// to via the reserved ``change_context`` tool. Entering a context begins at
/// its first step unless ``set_initial_step`` names another — useful when
/// re-entry should skip a preamble.
///
/// Beyond its steps, a context carries prompt material applied for as long as
/// it is active: ``set_prompt``/``add_section``/``add_bullets`` for the
/// context prompt, ``set_system_prompt``/``add_system_section``/
/// ``add_system_bullets`` for the system prompt, ``set_post_prompt`` to
/// override the agent's summary prompt, and enter/exit fillers spoken across
/// the switch. Conversation visibility is controlled by ``set_history`` (the
/// default each step's own ``set_history`` overrides) and by
/// ``set_isolated`` — noting that a reset configuration
/// (``set_consolidate`` / ``set_full_reset``) takes precedence over the
/// isolated wipe.
///
/// Steps are keyed by name and kept in insertion order; ``add_step`` returns a
/// reference to the step for chaining, and ``move_step``/``remove_step``
/// rearrange that order. All context mutators return ``*this``.
class Context {
 public:
  Context() = default;
  explicit Context(const std::string& name);

  /// Add a new step to this context (returns reference for chaining)
  Step& add_step(const std::string& name, const std::string& task = "",
                 const std::vector<std::string>& bullets = {}, const std::string& criteria = "",
                 const std::optional<std::variant<std::string, std::vector<std::string>>>&
                     functions = std::nullopt,
                 const std::vector<std::string>& valid_steps = {});

  /// Get an existing step by name
  [[nodiscard]] Step* get_step(const std::string& name);

  /// Remove a step
  Context& remove_step(const std::string& name);

  /// Move a step to a specific position
  Context& move_step(const std::string& name, int position);

  /// Set which step the context starts on when entered.
  ///
  /// By default, a context starts on its first step (index 0). Use
  /// this to skip a preamble step on re-entry via change_context.
  Context& set_initial_step(const std::string& step_name);

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

  /// Mark this context as isolated — entering it wipes conversation
  /// history.
  ///
  /// When isolated=true and the context is entered via
  /// change_context, the runtime wipes the conversation array. The
  /// model starts fresh with only the new context's system_prompt +
  /// step instructions, with no memory of prior turns.
  ///
  /// EXCEPTION — reset overrides the wipe:
  ///   If the context also has a reset configuration (via
  ///   set_consolidate or set_full_reset), the wipe is skipped in
  ///   favor of the reset behavior. Use reset with consolidate=true
  ///   to summarize prior history into a single message instead of
  ///   dropping it entirely.
  ///
  /// Use cases: switching to a sensitive billing flow that should
  /// not see prior small-talk; handing off to a different agent
  /// persona; resetting after a long off-topic detour.
  Context& set_isolated(bool isolated);

  /// Set the default visibility mode for every step in this context.
  ///
  /// A step's own set_history overrides this. See Step::set_history for what
  /// each mode does.
  ///
  /// @param history One of "keep", "default", or "hide".
  ///
  /// Throws std::invalid_argument if history is not one of the three modes.
  Context& set_history(const std::string& history);

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
  [[nodiscard]] json to_json() const;

  const std::string& name() const { return name_; }
  [[nodiscard]] bool has_steps() const { return !steps_.empty(); }
  [[nodiscard]] const std::map<std::string, Step>& steps() const { return steps_; }
  [[nodiscard]] const std::vector<std::string>& step_order() const { return step_order_; }
  [[nodiscard]] const std::optional<std::string>& initial_step() const { return initial_step_; }
  [[nodiscard]] const std::optional<std::vector<std::string>>& valid_contexts() const {
    return valid_contexts_;
  }

 private:
  std::optional<std::string> render_prompt() const;
  std::optional<std::string> render_system_prompt() const;

  std::string name_;
  std::map<std::string, Step> steps_;
  std::vector<std::string> step_order_;
  std::optional<std::string> initial_step_;
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

  // Default visibility mode for the steps in this context (nullopt = unset).
  std::optional<std::string> history_;
};

// ============================================================================
// ContextBuilder
// ============================================================================

/// Builder for multi-step, multi-context AI agent workflows.
///
/// A ContextBuilder owns one or more Contexts; each Context owns an ordered
/// list of Steps. Only one context and one step is active at a time. Per
/// chat turn, the runtime injects the current step's instructions as a
/// system message, then asks the LLM for a response.
///
/// ## Native tools auto-injected by the runtime
///
/// When a step (or its enclosing context) declares valid_steps or
/// valid_contexts, the runtime auto-injects two native tools so the model
/// can navigate the flow:
///
///   - next_step(step: enum)         — present when valid_steps is set
///   - change_context(context: enum) — present when valid_contexts is set
///
/// A third native tool — gather_submit — is injected during gather_info
/// questioning. These three names are reserved: validate() rejects any
/// agent that defines a SWAIG tool with one of them. See
/// reserved_native_tool_names().
///
/// ## Function whitelisting (Step::set_functions)
///
/// Each step may declare a functions whitelist. The whitelist is applied
/// in-memory at the start of each LLM turn. CRITICALLY: if a step does NOT
/// declare a functions field, it INHERITS the previous step's active set.
/// See Step::set_functions for details and examples.
class ContextBuilder {
 public:
  ContextBuilder() = default;

  /// Remove all contexts, returning the builder to its initial state.
  ContextBuilder& reset();

  /// Add a new context
  Context& add_context(const std::string& name);

  /// Get an existing context
  [[nodiscard]] Context* get_context(const std::string& name);

  /// Validate all contexts. Checks:
  ///   - At least one context is defined
  ///   - A single context must be named "default"
  ///   - Every context has at least one step
  ///   - gather_info completion_action targets an existing step
  ///   - No user-defined SWAIG tool collides with a reserved
  ///     native name (via the attached tool-name supplier)
  void validate() const;

  /// Serialize all contexts to JSON
  [[nodiscard]] json to_json() const;

  [[nodiscard]] bool has_contexts() const { return !contexts_.empty(); }

 private:
  /// INTERNAL WIRING SEAM — deliberately NOT public API.
  ///
  /// Attaches a tool-name supplier so validate() can check user-defined SWAIG
  /// tool names against reserved_native_tool_names().
  ///
  /// The Python reference has no equivalent method: there, the agent reaches its
  /// own tool registry directly. C++ cannot, so AgentBase hands the builder a
  /// closure over list_tools() (agent_base.cpp:897) — an implementation detail
  /// of how this port wires the two together, not a capability a caller is meant
  /// to reach for. It was public by accident, which put an invented method on
  /// the audited surface and, because its std::function parameter has no
  /// vocabulary type, silently dropped the symbol from port_signatures.json.
  /// Made private 2026-07-30; behaviour is unchanged.
  ContextBuilder& attach_tool_name_supplier(std::function<std::vector<std::string>()> supplier);

  /// AgentBase::define_contexts() is the ONLY intended caller of the seam above.
  friend class ::signalwire::agent::AgentBase;

  std::map<std::string, Context> contexts_;
  std::vector<std::string> context_order_;
  std::function<std::vector<std::string>()> tool_name_supplier_;
};

}  // namespace contexts
}  // namespace signalwire
