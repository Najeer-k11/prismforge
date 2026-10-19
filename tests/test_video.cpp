#include "media/VideoEncoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#ifdef PRISMFORGE_HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
}
#endif

using namespace pf::media;

namespace {

// A recognisable frame: horizontal gradient whose brightness depends on `i`.
QImage testFrame(QSize size, int i)
{
    QImage image(size, QImage::Format_RGBX8888);
    for (int y = 0; y < size.height(); ++y) {
        uchar* line = image.scanLine(y);
        for (int x = 0; x < size.width(); ++x) {
            line[x * 4 + 0] = uchar(x * 255 / size.width());
            line[x * 4 + 1] = uchar((i * 20) % 256);
            line[x * 4 + 2] = uchar(y * 255 / size.height());
            line[x * 4 + 3] = 255;
        }
    }
    return image;
}

#ifdef PRISMFORGE_HAVE_FFMPEG
struct Probe {
    int width = 0, height = 0;
    double fps = 0;
    int64_t frames = 0;
    QString codec;
};

// Decodes the container headers and counts video packets.
std::optional<Probe> probe(const QString& path)
{
    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, QFile::encodeName(path).constData(), nullptr, nullptr) < 0)
        return std::nullopt;
    avformat_find_stream_info(fmt, nullptr);
    const int stream = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) {
        avformat_close_input(&fmt);
        return std::nullopt;
    }
    Probe p;
    const AVCodecParameters* par = fmt->streams[stream]->codecpar;
    p.width = par->width;
    p.height = par->height;
    p.fps = av_q2d(fmt->streams[stream]->avg_frame_rate);
    p.codec = QString::fromLatin1(avcodec_get_name(par->codec_id));
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(fmt, packet) >= 0) {
        if (packet->stream_index == stream)
            ++p.frames;
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    avformat_close_input(&fmt);
    return p;
}
#endif

} // namespace

TEST_CASE("PNG sequences write one numbered file per frame", "[video]")
{
    QTemporaryDir dir;
    VideoEncoder encoder;
    QString error;
    const QString out = dir.filePath("frames");
    REQUIRE(encoder.open({out, VideoFormat::PngSequence, {64, 36}, 24, VideoQuality::High}, &error));
    for (int i = 0; i < 5; ++i)
        REQUIRE(encoder.writeFrame(testFrame({64, 36}, i), &error));
    REQUIRE(encoder.finish(&error));
    CHECK(encoder.framesWritten() == 5);
    const QStringList files = QDir(out).entryList({"*.png"}, QDir::Files, QDir::Name);
    CHECK(files == QStringList{"frame_00000.png", "frame_00001.png", "frame_00002.png", "frame_00003.png",
                               "frame_00004.png"});
    const QImage first(QDir(out).filePath(files.first()));
    CHECK(first.size() == QSize(64, 36));
    CHECK(first.pixel(63, 0) == testFrame({64, 36}, 0).pixel(63, 0)); // lossless
}

TEST_CASE("Invalid video settings are rejected", "[video]")
{
    QTemporaryDir dir;
    VideoEncoder encoder;
    QString error;
    CHECK_FALSE(encoder.open({dir.filePath("a"), VideoFormat::PngSequence, {65, 36}, 30, {}}, &error));
    CHECK(error.contains("even"));
    CHECK_FALSE(encoder.open({dir.filePath("a"), VideoFormat::PngSequence, {64, 36}, 0, {}}, &error));

    REQUIRE(encoder.open({dir.filePath("b"), VideoFormat::PngSequence, {64, 36}, 30, {}}, &error));
    CHECK_FALSE(encoder.writeFrame(testFrame({32, 32}, 0), &error)); // wrong size
    CHECK(error.contains("size"));
}

TEST_CASE("Video formats are listed with keys and a reason when unavailable", "[video]")
{
    const auto& formats = videoFormats();
    REQUIRE(formats.size() == 6);
    for (const VideoFormatInfo& f : formats) {
        CHECK(findVideoFormat(f.key) == &f);
        if (!f.available)
            CHECK_FALSE(f.reason.isEmpty());
    }
    CHECK(findVideoFormat("png")->available);
    CHECK_FALSE(findVideoFormat("gif"));
}

#ifdef PRISMFORGE_HAVE_FFMPEG
TEST_CASE("Every available video format encodes a playable clip", "[video][ffmpeg]")
{
    QTemporaryDir dir;
    int encoded = 0;
    for (const VideoFormatInfo& f : videoFormats()) {
        if (!f.available || f.format == VideoFormat::PngSequence)
            continue;
        INFO(f.label.toStdString());
        const QString path = dir.filePath("clip." + f.extension);
        const QSize size(320, 180);
        VideoEncoder encoder;
        QString error;
        REQUIRE(encoder.open({path, f.format, size, 24, VideoQuality::Medium}, &error));
        for (int i = 0; i < 24; ++i) {
            INFO("frame " << i << ": " << error.toStdString());
            REQUIRE(encoder.writeFrame(testFrame(size, i), &error));
        }
        INFO(error.toStdString());
        REQUIRE(encoder.finish(&error));

        const auto p = probe(path);
        REQUIRE(p);
        CHECK(p->width == 320);
        CHECK(p->height == 180);
        CHECK(p->frames == 24);
        CHECK(std::abs(p->fps - 24.0) < 0.01);
        UNSCOPED_INFO(f.key.toStdString() << " -> " << p->codec.toStdString() << " via "
                                          << encoder.encoderName().toStdString());
        ++encoded;
    }
    CHECK(encoded > 0);
}

TEST_CASE("Aborting removes the partial video file", "[video][ffmpeg]")
{
    const VideoFormatInfo* format = nullptr;
    for (const VideoFormatInfo& f : videoFormats())
        if (f.available && f.format != VideoFormat::PngSequence && !format)
            format = &f;
    REQUIRE(format);
    QTemporaryDir dir;
    const QString path = dir.filePath("partial." + format->extension);
    {
        VideoEncoder encoder;
        QString error;
        REQUIRE(encoder.open({path, format->format, {320, 180}, 30, VideoQuality::Small}, &error));
        REQUIRE(encoder.writeFrame(testFrame({320, 180}, 0), &error));
        CHECK(QFileInfo::exists(path));
    } // destroyed without finish()
    CHECK_FALSE(QFileInfo::exists(path));
}
#endif
