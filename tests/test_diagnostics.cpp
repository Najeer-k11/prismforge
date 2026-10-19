#include "diagnostics/Diagnostic.h"

#include <catch2/catch_test_macros.hpp>

using namespace pf;

TEST_CASE("glslang log lines carry line and column", "[diagnostics]")
{
    const auto d = parseCompilerLog(
        "ERROR: 0:12:5: 'foo' : undeclared identifier\n"
        "WARNING: 0:3: 'bar' : unused\n"
        "ERROR: 0:12:15: '' : compilation terminated\n"
        "ERROR: 1 compilation errors.  No code generated.\n",
        ShaderStage::Fragment, "glslang");
    REQUIRE(d.size() == 2);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[0].line == 12);
    CHECK(d[0].column == 5);
    CHECK(d[0].message == "'foo' : undeclared identifier");
    CHECK(d[0].stage == ShaderStage::Fragment);
    CHECK(d[0].origin == "glslang");
    CHECK(d[1].severity == Severity::Warning);
    CHECK(d[1].line == 3);
    CHECK(d[1].column == -1);
}

TEST_CASE("Mesa log format", "[diagnostics]")
{
    const auto d = parseCompilerLog("0:7(14): error: `x' undeclared\n0:9(1): warning: unused variable",
                                    ShaderStage::Vertex, "driver");
    REQUIRE(d.size() == 2);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[0].line == 7);
    CHECK(d[0].column == 14);
    CHECK(d[0].message == "`x' undeclared");
    CHECK(d[1].severity == Severity::Warning);
}

TEST_CASE("NVIDIA log format", "[diagnostics]")
{
    const auto d = parseCompilerLog("0(42) : error C1008: undefined variable \"q\"",
                                    ShaderStage::Fragment, "driver");
    REQUIRE(d.size() == 1);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[0].line == 42);
    CHECK(d[0].message == "undefined variable \"q\"");
}

TEST_CASE("Location-less and unrecognised lines are preserved", "[diagnostics]")
{
    const auto d = parseCompilerLog(
        "ERROR: Linking fragment stage: Missing entry point\n"
        "error: fragment shader input `vFoo' has no matching output\n"
        "something the driver said\n",
        std::nullopt, "driver");
    REQUIRE(d.size() == 3);
    CHECK(d[0].severity == Severity::Error);
    CHECK(d[0].line == -1);
    CHECK(d[0].message == "Linking fragment stage: Missing entry point");
    CHECK(d[1].severity == Severity::Error);
    CHECK(d[2].severity == Severity::Info);
    CHECK(d[2].message == "something the driver said");
    CHECK_FALSE(d[2].stage.has_value());
}

TEST_CASE("Empty logs produce no diagnostics", "[diagnostics]")
{
    CHECK(parseCompilerLog("", ShaderStage::Vertex, "driver").isEmpty());
    CHECK(parseCompilerLog("\n  \n", ShaderStage::Vertex, "driver").isEmpty());
    CHECK_FALSE(hasErrors({}));
}
