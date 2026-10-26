#include "render/FrameSetup.h"

#include <algorithm>

namespace pf {

FrameParams makeFrameParams(const OrbitCamera& camera, PreviewMesh mesh, QSize framebufferSize, float timeSeconds)
{
    FrameParams params;
    params.framebufferSize = framebufferSize;
    params.timeSeconds = timeSeconds;
    if (mesh == PreviewMesh::Screen) {
        // Identity transforms put the quad exactly over the viewport. The
        // "camera" sits in front of it so view-dependent shading still works.
        params.cameraPosition = QVector3D(0, 0, 2);
        return params;
    }
    params.view = camera.viewMatrix();
    params.projection =
        camera.projectionMatrix(float(framebufferSize.width()) / float(std::max(framebufferSize.height(), 1)));
    params.cameraPosition = camera.position();
    return params;
}

} // namespace pf
