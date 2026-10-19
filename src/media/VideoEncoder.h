#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <memory>

namespace pf::media {

enum class VideoFormat { H264, Hevc, Av1, Vp9, ProRes, PngSequence };

enum class VideoQuality { High, Medium, Small };

struct VideoFormatInfo {
    VideoFormat format;
    QString key;       // command-line name: h264, hevc, av1, vp9, prores, png
    QString label;     // for dialogs, includes the encoder actually used
    QString extension; // without dot; empty for PNG sequences (a directory)
    bool available = false;
    QString reason;    // why it is unavailable
};

// Formats in preference order with availability probed against the linked
// FFmpeg libraries and this machine (hardware encoders are test-opened).
// Probing happens once; later calls return the cached result.
const QList<VideoFormatInfo>& videoFormats();
const VideoFormatInfo* findVideoFormat(const QString& key);

// Version of the linked FFmpeg libraries, or empty when built without them.
QString ffmpegVersion();

struct VideoSettings {
    QString path; // file, or directory for PngSequence
    VideoFormat format = VideoFormat::H264;
    QSize size;
    int fps = 30;
    VideoQuality quality = VideoQuality::High;
};

// Encodes RGBX8888 frames to a video file (or PNG files). Not thread-safe;
// use from one thread at a time. Frames are timed at 1/fps intervals.
class VideoEncoder {
public:
    VideoEncoder();
    ~VideoEncoder(); // aborts (and deletes a partial file) if finish() was not called

    VideoEncoder(const VideoEncoder&) = delete;
    VideoEncoder& operator=(const VideoEncoder&) = delete;

    bool open(const VideoSettings& settings, QString* error);
    bool writeFrame(const QImage& frame, QString* error);
    bool finish(QString* error);
    // Stops without finalising and removes the incomplete output.
    void abort();

    QString encoderName() const; // e.g. "h264_vaapi"
    int framesWritten() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace pf::media
