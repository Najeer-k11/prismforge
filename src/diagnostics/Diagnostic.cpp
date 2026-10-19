#include "diagnostics/Diagnostic.h"

#include <QRegularExpression>

#include <algorithm>

namespace pf {

QString toString(Severity severity)
{
    switch (severity) {
    case Severity::Error: return QStringLiteral("error");
    case Severity::Warning: return QStringLiteral("warning");
    case Severity::Info: return QStringLiteral("info");
    }
    return {};
}

bool hasErrors(const QList<Diagnostic>& diagnostics)
{
    return std::any_of(diagnostics.begin(), diagnostics.end(),
                       [](const Diagnostic& d) { return d.severity == Severity::Error; });
}

namespace {

Severity severityFromWord(const QString& word)
{
    const QString w = word.toLower();
    if (w.startsWith(u"warn"))
        return Severity::Warning;
    if (w.startsWith(u"err"))
        return Severity::Error;
    return Severity::Info;
}

// Summary lines that repeat information already reported per-line.
bool isSummaryLine(const QString& line)
{
    static const QRegularExpression summary(
        QStringLiteral(R"(^(ERROR|WARNING): \d+ (compilation errors|compilation warnings)\b)"));
    return summary.match(line).hasMatch() || line.contains(u"No code generated") ||
           line.endsWith(u"compilation terminated");
}

} // namespace

QList<Diagnostic> parseCompilerLog(const QString& log,
                                   std::optional<ShaderStage> stage,
                                   const QString& origin)
{
    // glslang / AMD: "ERROR: 0:12: msg" or "ERROR: 0:12:5: msg"
    static const QRegularExpression glslangRe(
        QStringLiteral(R"(^(ERROR|WARNING|INFO): \d+:(\d+):(?:(\d+):)? (.*)$)"));
    // Mesa: "0:12(5): error: msg"
    static const QRegularExpression mesaRe(
        QStringLiteral(R"(^\d+:(\d+)\((\d+)\): (error|warning|info)\w*: (.*)$)"));
    // NVIDIA: "0(12) : error C1008: msg"
    static const QRegularExpression nvidiaRe(
        QStringLiteral(R"(^\d+\((\d+)\) : (error|warning|info)\s*(?:\w+)?: (.*)$)"));
    // Location-less with a severity prefix: "ERROR: Linking fragment stage: msg"
    static const QRegularExpression prefixedRe(
        QStringLiteral(R"(^(ERROR|WARNING|error|warning)\s*:\s*(.*)$)"));

    QList<Diagnostic> out;
    const QStringList lines = log.split(u'\n');
    for (QString line : lines) {
        line = line.trimmed();
        if (line.isEmpty() || isSummaryLine(line))
            continue;

        Diagnostic d;
        d.stage = stage;
        d.origin = origin;

        if (auto m = glslangRe.match(line); m.hasMatch()) {
            d.severity = severityFromWord(m.captured(1));
            d.line = m.captured(2).toInt();
            if (!m.captured(3).isEmpty())
                d.column = m.captured(3).toInt();
            d.message = m.captured(4).trimmed();
        } else if (auto m = mesaRe.match(line); m.hasMatch()) {
            d.line = m.captured(1).toInt();
            d.column = m.captured(2).toInt();
            d.severity = severityFromWord(m.captured(3));
            d.message = m.captured(4).trimmed();
        } else if (auto m = nvidiaRe.match(line); m.hasMatch()) {
            d.line = m.captured(1).toInt();
            d.severity = severityFromWord(m.captured(2));
            d.message = m.captured(3).trimmed();
        } else if (auto m = prefixedRe.match(line); m.hasMatch()) {
            d.severity = severityFromWord(m.captured(1));
            d.message = m.captured(2).trimmed();
        } else {
            d.severity = line.contains(u"error", Qt::CaseInsensitive) ? Severity::Error
                                                                      : Severity::Info;
            d.message = line;
        }
        // Line 0 is used by some drivers for "no location".
        if (d.line == 0)
            d.line = -1;
        out.append(d);
    }
    return out;
}

} // namespace pf
