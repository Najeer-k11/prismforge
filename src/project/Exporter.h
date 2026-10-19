#pragma once

#include "material/MaterialParameter.h"
#include "shader/ShaderTypes.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

namespace pf {

// Everything needed to reproduce a material outside PrismForge.
struct ExportRequest {
    QString directory;
    QString name;                         // base file name, e.g. "rusty_metal"
    ShaderSources sources;                // exactly what was compiled and rendered
    QList<MaterialParameter> parameters;  // with current values
    QByteArray graphJson;                 // set when the fragment shader came from a graph
    QByteArray previewPng;                // optional
};

struct ExportResult {
    bool success = false;
    QStringList writtenFiles; // absolute paths
    QString error;
};

namespace Exporter {

// Files that export() would write (absolute paths), for overwrite checks.
QStringList plannedFiles(const ExportRequest& request);

// Writes <name>.vert, <name>.frag, <name>.material.json and, when present,
// <name>.pfgraph and <name>.preview.png. Each file is written atomically.
ExportResult exportMaterial(const ExportRequest& request);

// The material description document (also used by tests).
QByteArray materialJson(const ExportRequest& request);

// File-system safe version of a display name.
QString sanitizeName(const QString& name);

} // namespace Exporter
} // namespace pf
