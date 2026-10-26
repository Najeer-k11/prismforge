// Integration tests that run the real OpenGL backend on the real GPU.
// They render into an offscreen framebuffer and inspect the pixels.

#include "graph/GraphCodegen.h"
#include "render/FrameSetup.h"
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

#include <cmath>
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

TEST_CASE("Offscreen render produces a full-size opaque image", "[gl][export]")
{
    RendererScope s;
    REQUIRE(s.renderer().buildProgram({kSolidVertex, solidFragment("1.0, 0.5, 0.0")}).success);
    s.renderer().setMesh(makeScreenQuad());

    // A non-square size larger than any window, with MSAA.
    const QSize size(2560, 1440);
    QImage image;
    QString error;
    REQUIRE(s.renderer().renderToImage(makeFrameParams({}, PreviewMesh::Screen, size, 0.0f), 4, &image, &error));
    CHECK(image.size() == size);
    // The screen quad covers every pixel, corners included.
    // 0.5 * 255 = 127.5: drivers may round either way (Mesa 128, NVIDIA 127).
    auto nearOrange = [](QRgb c) { return qRed(c) == 255 && std::abs(qGreen(c) - 128) <= 1 && qBlue(c) == 0; };
    for (QPoint p : {QPoint(0, 0), QPoint(2559, 0), QPoint(0, 1439), QPoint(2559, 1439), QPoint(1280, 720)})
        CHECK(nearOrange(image.pixel(p)));
    CHECK(qAlpha(image.pixel(10, 10)) == 255);

    // Too large for the device: a clear error, no crash.
    const QSize huge(s.renderer().maxRenderSize().width() + 1, 16);
    CHECK_FALSE(s.renderer().renderToImage(makeFrameParams({}, PreviewMesh::Screen, huge, 0.0f), 0, &image, &error));
    CHECK(error.contains("maximum render size"));
}

TEST_CASE("Offscreen render is top-down and respects the camera", "[gl][export]")
{
    RendererScope s;
    // Red above the horizon (world y > 0), blue below.
    const QString vertex = "#version 450 core\nlayout(location = 0) in vec3 aPosition;\n"
                           "uniform mat4 uModel, uView, uProjection;\nout float vY;\n"
                           "void main() { vY = aPosition.y; gl_Position = uProjection * uView * uModel * vec4(aPosition, 1.0); }\n";
    const QString fragment = "#version 450 core\nin float vY;\nout vec4 c;\n"
                             "void main() { c = vY > 0.0 ? vec4(1, 0, 0, 1) : vec4(0, 0, 1, 1); }\n";
    REQUIRE(s.renderer().buildProgram({vertex, fragment}).success);
    s.renderer().setMesh(makeScreenQuad());
    QImage image;
    QString error;
    REQUIRE(s.renderer().renderToImage(makeFrameParams({}, PreviewMesh::Screen, {64, 64}, 0.0f), 0, &image, &error));
    CHECK(image.pixel(32, 4) == qRgb(255, 0, 0));  // top row is up
    CHECK(image.pixel(32, 60) == qRgb(0, 0, 255));
}

TEST_CASE("Nothing is drawn before any program is built", "[gl]")
{
    RendererScope s;
    CHECK_FALSE(s.renderer().hasProgram());
    const QImage img = renderOnce(s.renderer());
    CHECK(centre(img) == corner(img));
    CHECK(s.renderer().lastFrameStats().drawCalls == 0);
}

// ---------------------------------------------------------------- channels

namespace {

const char* kScreenVertex = R"(#version 450 core
layout(location = 0) in vec3 aPosition;
void main() { gl_Position = vec4(aPosition.xy, 0.0, 1.0); }
)";

// Fragment shader that shows uChannel0 at the uv in uniform uSample.
const char* kSampleFragment = R"(#version 450 core
uniform sampler2D uChannel0;
uniform vec2 uSample = vec2(0.25, 0.25);
out vec4 c;
void main() { c = vec4(texture(uChannel0, uSample).rgb, 1.0); }
)";

// 2×2, top row first: red green / blue white.
QImage quadColours()
{
    QImage image(2, 2, QImage::Format_RGBA8888);
    image.setPixelColor(0, 0, Qt::red);
    image.setPixelColor(1, 0, Qt::green);
    image.setPixelColor(0, 1, Qt::blue);
    image.setPixelColor(1, 1, Qt::white);
    return image;
}

QRgb sampleAt(Renderer& renderer, float u, float v)
{
    renderer.setParameter("uSample", ParamType::Vec2, {u, v, 0.0f, 0.0f});
    return centre(renderOnce(renderer, PreviewMesh::Screen));
}

ChannelSampler nearest(bool flipY, TextureWrap wrap = TextureWrap::Repeat)
{
    return {TextureFilter::Nearest, wrap, flipY};
}

} // namespace

TEST_CASE("Channel textures are sampled through uChannel0", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    REQUIRE(r.buildProgram({kScreenVertex, kSampleFragment}).success);
    QString error;

    // Without flipping, v = 0 is the image's first (top) row.
    REQUIRE(r.setChannel(0, quadColours(), nearest(false), &error));
    CHECK(sampleAt(r, 0.25f, 0.25f) == qRgb(255, 0, 0));
    CHECK(sampleAt(r, 0.75f, 0.25f) == qRgb(0, 255, 0));
    CHECK(sampleAt(r, 0.25f, 0.75f) == qRgb(0, 0, 255));

    // Flip Y: v = 0 is the picture's bottom row, as with gl_FragCoord.
    REQUIRE(r.setChannel(0, quadColours(), nearest(true), &error));
    CHECK(sampleAt(r, 0.25f, 0.25f) == qRgb(0, 0, 255));
    CHECK(sampleAt(r, 0.75f, 0.75f) == qRgb(0, 255, 0));
}

