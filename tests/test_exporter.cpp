#include "project/Exporter.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace pf;

namespace {
ExportRequest sampleRequest(const QString& dir)
{
    ExportRequest r;
    r.directory = dir;
    r.name = "Rusty Metal/v2";
    r.sources = {"#version 450 core\nvoid main() {}\n", "#version 450 core\nout vec4 c;\nvoid main() { c = vec4(1); }\n"};
    MaterialParameter rough{"uRoughness", ParamType::Float, {0.25f, 0, 0, 0}, {0.5f, 0, 0, 0}, {}};
    rough.hints.min = 0.0f;
    rough.hints.max = 1.0f;
    rough.hints.label = "Roughness";
    MaterialParameter tint{"uTint", ParamType::Vec3, {1.0f, 0.5f, 0.25f, 0}, {1, 1, 1, 0}, {}};
    tint.hints.color = true;
    r.parameters = {rough, tint};
    return r;
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    REQUIRE(f.open(QIODevice::ReadOnly));
    return f.readAll();
}
} // namespace

TEST_CASE("Export names are made file-system safe", "[export]")
{
    CHECK(Exporter::sanitizeName("Rusty Metal/v2") == "Rusty_Metal_v2");
    CHECK(Exporter::sanitizeName("../../etc") == "etc");
    CHECK(Exporter::sanitizeName("   ") == "material");
    CHECK(Exporter::sanitizeName("ok-name_1.0") == "ok-name_1.0");
}

TEST_CASE("Exporting writes shaders and a material description", "[export]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const ExportRequest request = sampleRequest(dir.filePath("out"));
    const ExportResult result = Exporter::exportMaterial(request);
    REQUIRE(result.success);
    CHECK(result.writtenFiles == Exporter::plannedFiles(request));
    CHECK(result.writtenFiles.size() == 3);

    CHECK(readAll(dir.filePath("out/Rusty_Metal_v2.vert")) == request.sources.vertex.toUtf8());
    CHECK(readAll(dir.filePath("out/Rusty_Metal_v2.frag")) == request.sources.fragment.toUtf8());

    const QJsonObject json = QJsonDocument::fromJson(readAll(dir.filePath("out/Rusty_Metal_v2.material.json"))).object();
    CHECK(json["formatVersion"].toInt() == 1);
    CHECK(json["shaders"].toObject()["fragment"].toString() == "Rusty_Metal_v2.frag");
    CHECK_FALSE(json["shaders"].toObject().contains("graph"));
    CHECK(json["interface"].toObject()["attributes"].toArray().size() == 3);

    const QJsonArray params = json["parameters"].toArray();
    REQUIRE(params.size() == 2);
    const QJsonObject rough = params[0].toObject();
    CHECK(rough["name"].toString() == "uRoughness");
    CHECK(rough["value"].toArray() == QJsonArray{0.25});
    CHECK(rough["default"].toArray() == QJsonArray{0.5});
    CHECK(rough["hints"].toObject()["max"].toDouble() == 1.0);
    CHECK(rough["hints"].toObject()["label"].toString() == "Roughness");
    const QJsonObject tint = params[1].toObject();
    CHECK(tint["value"].toArray() == QJsonArray{1.0, 0.5, 0.25});
    CHECK(tint["hints"].toObject()["color"].toBool());
}

TEST_CASE("Graph and preview are exported when provided", "[export]")
{
    QTemporaryDir dir;
    ExportRequest request = sampleRequest(dir.path());
    request.graphJson = R"({"formatVersion": 1})";
    request.previewPng = QByteArray("\x89PNG fake", 9);
    const ExportResult result = Exporter::exportMaterial(request);
    REQUIRE(result.success);
    CHECK(result.writtenFiles.size() == 5);
    CHECK(readAll(dir.filePath("Rusty_Metal_v2.pfgraph")) == request.graphJson);
    const QJsonObject json = QJsonDocument::fromJson(readAll(dir.filePath("Rusty_Metal_v2.material.json"))).object();
    CHECK(json["shaders"].toObject()["graph"].toString() == "Rusty_Metal_v2.pfgraph");
    CHECK(json["preview"].toString() == "Rusty_Metal_v2.preview.png");
}

TEST_CASE("Export reports unwritable destinations", "[export]")
{
    QTemporaryDir dir;
    QFile blocker(dir.filePath("file"));
    REQUIRE(blocker.open(QIODevice::WriteOnly));
    blocker.close();
    ExportRequest request = sampleRequest(dir.filePath("file/sub")); // parent is a file
    const ExportResult result = Exporter::exportMaterial(request);
    CHECK_FALSE(result.success);
    CHECK_FALSE(result.error.isEmpty());
}
