#include "material/MaterialParameter.h"

#include <QJsonArray>

#include <catch2/catch_test_macros.hpp>

using namespace pf;

TEST_CASE("Parameter hints are parsed from trailing comments", "[material]")
{
    const QString source = R"(
uniform float uRoughness = 0.5; // @range(0, 1) @label(Roughness)
uniform vec3 uTint = vec3(1.0);  // @color
layout(location = 3) uniform vec4 uGlow; // @colour @range(-2, 2.5e1)
uniform float uPlain;
uniform float uBadRange; // @range(1, 0)
uniform float uNotes; // just a note
)";
    const auto hints = parseParameterHints(source);

    REQUIRE(hints.contains("uRoughness"));
    CHECK(hints["uRoughness"].min == 0.0f);
    CHECK(hints["uRoughness"].max == 1.0f);
    CHECK(hints["uRoughness"].label == "Roughness");
    CHECK_FALSE(hints["uRoughness"].color);

    REQUIRE(hints.contains("uTint"));
    CHECK(hints["uTint"].color);
    CHECK_FALSE(hints["uTint"].min);

    REQUIRE(hints.contains("uGlow"));
    CHECK(hints["uGlow"].color);
    CHECK(hints["uGlow"].min == -2.0f);
    CHECK(hints["uGlow"].max == 25.0f);

    CHECK_FALSE(hints.contains("uPlain"));
    CHECK_FALSE(hints.contains("uBadRange")); // inverted range is ignored
    CHECK_FALSE(hints.contains("uNotes"));
}

TEST_CASE("Parameter values round-trip through JSON", "[material]")
{
    MaterialParameter tint{"uTint", ParamType::Vec3, {0.25f, 0.5f, 1.0f, 0.0f}, {}, {}};
    MaterialParameter on{"uEnabled", ParamType::Bool, {1.0f, 0, 0, 0}, {}, {}};
    QHash<QString, MaterialParameter> values{{tint.name, tint}, {on.name, on}};

    const auto loaded = parameterValuesFromJson(parameterValuesToJson(values));
    REQUIRE(loaded.size() == 2);
    CHECK(loaded["uTint"].type == ParamType::Vec3);
    CHECK(loaded["uTint"].value == tint.value);
    CHECK(loaded["uEnabled"].value[0] == 1.0f);
}

TEST_CASE("JSON numbers are the shortest float round-trip", "[material]")
{
    CHECK(toJsonNumber(0.05f) == 0.05);
    CHECK(toJsonNumber(0.18f) == 0.18);
    CHECK(float(toJsonNumber(1.0f / 3.0f)) == 1.0f / 3.0f);
    CHECK(float(toJsonNumber(123456.789f)) == 123456.789f);
    for (float f : {0.1f, 1e-7f, 3.14159265f, -2.5f, 65504.0f})
        CHECK(float(toJsonNumber(f)) == f);
}

TEST_CASE("Malformed parameter entries are skipped", "[material]")
{
    const QJsonObject json{
        {"uGood", QJsonObject{{"type", "float"}, {"value", QJsonArray{0.5}}}},
        {"uWrongCount", QJsonObject{{"type", "vec3"}, {"value", QJsonArray{1, 2}}}},
        {"uUnknownType", QJsonObject{{"type", "mat4"}, {"value", QJsonArray{1}}}},
    };
    const auto loaded = parameterValuesFromJson(json);
    CHECK(loaded.size() == 1);
    CHECK(loaded.contains("uGood"));
}

TEST_CASE("Built-in uniforms are recognised", "[material]")
{
    CHECK(isBuiltinUniform("uTime"));
    CHECK(isBuiltinUniform("uModel"));
    CHECK(isBuiltinUniform("uMouse"));
    CHECK(isBuiltinUniform("uChannel3"));
    CHECK(isBuiltinUniform("uChannelResolution[0]")); // arrays as named by introspection
    CHECK_FALSE(isBuiltinUniform("uTint"));
    CHECK_FALSE(isBuiltinUniform("uChannel4"));
}
