#pragma once

#include "render/Renderer.h"

#include <QOpenGLFunctions_4_5_Core>

#include <array>
#include <utility>

class QOpenGLContext;

namespace pf::gl {

using Functions = QOpenGLFunctions_4_5_Core;

// Move-only owner of a single GL object name.
class GlName {
public:
    using Deleter = void (*)(Functions*, GLuint);

    GlName() = default;
    GlName(Functions* gl, GLuint name, Deleter deleter) : m_gl(gl), m_name(name), m_deleter(deleter) {}
    ~GlName() { reset(); }

    GlName(GlName&& other) noexcept { *this = std::move(other); }
    GlName& operator=(GlName&& other) noexcept
    {
        if (this != &other) {
            reset();
            m_gl = std::exchange(other.m_gl, nullptr);
            m_name = std::exchange(other.m_name, 0);
            m_deleter = std::exchange(other.m_deleter, nullptr);
        }
        return *this;
    }
    GlName(const GlName&) = delete;
    GlName& operator=(const GlName&) = delete;

    GLuint get() const { return m_name; }
    explicit operator bool() const { return m_name != 0; }

    // Forgets the name without deleting it (used when the context is gone).
    void release() { m_name = 0; }

    void reset()
    {
        if (m_name && m_deleter)
            m_deleter(m_gl, m_name);
        m_name = 0;
    }

private:
    Functions* m_gl = nullptr;
    GLuint m_name = 0;
    Deleter m_deleter = nullptr;
};

// OpenGL 4.5 core backend. Renders into whichever framebuffer is bound when
// renderFrame() is called (the viewport binds its own FBO beforehand).
class GLRenderer final : public Renderer {
public:
    static constexpr int kRequiredMajor = 4;
    static constexpr int kRequiredMinor = 5;

    explicit GLRenderer(QOpenGLContext* context);
    ~GLRenderer() override;

    bool initialize(QString* error) override;
    void shutdown() override;

    ProgramBuildResult buildProgram(const ShaderSources& sources) override;
    bool hasProgram() const override { return bool(m_program); }
    void setParameter(const QString& name, ParamType type, const ParamValue& value) override;

    void setMesh(const MeshData& mesh) override;
    void renderFrame(const FrameParams& params) override;

    FrameStats lastFrameStats() const override { return m_stats; }
    RendererInfo info() const override { return m_info; }

private:
    struct UniformLocations {
        GLint model = -1, view = -1, projection = -1, normalMatrix = -1;
        GLint cameraPosition = -1, time = -1, resolution = -1;
    };

    struct ParameterSlot {
        GLint location = -1;
        ParamType type = ParamType::Float;
    };

    QList<MaterialParameter> introspectParameters(GLuint program, QList<Diagnostic>& diagnostics,
                                                  QHash<QString, ParameterSlot>& slotTable);
    void queryCapabilities(const QSurfaceFormat& format, GLint timerBits);
    GlName compileStage(ShaderStage stage, const QString& source, QList<Diagnostic>& diagnostics);
    void collectGpuTimings();

    QOpenGLContext* m_context = nullptr;
    Functions* m_gl = nullptr;
    RendererInfo m_info;

    GlName m_program;
    UniformLocations m_uniforms;
    QHash<QString, ParameterSlot> m_parameterSlots;

    GlName m_vao, m_vbo, m_ibo;
    GLsizei m_indexCount = 0;

    // Ring of GL_TIME_ELAPSED queries so we never stall waiting on the GPU.
    static constexpr int kTimerQueries = 4;
    std::array<GLuint, kTimerQueries> m_timerQueries{};
    std::array<bool, kTimerQueries> m_timerPending{};
    int m_timerIndex = 0;

    FrameStats m_stats;
};

} // namespace pf::gl
