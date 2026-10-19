#include "project/Project.h"

#include <QDir>
#include <QJsonDocument>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace pf;

namespace {
Project sampleProject()
{
    Project p;
    p.name = "Demo";
    p.vertexShaderPath = "shaders/demo.vert";
    p.fragmentShaderPath = "shaders/demo.frag";
    p.previewMesh = "cube";
    p.autoCompile = false;
    p.camera = {45.0f, -10.0f, 6.0f};
    return p;
}
} // namespace

TEST_CASE("Project round-trips through JSON", "[project]")
{
    const Project original = sampleProject();
    const auto loaded = ProjectIO::deserialize(ProjectIO::serialize(original));
    REQUIRE(loaded.project.has_value());
    CHECK(*loaded.project == original);
}

TEST_CASE("Serialisation is deterministic", "[project]")
{
    CHECK(ProjectIO::serialize(sampleProject()) == ProjectIO::serialize(sampleProject()));
    CHECK(ProjectIO::serialize(sampleProject()).endsWith('\n'));
}

TEST_CASE("Invalid project files are rejected with a reason", "[project]")
{
    CHECK_FALSE(ProjectIO::deserialize("{not json").project);
    CHECK_FALSE(ProjectIO::deserialize("[]").project);

    QJsonObject json = ProjectIO::toJson(sampleProject());
    json["formatVersion"] = Project::kFormatVersion + 1;
    auto newer = ProjectIO::fromJson(json);
    CHECK_FALSE(newer.project);
    CHECK(newer.error.contains("newer"));

    json = ProjectIO::toJson(sampleProject());
    json.remove("formatVersion");
    CHECK_FALSE(ProjectIO::fromJson(json).project);

    json = ProjectIO::toJson(sampleProject());
    json["shaders"] = QJsonObject{{"vertex", "a.vert"}};
    auto missing = ProjectIO::fromJson(json);
    CHECK_FALSE(missing.project);
    CHECK(missing.error.contains("fragment"));
}

TEST_CASE("Missing optional fields fall back to defaults", "[project]")
{
    const QJsonObject json{
        {"formatVersion", 1},
        {"shaders", QJsonObject{{"vertex", "a.vert"}, {"fragment", "a.frag"}}},
    };
    const auto loaded = ProjectIO::fromJson(json);
    REQUIRE(loaded.project);
    const Project defaults;
    CHECK(loaded.project->previewMesh == defaults.previewMesh);
    CHECK(loaded.project->camera == defaults.camera);
}

TEST_CASE("Shader paths are stored relative to the project file", "[project]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString projectFile = dir.filePath("proj/demo.pfproj");
    const QString shader = dir.filePath("proj/shaders/demo.frag");

    const QString relative = ProjectIO::toRelativePath(projectFile, shader);
    CHECK(relative == "shaders/demo.frag");
    CHECK(ProjectIO::toAbsolutePath(projectFile, relative) == QDir::cleanPath(shader));

    const QString sibling = dir.filePath("common/lib.frag");
    CHECK(ProjectIO::toRelativePath(projectFile, sibling) == "../common/lib.frag");
}

TEST_CASE("Project save and load through the filesystem", "[project]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath("demo.pfproj");

    QString error;
    REQUIRE(ProjectIO::save(sampleProject(), path, &error));
    const auto loaded = ProjectIO::load(path);
    REQUIRE(loaded.project);
    CHECK(*loaded.project == sampleProject());

    CHECK_FALSE(ProjectIO::load(dir.filePath("missing.pfproj")).project);
}
