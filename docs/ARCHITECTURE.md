# PrismForge Architecture (Milestone 1)

## Modules

```
src/
  diagnostics/   Diagnostic model, compiler-log parsers, logging categories     ┐
  shader/        ShaderStage/ShaderSources, glslang front-end validation        ├─ pf_core   (Qt Core, glslang)
  project/       Project model and JSON serialisation (*.pfproj)                ┘
  render/        Renderer interface, preview meshes, orbit camera               ┐  pf_render (Qt Gui/OpenGL)
  render/gl/     OpenGL 4.5 core backend (GLRenderer)                           ┘
  editor/        CodeEditor (gutter, diagnostic marks), GLSL highlighter           pf_editor (Qt Widgets)
  app/           MainWindow, ViewportWidget, DiagnosticsPanel, theme, main()       prismforge executable
```

Dependencies only point downwards: `app → editor, render → core`. Nothing in
`pf_core` knows about GPUs or widgets, so it is unit-tested without a display.
Only `render/gl/` includes OpenGL headers; `ViewportWidget` is the single place
that creates the GL backend. A Vulkan backend would be another `Renderer`
implementation plus a different viewport surface.

## Compile pipeline

```
editor text ──(F5 or 350 ms after last edit)──► MainWindow::requestCompile
   │ snapshot ShaderSources
   ▼
glslang validate  (QtConcurrent worker thread, no GL context needed)
   │ errors? ──yes──► report diagnostics, stop. Active program unchanged.
   ▼ no
GLRenderer::buildProgram  (GUI thread, viewport context current)
   │ compile VS, FS → link
   │ failure ──► report driver diagnostics. Active program unchanged.
   ▼ success
new program atomically replaces the old one (RAII GlName move-assign)
```

* The **driver is authoritative**. glslang is a fast, off-thread pre-check with
  consistent messages; it does not decide what renders.
* Edits made while a compile is in flight set a "queued" flag; the stale result
  is discarded and one new compile runs. Compiles never pile up.
* Diagnostics from both tools are parsed into `pf::Diagnostic` (stage, line,
  column, severity, origin). Parsers cover glslang/AMD, Mesa and NVIDIA formats;
  unrecognised lines are kept verbatim rather than dropped.

## Shader interface contract

Users write complete GLSL 4.50 shaders (including `#version`). PrismForge does
not inject code, so reported line numbers match the editor exactly.

| Kind      | Name              | Type    | Notes                         |
|-----------|-------------------|---------|-------------------------------|
| attribute | location 0        | vec3    | object-space position         |
| attribute | location 1        | vec3    | object-space normal           |
| attribute | location 2        | vec2    | UV in [0,1]                   |
| uniform   | `uModel`          | mat4    | identity in M1                |
| uniform   | `uView`           | mat4    |                               |
| uniform   | `uProjection`     | mat4    |                               |
| uniform   | `uNormalMatrix`   | mat3    |                               |
| uniform   | `uCameraPosition` | vec3    | world space                   |
| uniform   | `uTime`           | float   | seconds since app start       |
| uniform   | `uResolution`     | vec2    | framebuffer size in pixels    |

Uniforms are optional: any the program does not declare are skipped.

## Timing

* *Frame interval (CPU)*: wall-clock time between consecutive `paintGL` calls.
  Paced by vsync, so it reflects presentation rate, not GPU cost.
* *Submit time (CPU)*: CPU time spent inside `renderFrame`.
* *GPU time*: `GL_TIME_ELAPSED` queries in a 4-deep ring, read without stalling.
  Shown as "unsupported" if the counter has no bits. It is never substituted
  with a CPU number.

## Threading

All GL calls happen on the GUI thread with the `QOpenGLWidget` context current.
glslang validation runs on the Qt global thread pool. Driver compilation of
large shaders can still block the UI briefly; moving it to a shared context on a
worker thread is a possible later step if it turns out to matter.

## Project format

`*.pfproj` is indented JSON with sorted keys (deterministic, diff-friendly).
Shader sources live in separate files referenced by paths relative to the
project file. `formatVersion` is checked; newer versions are rejected with a
clear message instead of being half-loaded.

```json
{
    "formatVersion": 1,
    "name": "Demo",
    "preview": {
        "autoCompile": true,
        "camera": { "distance": 3.5, "pitch": 20, "yaw": 30 },
        "mesh": "sphere"
    },
    "shaders": { "fragment": "shaders/Demo.frag", "vertex": "shaders/Demo.vert" }
}
```
