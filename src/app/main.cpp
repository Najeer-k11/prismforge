#include "app/MainWindow.h"
#include "app/Theme.h"
#include "diagnostics/LogCapture.h"
#include "diagnostics/Logging.h"

#include <QApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QSurfaceFormat>
#include <QTimer>

#include <algorithm>
#include <cstring>

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
    parser.addOptions({screenshotOption, delayOption, smokeOption, glDebugOption, graphOption, exportOption});
    parser.process(app);

    pf::applyTheme(app);

    pf::MainWindow window;
    if (!parser.positionalArguments().isEmpty())
        window.openProject(parser.positionalArguments().constFirst());

    if (parser.isSet(graphOption))
        window.enableGraphMode();
    if (parser.isSet(exportOption))
        window.exportAfterFirstBuild(parser.value(exportOption));

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
