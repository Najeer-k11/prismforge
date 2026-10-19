#include "material/MaterialParameter.h"

#include <QJsonArray>
#include <QRegularExpression>

namespace pf {

int componentCount(ParamType type)
{
    switch (type) {
    case ParamType::Float:
    case ParamType::Int:
    case ParamType::Bool: return 1;
    case ParamType::Vec2: return 2;
    case ParamType::Vec3: return 3;
    case ParamType::Vec4: return 4;
    }
    return 1;
}

QString toString(ParamType type)
{
    switch (type) {
    case ParamType::Float: return QStringLiteral("float");
    case ParamType::Vec2: return QStringLiteral("vec2");
    case ParamType::Vec3: return QStringLiteral("vec3");
    case ParamType::Vec4: return QStringLiteral("vec4");
    case ParamType::Int: return QStringLiteral("int");
    case ParamType::Bool: return QStringLiteral("bool");
    }
    return {};
}

std::optional<ParamType> paramTypeFromString(const QString& name)
{
    for (ParamType t : {ParamType::Float, ParamType::Vec2, ParamType::Vec3, ParamType::Vec4,
                        ParamType::Int, ParamType::Bool})
        if (toString(t) == name)
            return t;
    return std::nullopt;
}

bool isBuiltinUniform(const QString& name)
{
    static const QStringList builtins = {"uModel", "uView", "uProjection", "uNormalMatrix",
                                         "uCameraPosition", "uTime", "uResolution"};
    return builtins.contains(name);
}

QHash<QString, ParamHints> parseParameterHints(const QString& source)
{
    // uniform <type> <name> [= ...]; // <hints>
    static const QRegularExpression declRe(
        QStringLiteral(R"(^\s*(?:layout\s*\([^)]*\)\s*)?uniform\s+\w+\s+(\w+)[^;]*;\s*//(.*)$)"),
        QRegularExpression::MultilineOption);
    static const QRegularExpression rangeRe(
        QStringLiteral(R"(@range\(\s*([-+0-9.eE]+)\s*,\s*([-+0-9.eE]+)\s*\))"));
    static const QRegularExpression labelRe(QStringLiteral(R"(@label\(([^)]*)\))"));
    static const QRegularExpression colorRe(QStringLiteral(R"(@colou?r\b)"));

    QHash<QString, ParamHints> hints;
    auto it = declRe.globalMatch(source);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString comment = m.captured(2);
        ParamHints h;
        if (const auto r = rangeRe.match(comment); r.hasMatch()) {
            bool okMin = false, okMax = false;
            const float lo = r.captured(1).toFloat(&okMin);
            const float hi = r.captured(2).toFloat(&okMax);
            if (okMin && okMax && lo < hi) {
                h.min = lo;
                h.max = hi;
            }
        }
        if (const auto l = labelRe.match(comment); l.hasMatch())
            h.label = l.captured(1).trimmed();
        h.color = colorRe.match(comment).hasMatch();
        if (h != ParamHints{})
            hints.insert(m.captured(1), h);
    }
    return hints;
}

double toJsonNumber(float value)
{
    // 9 significant digits always round-trip a float; try shorter first.
    for (int precision = 1; precision < 9; ++precision) {
        const double candidate = QString::number(double(value), 'g', precision).toDouble();
        if (float(candidate) == value)
            return candidate;
    }
    return double(value);
}

QJsonObject parameterValuesToJson(const QHash<QString, MaterialParameter>& values)
{
    QJsonObject json;
    for (const MaterialParameter& p : values) {
        QJsonArray components;
        for (int i = 0; i < componentCount(p.type); ++i)
            components.append(toJsonNumber(p.value[std::size_t(i)]));
        json.insert(p.name, QJsonObject{{"type", toString(p.type)}, {"value", components}});
    }
    return json;
}

QHash<QString, MaterialParameter> parameterValuesFromJson(const QJsonObject& json)
{
    QHash<QString, MaterialParameter> values;
    for (auto it = json.begin(); it != json.end(); ++it) {
        const QJsonObject entry = it.value().toObject();
        const auto type = paramTypeFromString(entry.value("type").toString());
        const QJsonArray components = entry.value("value").toArray();
        if (!type || components.size() != componentCount(*type))
            continue; // ignore malformed entries rather than failing the whole project
        MaterialParameter p;
        p.name = it.key();
        p.type = *type;
        for (int i = 0; i < components.size(); ++i)
            p.value[std::size_t(i)] = float(components[i].toDouble());
        p.defaultValue = p.value;
        values.insert(p.name, p);
    }
    return values;
}

} // namespace pf
