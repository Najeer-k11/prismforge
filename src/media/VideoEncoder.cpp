#include "media/VideoEncoder.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <array>

#ifdef PRISMFORGE_HAVE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace pf::media {

namespace {

#ifdef PRISMFORGE_HAVE_FFMPEG
QString avError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(code, buffer, sizeof(buffer));
    return QString::fromUtf8(buffer);
}

// Software encoder preferred over the hardware one where both exist.
struct Candidate {
    const char* encoder;
    bool vaapi;
};

QList<Candidate> candidatesFor(VideoFormat format)
{
    switch (format) {
    case VideoFormat::H264: return {{"libx264", false}, {"h264_vaapi", true}};
    case VideoFormat::Hevc: return {{"libx265", false}, {"hevc_vaapi", true}};
    case VideoFormat::Av1: return {{"libsvtav1", false}, {"libaom-av1", false}, {"av1_vaapi", true}};
    case VideoFormat::Vp9: return {{"libvpx-vp9", false}};
    case VideoFormat::ProRes: return {{"prores_ks", false}};
    case VideoFormat::PngSequence: return {};
    }
    return {};
}

struct CodecSetup {
    AVCodecContext* ctx = nullptr;
    AVBufferRef* hwDevice = nullptr;
};

void freeSetup(CodecSetup& s)
{
    avcodec_free_context(&s.ctx);
    av_buffer_unref(&s.hwDevice);
}

// Configures and opens an encoder. Shared by probing and real encoding so a
// format is only offered if this exact configuration opens.
bool openCodec(const Candidate& candidate, VideoFormat format, QSize size, int fps, VideoQuality quality,
               bool globalHeader, CodecSetup* out, QString* error)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(candidate.encoder);
    if (!codec) {
        *error = QStringLiteral("encoder %1 not in this FFmpeg build").arg(candidate.encoder);
        return false;
    }
    CodecSetup s;
    s.ctx = avcodec_alloc_context3(codec);
    AVCodecContext* c = s.ctx;
    c->width = size.width();
    c->height = size.height();
    c->time_base = AVRational{1, fps};
    c->framerate = AVRational{fps, 1};
    c->gop_size = fps * 2;
    c->color_primaries = AVCOL_PRI_BT709;
    c->color_trc = AVCOL_TRC_BT709;
    c->colorspace = AVCOL_SPC_BT709;
    c->color_range = AVCOL_RANGE_MPEG;
    if (globalHeader)
        c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    const int q = int(quality);
    AVDictionary* options = nullptr;
    auto set = [&options](const char* key, const QString& value) {
        av_dict_set(&options, key, value.toUtf8().constData(), 0);
    };

    if (candidate.vaapi) {
        int rc = av_hwdevice_ctx_create(&s.hwDevice, AV_HWDEVICE_TYPE_VAAPI, nullptr, nullptr, 0);
        if (rc < 0) {
            *error = QStringLiteral("no VAAPI device (%1)").arg(avError(rc));
            freeSetup(s);
            return false;
        }
        AVBufferRef* frames = av_hwframe_ctx_alloc(s.hwDevice);
        auto* fc = reinterpret_cast<AVHWFramesContext*>(frames->data);
        fc->format = AV_PIX_FMT_VAAPI;
        fc->sw_format = AV_PIX_FMT_NV12;
        fc->width = c->width;
        fc->height = c->height;
        fc->initial_pool_size = 8;
        rc = av_hwframe_ctx_init(frames);
        if (rc < 0) {
            av_buffer_unref(&frames);
            *error = QStringLiteral("VAAPI surfaces unavailable (%1)").arg(avError(rc));
            freeSetup(s);
            return false;
        }
        c->hw_frames_ctx = frames; // codec context takes ownership
        c->pix_fmt = AV_PIX_FMT_VAAPI;
        set("rc_mode", QStringLiteral("CQP"));
        c->global_quality = std::array{20, 25, 30}[q];
        set("qp", QString::number(c->global_quality));
    } else {
        c->pix_fmt = format == VideoFormat::ProRes ? AV_PIX_FMT_YUV422P10LE : AV_PIX_FMT_YUV420P;
        const QString name = QString::fromLatin1(candidate.encoder);
        if (name == u"libx264" || name == u"libx265") {
            set("crf", QString::number(std::array{18, 23, 28}[q]));
            set("preset", QStringLiteral("medium"));
        } else if (name == u"libsvtav1") {
            set("crf", QString::number(std::array{28, 35, 42}[q]));
            set("preset", QStringLiteral("8"));
        } else if (name == u"libaom-av1") {
            set("crf", QString::number(std::array{28, 35, 42}[q]));
            set("cpu-used", QStringLiteral("6"));
            c->bit_rate = 0;
        } else if (name == u"libvpx-vp9") {
            set("crf", QString::number(std::array{24, 31, 38}[q]));
            set("deadline", QStringLiteral("good"));
            set("cpu-used", QStringLiteral("4"));
            set("row-mt", QStringLiteral("1"));
            c->bit_rate = 0;
        } else if (name == u"prores_ks") {
            set("profile", QString::number(std::array{3, 2, 1}[q])); // HQ, standard, LT
            c->color_range = AVCOL_RANGE_MPEG;
        }
    }

    const int rc = avcodec_open2(c, codec, &options);
    av_dict_free(&options);
    if (rc < 0) {
        *error = QStringLiteral("%1 failed to open (%2)").arg(candidate.encoder, avError(rc));
        freeSetup(s);
        return false;
    }
    *out = s;
    return true;
}
#endif // PRISMFORGE_HAVE_FFMPEG

