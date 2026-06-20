// Copyright (c) 2025 SignalWire
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace signalwire {
namespace relay {

/// TTS speaker gender as a typed, compile-time-checked closed set.
///
/// `Call::play_tts()` / `Call::prompt_tts()` accept this `enum class` OR a
/// `std::string` for their `gender` argument. The enum gives editor
/// autocompletion and makes a typo fail at the call site — a bare string
/// like `"femaie"` only fails at runtime, on the TTS engine. The string
/// overload keeps parity with the Python reference (which uses a bare
/// `Optional[str]`) and still allows engine-/voice-specific values that
/// aren't one of the two canonical genders.
///
///     call.play_tts("hi", "en-US", Gender::Female);  // typed, autocompleted
///     call.play_tts("hi", "en-US", "female");        // string still works
///     call.play_tts("hi", "en-US", "neutral");       // open set: engine value
///
/// Members are the two canonical `say_gender` values the calling/fabric
/// OpenAPI documents (`female` is the default; `male` is the only other
/// documented value). `tts_gender_value()` maps each member to that wire
/// string, so the enum and string overloads emit the identical TTS frame.
enum class Gender {
  Male,
  Female,
};

/// Map a `Gender` to its canonical wire string (the value emitted under the
/// TTS media params' `gender` key). This is the single normalization point
/// shared by the typed `play_tts` / `prompt_tts` overloads, so their
/// behavior is identical to passing the bare string.
inline std::string tts_gender_value(Gender gender) {
  switch (gender) {
    case Gender::Male:
      return "male";
    case Gender::Female:
      return "female";
  }
  return "";  // unreachable for a valid enumerator; keeps the compiler quiet
}

/// `to_string` overload so `Gender` interoperates with ADL-based
/// stringification the same way `tts_gender_value()` does.
inline std::string to_string(Gender gender) { return tts_gender_value(gender); }

}  // namespace relay
}  // namespace signalwire
