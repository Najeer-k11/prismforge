# PrismForge

A native desktop environment for writing GLSL shaders and previewing them
on the GPU in real time. C++20, Qt 6, OpenGL 4.5 core.

This is **Milestone 1: a working shader workbench**. See
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the module layout, compile
pipeline and the shader interface (attributes and uniforms) your shaders can use.

## Requirements

| Component | Tested version | Fedora package |
|---|---|---|
| C++20 compiler | GCC 16.2 | `gcc-c++` |
| CMake ≥ 3.24 + Ninja | 4.3 / 1.13 | `cmake ninja-build` |
| Qt 6 (Core, Gui, Widgets, OpenGL, OpenGLWidgets, Concurrent) | 6.11.2 | `qt6-qtbase-devel` |
| glslang (optional, recommended) | 16.2.0 | `glslang glslang-devel` |
| Catch2 v3 (tests only) | 3.13.0 | `catch-devel` |
| OpenGL 4.5 core driver | Mesa 26.2.5 (Intel) | — |

```sh
sudo dnf install gcc-c++ cmake ninja-build qt6-qtbase-devel glslang glslang-devel catch-devel
```

Without glslang the build still works, and shaders are validated by the GL driver
alone (CMake prints a warning). Use `-DPRISMFORGE_BUILD_TESTS=OFF` to skip Catch2.

## Build, test, run

```sh
cmake --preset release            # or: debug
cmake --build --preset release
ctest --preset release            # unit tests + GPU integration tests
./build/release/src/prismforge [project.pfproj]
```

The GPU tests (`-L gpu`) need an OpenGL 4.5 context. If none is available they
exit with a skip code instead of failing.

### Command-line options

| Option | Purpose |
|---|---|
| `project.pfproj` | Open a project on startup |
| `--screenshot <file.png>` | Save a screenshot of the window, then quit |
| `--screenshot-delay <ms>` | Delay before the screenshot (default 2000) |
| `--smoke-replace-fragment <file>` | After the first successful compile, swap in this fragment shader and recompile. Used to check the "failed compile keeps the last working preview" behaviour from a script. |

## Using it

| Action | Shortcut |
|---|---|
| Compile | F5 |
| Toggle auto-compile (350 ms after the last edit) | toolbar |
| Fragment / vertex tab | Ctrl+1 / Ctrl+2 |
| Sphere / Plane / Cube | Alt+1 / Alt+2 / Alt+3 |
| Orbit / zoom / reset camera | left- or middle-drag / wheel / Home |
| New / Open / Save / Save As project | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S |
| Open shader / Save shader | Ctrl+Shift+O / Ctrl+Alt+S |

Double-click a diagnostic to jump to its line. Saving an untitled project writes
the shader buffers to `shaders/<name>.vert|.frag` next to the `.pfproj` file.

## Status

**Verified on Fedora 44 / Wayland, Mesa 26.2.5, Intel RPL-P (GL 4.6 core):**

- Debug and Release builds compile with `-Wall -Wextra -Wpedantic` and no warnings.
- 26/26 automated tests pass: 20 unit tests (log parsing, project I/O, meshes,
  camera, glslang) and 6 GPU tests that render offscreen and check pixels:
  - default shaders compile, link and draw
  - a solid-colour shader produces exactly that colour on the sphere and cube
  - **a failed compile, or a failed link, leaves the previous program rendering**
  - driver (Mesa) errors are parsed to the correct source line
- Running the app (checked with screenshots):
  - the default shader renders on the sphere, and the docked layout works
  - a broken fragment shader injected at runtime shows errors with line:column,
    marks the line in the gutter, and the preview keeps showing the last working shader
  - opening a `.pfproj` resolves relative shader paths and applies its mesh,
    camera and auto-compile settings; the cube and plane render with correct
    orientation and back-face culling

**Implemented but only exercised by hand-reading, not by an automated or scripted run:**
Save Project / Save Shader through the file dialogs, mouse orbit/zoom, the
unsaved-changes prompts, and saved window layout. `ProjectIO::save` itself is
unit-tested.

## Known limitations

- Shader compilation by the driver runs on the GUI thread (glslang validation
  is off-thread). Very large shaders can briefly stall the UI.
- If glslang rejects a shader, the driver is not asked to compile it. A shader
  that glslang rejects but a lenient driver would accept is therefore treated
  as failed.
- The plane is single-sided: viewed from below it is culled.
- No material parameter panel yet. Shaders get only the built-in uniforms.
- No textures, render targets, or multi-pass rendering.
- `uTime` counts from application start, with no pause or reset.
- GPU time comes from `GL_TIME_ELAPSED` queries, which arrive a few frames late.
  Shown as "unsupported" where the driver has no timer bits.
- Only tested on one Linux machine with an Intel/Mesa driver. The NVIDIA log
  parser is tested against sample strings only.

## Not in this milestone

Visual node graph, Vulkan backend, plugin system, export, AI features.