QString formatLabel(VideoFormat f)
{
    switch (f) {
    case VideoFormat::H264: return QStringLiteral("MP4 · H.264");
    case VideoFormat::Hevc: return QStringLiteral("MP4 · H.265 / HEVC");
    case VideoFormat::Av1: return QStringLiteral("MP4 · AV1");
    case VideoFormat::Vp9: return QStringLiteral("WebM · VP9");
    case VideoFormat::ProRes: return QStringLiteral("MOV · ProRes (editing)");
    case VideoFormat::PngSequence: return QStringLiteral("PNG image sequence (lossless)");
    }
    return {};
}

} // namespace

const QList<VideoFormatInfo>& videoFormats()
{
    static const QList<VideoFormatInfo> formats = [] {
        const struct {
            VideoFormat format;
            const char* key;
            const char* extension;
        } table[] = {
            {VideoFormat::H264, "h264", "mp4"},   {VideoFormat::Hevc, "hevc", "mp4"},
            {VideoFormat::Av1, "av1", "mp4"},     {VideoFormat::Vp9, "vp9", "webm"},
            {VideoFormat::ProRes, "prores", "mov"}, {VideoFormat::PngSequence, "png", ""},
        };
        QList<VideoFormatInfo> list;
        for (const auto& row : table) {
            VideoFormatInfo info{row.format, QString::fromLatin1(row.key), formatLabel(row.format),
                                 QString::fromLatin1(row.extension), false, {}};
            if (row.format == VideoFormat::PngSequence) {
                info.available = true;
                list.append(info);
                continue;
            }
#ifdef PRISMFORGE_HAVE_FFMPEG
            QStringList reasons;
            for (const Candidate& candidate : candidatesFor(row.format)) {
                CodecSetup s;
                QString why;
                if (openCodec(candidate, row.format, {256, 256}, 30, VideoQuality::Medium, false, &s, &why)) {
                    freeSetup(s);
                    info.available = true;
                    info.label += QStringLiteral(" (%1%2)")
                                      .arg(QString::fromLatin1(candidate.encoder),
                                           candidate.vaapi ? QStringLiteral(", GPU") : QString());
                    break;
                }
                reasons << why;
            }
            info.reason = reasons.join(QStringLiteral("; "));
#else
            info.reason = QStringLiteral("PrismForge was built without FFmpeg");
#endif
            list.append(info);
        }
        return list;
    }();
    return formats;
}

const VideoFormatInfo* findVideoFormat(const QString& key)
{
    for (const VideoFormatInfo& f : videoFormats())
        if (f.key == key.toLower())
            return &f;
    return nullptr;
}

QString ffmpegVersion()
{
#ifdef PRISMFORGE_HAVE_FFMPEG
    return QString::fromLatin1(av_version_info());
#else
    return {};
#endif
}

// ---------------------------------------------------------------- encoder

struct VideoEncoder::Impl {
    VideoSettings settings;
    int frames = 0;
    bool open = false;
    QString encoder;
#ifdef PRISMFORGE_HAVE_FFMPEG
    AVFormatContext* format = nullptr;
    AVStream* stream = nullptr;
    CodecSetup codec;
    SwsContext* sws = nullptr;
    AVFrame* swFrame = nullptr; // converted (NV12 for VAAPI, YUV otherwise)
    AVPacket* packet = nullptr;

