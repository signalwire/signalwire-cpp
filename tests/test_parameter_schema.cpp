// ParameterSchema — typed SWAIG tool-parameter builder (Tier-2 flagship).
//
// Proves the builder is a TYPED CONVENIENCE OVER THE SAME WIRE OUTPUT as the
// hand-written nested-json `parameters` blob:
//
//   (a) BYTE-IDENTICAL: ParameterSchema{}...to_json() produces the EXACT same
//       `json` value (and the same canonical `.dump()` string) as the
//       equivalent hand-written json::object(...) — across every property kind
//       (string/number/integer/boolean/ENUM/array/nested-object) and the
//       `required` list. nlohmann::json sorts object keys, so .dump() is
//       canonical and insertion-order-independent; we assert BOTH value
//       equality and the serialised bytes.
//
//   (b) REAL define_tool path: a tool defined with builder-built parameters,
//       rendered via AgentBase::render_swml(), then INVOKED via
//       on_function_call — and the builder's parameters appear verbatim in the
//       generated SWAIG function JSON. No mocks: a real AgentBase, real render,
//       real dispatch.
//
// The builder is additive (PORT_ADDITION) — the untyped json path keeps
// working; see test_tool.cpp for the raw-json equivalents.

#include "signalwire/swaig/parameter_schema.hpp"
#include "signalwire/agent/agent_base.hpp"

using json = nlohmann::json;
namespace sw_swaig = signalwire::swaig;

// ===========================================================================
// (a) Byte-identical proofs
// ===========================================================================

// The faq_bot.cpp hand-written form: a single required string property.
// ParameterSchema{}.string("query","...").required({"query"}) must produce it
// EXACTLY — same value, same serialised bytes.
TEST(parameter_schema_byte_identical_single_string) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"query", json::object({{"type", "string"}, {"description", "Search query"}})}
    })}, {"required", json::array({"query"})}});

    json built = sw_swaig::ParameterSchema{}
        .string("query", "Search query")
        .required({"query"})
        .to_json();

    ASSERT_EQ(built, hand);                 // semantic json equality
    ASSERT_EQ(built.dump(), hand.dump());   // canonical serialised bytes
    return true;
}

// survey.cpp hand-written form: string + integer, one required.
TEST(parameter_schema_byte_identical_string_and_integer) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"answer", json::object({{"type", "string"}, {"description", "The survey answer"}})},
        {"question_index", json::object({{"type", "integer"}, {"description", "Question index"}})}
    })}, {"required", json::array({"answer"})}});

    json built = sw_swaig::ParameterSchema{}
        .string("answer", "The survey answer")
        .integer("question_index", "Question index")
        .required({"answer"})
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    return true;
}

// Insertion order must NOT matter: building "answer" then "question_index"
// vs the reverse yields the identical canonical bytes (keys are sorted).
TEST(parameter_schema_insertion_order_irrelevant) {
    json forward = sw_swaig::ParameterSchema{}
        .string("answer", "The survey answer")
        .integer("question_index", "Question index")
        .to_json();
    json reverse = sw_swaig::ParameterSchema{}
        .integer("question_index", "Question index")
        .string("answer", "The survey answer")
        .to_json();

    ASSERT_EQ(forward, reverse);
    ASSERT_EQ(forward.dump(), reverse.dump());
    // And it's the expected shape (no required list here -> key absent).
    ASSERT_FALSE(forward.contains("required"));
    ASSERT_EQ(forward["properties"].size(), 2u);
    return true;
}

// number + boolean scalar kinds, byte-identical to hand-written.
TEST(parameter_schema_byte_identical_number_and_boolean) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"amount",  json::object({{"type", "number"},  {"description", "Dollar amount"}})},
        {"confirm", json::object({{"type", "boolean"}, {"description", "Confirm?"}})}
    })}});

    json built = sw_swaig::ParameterSchema{}
        .number("amount", "Dollar amount")
        .boolean("confirm", "Confirm?")
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    // No properties marked required -> no "required" key (matches hand form).
    ASSERT_FALSE(built.contains("required"));
    return true;
}

