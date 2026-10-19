#include "render/gl/GLRenderer.h"

#include "diagnostics/Logging.h"

#include <QElapsedTimer>
#include <QOpenGLContext>
#include <QOpenGLVersionFunctionsFactory>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <tuple>

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
void deleteFramebuffer(Functions* gl, GLuint n) { gl->glDeleteFramebuffers(1, &n); }
void deleteRenderbuffer(Functions* gl, GLuint n) { gl->glDeleteRenderbuffers(1, &n); }

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
    for (GlName* name : {&m_program, &m_vao, &m_vbo, &m_ibo, &m_offscreen.msFbo, &m_offscreen.msColor,
                         &m_offscreen.msDepth, &m_offscreen.resolveFbo, &m_offscreen.resolveColor})
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
    queryCapabilities(fmt, timerBits);

    GLint maxRenderbuffer = 0, maxTexture = 0, viewport[2] = {};
    m_gl->glGetIntegerv(GL_MAX_RENDERBUFFER_SIZE, &maxRenderbuffer);
    m_gl->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexture);
    m_gl->glGetIntegerv(GL_MAX_VIEWPORT_DIMS, viewport);
    m_maxRenderSize = QSize(std::min({maxRenderbuffer, maxTexture, viewport[0]}),
                            std::min({maxRenderbuffer, maxTexture, viewport[1]}));
    m_gl->glGetIntegerv(GL_MAX_SAMPLES, &m_maxSamples);
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

void GLRenderer::queryCapabilities(const QSurfaceFormat& format, GLint timerBits)
{
    auto integer = [this](GLenum name) {
        GLint v = 0;
        m_gl->glGetIntegerv(name, &v);
        return QString::number(v);
    };
    auto add = [this](const QString& name, const QString& value) {
        m_info.capabilities.append({name, value});
    };

    GLint flags = 0;
    m_gl->glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    QStringList flagNames;
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT)
        flagNames << QStringLiteral("debug");
    if (flags & GL_CONTEXT_FLAG_FORWARD_COMPATIBLE_BIT)
        flagNames << QStringLiteral("forward-compatible");
    if (flags & GL_CONTEXT_FLAG_ROBUST_ACCESS_BIT)
        flagNames << QStringLiteral("robust");

    add(QStringLiteral("Context"),
        QStringLiteral("%1.%2 %3").arg(format.majorVersion()).arg(format.minorVersion())
            .arg(format.profile() == QSurfaceFormat::CoreProfile ? QStringLiteral("core")
                                                                 : QStringLiteral("compatibility")));
    add(QStringLiteral("Context flags"), flagNames.isEmpty() ? QStringLiteral("none") : flagNames.join(", "));
    add(QStringLiteral("Default framebuffer MSAA"), QString::number(std::max(format.samples(), 0)));
    add(QStringLiteral("Timer query bits"), QString::number(timerBits));
    add(QStringLiteral("Max texture size"), integer(GL_MAX_TEXTURE_SIZE));
    add(QStringLiteral("Max 3D texture size"), integer(GL_MAX_3D_TEXTURE_SIZE));
    add(QStringLiteral("Max cube map size"), integer(GL_MAX_CUBE_MAP_TEXTURE_SIZE));
    add(QStringLiteral("Max array texture layers"), integer(GL_MAX_ARRAY_TEXTURE_LAYERS));
    add(QStringLiteral("Max texture image units (combined)"), integer(GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS));
    add(QStringLiteral("Max vertex attributes"), integer(GL_MAX_VERTEX_ATTRIBS));
    add(QStringLiteral("Max vertex uniform components"), integer(GL_MAX_VERTEX_UNIFORM_COMPONENTS));
    add(QStringLiteral("Max fragment uniform components"), integer(GL_MAX_FRAGMENT_UNIFORM_COMPONENTS));
    add(QStringLiteral("Max uniform block size"), integer(GL_MAX_UNIFORM_BLOCK_SIZE));
    add(QStringLiteral("Max varying components"), integer(GL_MAX_VARYING_COMPONENTS));
    add(QStringLiteral("Max draw buffers"), integer(GL_MAX_DRAW_BUFFERS));
    add(QStringLiteral("Max colour attachments"), integer(GL_MAX_COLOR_ATTACHMENTS));
    add(QStringLiteral("Max samples"), integer(GL_MAX_SAMPLES));
    add(QStringLiteral("Max renderbuffer size"), integer(GL_MAX_RENDERBUFFER_SIZE));
    add(QStringLiteral("Max viewport dims"), [this] {
        GLint dims[2] = {};
        m_gl->glGetIntegerv(GL_MAX_VIEWPORT_DIMS, dims);
        return QStringLiteral("%1 × %2").arg(dims[0]).arg(dims[1]);
    }());
    add(QStringLiteral("Max compute invocations / group"), integer(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS));
    add(QStringLiteral("Max shader storage block size"), integer(GL_MAX_SHADER_STORAGE_BLOCK_SIZE));

    GLint extensionCount = 0;
    m_gl->glGetIntegerv(GL_NUM_EXTENSIONS, &extensionCount);
    for (GLint i = 0; i < extensionCount; ++i)
        m_info.extensions << QString::fromLatin1(
            reinterpret_cast<const char*>(m_gl->glGetStringi(GL_EXTENSIONS, GLuint(i))));
    m_info.extensions.sort();
}

