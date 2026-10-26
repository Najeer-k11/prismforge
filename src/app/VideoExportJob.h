#pragma once

#include "app/RenderDialogs.h"
#include "render/OrbitCamera.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>

#include <atomic>
#include <memory>
#include <optional>

namespace pf {

class ViewportWidget;

// Renders a clip frame by frame and encodes it. Rendering happens on the GUI
// thread (it owns the GL context); encoding runs on a worker thread, so frame
// N+1 renders while frame N encodes. Frame i is rendered at
// uTime = startTime + i / fps, independent of how long encoding takes.
class VideoExportJob final : public QObject {
    Q_OBJECT
public:
    VideoExportJob(ViewportWidget* viewport, VideoRenderOptions options, const OrbitCamera& camera,
                   QObject* parent = nullptr);
    ~VideoExportJob() override;

    void start();
    void cancel();
    int totalFrames() const { return m_total; }

signals:
    void progress(int framesDone, int totalFrames, double framesPerSecond);
    void finished(bool success, const QString& message);

private:
    bool renderFrame(int index, QImage* image, QString* error);
    void pump();
    void onEncoded();
    void fail(const QString& message);

    ViewportWidget* m_viewport;
    VideoRenderOptions m_options;
    OrbitCamera m_camera;
    std::unique_ptr<media::VideoEncoder> m_encoder;
    QFutureWatcher<QString> m_watcher;
    std::optional<QImage> m_next; // rendered, waiting for the encoder
    int m_total = 0;
    int m_rendered = 0;
    int m_encoded = 0;
    bool m_encoding = false;
    bool m_cancelled = false;
    bool m_done = false;
    QElapsedTimer m_clock;
    qint64 m_renderNs = 0;                    // GUI thread: render + read-back
    std::shared_ptr<std::atomic<qint64>> m_encodeNs = std::make_shared<std::atomic<qint64>>(0);
};

} // namespace pf