    bool drain(QString* error)
    {
        for (;;) {
            const int rc = avcodec_receive_packet(codec.ctx, packet);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0) {
                *error = QStringLiteral("Encoding failed: %1").arg(avError(rc));
                return false;
            }
            av_packet_rescale_ts(packet, codec.ctx->time_base, stream->time_base);
            packet->stream_index = stream->index;
            const int wrc = av_interleaved_write_frame(format, packet);
            av_packet_unref(packet);
            if (wrc < 0) {
                *error = QStringLiteral("Writing failed: %1").arg(avError(wrc));
                return false;
            }
        }
    }

    void release()
    {
        sws_freeContext(sws);
        sws = nullptr;
        av_frame_free(&swFrame);
        av_packet_free(&packet);
        freeSetup(codec);
        if (format) {
            if (!(format->oformat->flags & AVFMT_NOFILE))
                avio_closep(&format->pb);
            avformat_free_context(format);
            format = nullptr;
        }
    }
#endif
};

VideoEncoder::VideoEncoder() : d(std::make_unique<Impl>()) {}

VideoEncoder::~VideoEncoder()
{
    if (d->open)
        abort();
}

QString VideoEncoder::encoderName() const { return d->encoder; }
int VideoEncoder::framesWritten() const { return d->frames; }

bool VideoEncoder::open(const VideoSettings& settings, QString* error)
{
    d->settings = settings;
    d->frames = 0;
    if (settings.size.width() % 2 || settings.size.height() % 2 || settings.size.isEmpty()) {
        *error = QStringLiteral("Video dimensions must be even and non-zero");
        return false;
    }
    if (settings.fps < 1 || settings.fps > 240) {
        *error = QStringLiteral("Frame rate must be between 1 and 240");
        return false;
    }

    if (settings.format == VideoFormat::PngSequence) {
        if (!QDir().mkpath(settings.path)) {
            *error = QStringLiteral("Cannot create directory %1").arg(settings.path);
            return false;
        }
        d->encoder = QStringLiteral("png");
        d->open = true;
        return true;
    }

#ifdef PRISMFORGE_HAVE_FFMPEG
    const VideoFormatInfo* info = nullptr;
    for (const VideoFormatInfo& f : videoFormats())
        if (f.format == settings.format)
            info = &f;
    if (!info || !info->available) {
        *error = QStringLiteral("%1 is not available: %2").arg(info ? info->label : QStringLiteral("format"),
                                                              info ? info->reason : QString());
        return false;
    }

    const QByteArray path = QFile::encodeName(settings.path);
    int rc = avformat_alloc_output_context2(&d->format, nullptr, nullptr, path.constData());
    if (rc < 0 || !d->format) {
        *error = QStringLiteral("Unsupported output file %1: %2").arg(settings.path, avError(rc));
        return false;
    }
    const bool globalHeader = d->format->oformat->flags & AVFMT_GLOBALHEADER;

    QString why;
    bool opened = false;
    for (const Candidate& candidate : candidatesFor(settings.format)) {
        if (openCodec(candidate, settings.format, settings.size, settings.fps, settings.quality, globalHeader,
                      &d->codec, &why)) {
            d->encoder = QString::fromLatin1(candidate.encoder);
            opened = true;
            break;
        }
    }
    if (!opened) {
        *error = QStringLiteral("Could not open an encoder: %1").arg(why);
        d->release();
        return false;
    }

    d->stream = avformat_new_stream(d->format, nullptr);
    avcodec_parameters_from_context(d->stream->codecpar, d->codec.ctx);
    d->stream->time_base = d->codec.ctx->time_base;
    d->stream->avg_frame_rate = d->codec.ctx->framerate;

    if (!(d->format->oformat->flags & AVFMT_NOFILE)) {
        rc = avio_open(&d->format->pb, path.constData(), AVIO_FLAG_WRITE);
        if (rc < 0) {
            *error = QStringLiteral("Cannot write %1: %2").arg(settings.path, avError(rc));
            d->release();
            return false;
        }
    }
    AVDictionary* muxOptions = nullptr;
    if (settings.path.endsWith(QStringLiteral(".mp4"), Qt::CaseInsensitive) ||
        settings.path.endsWith(QStringLiteral(".mov"), Qt::CaseInsensitive))
        av_dict_set(&muxOptions, "movflags", "+faststart", 0); // playable while streaming
    rc = avformat_write_header(d->format, &muxOptions);
    av_dict_free(&muxOptions);
    if (rc < 0) {
        *error = QStringLiteral("Cannot start %1: %2").arg(settings.path, avError(rc));
        d->release();
        QFile::remove(settings.path);
        return false;
    }

    const AVPixelFormat target = d->codec.ctx->pix_fmt == AV_PIX_FMT_VAAPI ? AV_PIX_FMT_NV12 : d->codec.ctx->pix_fmt;
    d->swFrame = av_frame_alloc();
    d->swFrame->format = target;
    d->swFrame->width = settings.size.width();
    d->swFrame->height = settings.size.height();
    av_frame_get_buffer(d->swFrame, 0);
    d->packet = av_packet_alloc();
    d->sws = sws_getContext(settings.size.width(), settings.size.height(), AV_PIX_FMT_RGB0, settings.size.width(),
                            settings.size.height(), target, SWS_BICUBIC, nullptr, nullptr, nullptr);
    // RGB -> limited-range BT.709 YUV, matching the colour tags on the stream.
    sws_setColorspaceDetails(d->sws, sws_getCoefficients(SWS_CS_ITU709), 1, sws_getCoefficients(SWS_CS_ITU709), 0,
                             0, 1 << 16, 1 << 16);
    d->open = true;
    return true;
#else
    *error = QStringLiteral("PrismForge was built without FFmpeg; only PNG sequences are available");
    return false;
#endif
}

