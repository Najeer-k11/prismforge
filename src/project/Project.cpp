#include "project/Project.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>

namespace pf::ProjectIO {

QJsonObject toJson(const Project& project)
{
    return QJsonObject{
        {"formatVersion", Project::kFormatVersion},
        {"name", project.name},
        {"shaders", QJsonObject{
            {"vertex", project.vertexShaderPath},
            {"fragment", project.fragmentShaderPath},
        }},
        {"preview", QJsonObject{
            {"mesh", project.previewMesh},
            {"autoCompile", project.autoCompile},
            {"camera", QJsonObject{
                {"yaw", project.camera.yawDegrees},
                {"pitch", project.camera.pitchDegrees},
                {"distance", project.camera.distance},
            }},
        }},
        {"parameters", parameterValuesToJson(project.parameters)},
        {"graph", QJsonObject{{"file", project.graphPath}, {"enabled", project.useGraph}}},
        {"channels", channelsToJson(project.channels)},
    };
}

ProjectLoadResult fromJson(const QJsonObject& json)
{
    const int version = json.value("formatVersion").toInt(-1);
    if (version < 1)
        return {std::nullopt, QStringLiteral("Missing or invalid 'formatVersion'")};
    if (version > Project::kFormatVersion)
        return {std::nullopt,
                QStringLiteral("Project format version %1 is newer than supported version %2")
                    .arg(version).arg(Project::kFormatVersion)};

    Project p;
    const Project defaults;
    p.name = json.value("name").toString(defaults.name);

    const QJsonObject shaders = json.value("shaders").toObject();
    p.vertexShaderPath = shaders.value("vertex").toString();
    p.fragmentShaderPath = shaders.value("fragment").toString();
    if (p.fragmentShaderPath.isEmpty())
        return {std::nullopt, QStringLiteral("Project does not reference a fragment shader")};
    if (p.vertexShaderPath.isEmpty())
        return {std::nullopt, QStringLiteral("Project does not reference a vertex shader")};

    const QJsonObject preview = json.value("preview").toObject();
    p.previewMesh = preview.value("mesh").toString(defaults.previewMesh);
    p.autoCompile = preview.value("autoCompile").toBool(defaults.autoCompile);

    const QJsonObject camera = preview.value("camera").toObject();
    p.camera.yawDegrees = float(camera.value("yaw").toDouble(defaults.camera.yawDegrees));
    p.camera.pitchDegrees = float(camera.value("pitch").toDouble(defaults.camera.pitchDegrees));
    p.camera.distance = float(camera.value("distance").toDouble(defaults.camera.distance));

    p.parameters = parameterValuesFromJson(json.value("parameters").toObject());

    const QJsonObject graph = json.value("graph").toObject();
    p.graphPath = graph.value("file").toString();
    p.useGraph = graph.value("enabled").toBool(false) && !p.graphPath.isEmpty();

    p.channels = channelsFromJson(json.value("channels").toObject());

    return {p, {}};
}

QByteArray serialize(const Project& project)
{
    // QJsonObject keeps keys sorted, so output order is deterministic.
    return QJsonDocument(toJson(project)).toJson(QJsonDocument::Indented);
}

ProjectLoadResult deserialize(const QByteArray& data)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return {std::nullopt, QStringLiteral("Invalid JSON at offset %1: %2")
                                  .arg(parseError.offset).arg(parseError.errorString())};
    if (!doc.isObject())
        return {std::nullopt, QStringLiteral("Project file must contain a JSON object")};
    return fromJson(doc.object());
}

bool save(const Project& project, const QString& filePath, QString* error)
{
    // QSaveFile writes to a temporary file and renames on commit, so a failed
    // save never leaves a truncated project behind.
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(serialize(project));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

ProjectLoadResult load(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return {std::nullopt, file.errorString()};
    return deserialize(file.readAll());
}

QString toRelativePath(const QString& projectFilePath, const QString& absolutePath)
{
    const QDir projectDir = QFileInfo(projectFilePath).absoluteDir();
    return QDir::fromNativeSeparators(projectDir.relativeFilePath(absolutePath));
}

QString toAbsolutePath(const QString& projectFilePath, const QString& relativePath)
{
    const QDir projectDir = QFileInfo(projectFilePath).absoluteDir();
    return QDir::cleanPath(projectDir.absoluteFilePath(relativePath));
}

} // namespace pf::ProjectIO
