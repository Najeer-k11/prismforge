#pragma once

#include "render/OrbitCamera.h"
#include "render/Renderer.h"

#include <QElapsedTimer>
#include <QOpenGLWidget>

class QOpenGLDebugLogger;

#include <array>
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
    // Binds a texture to uChannel<index> (null image: unbind). Kept and
    // re-applied if the GL context is recreated. Returns false with a reason
    // if the GPU cannot use the image.
    bool setChannel(int index, const QImage& image, const ChannelSampler& sampler, QString* error);

    void setPreviewMesh(PreviewMesh mesh);
    PreviewMesh previewMesh() const { return m_mesh; }

    OrbitCamera& camera() { return m_camera; }
    void resetCamera();

    // Seconds since start; drives uTime in the preview.
    float timeSeconds() const { return float(double(m_clock.nsecsElapsed()) / 1.0e9); }
    // Renders one frame offscreen (any size, MSAA) with the current program,
    // mesh and camera. `camera` overrides the viewport camera when given.
    bool renderImage(QSize size, int samples, float timeSeconds, QImage* image, QString* error,
                     const OrbitCamera* camera = nullptr);
    QSize maxRenderSize() const;
    int maxSamples() const;

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
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void releaseRenderer();
    ProgramBuildResult buildNow(const ShaderSources& sources);
    FrameParams frameParams(const OrbitCamera& camera, QSize framebufferSize, float time) const;
    QPointF normalizedPosition(const QPointF& widgetPosition) const;

    std::unique_ptr<Renderer> m_renderer;
    QOpenGLDebugLogger* m_debugLogger = nullptr; // only with --gl-debug
    std::optional<ShaderSources> m_pendingSources;
    std::optional<ShaderSources> m_activeSources; // last successfully linked
    PreviewMesh m_mesh = PreviewMesh::Sphere;
    bool m_meshDirty = true;

    struct Channel {
        QImage image;
        ChannelSampler sampler;
    };
    std::array<Channel, kChannelCount> m_channels;

    OrbitCamera m_camera;
    QPoint m_lastMouse;
    // uMouse state in [0, 1] viewport coordinates (y up), so offscreen renders
    // at any size see the same relative position.
    QPointF m_mousePosition, m_mouseClick;
    bool m_mouseDown = false;

    QElapsedTimer m_clock;
    qint64 m_lastFrameNs = 0;
    double m_intervalAccumMs = 0.0;
    int m_intervalFrames = 0;
    qint64 m_lastStatsNs = 0;
};

} // namespace pf
