#include "app/ViewportWidget.h"

#include "diagnostics/Logging.h"
#include "render/gl/GLRenderer.h"

#include <QMouseEvent>
#include <QOpenGLDebugLogger>
#include <QOpenGLContext>
#include <QWheelEvent>

namespace pf {

ViewportWidget::ViewportWidget(QWidget* parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::ClickFocus);
    m_clock.start();
}

ViewportWidget::~ViewportWidget()
{
    releaseRenderer();
    // ~QOpenGLWidget destroys the context, which would otherwise call
    // releaseRenderer() on this already-destroyed subclass.
    if (QOpenGLContext* ctx = context())
        disconnect(ctx, nullptr, this, nullptr);
}

void ViewportWidget::releaseRenderer()
{
    if (!m_renderer)
        return;
    makeCurrent();
    m_renderer->shutdown();
    m_renderer.reset();
    delete std::exchange(m_debugLogger, nullptr);
    doneCurrent();
}

RendererInfo ViewportWidget::rendererInfo() const
{
    return m_renderer ? m_renderer->info() : RendererInfo{};
}

void ViewportWidget::initializeGL()
{
    // The context can be recreated (e.g. when the widget is re-parented into a
    // floating window); drop resources tied to the old one first.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this,
            &ViewportWidget::releaseRenderer, Qt::UniqueConnection);

    auto renderer = std::make_unique<gl::GLRenderer>(context());
    QString error;
    if (!renderer->initialize(&error)) {
        qCCritical(lcRender).noquote() << "Renderer initialisation failed:" << error;
        renderer->shutdown();
        emit rendererFailed(error);
        return;
    }
    m_renderer = std::move(renderer);
    m_meshDirty = true;

    if (context()->format().testOption(QSurfaceFormat::DebugContext)) {
        m_debugLogger = new QOpenGLDebugLogger(this);
        if (m_debugLogger->initialize()) {
            connect(m_debugLogger, &QOpenGLDebugLogger::messageLogged, this,
                    [](const QOpenGLDebugMessage& m) {
                        const QString text = QStringLiteral("GL debug [%1]: %2").arg(m.id()).arg(m.message());
                        if (m.severity() == QOpenGLDebugMessage::HighSeverity)
                            qCCritical(lcRender).noquote() << text;
                        else if (m.severity() == QOpenGLDebugMessage::MediumSeverity)
                            qCWarning(lcRender).noquote() << text;
                        else
                            qCDebug(lcRender).noquote() << text;
                    });
            m_debugLogger->startLogging(QOpenGLDebugLogger::SynchronousLogging);
            qCInfo(lcRender) << "OpenGL debug output enabled";
        } else {
            qCWarning(lcRender) << "Debug context requested but GL_KHR_debug is unavailable";
            delete std::exchange(m_debugLogger, nullptr);
        }
    }
    emit rendererReady(m_renderer->info());

    if (m_pendingSources) {
        const ShaderSources sources = *std::exchange(m_pendingSources, std::nullopt);
        emit programBuilt(buildNow(sources));
    } else if (m_activeSources) {
        // Context was recreated: restore the program that was running before.
        buildNow(*m_activeSources);
    }

    // Continuous redraw, paced by the swap interval (vsync).
    connect(this, &QOpenGLWidget::frameSwapped, this, qOverload<>(&QWidget::update),
            Qt::UniqueConnection);
}

void ViewportWidget::buildProgram(const ShaderSources& sources)
{
    if (!m_renderer) {
        m_pendingSources = sources;
        return;
    }
    makeCurrent();
    const ProgramBuildResult result = buildNow(sources);
    doneCurrent();
    update();
    emit programBuilt(result);
}

ProgramBuildResult ViewportWidget::buildNow(const ShaderSources& sources)
{
    ProgramBuildResult result = m_renderer->buildProgram(sources);
    if (result.success)
        m_activeSources = sources;
    return result;
}

void ViewportWidget::setParameter(const MaterialParameter& parameter)
{
    if (!m_renderer)
        return;
    makeCurrent();
    m_renderer->setParameter(parameter.name, parameter.type, parameter.value);
    doneCurrent();
    update();
}

void ViewportWidget::setPreviewMesh(PreviewMesh mesh)
{
    m_mesh = mesh;
    m_meshDirty = true;
    update();
}

void ViewportWidget::resetCamera()
{
    m_camera = OrbitCamera{};
    update();
}

void ViewportWidget::paintGL()
{
    if (!m_renderer)
        return;

    if (m_meshDirty) {
        m_renderer->setMesh(makeMesh(m_mesh));
        m_meshDirty = false;
    }

    const qint64 now = m_clock.nsecsElapsed();
    const QSize fbSize = size() * devicePixelRatio();

    FrameParams params;
    params.view = m_camera.viewMatrix();
    params.projection = m_camera.projectionMatrix(float(fbSize.width()) / std::max(fbSize.height(), 1));
    params.cameraPosition = m_camera.position();
    params.timeSeconds = float(double(now) / 1.0e9);
    params.framebufferSize = fbSize;
    m_renderer->renderFrame(params);

    if (m_lastFrameNs != 0) {
        m_intervalAccumMs += double(now - m_lastFrameNs) / 1.0e6;
        ++m_intervalFrames;
    }
    m_lastFrameNs = now;

    if (now - m_lastStatsNs > 500'000'000 && m_intervalFrames > 0) {
        emit statsUpdated(m_intervalAccumMs / m_intervalFrames, m_renderer->lastFrameStats());
        m_intervalAccumMs = 0.0;
        m_intervalFrames = 0;
        m_lastStatsNs = now;
    }
}

void ViewportWidget::mousePressEvent(QMouseEvent* event)
{
    m_lastMouse = event->position().toPoint();
}

void ViewportWidget::mouseMoveEvent(QMouseEvent* event)
{
    if (!(event->buttons() & (Qt::LeftButton | Qt::MiddleButton)))
        return;
    const QPoint pos = event->position().toPoint();
    const QPoint delta = pos - m_lastMouse;
    m_lastMouse = pos;
    m_camera.orbit(-delta.x() * 0.4f, delta.y() * 0.4f);
    update();
}

void ViewportWidget::wheelEvent(QWheelEvent* event)
{
    m_camera.zoom(float(event->angleDelta().y()) / 120.0f);
    update();
}

} // namespace pf
