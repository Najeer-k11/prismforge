#include "render/Mesh.h"
#include "render/OrbitCamera.h"

#include <QVector3D>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace pf;
using Catch::Matchers::WithinAbs;

namespace {
QVector3D pos(const Vertex& v) { return {v.position[0], v.position[1], v.position[2]}; }
QVector3D nrm(const Vertex& v) { return {v.normal[0], v.normal[1], v.normal[2]}; }
} // namespace

TEST_CASE("Preview meshes are well formed", "[mesh]")
{
    const PreviewMesh kind = GENERATE(PreviewMesh::Sphere, PreviewMesh::Plane, PreviewMesh::Cube);
    const MeshData mesh = makeMesh(kind);
    INFO("mesh: " << toString(kind).toStdString());

    REQUIRE_FALSE(mesh.vertices.empty());
    REQUIRE(mesh.indices.size() % 3 == 0);

    for (std::uint32_t i : mesh.indices)
        REQUIRE(i < mesh.vertices.size());

    for (const Vertex& v : mesh.vertices) {
        REQUIRE_THAT(nrm(v).length(), WithinAbs(1.0, 1e-4));
        REQUIRE(v.uv[0] >= 0.0f);
        REQUIRE(v.uv[0] <= 1.0f);
        REQUIRE(v.uv[1] >= 0.0f);
        REQUIRE(v.uv[1] <= 1.0f);
    }

    // Counter-clockwise winding: the geometric normal agrees with the vertex
    // normals, so back-face culling keeps the outside visible.
    for (std::size_t t = 0; t < mesh.indices.size(); t += 3) {
        const Vertex& a = mesh.vertices[mesh.indices[t]];
        const Vertex& b = mesh.vertices[mesh.indices[t + 1]];
        const Vertex& c = mesh.vertices[mesh.indices[t + 2]];
        const QVector3D face = QVector3D::crossProduct(pos(b) - pos(a), pos(c) - pos(a));
        REQUIRE(face.length() > 0.0f); // no degenerate triangles
        REQUIRE(QVector3D::dotProduct(face, nrm(a) + nrm(b) + nrm(c)) > 0.0f);
    }
}

TEST_CASE("Mesh sizes", "[mesh]")
{
    CHECK(makePlane().triangleCount() == 2);
    CHECK(makeCube().vertices.size() == 24);
    CHECK(makeCube().triangleCount() == 12);
    // 2 triangles per quad minus one per segment at each pole.
    CHECK(makeSphere(16, 8).triangleCount() == 16 * 8 * 2 - 2 * 16);
}

TEST_CASE("Mesh names round-trip", "[mesh]")
{
    for (PreviewMesh m : {PreviewMesh::Sphere, PreviewMesh::Plane, PreviewMesh::Cube})
        CHECK(previewMeshFromString(toString(m)) == m);
    CHECK(previewMeshFromString("CUBE") == PreviewMesh::Cube);
    CHECK_FALSE(previewMeshFromString("teapot"));
}

TEST_CASE("Orbit camera clamps pitch and distance", "[camera]")
{
    OrbitCamera cam;
    cam.orbit(0.0f, 500.0f);
    CHECK(cam.pitch() == OrbitCamera::kMaxPitch);
    cam.orbit(0.0f, -1000.0f);
    CHECK(cam.pitch() == -OrbitCamera::kMaxPitch);

    cam.zoom(1000.0f);
    CHECK(cam.distance() == OrbitCamera::kMinDistance);
    cam.zoom(-1000.0f);
    CHECK(cam.distance() == OrbitCamera::kMaxDistance);
}

TEST_CASE("Orbit camera position is at the requested distance", "[camera]")
{
    OrbitCamera cam;
    cam.set(0.0f, 0.0f, 4.0f);
    CHECK_THAT(cam.position().z(), WithinAbs(4.0, 1e-5));
    CHECK_THAT(cam.position().x(), WithinAbs(0.0, 1e-5));

    cam.set(123.0f, 37.0f, 2.5f);
    CHECK_THAT(cam.position().length(), WithinAbs(2.5, 1e-5));

    // The origin projects to the centre of the screen.
    const QVector3D clip = cam.projectionMatrix(1.5f).map(cam.viewMatrix().map(QVector3D(0, 0, 0)));
    CHECK_THAT(clip.x(), WithinAbs(0.0, 1e-5));
    CHECK_THAT(clip.y(), WithinAbs(0.0, 1e-5));
}
