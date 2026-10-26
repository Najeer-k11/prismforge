# PrismForge

A native desktop environment for writing GPU shaders, previewing them in real
time, building them with a visual node graph and exporting reusable assets.
C++20, Qt 6, OpenGL 4.5 core, GLSL.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for modules, the compile
pipeline, the shader interface, the graph language and file formats.

## Features

| Area | What you get |
|---|---|
| **Shader editor** | GLSL vertex + fragment tabs, syntax highlighting, line gutter, inline error/warning marks, open/save/new shader files |
| **Compile pipeline** | glslang validation on a worker thread, then GL driver compile/link; auto-compile 350 ms after typing or F5; **a failed build always keeps the last working preview** |
| **Preview** | Sphere / plane / cube, orbit + zoom camera, continuous animation (`uTime`) |
| **Material parameters** | Every editable uniform (`float`, `vec2-4`, `int`, `bool`) gets a control; hints in comments (`// @range(0, 1) @color @label(Name)`); values saved in the project |
| **Shader graph** | Typed node graph with 67 built-in nodes (input, constant, parameter, math, vector, colour, UV, 2D/3D noise, fresnel, surface/unlit output), validation, deterministic GLSL generation, undo/redo, user node libraries in JSON |
| **Diagnostics** | Errors with stage/line/column from glslang, Mesa, NVIDIA and AMD formats; CPU frame interval and submit time; **GPU time from timer queries, never substituted with CPU numbers**; GPU capability table; application log; `--gl-debug` driver messages |
| **Projects** | `.pfproj` JSON with relative paths, sorted keys and compact numbers (diff-friendly); graphs in `.pfgraph` |
| **Export** | Shaders, `*.material.json` (interface + parameters + hints), the source graph and a preview PNG |
| **Render Image** | Offscreen frames from 720p to 8K (QHD "2K", 4K UHD, DCI 2K/4K, ultrawide, your screen's native size, custom, portrait) with up to 16× MSAA, saved as PNG/JPEG/WebP — wallpapers |
| **Render Video** | MP4 (H.264, HEVC, AV1), WebM (VP9), MOV (ProRes) or PNG frames; fps, duration, start time, quality, optional seamless 360° turntable; progress, ETA, cancel |
| **GPU encoding** | NVIDIA NVENC (H.264/HEVC/AV1) and VAAPI are used when they work on this machine; CPU encoders otherwise. Every encoder is test-opened before it is offered |
| **GPU selection** | On hybrid laptops PrismForge renders on the NVIDIA GPU automatically (View → GPU, or `--gpu`) |
| **Screen mode** | A full-viewport quad (Alt+4) for 2D effects and wallpapers; see `examples/wallpaper` |
| **Texture channels** | Shadertoy-style inputs `uChannel0`–`uChannel3` with per-channel filter (mipmap + anisotropic, linear, nearest), wrap (repeat, clamp, mirror) and Flip Y; `uChannelResolution[4]` and `uMouse`; saved in the project, copied on export, and *Channel N Texture* nodes in the graph |
| **Built-in textures** | 12 seamless textures bundled in the app: RGBA/gray/blue noise, fractal noise, clouds, Voronoi cells, rock, ground, wood, water normals, UV grid. Or use any image file |
| **Examples** | `mountains` (flight over eroded terrain), `ocean` (sunset sea with foam), `planet` (textured sphere with clouds and city lights), plus `wallpaper`, `lit-material`, `noise-graph` |

## Install a package

| Distribution | Package | Install |
|---|---|---|
| Fedora 44 | `prismforge-0.4.0-1.fc44.x86_64.rpm` | `sudo dnf install ./prismforge-0.4.0-1.fc44.x86_64.rpm` |
| Ubuntu 24.04 | `prismforge_0.3.0_amd64.deb` | `sudo apt install ./prismforge_0.3.0_amd64.deb` |

Packages install the `prismforge` binary, a desktop launcher with the app
icon, AppStream metadata, a `.pfproj` file association and example projects in
`/usr/share/doc/prismforge/examples/`.

Each package is built on the distribution it targets, because it links against
that distribution's Qt and C++ runtime. The RPM needs Qt ≥ 6.11 (Fedora 44);
the DEB is built against Ubuntu 24.04's Qt 6.4.

## Build from source

| Component | Fedora package | Ubuntu package |
|---|---|---|
| C++20 compiler, CMake ≥ 3.24, Ninja | `gcc-c++ cmake ninja-build` | `build-essential cmake ninja-build` |
| Qt 6 base (Widgets, OpenGL, Concurrent, Test) | `qt6-qtbase-devel` | `qt6-base-dev` |
| FFmpeg libraries (optional; video export) | `libavcodec-free-devel libavformat-free-devel libavutil-free-devel libswscale-free-devel` | `libavcodec-dev libavformat-dev libavutil-dev libswscale-dev` |
| glslang (optional, recommended) | `glslang glslang-devel` | `glslang-dev glslang-tools spirv-tools` |
| Catch2 v3 (tests) | `catch-devel` | `catch2` |
| OpenGL 4.5 driver | Mesa ≥ 20 or vendor driver | same |

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release
./build/release/src/prismforge [project.pfproj]
```

Packages:

```sh
cd build/release
cpack -G RPM        # on Fedora
cpack -G DEB        # on Debian/Ubuntu (needs dpkg-dev)
```

`cmake --install build/release --prefix ~/.local` installs without packaging.

### Command-line options

| Option | Purpose |
|---|---|
| `project.pfproj` | Open a project |
| `--graph` | Start with the shader graph generating the fragment shader |
| `--export <dir>` | Export after the first successful compile, then exit (0 = success) |
| `--gl-debug` | OpenGL debug context; driver messages go to the Log panel |
| `--gpu auto\|nvidia\|integrated` | GPU for this run (hybrid laptops) |
| `--render-image <file>` | Render a frame (`--size 4k`, `--samples 8`, `--time 12`) and exit |
| `--render-video <file>` | Render a clip (`--size`, `--fps`, `--duration`, `--time`, `--format`, `--quality`, `--turntable`) and exit |
| `--list-video-formats` | Show which video encoders work on this machine |
| `--screenshot <png>` / `--screenshot-delay <ms>` | Save a window screenshot and exit |
| `--smoke-replace-fragment <file>` | After the first good compile, swap in this fragment shader (scripted testing) |

## Using it

| Action | Shortcut |
|---|---|
| Compile / toggle shader graph mode | F5 / Ctrl+G |
| Fragment / vertex tab | Ctrl+1 / Ctrl+2 |
| Orbit / zoom / reset camera | drag / wheel / Home |
| New / open / save / save-as project | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Export material | Ctrl+E |
| Render image / render video | Ctrl+Shift+I / Ctrl+Shift+V |
| Sphere / plane / cube / screen | Alt+1 … Alt+4 |
| Graph: add node / connect / disconnect | right-click canvas / drag between ports / drag an input away |
| Graph: delete / duplicate / frame / undo / redo | Del / Ctrl+D / F / Ctrl+Z / Ctrl+Shift+Z |

**Graph mode** generates the fragment shader from the graph. The hand-written
fragment file is never modified or parsed into nodes: arbitrary GLSL cannot be
represented as a graph, so the two are kept separate and you switch between
them with Ctrl+G. Generated GLSL is shown read-only in the editor.

**Texture channels** (Channels panel): click a thumbnail to choose a built-in
texture or an image file (PNG, JPEG, WebP, …; paths are saved relative to the
project). In GLSL:

```glsl
uniform sampler2D uChannel0;          // ... uChannel3; unbound channels read black
uniform vec3 uChannelResolution[4];   // texture size in texels, z = 1 when bound
uniform vec4 uMouse;                  // xy = drag position, zw = click (negative after release)

vec3 rock = texture(uChannel1, uv).rgb;
float n = texelFetch(uChannel0, ivec2(p) & 255, 0).x; // exact noise texels
```

*RGBA Noise* has G = R and A = B shifted by (37, 17) texels, so 3D value noise
needs one lookup per z-slice pair, as on Shadertoy. Textures are generated by
`tools/generate_textures.py` (fixed seeds, no third-party assets).

**Porting a Shadertoy shader**: Screen mode (Alt+4), assign the channels it
uses, and wrap `mainImage`:

```glsl
#version 450 core
uniform vec2 uResolution;
uniform float uTime;
uniform vec4 uMouse;
uniform sampler2D uChannel0;
out vec4 fragColor;
#define iResolution vec3(uResolution, 1.0)
#define iTime uTime
#define iMouse uMouse
#define iChannel0 uChannel0
// ... paste the shader's code here ...
void main() { mainImage(fragColor, gl_FragCoord.xy); }
```

Multi-pass shaders (Buffer A–D), cube maps, keyboard, sound and video inputs
are not supported.

**Custom nodes**: put JSON files in `~/.local/share/prismforge/nodes/` (all
projects) or `<project>/nodes/` (one project) and use *Graph → Reload Node
Libraries*. The format is the one used by
[resources/nodes/builtin.json](resources/nodes/builtin.json); a definition with
the same `id` as a built-in replaces it.

## Verification status

Verified on Fedora 44 (Wayland), Mesa 26.2.5, Intel RPL-P, GL 4.6 core:

- Debug and Release builds with `-Wall -Wextra -Wpedantic`: no warnings.
- **88 automated tests pass**:
  - unit: diagnostics parsing, project/graph/material JSON, meshes, camera,
    glslang validation (including every example project), log capture, export
    (with channel textures), every built-in texture loads, the RGBA noise
    channel offsets, channel JSON round-trip and malformed entries
  - graph: every built-in node's GLSL compiled by glslang (each output, and
    generic nodes with both `float` and `vec3`), validation errors, determinism
  - GPU (offscreen render + pixel checks): compile/link/draw, failed compile or
    link keeps the previous program, uniform introspection, parameters change
    pixels, the default graph links and draws; channel textures sampled with
    and without Flip Y, wrap modes, unbinding, `uChannelResolution`, `uMouse`,
    sampler diagnostics, oversized images rejected
  - widgets (offscreen, synthetic input): connect by dragging in both
    directions, disconnect, move, delete, duplicate, undo/redo; Channels panel
    edits and errors, texture picker selection
  - offscreen rendering at 2560×1440 with MSAA: full coverage, orientation,
    size limits
  - video: every available encoder (here h264/hevc/av1 on NVENC, AV1 on
    SVT-AV1, VP9, ProRes) encodes a clip that decodes back with the exact frame
    count, size and rate; partial files are removed on abort
- GPU tests also pass on the NVIDIA RTX 4060 (driver 595) via PRIME offload.
- Example GPU time per frame at about 1000 × 600: mountains 2.0 ms (RTX 4060)
  / 15 ms (Intel), ocean 4.3 ms and planet 2.5 ms (Intel).
- End to end: a 3840×2160 wallpaper; 1080p and 4K clips verified with
  ffprobe (frame count, duration, BT.709 tags); a video frame matches a still
  render at the same uTime within compression noise (mean 1.3/255).
- App runs checked with screenshots: code mode, broken-shader recovery,
  parameters loaded from a project, graph mode, a graph project rendering pure
  red at the exact viewport pixel, both example projects, CLI export.
- Packages: see the container install tests in the commit history.

Not covered by automated tests: file dialogs (Save As, Open, Export
location), colour picker dialogs, mouse orbit in the 3D viewport.

## Known limitations

- 4K video renders at roughly 14 frames per second here: colour conversion and
  read-back on the CPU dominate, not the GPU encoder. 1080p is much faster.
- H.264/HEVC need NVENC, FFmpeg with libx264/libx265, or a VAAPI driver with
  encoding. Fedora's `ffmpeg-free` and `libva-intel-media-driver` have neither
  software H.264 nor Intel H.264 encoding, so on Intel-only Fedora machines use
  AV1 or VP9 (or install RPM Fusion's full codecs).
- On hybrid laptops under Wayland, rendering on the NVIDIA GPU runs PrismForge
  through XWayland (NVIDIA offload through Wayland EGL failed on the test
  machine). Choose View → GPU → Integrated for native Wayland.

- Driver compilation runs on the GUI thread (glslang validation does not);
  very large shaders can briefly stall the UI.
- If glslang rejects a shader the driver is not asked; a shader only a lenient
  driver accepts is treated as failed.
- No textures/samplers, render targets or multi-pass rendering yet; sampler,
  matrix and array uniforms are listed as "not editable".
- Shader graph: fragment stage only; no sub-graphs; no copy/paste between
  projects; 2D noise driven by UVs shows the sphere's UV seam (use the 3D
  noise nodes for seamless results).
- Preview lighting is fixed (two lights); the plane is single-sided.
- Only tested on Intel/Mesa. NVIDIA/AMD log parsing is tested against sample
  strings, not real drivers.
- The project has no license file yet; packages declare `Proprietary`.

## Not implemented

Vulkan backend, plugin system, AI features.
