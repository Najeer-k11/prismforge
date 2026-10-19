#include "render/Mesh.h"

#include <cmath>
#include <numbers>

namespace pf {

QString toString(PreviewMesh mesh)
{
    switch (mesh) {
    case PreviewMesh::Sphere: return QStringLiteral("sphere");
    case PreviewMesh::Plane: return QStringLiteral("plane");
    case PreviewMesh::Cube: return QStringLiteral("cube");
    }
    return {};
}

std::optional<PreviewMesh> previewMeshFromString(const QString& name)
{
    for (PreviewMesh m : {PreviewMesh::Sphere, PreviewMesh::Plane, PreviewMesh::Cube})
        if (name.compare(toString(m), Qt::CaseInsensitive) == 0)
            return m;
    return std::nullopt;
}

MeshData makeSphere(int segments, int rings, float radius)
{
    using std::numbers::pi_v;
    MeshData mesh;
    mesh.vertices.reserve(std::size_t(segments + 1) * std::size_t(rings + 1));

    // Duplicate the seam column so UVs wrap cleanly.
    for (int r = 0; r <= rings; ++r) {
        const float v = float(r) / float(rings);
        const float theta = v * pi_v<float>; // 0 at +Y pole
        for (int s = 0; s <= segments; ++s) {
            const float u = float(s) / float(segments);
            const float phi = u * 2.0f * pi_v<float>;
            const float nx = std::sin(theta) * std::sin(phi);
            const float ny = std::cos(theta);
            const float nz = std::sin(theta) * std::cos(phi);
            mesh.vertices.push_back({{nx * radius, ny * radius, nz * radius},
                                     {nx, ny, nz},
                                     {u, 1.0f - v}});
        }
    }

    const std::uint32_t stride = std::uint32_t(segments + 1);
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < segments; ++s) {
            const std::uint32_t a = std::uint32_t(r) * stride + std::uint32_t(s);
            const std::uint32_t b = a + stride;
            // Skip the degenerate triangles that collapse onto each pole.
            if (r != 0)
                mesh.indices.insert(mesh.indices.end(), {a, b, a + 1});
            if (r != rings - 1)
                mesh.indices.insert(mesh.indices.end(), {a + 1, b, b + 1});
        }
    }
    return mesh;
}

MeshData makePlane(float size)
{
    const float h = size * 0.5f;
    MeshData mesh;
    mesh.vertices = {
        {{-h, 0, h}, {0, 1, 0}, {0, 0}},
        {{h, 0, h}, {0, 1, 0}, {1, 0}},
        {{h, 0, -h}, {0, 1, 0}, {1, 1}},
        {{-h, 0, -h}, {0, 1, 0}, {0, 1}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

MeshData makeCube(float size)
{
    const float h = size * 0.5f;
    struct Face { float n[3]; float u[3]; float v[3]; };
    // For each face: normal, and the tangent (u) / bitangent (v) axes such that
    // u x v = n, which yields counter-clockwise winding seen from outside.
    static constexpr Face faces[] = {
        {{1, 0, 0}, {0, 0, -1}, {0, 1, 0}},
        {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {1, 0, 0}, {0, 0, -1}},
        {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},
        {{0, 0, -1}, {-1, 0, 0}, {0, 1, 0}},
    };
    static constexpr float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    MeshData mesh;
    for (const Face& f : faces) {
        const auto base = std::uint32_t(mesh.vertices.size());
        for (const auto& c : corners) {
            Vertex vtx{};
            for (int i = 0; i < 3; ++i) {
                vtx.position[i] = h * (f.n[i] + c[0] * f.u[i] + c[1] * f.v[i]);
                vtx.normal[i] = f.n[i];
            }
            vtx.uv[0] = (c[0] + 1.0f) * 0.5f;
            vtx.uv[1] = (c[1] + 1.0f) * 0.5f;
            mesh.vertices.push_back(vtx);
        }
        mesh.indices.insert(mesh.indices.end(),
                            {base, base + 1, base + 2, base, base + 2, base + 3});
    }
    return mesh;
}

MeshData makeMesh(PreviewMesh mesh)
{
    switch (mesh) {
    case PreviewMesh::Sphere: return makeSphere();
    case PreviewMesh::Plane: return makePlane();
    case PreviewMesh::Cube: return makeCube();
    }
    return makeSphere();
}

} // namespace pf
