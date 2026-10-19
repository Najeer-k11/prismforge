#include "render/gl/GLRenderer.h"

#include "diagnostics/Logging.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>

#include <cstddef>

namespace pf::gl {

namespace {

QString glString(Functions* gl, GLenum name)
{
    return QString::fromLatin1(reinterpret_cast<const char*>(gl->glGetString(name)));
}

void deleteShader(Functions* gl, GLuint n) { gl->glDeleteShader(n); }
void deleteProgram(Functions* gl, GLuint n) { gl->glDeleteProgram(n); }
void deleteBuffer(Functions* gl, GLuint n) { gl->glDeleteBuffers(1, &n); }
void deleteVertexArray(Functions* gl, GLuint n) { gl->glDeleteVertexArrays(1, &n); }

QString shaderInfoLog(Functions* gl, GLuint shader)
{
    GLint length = 0;
    gl->glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    QByteArray log(std::max(length, 1), '\0');
    gl->glGetShaderInfoLog(shader, length, nullptr, log.data());
    return QString::fromUtf8(log.constData());
}

QString programInfoLog(Functions* gl, GLuint program)
{
    GLint length = 0;
    gl->glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    QByteArray log(std::max(length, 1), '\0');
    gl->glGetProgramInfoLog(program, length, nullptr, log.data());
    return QString::fromUtf8(log.constData());
}

const QString kOrigin = QStringLiteral("driver");

} // namespace

GLRenderer::GLRenderer(QOpenGLContext* context) : m_context(context) {}

GLRenderer::~GLRenderer()
{
    if (!m_gl)
        return;
    if (QOpenGLContext::currentContext() == m_context) {
        shutdown();
        return;
    }
    // Deleting names on a different (or no) context would be wrong; leak them.
    qCWarning(lcRender) << "GLRenderer destroyed without its context current; leaking GL objects";
    for (GlName* name : {&m_program, &m_vao, &m_vbo, &m_ibo})
        name->release();
}

bool GLRenderer::initialize(QString* error)
{
    const QSurfaceFormat fmt = m_context->format();
    m_info.api = QStringLiteral("OpenGL");

    if (std::pair(fmt.majorVersion(), fmt.minorVersion()) < std::pair(kRequiredMajor, kRequiredMinor)) {
        if (error)
            *error = QStringLiteral("OpenGL %1.%2 core is required, but the context is %3.%4")
                         .arg(kRequiredMajor).arg(kRequiredMinor)
                         .arg(fmt.majorVersion()).arg(fmt.minorVersion());
        return false;
    }

    m_gl = QOpenGLVersionFunctionsFactory::get<Functions>(m_context);
    if (!m_gl || !m_gl->initializeOpenGLFunctions()) {
        m_gl = nullptr;
        if (error)
            *error = QStringLiteral("Failed to resolve OpenGL 4.5 core entry points");
        return false;
    }

    m_info.vendor = glString(m_gl, GL_VENDOR);
    m_info.device = glString(m_gl, GL_RENDERER);
    m_info.version = glString(m_gl, GL_VERSION);
    m_info.shadingLanguageVersion = glString(m_gl, GL_SHADING_LANGUAGE_VERSION);

    // GL_TIME_ELAPSED is core since 3.3; still verify the counter has bits.
    GLint timerBits = 0;
    m_gl->glGetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &timerBits);
    m_info.gpuTimerQueries = timerBits > 0;
    if (m_info.gpuTimerQueries)
        m_gl->glCreateQueries(GL_TIME_ELAPSED, kTimerQueries, m_timerQueries.data());
    else
        m_info.notes << QStringLiteral("GPU timer queries unavailable: GPU time not measured");

    GLuint vao = 0, vbo = 0, ibo = 0;
    m_gl->glCreateVertexArrays(1, &vao);
    m_gl->glCreateBuffers(1, &vbo);
    m_gl->glCreateBuffers(1, &ibo);
    m_vao = GlName(m_gl, vao, deleteVertexArray);
    m_vbo = GlName(m_gl, vbo, deleteBuffer);
    m_ibo = GlName(m_gl, ibo, deleteBuffer);

