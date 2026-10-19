#include "shader/GlslangValidator.h"

#include <QFile>

#include <catch2/catch_test_macros.hpp>

using namespace pf;

namespace {
QString readShader(const char* name)
{
    QFile file(QStringLiteral(PF_SHADER_DIR "/") + QString::fromLatin1(name));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
}

ShaderSources defaultSources()
{
    return {readShader("default.vert"), readShader("default.frag")};
}
} // namespace

#ifdef PRISMFORGE_HAVE_GLSLANG

TEST_CASE("Built-in shaders validate cleanly", "[glslang]")
{
    REQUIRE(GlslangValidator::isAvailable());
    const auto diagnostics = GlslangValidator::validate(defaultSources());
    for (const Diagnostic& d : diagnostics)
        UNSCOPED_INFO(d.line << ": " << d.message.toStdString());
    CHECK_FALSE(hasErrors(diagnostics));
}

TEST_CASE("Syntax errors are reported with stage and line", "[glslang]")
{
    ShaderSources sources = defaultSources();
    sources.fragment = "#version 450 core\n"
                       "out vec4 fragColor;\n"
                       "void main()\n"
                       "{\n"
                       "    fragColor = vec4(undefinedThing, 1.0);\n"
                       "}\n";
    const auto diagnostics = GlslangValidator::validate(sources);
    REQUIRE(hasErrors(diagnostics));
    const Diagnostic& first = diagnostics.constFirst();
    CHECK(first.severity == Severity::Error);
    CHECK(first.stage == ShaderStage::Fragment);
    CHECK(first.line == 5);
    CHECK(first.message.contains("undefinedThing"));
}

TEST_CASE("Errors in different stages are attributed correctly", "[glslang]")
{
    ShaderSources sources = defaultSources();
    sources.vertex = "#version 450 core\nvoid main() { gl_Position = vec3(1.0); }\n";
    const auto diagnostics = GlslangValidator::validate(sources);
    REQUIRE(hasErrors(diagnostics));
    CHECK(diagnostics.constFirst().stage == ShaderStage::Vertex);
    CHECK(diagnostics.constFirst().line == 2);
}

TEST_CASE("Missing entry point is a link error", "[glslang]")
{
    ShaderSources sources = defaultSources();
    sources.fragment = "#version 450 core\nout vec4 c;\nvoid notMain() { c = vec4(1.0); }\n";
    const auto diagnostics = GlslangValidator::validate(sources);
    CHECK(hasErrors(diagnostics));
}

#else

TEST_CASE("glslang is reported unavailable", "[glslang]")
{
    CHECK_FALSE(GlslangValidator::isAvailable());
    CHECK(GlslangValidator::validate(defaultSources()).isEmpty());
}

#endif
