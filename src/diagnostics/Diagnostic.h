#pragma once

#include "shader/ShaderTypes.h"

#include <QList>
#include <QString>

#include <optional>

namespace pf {

enum class Severity { Error, Warning, Info };

QString toString(Severity severity);

struct Diagnostic {
    Severity severity = Severity::Error;
    std::optional<ShaderStage> stage; // unset for program-level (link) messages
    int line = -1;                    // 1-based; -1 when the tool did not report one
    int column = -1;                  // 1-based; -1 when unknown
    QString message;
    QString origin;                   // which tool produced it, e.g. "glslang" or "driver"
    int graphNode = -1;               // shader-graph node the message refers to, if any
};

bool hasErrors(const QList<Diagnostic>& diagnostics);

// Parses a compiler or linker info log into structured diagnostics.
// Recognises glslang / AMD ("ERROR: 0:12: msg"), Mesa ("0:12(5): error: msg")
// and NVIDIA ("0(12) : error C1008: msg") formats. Lines in any other format
// are kept verbatim without a source location, so no message is ever dropped.
QList<Diagnostic> parseCompilerLog(const QString& log,
                                   std::optional<ShaderStage> stage,
                                   const QString& origin);

} // namespace pf