    // Fixed attribute layout (see Vertex): 0 = position, 1 = normal, 2 = uv.
    m_gl->glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
    m_gl->glVertexArrayElementBuffer(vao, ibo);
    const GLuint offsets[] = {offsetof(Vertex, position), offsetof(Vertex, normal),
                              offsetof(Vertex, uv)};
    const GLint sizes[] = {3, 3, 2};
    for (GLuint attrib = 0; attrib < 3; ++attrib) {
        m_gl->glEnableVertexArrayAttrib(vao, attrib);
        m_gl->glVertexArrayAttribFormat(vao, attrib, sizes[attrib], GL_FLOAT, GL_FALSE,
                                        offsets[attrib]);
        m_gl->glVertexArrayAttribBinding(vao, attrib, 0);
    }

    qCInfo(lcRender).noquote() << "OpenGL" << m_info.version << "|" << m_info.device;
    return true;
}

void GLRenderer::shutdown()
{
    if (!m_gl)
        return;
    m_program.reset();
    m_vao.reset();
    m_vbo.reset();
    m_ibo.reset();
    if (m_info.gpuTimerQueries)
        m_gl->glDeleteQueries(kTimerQueries, m_timerQueries.data());
    m_timerQueries.fill(0);
    m_gl = nullptr;
}

GlName GLRenderer::compileStage(ShaderStage stage, const QString& source,
                                QList<Diagnostic>& diagnostics)
{
    const GLenum type = stage == ShaderStage::Vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER;
    GlName shader(m_gl, m_gl->glCreateShader(type), deleteShader);

    const QByteArray utf8 = source.toUtf8();
    const char* text = utf8.constData();
    const GLint length = GLint(utf8.size());
    m_gl->glShaderSource(shader.get(), 1, &text, &length);
    m_gl->glCompileShader(shader.get());

    GLint ok = GL_FALSE;
    m_gl->glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &ok);
    auto stageDiagnostics = parseCompilerLog(shaderInfoLog(m_gl, shader.get()), stage, kOrigin);
    if (!ok && !hasErrors(stageDiagnostics))
        stageDiagnostics.append({Severity::Error, stage, -1, -1,
                                 QStringLiteral("%1 shader failed to compile").arg(toString(stage)),
                                 kOrigin});
    diagnostics += stageDiagnostics;

    if (!ok)
        shader.reset();
    return shader;
}

ProgramBuildResult GLRenderer::buildProgram(const ShaderSources& sources)
{
    ProgramBuildResult result;
    if (!m_gl) {
        result.diagnostics.append({Severity::Error, std::nullopt, -1, -1,
                                   QStringLiteral("Renderer is not initialized"), kOrigin});
        return result;
    }

    GlName vs = compileStage(ShaderStage::Vertex, sources.vertex, result.diagnostics);
    GlName fs = compileStage(ShaderStage::Fragment, sources.fragment, result.diagnostics);
    if (!vs || !fs)
        return result; // active program untouched

    GlName program(m_gl, m_gl->glCreateProgram(), deleteProgram);
    m_gl->glAttachShader(program.get(), vs.get());
    m_gl->glAttachShader(program.get(), fs.get());
    m_gl->glLinkProgram(program.get());
    m_gl->glDetachShader(program.get(), vs.get());
    m_gl->glDetachShader(program.get(), fs.get());

    GLint linked = GL_FALSE;
    m_gl->glGetProgramiv(program.get(), GL_LINK_STATUS, &linked);
    auto linkDiagnostics = parseCompilerLog(programInfoLog(m_gl, program.get()), std::nullopt, kOrigin);
    if (!linked && !hasErrors(linkDiagnostics))
        linkDiagnostics.append({Severity::Error, std::nullopt, -1, -1,
                                QStringLiteral("Program failed to link"), kOrigin});
    result.diagnostics += linkDiagnostics;
    if (!linked)
        return result; // active program untouched

    const GLuint p = program.get();
    m_uniforms.model = m_gl->glGetUniformLocation(p, "uModel");
    m_uniforms.view = m_gl->glGetUniformLocation(p, "uView");
    m_uniforms.projection = m_gl->glGetUniformLocation(p, "uProjection");
    m_uniforms.normalMatrix = m_gl->glGetUniformLocation(p, "uNormalMatrix");
    m_uniforms.cameraPosition = m_gl->glGetUniformLocation(p, "uCameraPosition");
    m_uniforms.time = m_gl->glGetUniformLocation(p, "uTime");
    m_uniforms.resolution = m_gl->glGetUniformLocation(p, "uResolution");

    m_program = std::move(program); // old program is deleted here
    result.success = true;
    return result;
}

