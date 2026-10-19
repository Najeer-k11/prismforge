#include "shader/ShaderTypes.h"

namespace pf {

QString toString(ShaderStage stage)
{
    switch (stage) {
    case ShaderStage::Vertex: return QStringLiteral("vertex");
    case ShaderStage::Fragment: return QStringLiteral("fragment");
    }
    return {};
}

std::optional<ShaderStage> stageFromFileSuffix(const QString& suffix)
{
    const QString s = suffix.toLower();
    if (s == u"vert" || s == u"vs")
        return ShaderStage::Vertex;
    if (s == u"frag" || s == u"fs")
        return ShaderStage::Fragment;
    return std::nullopt;
}

} // namespace pf