// ENUM property (the closed-set kind): byte-identical to the hand-written
// {"type":"string","enum":[...]} form. This is the Tier-1 integration point.
TEST(parameter_schema_byte_identical_enum_property) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"fmt", json::object({
            {"type", "string"},
            {"description", "Recording format"},
            {"enum", json::array({"wav", "mp3", "mp4"})}
        })}
    })}, {"required", json::array({"fmt"})}});

    json built = sw_swaig::ParameterSchema{}
        .enum_of("fmt", {"wav", "mp3", "mp4"}, "Recording format")
        .required({"fmt"})
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    // The enum array is present with exactly the three wire values.
    ASSERT_EQ(built["properties"]["fmt"]["enum"], json::array({"wav", "mp3", "mp4"}));
    return true;
}

// enum_of fed by the Tier-1 RecordFormat enum's *_value() helpers produces the
// IDENTICAL enum list as hand-writing the wire strings — proving the typed
// enum class is the single source of truth for the schema's accepted set.
TEST(parameter_schema_enum_from_tier1_record_format) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"format", json::object({
            {"type", "string"},
            {"enum", json::array({"wav", "mp3", "mp4"})}
        })}
    })}});

    json built = sw_swaig::ParameterSchema{}
        .enum_of("format", sw_swaig::record_format_values())
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    return true;
}

// enum_of fed by the Tier-1 Codec enum — the {PCMU,PCMA} upper-case set,
// distinct from the wider RELAY codec set. Proves codec_values() == the wire.
TEST(parameter_schema_enum_from_tier1_codec) {
    json built = sw_swaig::ParameterSchema{}
        .enum_of("codec", sw_swaig::codec_values(), "Tap codec")
        .to_json();

    ASSERT_EQ(built["properties"]["codec"]["enum"], json::array({"PCMU", "PCMA"}));
    ASSERT_EQ(built["properties"]["codec"]["type"].get<std::string>(), "string");
    ASSERT_EQ(built["properties"]["codec"]["description"].get<std::string>(), "Tap codec");
    return true;
}

// The three direction/codec helpers stay distinct (the never-unify rule):
// tap uses "hear", record uses "listen"; tap codec != RELAY codecs.
TEST(parameter_schema_tier1_direction_vocabularies_distinct) {
    json rec = sw_swaig::ParameterSchema{}.enum_of("d", sw_swaig::record_direction_values()).to_json();
    json tap = sw_swaig::ParameterSchema{}.enum_of("d", sw_swaig::tap_direction_values()).to_json();

    ASSERT_EQ(rec["properties"]["d"]["enum"], json::array({"speak", "listen", "both"}));
    ASSERT_EQ(tap["properties"]["d"]["enum"], json::array({"speak", "hear", "both"}));
    ASSERT_NE(rec["properties"]["d"]["enum"], tap["properties"]["d"]["enum"]);
    return true;
}

// ARRAY of a scalar kind: byte-identical to {"type":"array","items":{"type":..}}.
TEST(parameter_schema_byte_identical_array_of_scalar) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"tags", json::object({
            {"type", "array"},
            {"description", "Tags"},
            {"items", json::object({{"type", "string"}})}
        })}
    })}});

    json built = sw_swaig::ParameterSchema{}
        .array_of("tags", "string", "Tags")
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    ASSERT_EQ(built["properties"]["tags"]["items"]["type"].get<std::string>(), "string");
    return true;
}

// ARRAY of an OBJECT (nested schema as items): byte-identical to the
// hand-written nested array-of-objects form.
TEST(parameter_schema_byte_identical_array_of_object) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"items", json::object({
            {"type", "array"},
            {"items", json::object({
                {"type", "object"},
                {"properties", json::object({
                    {"sku", json::object({{"type", "string"}, {"description", "SKU"}})},
                    {"qty", json::object({{"type", "integer"}, {"description", "Quantity"}})}
                })},
                {"required", json::array({"sku"})}
            })}
        })}
    })}});

    sw_swaig::ParameterSchema item;
    item.string("sku", "SKU").integer("qty", "Quantity").required({"sku"});

    json built = sw_swaig::ParameterSchema{}
        .array_of("items", item)
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    return true;
}

// NESTED OBJECT property: byte-identical to a hand-written object property with
// its own properties + required + a description.
TEST(parameter_schema_byte_identical_nested_object) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"address", json::object({
            {"type", "object"},
            {"description", "Shipping address"},
            {"properties", json::object({
                {"street", json::object({{"type", "string"}, {"description", "Street"}})},
                {"zip",    json::object({{"type", "string"}, {"description", "ZIP"}})}
            })},
            {"required", json::array({"street", "zip"})}
        })}
    })}, {"required", json::array({"address"})}});

    sw_swaig::ParameterSchema addr;
    addr.string("street", "Street").string("zip", "ZIP").required({"street", "zip"});

    json built = sw_swaig::ParameterSchema{}
        .object_of("address", addr, "Shipping address")
        .required({"address"})
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    return true;
}