void GLRenderer::setMesh(const MeshData& mesh)
{
    if (!m_gl)
        return;
    m_gl->glNamedBufferData(m_vbo.get(), GLsizeiptr(mesh.vertices.size() * sizeof(Vertex)),
                            mesh.vertices.data(), GL_STATIC_DRAW);
    m_gl->glNamedBufferData(m_ibo.get(), GLsizeiptr(mesh.indices.size() * sizeof(std::uint32_t)),
                            mesh.indices.data(), GL_STATIC_DRAW);
    m_indexCount = GLsizei(mesh.indices.size());
}

void GLRenderer::collectGpuTimings()
{
    // Read the oldest completed query, if any, without blocking.
    for (int i = 0; i < kTimerQueries; ++i) {
        const int idx = (m_timerIndex + i) % kTimerQueries;
        if (!m_timerPending[idx])
            continue;
        GLint available = GL_FALSE;
        m_gl->glGetQueryObjectiv(m_timerQueries[idx], GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available)
            continue;
        GLuint64 ns = 0;
        m_gl->glGetQueryObjectui64v(m_timerQueries[idx], GL_QUERY_RESULT, &ns);
        m_stats.gpuMs = double(ns) / 1.0e6;
        m_timerPending[idx] = false;
    }
}

void GLRenderer::renderFrame(const FrameParams& params)
{
    if (!m_gl)
        return;

    QElapsedTimer cpuTimer;
    cpuTimer.start();

    if (m_info.gpuTimerQueries)
        collectGpuTimings();

    const bool timing = m_info.gpuTimerQueries && !m_timerPending[m_timerIndex];
    if (timing)
        m_gl->glBeginQuery(GL_TIME_ELAPSED, m_timerQueries[m_timerIndex]);

    m_gl->glViewport(0, 0, params.framebufferSize.width(), params.framebufferSize.height());
    m_gl->glClearColor(0.105f, 0.108f, 0.115f, 1.0f);
    m_gl->glClearDepth(1.0);
    m_gl->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    m_gl->glEnable(GL_DEPTH_TEST);
    m_gl->glDepthFunc(GL_LESS);
    m_gl->glEnable(GL_CULL_FACE);
    m_gl->glCullFace(GL_BACK);
    m_gl->glFrontFace(GL_CCW);

    int drawCalls = 0;
    if (m_program && m_indexCount > 0) {
        const GLuint p = m_program.get();
        m_gl->glUseProgram(p);
        const QMatrix3x3 normalMatrix = params.model.normalMatrix();
        auto setMat4 = [&](GLint loc, const QMatrix4x4& m) {
            if (loc >= 0)
                m_gl->glProgramUniformMatrix4fv(p, loc, 1, GL_FALSE, m.constData());
        };
        setMat4(m_uniforms.model, params.model);
        setMat4(m_uniforms.view, params.view);
        setMat4(m_uniforms.projection, params.projection);
        if (m_uniforms.normalMatrix >= 0)
            m_gl->glProgramUniformMatrix3fv(p, m_uniforms.normalMatrix, 1, GL_FALSE,
                                            normalMatrix.constData());
        if (m_uniforms.cameraPosition >= 0)
            m_gl->glProgramUniform3f(p, m_uniforms.cameraPosition, params.cameraPosition.x(),
                                     params.cameraPosition.y(), params.cameraPosition.z());
        if (m_uniforms.time >= 0)
            m_gl->glProgramUniform1f(p, m_uniforms.time, params.timeSeconds);
        if (m_uniforms.resolution >= 0)
            m_gl->glProgramUniform2f(p, m_uniforms.resolution,
                                     float(params.framebufferSize.width()),
                                     float(params.framebufferSize.height()));

        m_gl->glBindVertexArray(m_vao.get());
        m_gl->glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
        m_gl->glBindVertexArray(0);
        m_gl->glUseProgram(0);
        drawCalls = 1;
    }

    if (timing) {
        m_gl->glEndQuery(GL_TIME_ELAPSED);
        m_timerPending[m_timerIndex] = true;
        m_timerIndex = (m_timerIndex + 1) % kTimerQueries;
    }

    m_stats.drawCalls = drawCalls;
    m_stats.triangles = drawCalls ? std::size_t(m_indexCount / 3) : 0;
    m_stats.cpuSubmitMs = double(cpuTimer.nsecsElapsed()) / 1.0e6;
}

} // namespace pf::gl