void GLRenderer::shutdown()
{
    if (!m_gl)
        return;
    m_program.reset();
    m_parameterSlots.clear();
    m_offscreen = OffscreenTarget{};
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

    QHash<QString, ParameterSlot> slotTable;
    result.parameters = introspectParameters(p, result.diagnostics, slotTable);

    m_program = std::move(program); // old program is deleted here
    m_parameterSlots = std::move(slotTable);
    result.success = true;
    return result;
}

QList<MaterialParameter> GLRenderer::introspectParameters(GLuint program,
                                                          QList<Diagnostic>& diagnostics,
                                                          QHash<QString, ParameterSlot>& slotTable)
{
    QList<MaterialParameter> parameters;
    GLint count = 0;
    m_gl->glGetProgramInterfaceiv(program, GL_UNIFORM, GL_ACTIVE_RESOURCES, &count);

    for (GLint i = 0; i < count; ++i) {
        const GLenum props[] = {GL_TYPE, GL_LOCATION, GL_ARRAY_SIZE, GL_BLOCK_INDEX, GL_NAME_LENGTH};
        GLint values[5] = {};
        m_gl->glGetProgramResourceiv(program, GL_UNIFORM, GLuint(i), 5, props, 5, nullptr, values);
        const auto [glType, location, arraySize, blockIndex, nameLength] =
            std::tuple(values[0], values[1], values[2], values[3], values[4]);
        if (blockIndex != -1 || location < 0)
            continue; // uniform-block members are not individually settable

        QByteArray nameBuffer(nameLength, '\0');
        m_gl->glGetProgramResourceName(program, GL_UNIFORM, GLuint(i), nameLength, nullptr,
                                       nameBuffer.data());
        const QString name = QString::fromUtf8(nameBuffer.constData());
        if (isBuiltinUniform(name))
            continue;

        std::optional<ParamType> type;
        switch (glType) {
        case GL_FLOAT: type = ParamType::Float; break;
        case GL_FLOAT_VEC2: type = ParamType::Vec2; break;
        case GL_FLOAT_VEC3: type = ParamType::Vec3; break;
        case GL_FLOAT_VEC4: type = ParamType::Vec4; break;
        case GL_INT: type = ParamType::Int; break;
        case GL_BOOL: type = ParamType::Bool; break;
        default: break;
        }
        if (!type || arraySize != 1) {
            diagnostics.append({Severity::Info, std::nullopt, -1, -1,
                                QStringLiteral("Uniform '%1' is not editable (only float, vec2-4, int "
                                               "and bool non-array uniforms are supported)")
                                    .arg(name),
                                QStringLiteral("parameters")});
            continue;
        }

        MaterialParameter param;
        param.name = name;
        param.type = *type;
        if (*type == ParamType::Int || *type == ParamType::Bool) {
            GLint v = 0;
            m_gl->glGetUniformiv(program, location, &v);
            param.defaultValue[0] = float(v);
        } else {
            m_gl->glGetUniformfv(program, location, param.defaultValue.data());
        }
        param.value = param.defaultValue;
        parameters.append(param);
        slotTable.insert(name, {location, *type});
    }

    std::sort(parameters.begin(), parameters.end(),
              [](const MaterialParameter& a, const MaterialParameter& b) { return a.name < b.name; });
    return parameters;
}

