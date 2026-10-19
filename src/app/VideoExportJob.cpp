#include "app/VideoExportJob.h"

#include "app/ViewportWidget.h"
#include "diagnostics/Logging.h"

#include <QtConcurrent>

#include <cmath>

namespace pf {

VideoExportJob::VideoExportJob(ViewportWidget* viewport, VideoRenderOptions options, const OrbitCamera& camera,
                               QObject* parent)
    : QObject(parent), m_viewport(viewport), m_options(std::move(options)), m_camera(camera),
      m_encoder(std::make_unique<media::VideoEncoder>())
{
    m_total = std::max(1, int(std::lround(m_options.duration * m_options.video.fps)));
    connect(&m_watcher, &QFutureWatcher<QString>::finished, this, &VideoExportJob::onEncoded);
}

VideoExportJob::~VideoExportJob()
{
    m_watcher.waitForFinished(); // the worker uses m_encoder
}

void VideoExportJob::start()
{
    QString error;
    if (!m_encoder->open(m_options.video, &error)) {
        fail(error);
        return;
    }
    qCInfo(lcApp).noquote() << QStringLiteral("Rendering %1 frames at %2x%3, %4 fps with %5 to %6")
                                   .arg(m_total)
                                   .arg(m_options.video.size.width())
                                   .arg(m_options.video.size.height())
                                   .arg(m_options.video.fps)
                                   .arg(m_encoder->encoderName(), m_options.video.path);
    m_clock.start();
    pump();
}

void VideoExportJob::cancel()
{
    m_cancelled = true;
    if (!m_encoding)
        fail(tr("Cancelled"));
}

bool VideoExportJob::renderFrame(int index, QImage* image, QString* error)
{
    const double t = m_options.startTime + double(index) / m_options.video.fps;
    OrbitCamera camera = m_camera;
    if (m_options.turntable) // frame m_total would equal frame 0: a seamless loop
        camera.orbit(360.0f * float(index) / float(m_total), 0.0f);
    return m_viewport->renderImage(m_options.video.size, m_options.samples, float(t), image, error, &camera);
}

void VideoExportJob::pump()
{
    if (m_done)
        return;
    // Keep one frame rendered ahead of the encoder.
    if (!m_next && m_rendered < m_total) {
        QImage image;
        QString error;
        if (!renderFrame(m_rendered, &image, &error)) {
            fail(tr("Rendering frame %1 failed: %2").arg(m_rendered).arg(error));
            return;
        }
        m_next = std::move(image);
        ++m_rendered;
    }
    if (!m_encoding && m_next) {
        m_encoding = true;
        media::VideoEncoder* encoder = m_encoder.get();
        m_watcher.setFuture(QtConcurrent::run([encoder, frame = std::exchange(m_next, std::nullopt).value()] {
            QString error;
            encoder->writeFrame(frame, &error);
            return error;
        }));
        // Render the following frame while this one encodes.
        if (m_rendered < m_total) {
            QImage image;
            QString error;
            if (!renderFrame(m_rendered, &image, &error)) {
                fail(tr("Rendering frame %1 failed: %2").arg(m_rendered).arg(error));
                return;
            }
            m_next = std::move(image);
            ++m_rendered;
        }
    }
}

void VideoExportJob::onEncoded()
{
    m_encoding = false;
    if (m_done)
        return;
    if (const QString error = m_watcher.result(); !error.isEmpty()) {
        fail(tr("Encoding frame %1 failed: %2").arg(m_encoded).arg(error));
        return;
    }
    ++m_encoded;
    const double seconds = std::max(0.001, double(m_clock.elapsed()) / 1000.0);
    emit progress(m_encoded, m_total, m_encoded / seconds);

    if (m_cancelled) {
        fail(tr("Cancelled"));
        return;
    }
    if (m_encoded < m_total) {
        pump();
        return;
    }
    // Flush delayed frames (can take a moment for AV1/VP9) off the GUI thread.
    m_done = true;
    media::VideoEncoder* encoder = m_encoder.get();
    auto* finishWatcher = new QFutureWatcher<QString>(this);
    connect(finishWatcher, &QFutureWatcher<QString>::finished, this, [this, finishWatcher] {
        const QString error = finishWatcher->result();
        finishWatcher->deleteLater();
        if (!error.isEmpty()) {
            emit finished(false, error);
            return;
        }
        const double seconds = double(m_clock.elapsed()) / 1000.0;
        emit finished(true, tr("Rendered %n frame(s) in %1 s", nullptr, m_total).arg(seconds, 0, 'f', 1));
    });
    finishWatcher->setFuture(QtConcurrent::run([encoder] {
        QString error;
        encoder->finish(&error);
        return error;
    }));
}

void VideoExportJob::fail(const QString& message)
{
    if (m_done)
        return;
    m_done = true;
    m_watcher.waitForFinished();
    m_encoder->abort(); // removes the partial file
    emit finished(false, message);
}

} // namespace pf
