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

## Install a package

| Distribution | Package | Install |
|---|---|---|
| Fedora 44 | `prismforge-0.2.0-1.fc44.x86_64.rpm` | `sudo dnf install ./prismforge-0.2.0-1.fc44.x86_64.rpm` |
| Ubuntu 24.04 | `prismforge_0.2.0_amd64.deb` | `sudo apt install ./prismforge_0.2.0_amd64.deb` |

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
| `--screenshot <png>` / `--screenshot-delay <ms>` | Save a window screenshot and exit |
| `--smoke-replace-fragment <file>` | After the first good compile, swap in this fragment shader (scripted testing) |

## Using it

| Action | Shortcut |
|---|---|
| Compile / toggle shader graph mode | F5 / Ctrl+G |
| Fragment / vertex tab | Ctrl+1 / Ctrl+2 |
| Sphere / plane / cube | Alt+1 / Alt+2 / Alt+3 |
| Orbit / zoom / reset camera | drag / wheel / Home |
| New / open / save / save-as project | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Export material | Ctrl+E |
| Graph: add node / connect / disconnect | right-click canvas / drag between ports / drag an input away |
| Graph: delete / duplicate / frame / undo / redo | Del / Ctrl+D / F / Ctrl+Z / Ctrl+Shift+Z |

**Graph mode** generates the fragment shader from the graph. The hand-written
fragment file is never modified or parsed into nodes: arbitrary GLSL cannot be
represented as a graph, so the two are kept separate and you switch between
them with Ctrl+G. Generated GLSL is shown read-only in the editor.

**Custom nodes**: put JSON files in `~/.local/share/prismforge/nodes/` (all
projects) or `<project>/nodes/` (one project) and use *Graph → Reload Node
Libraries*. The format is the one used by
[resources/nodes/builtin.json](resources/nodes/builtin.json); a definition with
the same `id` as a built-in replaces it.

## Verification status

Verified on Fedora 44 (Wayland), Mesa 26.2.5, Intel RPL-P, GL 4.6 core:

- Debug and Release builds with `-Wall -Wextra -Wpedantic`: no warnings.
- **58 automated tests pass**:
  - unit: diagnostics parsing, project/graph/material JSON, meshes, camera,
    glslang validation, log capture, export
  - graph: every built-in node's GLSL compiled by glslang (each output, and
    generic nodes with both `float` and `vec3`), validation errors, determinism
  - GPU (offscreen render + pixel checks): compile/link/draw, failed compile or
    link keeps the previous program, uniform introspection, parameters change
    pixels, the default graph links and draws
  - widgets (offscreen, synthetic input): connect by dragging in both
    directions, disconnect, move, delete, duplicate, undo/redo
- App runs checked with screenshots: code mode, broken-shader recovery,
  parameters loaded from a project, graph mode, a graph project rendering pure
  red at the exact viewport pixel, both example projects, CLI export.
- Packages: see the container install tests in the commit history.

Not covered by automated tests: file dialogs (Save As, Open, Export
location), colour picker dialogs, mouse orbit in the 3D viewport.

## Known limitations

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