void GLRenderer::setParameter(const QString& name, ParamType type, const ParamValue& value)
{
    const auto it = m_parameterSlots.constFind(name);
    if (!m_gl || !m_program || it == m_parameterSlots.constEnd() || it->type != type)
        return;
    const GLuint p = m_program.get();
    const GLint loc = it->location;
    switch (type) {
    case ParamType::Float: m_gl->glProgramUniform1f(p, loc, value[0]); break;
    case ParamType::Vec2: m_gl->glProgramUniform2f(p, loc, value[0], value[1]); break;
    case ParamType::Vec3: m_gl->glProgramUniform3f(p, loc, value[0], value[1], value[2]); break;
    case ParamType::Vec4:
        m_gl->glProgramUniform4f(p, loc, value[0], value[1], value[2], value[3]);
        break;
    case ParamType::Int:
    case ParamType::Bool: m_gl->glProgramUniform1i(p, loc, GLint(std::lround(value[0]))); break;
    }
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

bool GLRenderer::ensureOffscreenTarget(QSize size, int samples, QString* error)
{
    if (m_offscreen.msFbo && m_offscreen.size == size && m_offscreen.samples == samples)
        return true;
    m_offscreen = OffscreenTarget{};

    auto makeRenderbuffer = [this](GLenum format, int sampleCount, QSize s) {
        GLuint rb = 0;
        m_gl->glCreateRenderbuffers(1, &rb);
        m_gl->glNamedRenderbufferStorageMultisample(rb, sampleCount, format, s.width(), s.height());
        return GlName(m_gl, rb, deleteRenderbuffer);
    };
    auto makeFramebuffer = [this] {
        GLuint fb = 0;
        m_gl->glCreateFramebuffers(1, &fb);
        return GlName(m_gl, fb, deleteFramebuffer);
    };

    OffscreenTarget t;
    t.size = size;
    t.samples = samples;
    t.msFbo = makeFramebuffer();
    t.msColor = makeRenderbuffer(GL_RGBA8, samples, size);
    t.msDepth = makeRenderbuffer(GL_DEPTH_COMPONENT24, samples, size);
    m_gl->glNamedFramebufferRenderbuffer(t.msFbo.get(), GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, t.msColor.get());
    m_gl->glNamedFramebufferRenderbuffer(t.msFbo.get(), GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.msDepth.get());
    t.resolveFbo = makeFramebuffer();
    t.resolveColor = makeRenderbuffer(GL_RGBA8, 0, size);
    m_gl->glNamedFramebufferRenderbuffer(t.resolveFbo.get(), GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER,
                                         t.resolveColor.get());

    for (GLuint fbo : {t.msFbo.get(), t.resolveFbo.get()}) {
        const GLenum status = m_gl->glCheckNamedFramebufferStatus(fbo, GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            if (error)
                *error = QStringLiteral("Could not create a %1x%2 render target (status 0x%3); "
                                        "the GPU may be out of memory")
                             .arg(size.width()).arg(size.height()).arg(status, 0, 16);
            return false;
        }
    }
    m_offscreen = std::move(t);
    return true;
}

bool GLRenderer::renderToImage(const FrameParams& params, int samples, QImage* image, QString* error)
{
    const QSize size = params.framebufferSize;
    if (!m_gl || size.isEmpty()) {
        if (error)
            *error = QStringLiteral("Renderer is not initialized");
        return false;
    }
    if (size.width() > m_maxRenderSize.width() || size.height() > m_maxRenderSize.height()) {
        if (error)
            *error = QStringLiteral("%1x%2 exceeds this GPU's maximum render size of %3x%4")
                         .arg(size.width()).arg(size.height())
                         .arg(m_maxRenderSize.width()).arg(m_maxRenderSize.height());
        return false;
    }
    samples = std::clamp(samples, 0, m_maxSamples);
    if (!ensureOffscreenTarget(size, samples, error))
        return false;

    GLint previousDraw = 0, previousRead = 0;
    m_gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);
    m_gl->glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);

    m_gl->glBindFramebuffer(GL_FRAMEBUFFER, m_offscreen.msFbo.get());
    renderFrame(params);
    m_gl->glBlitNamedFramebuffer(m_offscreen.msFbo.get(), m_offscreen.resolveFbo.get(), 0, 0, size.width(),
                                 size.height(), 0, 0, size.width(), size.height(), GL_COLOR_BUFFER_BIT,
                                 GL_NEAREST);

    // GL rows are bottom-up; read into a buffer and flip into the image.
    const qsizetype rowBytes = qsizetype(size.width()) * 4;
    QByteArray pixels(rowBytes * size.height(), Qt::Uninitialized);
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, m_offscreen.resolveFbo.get());
    m_gl->glPixelStorei(GL_PACK_ALIGNMENT, 1);
    m_gl->glReadPixels(0, 0, size.width(), size.height(), GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    m_gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previousDraw));
    m_gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousRead));

    QImage out(size, QImage::Format_RGBX8888);
    if (out.isNull()) {
        if (error)
            *error = QStringLiteral("Out of memory allocating a %1x%2 image").arg(size.width()).arg(size.height());
        return false;
    }
    for (int y = 0; y < size.height(); ++y) {
        uchar* dst = out.scanLine(size.height() - 1 - y);
        std::memcpy(dst, pixels.constData() + rowBytes * y, size_t(rowBytes));
        for (int x = 0; x < size.width(); ++x)
            dst[x * 4 + 3] = 0xff; // opaque: alpha written by the shader is not meaningful here
    }
    *image = std::move(out);
    return true;
}

} // namespace pf::gl
