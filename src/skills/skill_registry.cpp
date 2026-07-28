// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
//
// Built-in skill LINKAGE, not implementation.
//
// Every built-in skill is defined exactly once, in ``src/skills/builtin/<name>.cpp``,
// and self-registers there via the ``REGISTER_SKILL`` macro. This TU exists only
// to give callers a symbol they can reference to guarantee the library — and with
// it those self-registering statics — is loaded.
//
// This TU deliberately declares NO skill of its own. It once carried a second,
// parallel implementation of all 18 skills registered under the same names, which
// silently shadowed the real ones: ``register_skill`` overwrote on collision, and
// static-initialization order ACROSS translation units is unspecified in C++, so
// which implementation a caller got was decided by link order rather than by
// anything in the source.
//
// Those shadow copies were also incomplete — ``swml_transfer`` had no transfer
// expressions, ``play_background_file`` no playback expressions, ``info_gatherer``
// no gathering state, ``custom_skills`` no per-tool parameter schemas,
// ``datasphere_serverless`` no auth headers, ``weather_api`` no celsius support,
// and ``datetime`` ignored the ``timezone`` argument outright. They are gone, and
// ``register_skill`` now THROWS on a duplicate name so a shadow cannot reappear.

#include "signalwire/skills/skill_registry.hpp"

namespace signalwire {
namespace skills {

void ensure_builtin_skills_registered() {
  // Intentionally empty. The built-in skills register themselves from their own
  // translation units (``REGISTER_SKILL`` in ``src/skills/builtin/*.cpp``); all
  // this function provides is a named symbol in the library, so that a caller
  // that references it forces the library to be linked/loaded and those static
  // initializers to run. Registering anything here would be a SECOND
  // registration of a name a builtin already claimed — which now throws.
}

}  // namespace skills
}  // namespace signalwire