// The kitchen-sink: every property kind in one schema, byte-identical to the
// fully hand-written equivalent, with a multi-name required list.
TEST(parameter_schema_byte_identical_all_kinds_combined) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"name",    json::object({{"type", "string"},  {"description", "Name"}})},
        {"age",     json::object({{"type", "integer"}, {"description", "Age"}})},
        {"balance", json::object({{"type", "number"},  {"description", "Balance"}})},
        {"active",  json::object({{"type", "boolean"}, {"description", "Active"}})},
        {"fmt",     json::object({{"type", "string"},  {"description", "Format"},
                                  {"enum", json::array({"wav", "mp3", "mp4"})}})},
        {"tags",    json::object({{"type", "array"},   {"description", "Tags"},
                                  {"items", json::object({{"type", "string"}})}})},
        {"meta",    json::object({{"type", "object"},  {"description", "Metadata"},
                                  {"properties", json::object({
                                      {"k", json::object({{"type", "string"}, {"description", "Key"}})}
                                  })}})}
    })}, {"required", json::array({"name", "age"})}});

    sw_swaig::ParameterSchema meta;
    meta.string("k", "Key");

    json built = sw_swaig::ParameterSchema{}
        .string("name", "Name")
        .integer("age", "Age")
        .number("balance", "Balance")
        .boolean("active", "Active")
        .enum_of("fmt", sw_swaig::record_format_values(), "Format")
        .array_of("tags", "string", "Tags")
        .object_of("meta", meta, "Metadata")
        .required({"name", "age"})
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    return true;
}

// Properties with no description omit the "description" key (matches a
// hand-written property that only sets "type") — proves we don't inject empty
// strings that would break byte-identity.
TEST(parameter_schema_no_description_omits_key) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"q", json::object({{"type", "string"}})}
    })}});

    json built = sw_swaig::ParameterSchema{}
        .string("q")
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    ASSERT_FALSE(built["properties"]["q"].contains("description"));
    return true;
}

// The raw-json escape hatch (`property`) lets a hand-built fragment ride
// alongside typed kinds — proving the builder is a strict superset of the
// hand-written path (e.g. a custom format/minimum the typed kinds don't model).
TEST(parameter_schema_raw_property_escape_hatch) {
    json hand = json::object({{"type", "object"}, {"properties", json::object({
        {"when", json::object({{"type", "string"}, {"format", "date-time"}, {"description", "Timestamp"}})},
        {"q",    json::object({{"type", "string"}, {"description", "Query"}})}
    })}});

    json built = sw_swaig::ParameterSchema{}
        .property("when", json::object({{"type", "string"}, {"format", "date-time"}, {"description", "Timestamp"}}))
        .string("q", "Query")
        .to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    return true;
}

// `require` (singular, additive) builds the same required list as `required`
// (plural, replace) — both reach the identical wire output.
TEST(parameter_schema_require_additive_equals_required) {
    json built_plural = sw_swaig::ParameterSchema{}
        .string("a").string("b")
        .required({"a", "b"})
        .to_json();
    json built_additive = sw_swaig::ParameterSchema{}
        .string("a").string("b")
        .require("a").require("b")
        .to_json();

    ASSERT_EQ(built_plural, built_additive);
    ASSERT_EQ(built_plural["required"], json::array({"a", "b"}));
    return true;
}

// Empty schema (no properties) renders {"type":"object","properties":{}},
// matching the empty hand-written form define_tool tolerates.
TEST(parameter_schema_empty_renders_object_with_empty_properties) {
    json hand = json::object({{"type", "object"}, {"properties", json::object()}});
    json built = sw_swaig::ParameterSchema{}.to_json();

    ASSERT_EQ(built, hand);
    ASSERT_EQ(built.dump(), hand.dump());
    ASSERT_TRUE(sw_swaig::ParameterSchema{}.empty());
    return true;
}

// ===========================================================================
// (b) Real define_tool + render + invoke
// ===========================================================================

