// RecordFormat / RecordDirection / TapDirection / Codec typed-enum overloads
// for FunctionResult::record_call and FunctionResult::tap.
//
// Mirrors the SkillName / Gender proof and the Rust reference
// (signalwire-rust/src/swaig/media_enums.rs): the typed enum and the bare
// string produce the BYTE-IDENTICAL record_call / tap SWML action. The enum
// just adds call-site typo checking; the std::string overload keeps parity
// with Python's bare str (which `raise ValueError`s out-of-set values at
// runtime). Real behavior, no mocks: each test drives the actual method and
// asserts the serialized SWML via to_json().
//
// ★ Three direction vocabularies + two codec vocabularies that must NEVER be
// unified: record_call direction {speak,listen,both} vs tap direction
// {speak,hear,both} (`hear`, not `listen`) vs the wider RELAY sets; SWAIG tap
// codec {PCMU,PCMA} vs the wider RELAY connect codec superset. Each is its own
// enum — the cross-rejection tests below prove the sets stay distinct.

#include "signalwire/swaig/function_result.hpp"

using namespace signalwire::swaig;
using json = nlohmann::json;

// Pull the record_call verb params out of the SWML-wrapped action envelope.
static json record_call_params(const FunctionResult& r) {
    return r.to_json()["action"][0]["SWML"]["sections"]["main"][0]["record_call"];
}
// Pull the tap verb params out of the SWML-wrapped action envelope.
static json tap_params(const FunctionResult& r) {
    return r.to_json()["action"][0]["SWML"]["sections"]["main"][0]["tap"];
}

// ── RecordFormat {wav, mp3, mp4} ──────────────────────────────────────────

// (a) each enumerator maps to its canonical wire string; ADL to_string agrees.
TEST(record_format_enum_maps_to_wire_string) {
    ASSERT_EQ(record_format_value(RecordFormat::Wav), std::string("wav"));
    ASSERT_EQ(record_format_value(RecordFormat::Mp3), std::string("mp3"));
    ASSERT_EQ(record_format_value(RecordFormat::Mp4), std::string("mp4"));  // mp4 IS valid
    ASSERT_EQ(to_string(RecordFormat::Mp4), std::string("mp4"));
    return true;
}

// (b) the enum overload and the equivalent std::string produce the
// BYTE-IDENTICAL record_call action (drive the real method -> to_json).
// (c) every value round-trips to the wire under the format key.
TEST(record_format_enum_and_string_byte_identical_record_call) {
    for (auto fmt : {RecordFormat::Wav, RecordFormat::Mp3, RecordFormat::Mp4}) {
        const std::string wire = record_format_value(fmt);

        FunctionResult from_enum;
        from_enum.record_call("rec1", true, fmt, RecordDirection::Both);
        FunctionResult from_string;
        from_string.record_call("rec1", true, wire, "both");

        // Whole serialized result is byte-identical, not just the format key.
        ASSERT_EQ(from_enum.to_json(), from_string.to_json());
        // And the format value round-trips to the exact wire string.
        ASSERT_EQ(record_call_params(from_enum)["format"].get<std::string>(), wire);
    }
    return true;
}

// (d) an out-of-set string is still rejected by the method's existing
// validation (the enum can't express it; the string path still guards parity).
TEST(record_format_out_of_set_string_still_rejected) {
    FunctionResult r;
    ASSERT_THROWS(r.record_call("rec1", false, "ogg", "both"));
    return true;
}

// ── RecordDirection {speak, listen, both} ─────────────────────────────────

TEST(record_direction_enum_maps_to_wire_string) {
    ASSERT_EQ(record_direction_value(RecordDirection::Speak),  std::string("speak"));
    ASSERT_EQ(record_direction_value(RecordDirection::Listen), std::string("listen"));
    ASSERT_EQ(record_direction_value(RecordDirection::Both),   std::string("both"));
    ASSERT_EQ(to_string(RecordDirection::Listen), std::string("listen"));
    return true;
}

