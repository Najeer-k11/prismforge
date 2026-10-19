// Integration tests that run the real OpenGL backend on the real GPU.
// They render into an offscreen framebuffer and inspect the pixels.

#include "graph/GraphCodegen.h"
#include "render/gl/GLRenderer.h"
#include "render/OrbitCamera.h"

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <optional>

#include <memory>

using namespace pf;

namespace {

QString readShader(const char* name)
{
    QFile file(QStringLiteral(PF_SHADER_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

const char* kSolidVertex = R"(#version 450 core
layout(location = 0) in vec3 aPosition;
uniform mat4 uModel, uView, uProjection;
void main() { gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0); }
)";

QString solidFragment(const char* rgb)
{
    return QStringLiteral("#version 450 core\nout vec4 c;\nvoid main() { c = vec4(%1, 1.0); }\n")
        .arg(QLatin1StringView(rgb));
}

// One GL 4.5 context + FBO shared by all test cases.
struct GlFixture {
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QOpenGLContext> context;
    std::unique_ptr<QOpenGLFramebufferObject> fbo;
    QString error;

    bool create()
    {
        QSurfaceFormat fmt;
        fmt.setVersion(4, 5);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        context = std::make_unique<QOpenGLContext>();
        context->setFormat(fmt);
        if (!context->create()) {
            error = "could not create an OpenGL context";
            return false;
        }
        surface = std::make_unique<QOffscreenSurface>();
        surface->setFormat(context->format());
        surface->create();
        if (!context->makeCurrent(surface.get())) {
            error = "could not make the context current";
            return false;
        }
        fbo = std::make_unique<QOpenGLFramebufferObject>(
            QSize(128, 128), QOpenGLFramebufferObject::Depth);
        return fbo->isValid() || (error = "FBO creation failed", false);
    }
};

GlFixture* g_gl = nullptr;

// Created on first use, so listing tests needs no GL (CMake runs the binary
// at build time to discover them) and headless machines just skip.
// Owned here (not a function static) so main() can destroy it while the
// QGuiApplication still exists.
std::unique_ptr<GlFixture> g_fixture;
bool g_fixtureAttempted = false;

GlFixture* glFixture()
{
    if (!g_fixtureAttempted) {
        g_fixtureAttempted = true;
        auto f = std::make_unique<GlFixture>();
        if (f->create())
            g_fixture = std::move(f);
        else
            std::fprintf(stderr, "No OpenGL 4.5 core context: %s\n", qPrintable(f->error));
    }
    return g_fixture.get();
}

QImage renderOnce(Renderer& renderer, PreviewMesh mesh = PreviewMesh::Sphere)
{
    renderer.setMesh(makeMesh(mesh));
    OrbitCamera cam;
    FrameParams params;
    params.view = cam.viewMatrix();
    params.projection = cam.projectionMatrix(1.0f);
    params.cameraPosition = cam.position();
    params.framebufferSize = g_gl->fbo->size();
    g_gl->fbo->bind();
    renderer.renderFrame(params);
    g_gl->fbo->release();
    return g_gl->fbo->toImage();
}

QRgb centre(const QImage& img) { return img.pixel(img.width() / 2, img.height() / 2); }
QRgb corner(const QImage& img) { return img.pixel(1, 1); }

struct RendererScope {
    std::optional<gl::GLRenderer> holder;

    RendererScope()
    {
        g_gl = glFixture();
        if (!g_gl)
            SKIP("no OpenGL 4.5 core context available");
        holder.emplace(g_gl->context.get());
        QString error;
        REQUIRE(holder->initialize(&error));
    }
    ~RendererScope()
    {
        if (holder)
            holder->shutdown();
    }
    gl::GLRenderer& renderer() { return *holder; }
};

} // namespace

TEST_CASE("Renderer reports device information", "[gl]")
{
    RendererScope s;
    const RendererInfo info = s.renderer().info();
    CHECK(info.api == "OpenGL");
    CHECK_FALSE(info.version.isEmpty());
    CHECK_FALSE(info.device.isEmpty());
    WARN("Device: " << info.device.toStdString() << " | " << info.version.toStdString());
}

TEST_CASE("Default shaders compile, link and draw", "[gl]")
{
    RendererScope s;
    const auto result = s.renderer().buildProgram({readShader("default.vert"), readShader("default.frag")});
    for (const Diagnostic& d : result.diagnostics)
        UNSCOPED_INFO(d.message.toStdString());
    REQUIRE(result.success);
    REQUIRE(s.renderer().hasProgram());

    const QImage img = renderOnce(s.renderer());
    CHECK(centre(img) != corner(img)); // the sphere covers the centre, not the corner
    CHECK(s.renderer().lastFrameStats().drawCalls == 1);
    CHECK(s.renderer().lastFrameStats().triangles == makeSphere().triangleCount());
}

TEST_CASE("A solid colour shader produces that colour", "[gl]")
{
    RendererScope s;
    REQUIRE(s.renderer().buildProgram({kSolidVertex, solidFragment("1.0, 0.0, 0.0")}).success);
    for (PreviewMesh mesh : {PreviewMesh::Sphere, PreviewMesh::Cube}) {
        const QImage img = renderOnce(s.renderer(), mesh);
        CHECK(centre(img) == qRgb(255, 0, 0));
        CHECK(corner(img) != qRgb(255, 0, 0));
    }
}

TEST_CASE("A failed compile keeps the previous program active", "[gl]")
{
    RendererScope s;
    REQUIRE(s.renderer().buildProgram({kSolidVertex, solidFragment("0.0, 1.0, 0.0")}).success);
    REQUIRE(centre(renderOnce(s.renderer())) == qRgb(0, 255, 0));

    const QString broken = "#version 450 core\nout vec4 c;\nvoid main() {\n  c = vec4(nope);\n}\n";
    const auto result = s.renderer().buildProgram({kSolidVertex, broken});
    CHECK_FALSE(result.success);
    REQUIRE(hasErrors(result.diagnostics));

    bool foundLine = false;
    for (const Diagnostic& d : result.diagnostics) {
        UNSCOPED_INFO("driver: " << d.line << ": " << d.message.toStdString());
        if (d.severity == Severity::Error && d.stage == ShaderStage::Fragment && d.line == 4)
            foundLine = true;
    }
    CHECK(foundLine); // driver-specific format, parsed to a source line

    CHECK(s.renderer().hasProgram());
    CHECK(centre(renderOnce(s.renderer())) == qRgb(0, 255, 0)); // still the green program
}

TEST_CASE("A link failure keeps the previous program active", "[gl]")
{
    RendererScope s;
    REQUIRE(s.renderer().buildProgram({kSolidVertex, solidFragment("0.0, 0.0, 1.0")}).success);

    // Fragment input with no matching vertex output fails at link time.
    const QString fragment = "#version 450 core\nin vec3 vMissing;\nout vec4 c;\n"
                             "void main() { c = vec4(vMissing, 1.0); }\n";
    const auto result = s.renderer().buildProgram({kSolidVertex, fragment});
    CHECK_FALSE(result.success);
    CHECK(hasErrors(result.diagnostics));
    CHECK(centre(renderOnce(s.renderer())) == qRgb(0, 0, 255));
}

TEST_CASE("Editable uniforms are introspected with their initial values", "[gl]")
{
    RendererScope s;
    const QString fragment = "#version 450 core\nout vec4 c;\n"
                             "uniform vec3 uTint = vec3(0.25, 0.5, 1.0);\n"
                             "uniform float uGain = 2.0;\n"
                             "uniform bool uFlip = true;\n"
                             "uniform mat4 uUnsupported;\n"
                             "uniform float uTime;\n"
                             "void main() { c = vec4(uTint * uGain * (uFlip ? 1.0 : 0.5), 1.0) "
                             "+ uUnsupported[0] * 1e-6 + uTime * 0.0; }\n";
    const auto result = s.renderer().buildProgram({kSolidVertex, fragment});
    REQUIRE(result.success);

    QHash<QString, MaterialParameter> byName;
    for (const MaterialParameter& p : result.parameters)
        byName.insert(p.name, p);
    CHECK(byName.size() == 3); // uTime is built-in, the mat4 is not editable
    REQUIRE(byName.contains("uTint"));
    CHECK(byName["uTint"].type == ParamType::Vec3);
    CHECK(byName["uTint"].defaultValue == ParamValue{0.25f, 0.5f, 1.0f, 0.0f});
    CHECK(byName["uGain"].defaultValue[0] == 2.0f);
    CHECK(byName["uFlip"].type == ParamType::Bool);
    CHECK(byName["uFlip"].defaultValue[0] == 1.0f);

    bool reportedUnsupported = false;
    for (const Diagnostic& d : result.diagnostics)
        reportedUnsupported |= d.severity == Severity::Info && d.message.contains("uUnsupported");
    CHECK(reportedUnsupported);
}

TEST_CASE("Setting a parameter changes the rendered output", "[gl]")
{
    RendererScope s;
    const QString fragment = "#version 450 core\nout vec4 c;\n"
                             "uniform vec3 uTint = vec3(1.0, 0.0, 0.0);\n"
                             "void main() { c = vec4(uTint, 1.0); }\n";
    REQUIRE(s.renderer().buildProgram({kSolidVertex, fragment}).success);
    CHECK(centre(renderOnce(s.renderer())) == qRgb(255, 0, 0));

    s.renderer().setParameter("uTint", ParamType::Vec3, {0.0f, 0.0f, 1.0f, 0.0f});
    CHECK(centre(renderOnce(s.renderer())) == qRgb(0, 0, 255));

    // Wrong type or unknown name: ignored, no GL error, colour unchanged.
    s.renderer().setParameter("uTint", ParamType::Float, {1.0f, 1.0f, 1.0f, 1.0f});
    s.renderer().setParameter("uMissing", ParamType::Vec3, {1.0f, 1.0f, 1.0f, 1.0f});
    CHECK(centre(renderOnce(s.renderer())) == qRgb(0, 0, 255));
}

TEST_CASE("The default shader graph links and draws on the GPU", "[gl][graph]")
{
    RendererScope s;
    const auto generated = graph::generateFragmentShader(graph::makeDefaultGraph(), graph::NodeRegistry::builtin());
    REQUIRE(generated.success);
    const auto result = s.renderer().buildProgram({readShader("default.vert"), generated.source});
    for (const Diagnostic& d : result.diagnostics)
        UNSCOPED_INFO(d.message.toStdString());
    REQUIRE(result.success);

    QStringList names;
    for (const MaterialParameter& p : result.parameters)
        names << p.name;
    CHECK(names == QStringList{"uDeepColor", "uHighlightColor", "uRoughness"});

    const QImage img = renderOnce(s.renderer());
    CHECK(centre(img) != corner(img));
}

TEST_CASE("Nothing is drawn before any program is built", "[gl]")
{
    RendererScope s;
    CHECK_FALSE(s.renderer().hasProgram());
    const QImage img = renderOnce(s.renderer());
    CHECK(centre(img) == corner(img));
    CHECK(s.renderer().lastFrameStats().drawCalls == 0);
}

int main(int argc, char* argv[])
{
    // Without a display server, use the offscreen platform; GL may then be
    // unavailable and the GPU tests are skipped (Catch2 exits with 4).
    if (qEnvironmentVariableIsEmpty("DISPLAY") && qEnvironmentVariableIsEmpty("WAYLAND_DISPLAY"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    const int result = Catch::Session().run(argc, argv);
    if (g_fixture) {
        g_fixture->fbo.reset(); // needs the context current
        g_fixture->context->doneCurrent();
        g_fixture.reset();
    }
    return result;
}
