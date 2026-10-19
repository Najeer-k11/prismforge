#pragma once

#include "render/Mesh.h"
#include "render/OrbitCamera.h"
#include "render/Renderer.h"

namespace pf {

// Builds the per-frame inputs shared by the live viewport, image export and
// video export, so all three render identically.
FrameParams makeFrameParams(const OrbitCamera& camera, PreviewMesh mesh, QSize framebufferSize,
                            float timeSeconds);

} // namespace pf
