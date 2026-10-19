#include "graph/GraphTypes.h"

#include <QStringList>

#include <cmath>

namespace pf::graph {

QString glslName(DataType type)
{
    switch (type) {
    case DataType::Float: return QStringLiteral("float");
    case DataType::Vec2: return QStringLiteral("vec2");
    case DataType::Vec3: return QStringLiteral("vec3");
    case DataType::Vec4: return QStringLiteral("vec4");
    }
    return {};
}

std::optional<DataType> dataTypeFromString(const QString& name)
{
    for (DataType t : {DataType::Float, DataType::Vec2, DataType::Vec3, DataType::Vec4})
        if (glslName(t) == name)
            return t;
    return std::nullopt;
}

ParamType toParamType(DataType type)
{
    switch (type) {
    case DataType::Float: return ParamType::Float;
    case DataType::Vec2: return ParamType::Vec2;
    case DataType::Vec3: return ParamType::Vec3;
    case DataType::Vec4: return ParamType::Vec4;
    }
    return ParamType::Float;
}

bool canConvert(DataType from, DataType to)
{
    return from == to || from == DataType::Float || to != DataType::Float;
}

QString convertExpression(const QString& expr, DataType from, DataType to)
{
    if (from == to)
        return expr;
    const int n = componentCount(from);
    const int m = componentCount(to);
    if (from == DataType::Float)
        return QStringLiteral("%1(%2)").arg(glslName(to), expr);
    if (m < n)
        return QStringLiteral("(%1).%2").arg(expr, QStringLiteral("xyzw").left(m));
    // Pad: missing x/y/z components are 0, a missing w is 1 (opaque alpha).
    QStringList parts{expr};
    for (int c = n; c < m; ++c)
        parts << (c == 3 ? QStringLiteral("1.0") : QStringLiteral("0.0"));
    return QStringLiteral("%1(%2)").arg(glslName(to), parts.join(QStringLiteral(", ")));
}

namespace {
QString floatLiteral(float v)
{
    if (!std::isfinite(v))
        v = 0.0f;
    QString s = QString::number(double(v), 'g', 9);
    if (!s.contains(u'.') && !s.contains(u'e'))
        s += QStringLiteral(".0");
    return s;
}
} // namespace

QString glslLiteral(DataType type, const ParamValue& value)
{
    const int n = componentCount(type);
    if (n == 1)
        return floatLiteral(value[0]);
    QStringList parts;
    for (int i = 0; i < n; ++i)
        parts << floatLiteral(value[std::size_t(i)]);
    return QStringLiteral("%1(%2)").arg(glslName(type), parts.join(QStringLiteral(", ")));
}

} // namespace pf::graph
