# PrismForge Architecture

## Modules

```
src/
  diagnostics/   Diagnostic model, compiler-log parsers, log capture, categories   ┐
  shader/        ShaderStage/ShaderSources, glslang front-end validation          │
  material/      Material parameters, comment hints, JSON values                  ├─ pf_core (Qt Core, glslang)
  project/       Project model + JSON (*.pfproj), material exporter               ┘
  graph/         Graph model, JSON node library, validation, GLSL codegen            pf_graph (Qt Core)
  render/        Renderer interface, preview meshes, orbit camera                 ┐  pf_render (Qt Gui/OpenGL)
  render/gl/     OpenGL 4.5 core backend (GLRenderer)                             ┘
  editor/        CodeEditor (gutter, diagnostic marks), GLSL highlighter             pf_editor (Qt Widgets)
  grapheditor/   Node canvas, items, properties panel, undo                          pf_grapheditor (Qt Widgets)
  app/           MainWindow, viewport, panels (diagnostics, material, log), main()   prismforge executable
packaging/       Install rules, CPack RPM/DEB, desktop file, AppStream, MIME
resources/       Default shaders, built-in node library, icons
```

Dependencies only point downwards: `app → grapheditor, editor, render →
graph → core`. Nothing in `pf_core` or `pf_graph` knows about GPUs or widgets,
so both are unit-tested without a display. Only `render/gl/` includes OpenGL
headers; `ViewportWidget` is the single place that creates the GL backend.

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

## Material parameters

After a successful link the backend enumerates active uniforms
(`GL_PROGRAM_INTERFACE` queries) and reads their initial values, so GLSL
initialisers (`uniform float x = 0.5;`) become defaults. Built-in uniforms are
excluded; unsupported types are reported as info diagnostics. Editor hints come
from a trailing comment on the declaration: `@range(min, max)`, `@color`,
`@label(Text)`. User values are kept by name across recompiles and applied
only when the type still matches.

## Shader graph

* **Node definitions** are data (`resources/nodes/builtin.json`, user and
  project libraries). Each has typed inputs/outputs and GLSL *expression
  templates* with `{input}`, `{T}` and `{name}` placeholders, plus optional
  helper functions emitted once. Roles: `function`, `parameter` (becomes a
  hinted uniform), `output` (writes `fragColor`).
* **Types**: `float`, `vec2`, `vec3`, `vec4`, and generic `T`, resolved per node
  to the widest connected `T` input. Implicit conversions: splat
  (`float → vecN`), truncate (`.xy`), pad (`vec4(x, 1.0)`). `vecN → float` is an
  error that suggests a Split node.
* **Validation**: unknown types, invalid connections, exactly one output node,
  cycles, type mismatches, parameter name rules; unreachable nodes warn and are
  not emitted.
* **Determinism**: nodes are emitted in topological order with the smallest
  node id first; helpers in definition-id order; connections are stored sorted.
  The same graph always yields byte-identical GLSL.
* **Editing**: the graph editor never mutates the model directly; each edit
  replaces it with a modified copy and records a snapshot undo entry
  (consecutive edits of one value merge).

## File formats

All files are indented JSON with sorted keys; floats are written as the
shortest decimal that round-trips, so diffs stay small.

`*.pfproj` (format 3; versions 1-2 still load):

```json
{
    "formatVersion": 3,
    "graph": { "enabled": true, "file": "Demo.pfgraph" },
    "name": "Demo",
    "parameters": { "uTint": { "type": "vec3", "value": [1, 0.5, 0.25] } },
    "preview": {
        "autoCompile": true,
        "camera": { "distance": 3.5, "pitch": 20, "yaw": 30 },
        "mesh": "sphere"
    },
    "shaders": { "fragment": "shaders/Demo.frag", "vertex": "shaders/Demo.vert" }
}
```

`*.pfgraph`: nodes (`id`, `type`, position, input `values`, parameter name and
range) and connections (`from`/`fromPort` → `to`/`toPort`).

`*.material.json` (export): shader file names, vertex attribute and built-in
uniform interface, and every parameter with type, value, default and hints.
