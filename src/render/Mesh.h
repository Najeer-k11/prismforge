#pragma once

#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

namespace pf {

// Vertex layout shared by every preview mesh and by the shader interface:
// location 0 = position, 1 = normal, 2 = uv.
struct Vertex {
    float position[3];
    float normal[3];
    float uv[2];
};
static_assert(sizeof(Vertex) == 8 * sizeof(float), "Vertex must be tightly packed");

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices; // triangle list, counter-clockwise front faces

    std::size_t triangleCount() const { return indices.size() / 3; }
};

// Screen is a full-viewport quad (for 2D effects and wallpapers): it is drawn
// with identity matrices, so it ignores the camera.
enum class PreviewMesh { Sphere, Plane, Cube, Screen };
inline constexpr PreviewMesh kAllPreviewMeshes[] = {PreviewMesh::Sphere, PreviewMesh::Plane, PreviewMesh::Cube,
                                                   PreviewMesh::Screen};

QString toString(PreviewMesh mesh);
std::optional<PreviewMesh> previewMeshFromString(const QString& name);

// Unit-scale meshes centred on the origin.
MeshData makeSphere(int segments = 64, int rings = 32, float radius = 1.0f);
MeshData makePlane(float size = 2.0f);  // XZ plane, facing +Y
MeshData makeCube(float size = 1.6f);   // 24 vertices, hard per-face normals
MeshData makeScreenQuad();              // [-1, 1] in XY at z = 0, facing +Z
MeshData makeMesh(PreviewMesh mesh);

} // namespace pf
