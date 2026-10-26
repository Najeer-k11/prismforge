# PrismForge Architecture

## Modules

```
src/
  diagnostics/   Diagnostic model, compiler-log parsers, log capture, categories   ┐
  shader/        ShaderStage/ShaderSources, glslang front-end validation          │
  material/      Material parameters, comment hints, JSON values                  ├─ pf_core (Qt Core, glslang)
  project/       Project model + JSON (*.pfproj), material exporter               │
  texture/       Channel bindings (uChannel0-3), built-in texture catalogue       ┘
  graph/         Graph model, JSON node library, validation, GLSL codegen            pf_graph (Qt Core)
  render/        Renderer interface, meshes, camera, channel image loading       ┐  pf_render (Qt Gui/OpenGL)
  render/gl/     OpenGL 4.5 core backend (GLRenderer)                             ┘
  editor/        CodeEditor (gutter, diagnostic marks), GLSL highlighter             pf_editor (Qt Widgets)
  grapheditor/   Node canvas, items, properties panel, undo                          pf_grapheditor (Qt Widgets)
  app/           MainWindow, viewport, panels (diagnostics, material, log), main()   prismforge executable
packaging/       Install rules, CPack RPM/DEB, desktop file, AppStream, MIME
resources/       Default shaders, built-in node library, textures, icons
tools/           generate_textures.py: regenerates resources/textures/*.png
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
| uniform   | `uMouse`          | vec4    | xy = drag position, zw = click position (pixels, origin bottom-left); zw negative after release |
| uniform   | `uChannel0`–`3`   | sampler2D | texture units 0–3           |
| uniform   | `uChannelResolution` | vec3[4] | channel sizes in texels; z = 1 if bound, all 0 if not |

Uniforms are optional: any the program does not declare are skipped.

## Texture channels

* A `ChannelBinding` (pf_core) names a built-in texture id or an image file and
  carries a `ChannelSampler`: filter (nearest, linear, mipmap with up to 8×
  anisotropy), wrap (repeat, clamp, mirror) and Flip Y.
* `loadChannelImage` (pf_render) decodes the picture to RGBA8888, top row
  first. `Renderer::setChannel` uploads it with a full mip chain, bottom row
  first when Flip Y is set (the default, so v = 0 is the bottom of the
  picture, matching `gl_FragCoord` and Shadertoy). Changing only the filter or
  wrap of the same image re-parameterises the texture without an upload.
* Channels belong to the renderer, not the program: they survive recompiles,
  and each new program gets `uChannelN` set to texture unit N after linking.
  `ViewportWidget` keeps the images so they are restored if the GL context is
  recreated. Units are unbound after the draw.
* Introspection skips channel samplers. A `uChannelN` that is not a
  `sampler2D` is a warning; other samplers get an info diagnostic, since
  nothing binds them.
* Built-in textures are Qt resources (`:/textures/<id>.png`) compiled into
  pf_render. They are synthesised by `tools/generate_textures.py`; all tile.
* Export copies each bound texture's original file as
  `<name>.channel<N>.<ext>` and lists the bindings in `*.material.json`.

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

`*.pfproj` (format 4; versions 1-3 still load):

```json
{
    "channels": {
        "0": { "filter": "linear", "flipY": true, "texture": "rgba-noise-256", "wrap": "repeat" },
        "1": { "file": "textures/bricks.png", "filter": "mipmap", "flipY": true, "wrap": "repeat" }
    },
    "formatVersion": 4,
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
