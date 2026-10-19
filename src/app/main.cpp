#include "app/MainWindow.h"
#include "app/Theme.h"
#include "diagnostics/LogCapture.h"
#include "diagnostics/Logging.h"
#include "media/VideoEncoder.h"
#include "render/RenderPresets.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QIcon>
#include <QSurfaceFormat>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <optional>

int main(int argc, char* argv[])
{
    pf::LogCapture::install();

    // Parsed before QApplication: the surface format must be set first.
    const bool glDebug = std::any_of(argv + 1, argv + argc,
                                     [](const char* a) { return std::strcmp(a, "--gl-debug") == 0; });

    // The context format must be set before the application object exists.
    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setVersion(4, 5);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setDepthBufferSize(24);
    format.setSamples(4);
    format.setSwapInterval(1);
    if (glDebug)
        format.setOption(QSurfaceFormat::DebugContext);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("PrismForge"));
    QApplication::setApplicationName(QStringLiteral("PrismForge"));
    QApplication::setApplicationVersion(QStringLiteral(PRISMFORGE_VERSION));
    // Matches the installed .desktop file, so Wayland compositors and task
    // bars associate windows with the PrismForge launcher and icon.
    QGuiApplication::setDesktopFileName(QStringLiteral(PRISMFORGE_APP_ID));
    QIcon icon;
    for (int size : {16, 32, 48, 64, 128, 256})
        icon.addFile(QStringLiteral(":/icons/prismforge-%1.png").arg(size), QSize(size, size));
    QApplication::setWindowIcon(icon);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("GPU shader development environment"));
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(QStringLiteral("project"), QStringLiteral("Project file (*.pfproj) to open."));
    const QCommandLineOption screenshotOption(
        QStringLiteral("screenshot"),
        QStringLiteral("Save a screenshot of the main window to <file> and quit."), QStringLiteral("file"));
    const QCommandLineOption delayOption(
        QStringLiteral("screenshot-delay"),
        QStringLiteral("Milliseconds to wait before taking the screenshot (default 2000)."),
        QStringLiteral("ms"), QStringLiteral("2000"));
    const QCommandLineOption smokeOption(
        QStringLiteral("smoke-replace-fragment"),
        QStringLiteral("After the first successful compile, replace the fragment shader with the "
                       "contents of <file> and compile again (for scripted testing)."),
        QStringLiteral("file"));
    const QCommandLineOption glDebugOption(
        QStringLiteral("gl-debug"),
        QStringLiteral("Create an OpenGL debug context and show driver messages in the Log panel."));
    const QCommandLineOption graphOption(
        QStringLiteral("graph"), QStringLiteral("Start with the shader graph generating the fragment shader."));
    const QCommandLineOption exportOption(
        QStringLiteral("export"),
        QStringLiteral("Export the material to <directory> after the first successful compile, then quit."),
        QStringLiteral("directory"));
    const QCommandLineOption renderImageOption(
        QStringLiteral("render-image"),
        QStringLiteral("Render a frame to <file> (.png/.jpg/.webp) after the first successful compile, then quit."),
        QStringLiteral("file"));
    const QCommandLineOption renderVideoOption(
        QStringLiteral("render-video"),
        QStringLiteral("Render a video to <file> (a directory for --format png), then quit."), QStringLiteral("file"));
    const QCommandLineOption sizeOption(QStringLiteral("size"),
                                        QStringLiteral("Output size: WxH or 1080p, 2k, 4k, 5k, 8k (default 4k "
                                                       "for images, 1080p for video)."),
                                        QStringLiteral("size"));
    const QCommandLineOption samplesOption(QStringLiteral("samples"), QStringLiteral("MSAA samples (default 8)."),
                                           QStringLiteral("n"), QStringLiteral("8"));
    const QCommandLineOption timeOption(QStringLiteral("time"),
                                        QStringLiteral("uTime of the image, or of the first video frame (default 0)."),
                                        QStringLiteral("seconds"), QStringLiteral("0"));
    const QCommandLineOption fpsOption(QStringLiteral("fps"), QStringLiteral("Video frame rate (default 30)."),
                                       QStringLiteral("fps"), QStringLiteral("30"));
    const QCommandLineOption durationOption(QStringLiteral("duration"),
                                            QStringLiteral("Video length in seconds (default 10)."),
                                            QStringLiteral("seconds"), QStringLiteral("10"));
    const QCommandLineOption formatOption(QStringLiteral("format"),
                                          QStringLiteral("Video format: h264, hevc, av1, vp9, prores or png "
                                                         "(default: first available)."),
                                          QStringLiteral("format"));
    const QCommandLineOption qualityOption(QStringLiteral("quality"),
                                           QStringLiteral("Video quality: high, medium or small (default high)."),
                                           QStringLiteral("quality"), QStringLiteral("high"));
    const QCommandLineOption turntableOption(QStringLiteral("turntable"),
                                             QStringLiteral("Orbit the camera 360° over the video."));
    const QCommandLineOption listFormatsOption(QStringLiteral("list-video-formats"),
                                               QStringLiteral("Print the available video formats and exit."));
    parser.addOptions({screenshotOption, delayOption, smokeOption, glDebugOption, graphOption, exportOption,
                       renderImageOption, renderVideoOption, sizeOption, samplesOption, timeOption, fpsOption,
                       durationOption, formatOption, qualityOption, turntableOption, listFormatsOption});
    parser.process(app);

    if (parser.isSet(listFormatsOption)) {
        for (const pf::media::VideoFormatInfo& f : pf::media::videoFormats())
            std::printf("%-7s %-50s %s\n", qPrintable(f.key), qPrintable(f.label),
                        f.available ? "available" : qPrintable(QStringLiteral("unavailable: ") + f.reason));
        return 0;
    }

    auto fail = [](const QString& message) {
        std::fprintf(stderr, "prismforge: %s\n", qPrintable(message));
        return 2;
    };
    auto sizeFor = [&](const char* fallback) -> std::optional<QSize> {
        return pf::parseResolution(parser.isSet(sizeOption) ? parser.value(sizeOption) : QString::fromLatin1(fallback));
    };

    pf::applyTheme(app);

    pf::MainWindow window;
    if (!parser.positionalArguments().isEmpty())
        window.openProject(parser.positionalArguments().constFirst());

    if (parser.isSet(graphOption))
        window.enableGraphMode();
    if (parser.isSet(exportOption))
        window.exportAfterFirstBuild(parser.value(exportOption));
    if (parser.isSet(renderImageOption)) {
        const auto size = sizeFor("4k");
        if (!size)
            return fail(QStringLiteral("invalid --size '%1'").arg(parser.value(sizeOption)));
        pf::ImageRenderOptions options;
        options.path = parser.value(renderImageOption);
        options.size = *size;
        options.samples = parser.value(samplesOption).toInt();
        options.useCurrentTime = false;
        options.time = parser.value(timeOption).toFloat();
        window.renderImageAfterFirstBuild(options);
    }
    if (parser.isSet(renderVideoOption)) {
        const auto size = sizeFor("1080p");
        if (!size)
            return fail(QStringLiteral("invalid --size '%1'").arg(parser.value(sizeOption)));
        const pf::media::VideoFormatInfo* format = nullptr;
        if (parser.isSet(formatOption)) {
            format = pf::media::findVideoFormat(parser.value(formatOption));
            if (!format)
                return fail(QStringLiteral("unknown --format '%1'").arg(parser.value(formatOption)));
        } else {
            for (const pf::media::VideoFormatInfo& f : pf::media::videoFormats())
                if (f.available && !format)
                    format = &f;
        }
        if (!format->available)
            return fail(QStringLiteral("%1 is unavailable: %2").arg(format->label, format->reason));
        const QString quality = parser.value(qualityOption).toLower();
        pf::VideoRenderOptions options;
        options.video.path = parser.value(renderVideoOption);
        options.video.format = format->format;
        options.video.size = pf::evenSize(*size);
        options.video.fps = parser.value(fpsOption).toInt();
        options.video.quality = quality == u"small"    ? pf::media::VideoQuality::Small
                                : quality == u"medium" ? pf::media::VideoQuality::Medium
                                                       : pf::media::VideoQuality::High;
        options.samples = parser.value(samplesOption).toInt();
        options.duration = parser.value(durationOption).toDouble();
        options.startTime = parser.value(timeOption).toDouble();
        options.turntable = parser.isSet(turntableOption);
        window.renderVideoAfterFirstBuild(options);
    }

    if (parser.isSet(smokeOption)) {
        QFile file(parser.value(smokeOption));
        if (!file.open(QIODevice::ReadOnly)) {
            qCCritical(lcApp).noquote() << "Cannot read" << file.fileName() << ":" << file.errorString();
            return 2;
        }
        window.replaceFragmentAfterFirstBuild(QString::fromUtf8(file.readAll()));
    }

    window.show();

    if (parser.isSet(screenshotOption)) {
        const QString path = parser.value(screenshotOption);
        QTimer::singleShot(parser.value(delayOption).toInt(), &window, [&window, path] {
            const bool saved = window.grab().save(path);
            qCInfo(lcApp).noquote() << (saved ? "Saved screenshot to" : "FAILED to save screenshot to") << path;
            QApplication::exit(saved ? 0 : 1);
        });
    }

    return app.exec();
}
