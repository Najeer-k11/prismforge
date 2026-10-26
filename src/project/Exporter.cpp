#include "project/Exporter.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>

namespace pf::Exporter {

namespace {

QString filePath(const ExportRequest& r, const QString& suffix)
{
    return QDir(r.directory).absoluteFilePath(sanitizeName(r.name) + suffix);
}

QJsonArray components(const ParamValue& v, ParamType type)
{
    QJsonArray a;
    for (int i = 0; i < componentCount(type); ++i)
        a.append(toJsonNumber(v[std::size_t(i)]));
    return a;
}

QString channelSuffix(const ExportChannel& c)
{
    return QStringLiteral(".channel%1.%2").arg(c.index).arg(c.fileSuffix.isEmpty() ? QStringLiteral("png")
                                                                                     : c.fileSuffix.toLower());
}

bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

} // namespace

QString sanitizeName(const QString& name)
{
    QString s = name.trimmed();
    s.replace(QRegularExpression(QStringLiteral(R"([^A-Za-z0-9._-]+)")), QStringLiteral("_"));
    s.remove(QRegularExpression(QStringLiteral("^[._]+")));
    return s.isEmpty() ? QStringLiteral("material") : s;
}

QStringList plannedFiles(const ExportRequest& r)
{
    QStringList files{filePath(r, ".vert"), filePath(r, ".frag"), filePath(r, ".material.json")};
    if (!r.graphJson.isEmpty())
        files << filePath(r, ".pfgraph");
    if (!r.previewPng.isEmpty())
        files << filePath(r, ".preview.png");
    for (const ExportChannel& c : r.channels)
        files << filePath(r, channelSuffix(c));
    return files;
}

QByteArray materialJson(const ExportRequest& r)
{
    const QString base = sanitizeName(r.name);
    QJsonArray parameters;
    for (const MaterialParameter& p : r.parameters) {
        QJsonObject o{{"name", p.name},
                      {"type", toString(p.type)},
                      {"value", components(p.value, p.type)},
                      {"default", components(p.defaultValue, p.type)}};
        QJsonObject hints;
        if (p.hints.min && p.hints.max) {
            hints.insert("min", toJsonNumber(*p.hints.min));
            hints.insert("max", toJsonNumber(*p.hints.max));
        }
        if (p.hints.color)
            hints.insert("color", true);
        if (!p.hints.label.isEmpty())
            hints.insert("label", p.hints.label);
        if (!hints.isEmpty())
            o.insert("hints", hints);
        parameters.append(o);
    }

    QJsonObject shaders{{"language", "GLSL 450 core"},
                        {"vertex", base + ".vert"},
                        {"fragment", base + ".frag"}};
    if (!r.graphJson.isEmpty())
        shaders.insert("graph", base + ".pfgraph");

    const QJsonObject json{
        {"formatVersion", 1},
        {"name", r.name},
        {"generator", QStringLiteral("PrismForge")},
        {"shaders", shaders},
        {"interface", QJsonObject{
            {"attributes", QJsonArray{
                QJsonObject{{"location", 0}, {"name", "aPosition"}, {"type", "vec3"}},
                QJsonObject{{"location", 1}, {"name", "aNormal"}, {"type", "vec3"}},
                QJsonObject{{"location", 2}, {"name", "aUV"}, {"type", "vec2"}},
            }},
            {"builtinUniforms", QJsonArray{
                QJsonObject{{"name", "uModel"}, {"type", "mat4"}},
                QJsonObject{{"name", "uView"}, {"type", "mat4"}},
                QJsonObject{{"name", "uProjection"}, {"type", "mat4"}},
                QJsonObject{{"name", "uNormalMatrix"}, {"type", "mat3"}},
                QJsonObject{{"name", "uCameraPosition"}, {"type", "vec3"}},
                QJsonObject{{"name", "uTime"}, {"type", "float"}},
                QJsonObject{{"name", "uResolution"}, {"type", "vec2"}},
                QJsonObject{{"name", "uMouse"}, {"type", "vec4"}},
                QJsonObject{{"name", "uChannel0"}, {"type", "sampler2D"}},
                QJsonObject{{"name", "uChannel1"}, {"type", "sampler2D"}},
                QJsonObject{{"name", "uChannel2"}, {"type", "sampler2D"}},
                QJsonObject{{"name", "uChannel3"}, {"type", "sampler2D"}},
                QJsonObject{{"name", "uChannelResolution"}, {"type", "vec3[4]"}},
            }},
        }},
        {"parameters", parameters},
    };
    QJsonArray channels;
    for (const ExportChannel& c : r.channels) {
        QJsonObject o{{"index", c.index},
                      {"uniform", channelUniformName(c.index)},
                      {"file", base + channelSuffix(c)},
                      {"filter", toString(c.binding.sampler.filter)},
                      {"wrap", toString(c.binding.sampler.wrap)},
                      {"flipY", c.binding.sampler.flipY}};
        if (c.binding.source == ChannelBinding::Source::Builtin)
            o.insert("builtinTexture", c.binding.texture);
        channels.append(o);
    }
    QJsonObject document = json;
    if (!channels.isEmpty())
        document.insert("channels", channels);
    if (!r.previewPng.isEmpty())
        document.insert("preview", base + ".preview.png");
    return QJsonDocument(document).toJson(QJsonDocument::Indented);
}

ExportResult exportMaterial(const ExportRequest& r)
{
    ExportResult result;
    if (!QDir().mkpath(r.directory)) {
        result.error = QStringLiteral("Cannot create directory %1").arg(r.directory);
        return result;
    }
    const QList<std::pair<QString, QByteArray>> files = [&] {
        QList<std::pair<QString, QByteArray>> list{
            {filePath(r, ".vert"), r.sources.vertex.toUtf8()},
            {filePath(r, ".frag"), r.sources.fragment.toUtf8()},
            {filePath(r, ".material.json"), materialJson(r)},
        };
        if (!r.graphJson.isEmpty())
            list.append({filePath(r, ".pfgraph"), r.graphJson});
        if (!r.previewPng.isEmpty())
            list.append({filePath(r, ".preview.png"), r.previewPng});
        for (const ExportChannel& c : r.channels)
            list.append({filePath(r, channelSuffix(c)), c.data});
        return list;
    }();
    for (const auto& [path, data] : files) {
        if (!writeFile(path, data, &result.error))
            return result;
        result.writtenFiles << path;
    }
    result.success = true;
    return result;
}

} // namespace pf::Exporter
