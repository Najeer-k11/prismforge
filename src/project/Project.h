#pragma once

#include "material/MaterialParameter.h"

#include <QHash>
#include <QJsonObject>
#include <QString>

#include <optional>

namespace pf {

// On-disk project description (*.pfproj, JSON).
//
// Shader paths are stored relative to the project file so a project directory
// can be moved or checked into version control as a unit. Shader source is kept
// in separate files rather than embedded, so diffs stay readable.
struct Project {
    static constexpr int kFormatVersion = 2;

    QString name = QStringLiteral("Untitled");
    QString vertexShaderPath;   // relative to the project directory
    QString fragmentShaderPath; // relative to the project directory

    QString previewMesh = QStringLiteral("sphere");
    bool autoCompile = true;

    // Parameter values the user changed from the shader defaults (v2+).
    QHash<QString, MaterialParameter> parameters;

    struct Camera {
        float yawDegrees = 30.0f;
        float pitchDegrees = 20.0f;
        float distance = 3.5f;
        bool operator==(const Camera&) const = default;
    } camera;

    bool operator==(const Project&) const = default;
};

struct ProjectLoadResult {
    std::optional<Project> project;
    QString error;
};

namespace ProjectIO {

inline constexpr auto kFileSuffix = "pfproj";

QJsonObject toJson(const Project& project);
ProjectLoadResult fromJson(const QJsonObject& json);

// Serialises with a trailing newline and stable key order (diff-friendly).
QByteArray serialize(const Project& project);
ProjectLoadResult deserialize(const QByteArray& data);

bool save(const Project& project, const QString& filePath, QString* error);
ProjectLoadResult load(const QString& filePath);

// Path helpers for converting between project-relative and absolute paths.
QString toRelativePath(const QString& projectFilePath, const QString& absolutePath);
QString toAbsolutePath(const QString& projectFilePath, const QString& relativePath);

} // namespace ProjectIO
} // namespace pf
