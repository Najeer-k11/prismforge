#pragma once

#include <QImage>
#include <QList>
#include <QSize>
#include <QString>

#include <memory>

namespace pf::media {

enum class VideoFormat { H264, Hevc, Av1, Vp9, ProRes, PngSequence };

enum class VideoQuality { High, Medium, Small };

enum class EncoderBackend { Cpu, Nvenc, Vaapi };

// One way of producing a format: e.g. AV1 on the NVIDIA GPU, or AV1 on the CPU.
struct VideoFormatInfo {
    VideoFormat format;
    QString key;       // unique, e.g. "av1-nvenc", "av1-cpu", "vp9-cpu", "png"
    QString label;     // for dialogs, e.g. "MP4 · AV1 — NVIDIA GPU (av1_nvenc)"
    QString extension; // without dot; empty for PNG sequences (a directory)
    bool available = false;
    QString reason;    // why it is unavailable
    QString encoder;   // FFmpeg encoder name
    EncoderBackend backend = EncoderBackend::Cpu;
    bool lowPower = false; // internal: Intel VDENC variant that passed the probe
};

// Every working encoder for each format, GPU encoders first, probed against
// the linked FFmpeg libraries and this machine's drivers (each is actually
// opened). A format with no working encoder appears once, unavailable, with
// the reason. Probing happens once; later calls return the cached result.
const QList<VideoFormatInfo>& videoFormats();
// Exact key, or a format name ("h264", "av1", ...) for its preferred working
// encoder, or an FFmpeg encoder name ("libsvtav1").
const VideoFormatInfo* findVideoFormat(const QString& key);

// Version of the linked FFmpeg libraries, or empty when built without them.
QString ffmpegVersion();

struct VideoSettings {
    QString path; // file, or directory for PngSequence
    VideoFormat format = VideoFormat::H264;
    QString encoderKey; // a VideoFormatInfo key; empty = preferred encoder for `format`
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
