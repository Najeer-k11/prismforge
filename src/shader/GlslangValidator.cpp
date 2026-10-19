#include "shader/GlslangValidator.h"

#ifdef PRISMFORGE_HAVE_GLSLANG
#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/build_info.h>

#include <memory>
#include <mutex>
#endif

namespace pf {

#ifdef PRISMFORGE_HAVE_GLSLANG

namespace {

void ensureGlslangInitialized()
{
    static std::once_flag once;
    std::call_once(once, [] { glslang::InitializeProcess(); });
}

EShLanguage toGlslang(ShaderStage stage)
{
    return stage == ShaderStage::Vertex ? EShLangVertex : EShLangFragment;
}

QString infoLog(const char* log, const char* debugLog)
{
    QString text = QString::fromUtf8(log);
    const QString debug = QString::fromUtf8(debugLog).trimmed();
    if (!debug.isEmpty())
        text += u'\n' + debug;
    return text;
}

} // namespace

bool GlslangValidator::isAvailable() { return true; }

QString GlslangValidator::versionString()
{
    return QStringLiteral("%1.%2.%3")
        .arg(GLSLANG_VERSION_MAJOR)
        .arg(GLSLANG_VERSION_MINOR)
        .arg(GLSLANG_VERSION_PATCH);
}

QList<Diagnostic> GlslangValidator::validate(const ShaderSources& sources)
{
    ensureGlslangInitialized();

    const auto messages = static_cast<EShMessages>(EShMsgDefault | EShMsgDisplayErrorColumn);
    const QString origin = QStringLiteral("glslang");

    QList<Diagnostic> diagnostics;
    std::vector<std::unique_ptr<glslang::TShader>> shaders;
    bool allParsed = true;

    for (ShaderStage stage : kAllStages) {
        const QByteArray utf8 = sources.get(stage).toUtf8();
        const char* text = utf8.constData();

        auto shader = std::make_unique<glslang::TShader>(toGlslang(stage));
        shader->setStrings(&text, 1);
        // No client API is set: this validates desktop GLSL as consumed by
        // OpenGL, not GLSL-for-SPIR-V, which has stricter layout rules.
        const bool parsed = shader->parse(GetDefaultResources(), 450, ECoreProfile,
                                          false /*forceDefaultVersionAndProfile*/,
                                          false /*forwardCompatible*/, messages);
        diagnostics += parseCompilerLog(infoLog(shader->getInfoLog(), shader->getInfoDebugLog()),
                                        stage, origin);
        allParsed = allParsed && parsed;
        shaders.push_back(std::move(shader));
    }

    if (allParsed) {
        glslang::TProgram program;
        for (auto& shader : shaders)
            program.addShader(shader.get());
        if (!program.link(messages)) {
            auto linkDiagnostics = parseCompilerLog(
                infoLog(program.getInfoLog(), program.getInfoDebugLog()), std::nullopt, origin);
            // A failed link must be visible even if the log was empty.
            if (!hasErrors(linkDiagnostics))
                linkDiagnostics.append({Severity::Error, std::nullopt, -1, -1,
                                        QStringLiteral("Program link failed"), origin});
            diagnostics += linkDiagnostics;
        }
    } else if (!hasErrors(diagnostics)) {
        diagnostics.append({Severity::Error, std::nullopt, -1, -1,
                            QStringLiteral("Shader parsing failed"), origin});
    }

    // `program` must be destroyed before the shaders it references: it is
    // scoped above, and `shaders` is destroyed on return.
    return diagnostics;
}

#else // !PRISMFORGE_HAVE_GLSLANG

bool GlslangValidator::isAvailable() { return false; }
QString GlslangValidator::versionString() { return {}; }
QList<Diagnostic> GlslangValidator::validate(const ShaderSources&) { return {}; }

#endif

} // namespace pf
