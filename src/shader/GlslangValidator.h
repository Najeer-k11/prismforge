#pragma once

#include "diagnostics/Diagnostic.h"
#include "shader/ShaderTypes.h"

namespace pf {

// Front-end GLSL validation with glslang (the Khronos reference compiler).
//
// This runs without a GL context, so it is safe to call from a worker thread.
// It is a pre-check only: the GL driver remains the authority on whether a
// program can actually be used for rendering.
class GlslangValidator {
public:
    // False when PrismForge was built without glslang.
    static bool isAvailable();
    static QString versionString();

    // Parses each stage and, if all stages parse, links them to catch
    // interface mismatches. Returns an empty list for clean sources.
    static QList<Diagnostic> validate(const ShaderSources& sources);
};

} // namespace pf
