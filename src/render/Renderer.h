#pragma once

#include "diagnostics/Diagnostic.h"
#include "material/MaterialParameter.h"
#include "render/Mesh.h"
#include "shader/ShaderTypes.h"

#include <QMatrix4x4>
#include <QSize>
#include <QStringList>
#include <QVector3D>

#include <optional>

namespace pf {

struct ProgramBuildResult {
    bool success = false;
    QList<Diagnostic> diagnostics;
    // Editable uniforms of the new program (built-ins excluded), with the
    // values they had after linking. Only filled on success.
    QList<MaterialParameter> parameters;
};

struct FrameParams {
    QMatrix4x4 model;
    QMatrix4x4 view;
    QMatrix4x4 projection;
    QVector3D cameraPosition;
    float timeSeconds = 0.0f;
    QSize framebufferSize; // physical pixels
};

struct FrameStats {
    // CPU time spent recording/submitting GL commands for the frame. This is
    // NOT the time the GPU spent executing them.
    double cpuSubmitMs = 0.0;
    // Measured on the GPU with timer queries. Results arrive a few frames late;
    // unset when timer queries are unsupported or no result is ready yet.
    std::optional<double> gpuMs;
    std::size_t triangles = 0;
    int drawCalls = 0;
};

struct RendererInfo {
    QString api;           // e.g. "OpenGL"
    QString vendor;
    QString device;
    QString version;
    QString shadingLanguageVersion;
    bool gpuTimerQueries = false;
    QStringList notes;
};

// Graphics-API-neutral interface used by the viewport. All methods must be
// called on the thread that owns the API context, with the context current.
class Renderer {
public:
    virtual ~Renderer() = default;

    // Returns false (with a reason) if the device cannot run PrismForge.
    virtual bool initialize(QString* error) = 0;
    // Releases all API objects. Called before the context is destroyed.
    virtual void shutdown() = 0;

    // Compiles and links the sources. On success the new program replaces the
    // active one; on failure the previously active program is left untouched.
    virtual ProgramBuildResult buildProgram(const ShaderSources& sources) = 0;
    virtual bool hasProgram() const = 0;

    // Sets a uniform on the active program. Unknown names or mismatching
    // types are ignored (the shader may have changed underneath the UI).
    virtual void setParameter(const QString& name, ParamType type, const ParamValue& value) = 0;

    virtual void setMesh(const MeshData& mesh) = 0;
    virtual void renderFrame(const FrameParams& params) = 0;

    virtual FrameStats lastFrameStats() const = 0;
    virtual RendererInfo info() const = 0;
};

} // namespace pf