TEST(record_direction_enum_and_string_byte_identical_record_call) {
    for (auto dir : {RecordDirection::Speak, RecordDirection::Listen, RecordDirection::Both}) {
        const std::string wire = record_direction_value(dir);

        FunctionResult from_enum;
        from_enum.record_call("rec1", false, RecordFormat::Wav, dir);
        FunctionResult from_string;
        from_string.record_call("rec1", false, "wav", wire);

        ASSERT_EQ(from_enum.to_json(), from_string.to_json());
        ASSERT_EQ(record_call_params(from_enum)["direction"].get<std::string>(), wire);
    }
    return true;
}

TEST(record_direction_out_of_set_string_still_rejected) {
    FunctionResult r;
    // `hear` is valid for TAP but NOT for record_call — must be rejected here.
    ASSERT_THROWS(r.record_call("rec1", false, "wav", "hear"));
    return true;
}

// ── TapDirection {speak, hear, both} ──────────────────────────────────────

TEST(tap_direction_enum_maps_to_wire_string) {
    ASSERT_EQ(tap_direction_value(TapDirection::Speak), std::string("speak"));
    ASSERT_EQ(tap_direction_value(TapDirection::Hear),  std::string("hear"));
    ASSERT_EQ(tap_direction_value(TapDirection::Both),  std::string("both"));
    ASSERT_EQ(to_string(TapDirection::Hear), std::string("hear"));
    return true;
}

TEST(tap_direction_enum_and_string_byte_identical_tap) {
    for (auto dir : {TapDirection::Speak, TapDirection::Hear, TapDirection::Both}) {
        const std::string wire = tap_direction_value(dir);

        FunctionResult from_enum;
        from_enum.tap("wss://example.com", "t1", dir, Codec::Pcmu);
        FunctionResult from_string;
        from_string.tap("wss://example.com", "t1", wire, "PCMU");

        ASSERT_EQ(from_enum.to_json(), from_string.to_json());
        // tap only emits `direction` when != "both" (the default), so assert
        // the round-trip through whichever shape the verb takes.
        if (dir == TapDirection::Both) {
            ASSERT_FALSE(tap_params(from_enum).contains("direction"));
        } else {
            ASSERT_EQ(tap_params(from_enum)["direction"].get<std::string>(), wire);
        }
    }
    return true;
}

TEST(tap_direction_out_of_set_string_still_rejected) {
    FunctionResult r;
    // `listen` is valid for record_call but NOT for tap — must be rejected here.
    ASSERT_THROWS(r.tap("wss://example.com", "t1", "listen", "PCMU"));
    return true;
}

// ── Codec {PCMU, PCMA} (SWAIG tap only) ───────────────────────────────────

TEST(codec_enum_maps_to_wire_string) {
    ASSERT_EQ(codec_value(Codec::Pcmu), std::string("PCMU"));
    ASSERT_EQ(codec_value(Codec::Pcma), std::string("PCMA"));
    ASSERT_EQ(to_string(Codec::Pcma), std::string("PCMA"));
    return true;
}

TEST(codec_enum_and_string_byte_identical_tap) {
    for (auto codec : {Codec::Pcmu, Codec::Pcma}) {
        const std::string wire = codec_value(codec);

        FunctionResult from_enum;
        from_enum.tap("wss://example.com", "t1", TapDirection::Both, codec);
        FunctionResult from_string;
        from_string.tap("wss://example.com", "t1", "both", wire);

        ASSERT_EQ(from_enum.to_json(), from_string.to_json());
        // tap only emits `codec` when != "PCMU" (the default).
        if (codec == Codec::Pcmu) {
            ASSERT_FALSE(tap_params(from_enum).contains("codec"));
        } else {
            ASSERT_EQ(tap_params(from_enum)["codec"].get<std::string>(), wire);
        }
    }
    return true;
}

TEST(codec_out_of_set_string_still_rejected) {
    FunctionResult r;
    // Case-sensitive, mirroring the reference's literal list; "pcmu" is rejected.
    ASSERT_THROWS(r.tap("wss://example.com", "t1", "both", "pcmu"));
    ASSERT_THROWS(r.tap("wss://example.com", "t1", "both", "OPUS"));  // RELAY-only codec
    return true;
}
