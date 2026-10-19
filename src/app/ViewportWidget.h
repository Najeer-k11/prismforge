#pragma once

#include "render/OrbitCamera.h"
#include "render/Renderer.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>

class QOpenGLDebugLogger;

#include <memory>
#include <optional>

namespace pf {

// Central preview. Owns the renderer and drives continuous redraw; all GL work
// happens here, on the GUI thread, with this widget's context current.
class ViewportWidget final : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit ViewportWidget(QWidget* parent = nullptr);
    ~ViewportWidget() override;

    // Compiles on the GL thread; emits programBuilt(). If the GL context is
    // not ready yet the request is deferred until initializeGL().
    void buildProgram(const ShaderSources& sources);

    void setParameter(const MaterialParameter& parameter);

    void setPreviewMesh(PreviewMesh mesh);
    PreviewMesh previewMesh() const { return m_mesh; }

    OrbitCamera& camera() { return m_camera; }
    void resetCamera();

    bool isRendererReady() const { return m_renderer != nullptr; }
    RendererInfo rendererInfo() const;

signals:
    void rendererReady(const pf::RendererInfo& info);
    void rendererFailed(const QString& reason);
    void programBuilt(const pf::ProgramBuildResult& result);
    // Emitted about twice a second with smoothed statistics.
    void statsUpdated(double frameIntervalMs, const pf::FrameStats& stats);

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void releaseRenderer();
    ProgramBuildResult buildNow(const ShaderSources& sources);

    std::unique_ptr<Renderer> m_renderer;
    QOpenGLDebugLogger* m_debugLogger = nullptr; // only with --gl-debug
    std::optional<ShaderSources> m_pendingSources;
    std::optional<ShaderSources> m_activeSources; // last successfully linked
    PreviewMesh m_mesh = PreviewMesh::Sphere;
    bool m_meshDirty = true;

    OrbitCamera m_camera;
    QPoint m_lastMouse;

    QElapsedTimer m_clock;
    qint64 m_lastFrameNs = 0;
    double m_intervalAccumMs = 0.0;
    int m_intervalFrames = 0;
    qint64 m_lastStatsNs = 0;
};

} // namespace pf