// A tool defined with builder-built parameters: the parameters appear VERBATIM
// in the rendered SWAIG function JSON, and the function actually dispatches.
// No mocks — a real AgentBase render + real on_function_call.
TEST(parameter_schema_define_tool_render_and_invoke) {
    using namespace signalwire::agent;
    using signalwire::swaig::FunctionResult;

    // Build the params via the typed builder, mirroring a book_appointment tool.
    sw_swaig::ParameterSchema params;
    params.string("service", "The service to book")
          .string("date", "YYYY-MM-DD")
          .enum_of("fmt", sw_swaig::record_format_values(), "Recording format")
          .required({"service", "date"});
    json expected_params = params.to_json();

    AgentBase agent;
    agent.set_auth("u", "p");  // so web_hook_url is emitted (matches test_tool.cpp)
    // Pass the builder straight in (implicit ParameterSchema -> json conversion).
    agent.define_tool("book_appointment", "Book an appointment", params,
        [](const json& args, const json&) -> FunctionResult {
            return FunctionResult("Booked: " + args.value("service", "") +
                                  " on " + args.value("date", ""));
        });

    // --- Render and locate the function in the generated SWML.
    json swml = agent.render_swml();
    const json* found = nullptr;
    for (const auto& verb : swml["sections"]["main"]) {
        if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
            for (const auto& f : verb["ai"]["SWAIG"]["functions"]) {
                if (f.value("function", "") == "book_appointment") {
                    found = &f;
                }
            }
        }
    }
    ASSERT_NE(found, nullptr);

    // The builder's parameters appear VERBATIM in the rendered function.
    ASSERT_TRUE(found->contains("parameters"));
    ASSERT_EQ((*found)["parameters"], expected_params);
    ASSERT_EQ((*found)["parameters"].dump(), expected_params.dump());

    // Spot-check the actual schema content survived the round-trip.
    const json& rp = (*found)["parameters"];
    ASSERT_EQ(rp["type"].get<std::string>(), "object");
    ASSERT_EQ(rp["properties"]["service"]["type"].get<std::string>(), "string");
    ASSERT_EQ(rp["properties"]["service"]["description"].get<std::string>(), "The service to book");
    ASSERT_EQ(rp["properties"]["date"]["description"].get<std::string>(), "YYYY-MM-DD");
    ASSERT_EQ(rp["properties"]["fmt"]["enum"], json::array({"wav", "mp3", "mp4"}));
    ASSERT_EQ(rp["required"], json::array({"service", "date"}));

    // --- The function actually dispatches (real handler invocation).
    auto result = agent.on_function_call("book_appointment",
        json::object({{"service", "haircut"}, {"date", "2026-06-05"}}), json::object());
    ASSERT_EQ(result.to_json()["response"].get<std::string>(),
              "Booked: haircut on 2026-06-05");
    return true;
}

// Equivalence at the define_tool level: a tool defined with builder params and
// the SAME tool defined with the hand-written json blob render to the IDENTICAL
// SWAIG function JSON — the builder is a drop-in for the raw-json parameter.
TEST(parameter_schema_define_tool_matches_handwritten_render) {
    using namespace signalwire::agent;
    using signalwire::swaig::FunctionResult;

    auto handler = [](const json&, const json&) -> FunctionResult {
        return FunctionResult("ok");
    };

    json hand_params = json::object({{"type", "object"}, {"properties", json::object({
        {"query", json::object({{"type", "string"}, {"description", "Search query"}})}
    })}, {"required", json::array({"query"})}});

    AgentBase agent_hand;
    agent_hand.set_auth("u", "p");
    agent_hand.define_tool("search", "Search", hand_params, handler);

    AgentBase agent_built;
    agent_built.set_auth("u", "p");
    agent_built.define_tool("search", "Search",
        sw_swaig::ParameterSchema{}.string("query", "Search query").required({"query"}),
        handler);

    auto extract = [](const json& swml) -> json {
        for (const auto& verb : swml["sections"]["main"]) {
            if (verb.contains("ai") && verb["ai"].contains("SWAIG")) {
                for (const auto& f : verb["ai"]["SWAIG"]["functions"]) {
                    if (f.value("function", "") == "search") return f;
                }
            }
        }
        return json();
    };

    json fn_hand  = extract(agent_hand.render_swml());
    json fn_built = extract(agent_built.render_swml());

    ASSERT_FALSE(fn_hand.is_null());
    ASSERT_FALSE(fn_built.is_null());
    ASSERT_EQ(fn_built, fn_hand);
    ASSERT_EQ(fn_built.dump(), fn_hand.dump());
    return true;
}