bool VideoEncoder::writeFrame(const QImage& input, QString* error)
{
    if (!d->open) {
        *error = QStringLiteral("Encoder is not open");
        return false;
    }
    const QImage frame = input.format() == QImage::Format_RGBX8888 ? input
                                                                   : input.convertToFormat(QImage::Format_RGBX8888);
    if (frame.size() != d->settings.size) {
        *error = QStringLiteral("Frame size does not match the video size");
        return false;
    }

    if (d->settings.format == VideoFormat::PngSequence) {
        const QString path =
            QDir(d->settings.path).filePath(QStringLiteral("frame_%1.png").arg(d->frames, 5, 10, QLatin1Char('0')));
        if (!frame.save(path, "PNG")) {
            *error = QStringLiteral("Cannot write %1").arg(path);
            return false;
        }
        ++d->frames;
        return true;
    }

#ifdef PRISMFORGE_HAVE_FFMPEG
    int rc = av_frame_make_writable(d->swFrame);
    if (rc < 0) {
        *error = avError(rc);
        return false;
    }
    const uint8_t* src[1] = {frame.constBits()};
    const int stride[1] = {int(frame.bytesPerLine())};
    sws_scale(d->sws, src, stride, 0, frame.height(), d->swFrame->data, d->swFrame->linesize);
    d->swFrame->pts = d->frames;

    AVFrame* toSend = d->swFrame;
    AVFrame* hwFrame = nullptr;
    if (d->codec.ctx->pix_fmt == AV_PIX_FMT_VAAPI) {
        hwFrame = av_frame_alloc();
        rc = av_hwframe_get_buffer(d->codec.ctx->hw_frames_ctx, hwFrame, 0);
        if (rc >= 0)
            rc = av_hwframe_transfer_data(hwFrame, d->swFrame, 0);
        if (rc < 0) {
            av_frame_free(&hwFrame);
            *error = QStringLiteral("GPU upload failed: %1").arg(avError(rc));
            return false;
        }
        hwFrame->pts = d->swFrame->pts;
        toSend = hwFrame;
    }
    rc = avcodec_send_frame(d->codec.ctx, toSend);
    av_frame_free(&hwFrame);
    if (rc < 0) {
        *error = QStringLiteral("Encoding failed: %1").arg(avError(rc));
        return false;
    }
    if (!d->drain(error))
        return false;
    ++d->frames;
    return true;
#else
    *error = QStringLiteral("Built without FFmpeg");
    return false;
#endif
}

bool VideoEncoder::finish(QString* error)
{
    if (!d->open) {
        *error = QStringLiteral("Encoder is not open");
        return false;
    }
    d->open = false;
    if (d->settings.format == VideoFormat::PngSequence)
        return true;
#ifdef PRISMFORGE_HAVE_FFMPEG
    bool ok = avcodec_send_frame(d->codec.ctx, nullptr) >= 0 && d->drain(error);
    if (ok) {
        const int rc = av_write_trailer(d->format);
        if (rc < 0) {
            *error = QStringLiteral("Finalising failed: %1").arg(avError(rc));
            ok = false;
        }
    }
    d->release();
    if (!ok)
        QFile::remove(d->settings.path);
    return ok;
#else
    return false;
#endif
}

void VideoEncoder::abort()
{
    if (!d->open)
        return;
    d->open = false;
    if (d->settings.format == VideoFormat::PngSequence)
        return; // keep the frames written so far; they are complete files
#ifdef PRISMFORGE_HAVE_FFMPEG
    d->release();
    QFile::remove(d->settings.path);
#endif
}

} // namespace pf::media