TEST_CASE("Channel wrap modes apply outside [0, 1]", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    REQUIRE(r.buildProgram({kScreenVertex, kSampleFragment}).success);
    QString error;
    const QImage image = quadColours();
    REQUIRE(r.setChannel(0, image, nearest(false, TextureWrap::Repeat), &error));
    CHECK(sampleAt(r, 1.25f, 0.25f) == qRgb(255, 0, 0));
    // Same image, new settings: updated without re-uploading.
    REQUIRE(r.setChannel(0, image, nearest(false, TextureWrap::Clamp), &error));
    CHECK(sampleAt(r, 1.25f, 0.25f) == qRgb(0, 255, 0));
    REQUIRE(r.setChannel(0, image, nearest(false, TextureWrap::Mirror), &error));
    CHECK(sampleAt(r, 1.25f, 0.25f) == qRgb(0, 255, 0));
    CHECK(sampleAt(r, 1.75f, 0.25f) == qRgb(255, 0, 0));
}

TEST_CASE("Unbinding a channel makes it read black", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    REQUIRE(r.buildProgram({kScreenVertex, kSampleFragment}).success);
    QString error;
    REQUIRE(r.setChannel(0, quadColours(), nearest(false), &error));
    REQUIRE(sampleAt(r, 0.25f, 0.25f) == qRgb(255, 0, 0));
    REQUIRE(r.setChannel(0, QImage(), nearest(false), &error));
    CHECK(sampleAt(r, 0.25f, 0.25f) == qRgb(0, 0, 0));
}

TEST_CASE("Channels survive rebuilding the program", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    QString error;
    REQUIRE(r.setChannel(0, quadColours(), nearest(false), &error));
    REQUIRE(r.buildProgram({kScreenVertex, kSampleFragment}).success);
    CHECK(sampleAt(r, 0.75f, 0.25f) == qRgb(0, 255, 0));
}

TEST_CASE("uChannelResolution reports texture sizes", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    const QString fragment = "#version 450 core\nuniform vec3 uChannelResolution[4];\nout vec4 c;\n"
                             "void main() { c = vec4(uChannelResolution[2].xy / 255.0, "
                             "uChannelResolution[2].z - uChannelResolution[1].x, 1.0); }\n";
    REQUIRE(r.buildProgram({kScreenVertex, fragment}).success);
    QString error;
    REQUIRE(r.setChannel(2, QImage(4, 2, QImage::Format_RGBA8888), nearest(true), &error));
    CHECK(centre(renderOnce(r, PreviewMesh::Screen)) == qRgb(4, 2, 255)); // z = 1; channel 1 unbound: 0
}

TEST_CASE("uMouse is passed through", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    const QString fragment = "#version 450 core\nuniform vec4 uMouse;\nout vec4 c;\n"
                             "void main() { c = vec4(uMouse.xy, -uMouse.zw) / 255.0; c.a = 1.0; }\n";
    REQUIRE(r.buildProgram({kScreenVertex, fragment}).success);
    r.setMesh(makeMesh(PreviewMesh::Screen));
    FrameParams params;
    params.framebufferSize = g_gl->fbo->size();
    params.mouse = QVector4D(10.0f, 20.0f, -30.0f, -40.0f); // released
    g_gl->fbo->bind();
    r.renderFrame(params);
    g_gl->fbo->release();
    CHECK(centre(g_gl->fbo->toImage()) == qRgb(10, 20, 30));
}

TEST_CASE("Samplers are not parameters; misuse is diagnosed", "[gl][channels]")
{
    RendererScope s;
    const QString fragment = "#version 450 core\n"
                             "uniform samplerCube uChannel1;\n"
                             "uniform sampler2D uOther;\n"
                             "uniform sampler2D uChannel0;\n"
                             "out vec4 c;\n"
                             "void main() { c = texture(uChannel1, vec3(1.0)) + texture(uOther, vec2(0.5)) "
                             "+ texture(uChannel0, vec2(0.5)); }\n";
    const auto result = s.renderer().buildProgram({kScreenVertex, fragment});
    REQUIRE(result.success);
    CHECK(result.parameters.isEmpty());
    bool cubeWarning = false, otherInfo = false, channel0Reported = false;
    for (const Diagnostic& d : result.diagnostics) {
        cubeWarning |= d.severity == Severity::Warning && d.message.contains("uChannel1");
        otherInfo |= d.severity == Severity::Info && d.message.contains("uOther");
        channel0Reported |= d.message.contains("'uChannel0'");
    }
    CHECK(cubeWarning);
    CHECK(otherInfo);
    CHECK_FALSE(channel0Reported);
}

TEST_CASE("Channel images beyond the GPU's limit are rejected", "[gl][channels]")
{
    RendererScope s;
    Renderer& r = s.renderer();
    REQUIRE(r.buildProgram({kScreenVertex, kSampleFragment}).success);
    int maxTexture = 0;
    for (const auto& [name, value] : r.info().capabilities)
        if (name == "Max texture size")
            maxTexture = value.toInt();
    REQUIRE(maxTexture > 0);
    QString error;
    REQUIRE(r.setChannel(0, quadColours(), nearest(false), &error));
    CHECK_FALSE(r.setChannel(0, QImage(maxTexture + 1, 1, QImage::Format_RGBA8888), nearest(false), &error));
    CHECK(error.contains("maximum texture size"));
    CHECK(sampleAt(r, 0.25f, 0.25f) == qRgb(255, 0, 0)); // previous texture kept
    CHECK_FALSE(r.setChannel(kChannelCount, quadColours(), nearest(false), &error));
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
