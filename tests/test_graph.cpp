#include "graph/GraphCodegen.h"
#include "shader/GlslangValidator.h"

#include <QFile>

#include <catch2/catch_test_macros.hpp>

using namespace pf;
using namespace pf::graph;

namespace {

const NodeRegistry& reg() { return NodeRegistry::builtin(); }

QString defaultVertex()
{
    QFile file(QStringLiteral(PF_SHADER_DIR "/default.vert"));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return QString::fromUtf8(file.readAll());
}

bool hasIssue(const CodegenResult& r, Severity severity, const QString& fragment)
{
    for (const GraphIssue& i : r.issues)
        if (i.severity == severity && i.message.contains(fragment))
            return true;
    return false;
}

// Validates generated source with glslang when available.
void requireValidGlsl(const QString& fragment)
{
    if (!GlslangValidator::isAvailable())
        return;
    const auto diagnostics = GlslangValidator::validate({defaultVertex(), fragment});
    for (const Diagnostic& d : diagnostics)
        UNSCOPED_INFO(d.line << ": " << d.message.toStdString());
    if (hasErrors(diagnostics))
        UNSCOPED_INFO(fragment.toStdString());
    REQUIRE_FALSE(hasErrors(diagnostics));
}

} // namespace

TEST_CASE("Built-in node library loads without errors", "[graph]")
{
    NodeRegistry r;
    QStringList errors;
    const int loaded = r.loadFile(QStringLiteral(":/nodes/builtin.json"), &errors);
    for (const QString& e : errors)
        UNSCOPED_INFO(e.toStdString());
    CHECK(errors.isEmpty());
    CHECK(loaded == reg().size());
    CHECK(loaded >= 60);
    for (const char* id : {"math.add", "noise.fbm", "param.color", "output.surface", "vector.split"})
        CHECK(reg().find(id) != nullptr);
}

TEST_CASE("Invalid node definitions are rejected with reasons", "[graph]")
{
    NodeRegistry r;
    QStringList errors;
    const QByteArray json = R"({ "formatVersion": 1, "nodes": [
        { "id": "ok", "inputs": [{"id": "a", "type": "float"}], "outputs": [{"id": "out", "type": "float", "expr": "{a}"}] },
        { "id": "bad.placeholder", "outputs": [{"id": "out", "type": "float", "expr": "{nope}"}] },
        { "id": "bad.generic", "outputs": [{"id": "out", "type": "T", "expr": "1.0"}] },
        { "id": "bad.type", "inputs": [{"id": "a", "type": "mat4"}], "outputs": [{"id": "out", "type": "float", "expr": "1.0"}] },
        { "id": "bad.param", "role": "parameter", "outputs": [{"id": "v", "type": "float", "expr": "{name}"}] },
        { "id": "bad.output", "role": "output", "inputs": [], "outputs": [] },
        { "outputs": [] }
    ]})";
    CHECK(r.loadJson(json, "test", &errors) == 1);
    CHECK(errors.size() == 6);
    CHECK(errors.join('\n').contains("unknown placeholder {nope}"));
    CHECK(r.find("ok"));

    errors.clear();
    CHECK(r.loadJson(R"({"nodes": []})", "v0", &errors) == 0);
    CHECK(errors.size() == 1);
    CHECK(r.loadJson("not json", "broken", &errors) == 0);
    CHECK(errors.size() == 2);
}

TEST_CASE("User libraries can override built-in definitions", "[graph]")
{
    NodeRegistry r;
    r.loadFile(QStringLiteral(":/nodes/builtin.json"), nullptr);
    const QByteArray custom = R"json({ "formatVersion": 1, "nodes": [
        { "id": "math.add", "title": "My Add", "inputs": [{"id": "a", "type": "T"}, {"id": "b", "type": "T"}],
          "outputs": [{"id": "out", "type": "T", "expr": "{a} + {b} + {T}(0.0)"}] } ]})json";
    CHECK(r.loadJson(custom, "user", nullptr) == 1);
    CHECK(r.find("math.add")->title == "My Add");
    CHECK(r.find("math.add")->origin == "user");
}

TEST_CASE("Implicit conversions follow the documented rules", "[graph]")
{
    CHECK(canConvert(DataType::Float, DataType::Vec3));
    CHECK(canConvert(DataType::Vec4, DataType::Vec2));
    CHECK(canConvert(DataType::Vec2, DataType::Vec4));
    CHECK_FALSE(canConvert(DataType::Vec3, DataType::Float));

    CHECK(convertExpression("x", DataType::Float, DataType::Vec3) == "vec3(x)");
    CHECK(convertExpression("x", DataType::Vec4, DataType::Vec3) == "(x).xyz");
    CHECK(convertExpression("x", DataType::Vec3, DataType::Vec4) == "vec4(x, 1.0)");
    CHECK(convertExpression("x", DataType::Vec2, DataType::Vec4) == "vec4(x, 0.0, 1.0)");
    CHECK(convertExpression("x", DataType::Vec2, DataType::Vec2) == "x");

    CHECK(glslLiteral(DataType::Float, {2.0f}) == "2.0");
    CHECK(glslLiteral(DataType::Vec3, {0.5f, 1.0f, -3.25f, 9.0f}) == "vec3(0.5, 1.0, -3.25)");
    CHECK(glslLiteral(DataType::Float, {1.0e-7f}) == "1.00000001e-07");
}

TEST_CASE("Default graph generates valid and deterministic GLSL", "[graph]")
{
    const ShaderGraph graph = makeDefaultGraph();
    const CodegenResult first = generateFragmentShader(graph, reg());
    for (const GraphIssue& i : first.issues)
        UNSCOPED_INFO(i.nodeId << ": " << i.message.toStdString());
    REQUIRE(first.success);
    CHECK(first.issues.isEmpty());
    requireValidGlsl(first.source);

    // Same graph -> byte-identical source, also after a save/load round trip.
    CHECK(generateFragmentShader(graph, reg()).source == first.source);
    QString error;
    const auto reloaded = ShaderGraph::deserialize(graph.serialize(), &error);
    REQUIRE(reloaded);
    CHECK(*reloaded == graph);
    CHECK(generateFragmentShader(*reloaded, reg()).source == first.source);

    // Parameters become hinted uniforms; helpers are emitted once.
    CHECK(first.source.contains("uniform vec3 uDeepColor = vec3(0.0500000007, 0.180000007, 0.349999994); // @color"));
    CHECK(first.source.contains("uniform float uRoughness = 0.449999988; // @range(0.0, 1.0)"));
    CHECK(first.source.count("float pf_fbm3(") == 1);
    CHECK(first.source.contains("fragColor = pf_shadeSurface("));
}

TEST_CASE("Every built-in node generates GLSL that compiles", "[graph][glslang]")
{
    int graphs = 0;
    for (const NodeDefinition* def : reg().all()) {
        if (def->role == NodeRole::Output) {
            ShaderGraph g;
            g.addNode(def->id, {});
            const CodegenResult r = generateFragmentShader(g, reg());
            INFO("output node " << def->id.toStdString());
            REQUIRE(r.success);
            requireValidGlsl(r.source);
            ++graphs;
            continue;
        }
        // Each output, and for generic nodes both T = float and T = vec3.
        for (const OutputDefinition& out : def->outputs) {
            for (bool widen : {false, true}) {
                if (widen && !def->hasGenericPorts())
                    continue;
                ShaderGraph g;
                const int node = g.addNode(def->id, {});
                const int sink = g.addNode(QStringLiteral("output.unlit"), {});
                if (widen) {
                    const int v = g.addNode(QStringLiteral("const.vec3"), {});
                    for (const InputDefinition& in : def->inputs)
                        if (in.type.generic)
                            g.connect({v, QStringLiteral("out"), node, in.id});
                }
                g.connect({node, out.id, sink, QStringLiteral("color")});
                const CodegenResult r = generateFragmentShader(g, reg());
                INFO(def->id.toStdString() << "." << out.id.toStdString() << (widen ? " (vec3)" : ""));
                for (const GraphIssue& i : r.issues)
                    UNSCOPED_INFO(i.message.toStdString());
                REQUIRE(r.success);
                requireValidGlsl(r.source);
                if (widen)
                    CHECK(r.genericTypes.value(node) == DataType::Vec3);
                ++graphs;
            }
        }
    }
    CHECK(graphs > 80);
}

TEST_CASE("Graph validation reports structural errors", "[graph]")
{
    SECTION("missing output node")
    {
        ShaderGraph g;
        g.addNode("math.add", {});
        const auto r = generateFragmentShader(g, reg());
        CHECK_FALSE(r.success);
        CHECK(hasIssue(r, Severity::Error, "needs an output node"));
    }
    SECTION("two output nodes")
    {
        ShaderGraph g;
        g.addNode("output.unlit", {});
        g.addNode("output.surface", {});
        CHECK(hasIssue(generateFragmentShader(g, reg()), Severity::Error, "Only one output node"));
    }
    SECTION("cycle")
    {
        ShaderGraph g;
        const int out = g.addNode("output.unlit", {});
        const int a = g.addNode("math.add", {});
        const int b = g.addNode("math.add", {});
        g.connect({a, "out", b, "a"});
        g.connect({b, "out", a, "a"});
        g.connect({b, "out", out, "alpha"});
        const auto r = generateFragmentShader(g, reg());
        CHECK_FALSE(r.success);
        CHECK(hasIssue(r, Severity::Error, "cycle"));
    }
    SECTION("vector into float input is a type error")
    {
        ShaderGraph g;
        const int out = g.addNode("output.unlit", {});
        const int uv = g.addNode("input.uv", {});
        g.connect({uv, "out", out, "alpha"});
        const auto r = generateFragmentShader(g, reg());
        CHECK_FALSE(r.success);
        CHECK(hasIssue(r, Severity::Error, "Cannot connect vec2 to float"));
    }
    SECTION("unknown node type")
    {
        ShaderGraph g;
        g.addNode("output.unlit", {});
        g.addNode("does.not.exist", {});
        CHECK(hasIssue(generateFragmentShader(g, reg()), Severity::Error, "Unknown node type"));
    }
    SECTION("bad and duplicate parameter names")
    {
        ShaderGraph g;
        const int out = g.addNode("output.unlit", {});
        const int p1 = g.addNode("param.float", {});
        const int p2 = g.addNode("param.float", {});
        const int p3 = g.addNode("param.float", {});
        const int add = g.addNode("math.add", {});
        const int add2 = g.addNode("math.add", {});
        g.node(p1)->parameterName = "uSame";
        g.node(p2)->parameterName = "uSame";
        g.node(p3)->parameterName = "uTime"; // built-in
        g.connect({p1, "value", add, "a"});
        g.connect({p2, "value", add, "b"});
        g.connect({add, "out", add2, "a"});
        g.connect({p3, "value", add2, "b"});
        g.connect({add2, "out", out, "alpha"});
        const auto r = generateFragmentShader(g, reg());
        CHECK_FALSE(r.success);
        CHECK(hasIssue(r, Severity::Error, "already used"));
        CHECK(hasIssue(r, Severity::Error, "reserved"));

        g.node(p3)->parameterName = "2bad";
        CHECK(hasIssue(generateFragmentShader(g, reg()), Severity::Error, "not a valid GLSL identifier"));
    }
    SECTION("disconnected nodes only warn and are not emitted")
    {
        ShaderGraph g;
        g.addNode("output.unlit", {});
        const int stray = g.addNode("noise.voronoi", {});
        const auto r = generateFragmentShader(g, reg());
        REQUIRE(r.success);
        CHECK(hasIssue(r, Severity::Warning, "Not connected"));
        CHECK_FALSE(r.source.contains("pf_voronoi"));
        CHECK_FALSE(r.source.contains(QStringLiteral("n%1_").arg(stray)));
    }
}

TEST_CASE("Generic type resolves to the widest connected input", "[graph]")
{
    ShaderGraph g;
    const int out = g.addNode("output.unlit", {});
    const int f = g.addNode("const.float", {});
    const int v2 = g.addNode("const.vec2", {});
    const int add = g.addNode("math.add", {});
    g.connect({f, "out", add, "a"});
    g.connect({v2, "out", add, "b"});
    g.connect({add, "out", out, "color"});
    const auto r = generateFragmentShader(g, reg());
    REQUIRE(r.success);
    CHECK(r.genericTypes.value(add) == DataType::Vec2);
    CHECK(r.source.contains(QStringLiteral("vec2 n%1_out = (vec2(n%2_out)) + (n%3_out);").arg(add).arg(f).arg(v2)));
    CHECK(r.source.contains(QStringLiteral("vec3(n%1_out, 0.0)").arg(add))); // padded into the vec3 colour
    requireValidGlsl(r.source);
}

TEST_CASE("Unconnected inputs use stored values or default expressions", "[graph]")
{
    ShaderGraph g;
    const int out = g.addNode("output.unlit", {});
    const int noise = g.addNode("noise.value", {});
    g.node(noise)->inputValues.insert("scale", {3.5f, 0, 0, 0});
    g.connect({noise, "out", out, "color"});
    const auto r = generateFragmentShader(g, reg());
    REQUIRE(r.success);
    CHECK(r.source.contains("pf_valueNoise((vUV) * (3.5))"));
}

TEST_CASE("Graph documents reject malformed JSON", "[graph]")
{
    QString error;
    CHECK_FALSE(ShaderGraph::deserialize("{", &error));
    CHECK_FALSE(ShaderGraph::deserialize(R"({"formatVersion": 99})", &error));
    CHECK(error.contains("99"));
    CHECK_FALSE(ShaderGraph::deserialize(
        R"({"formatVersion": 1, "nodes": [{"id": 1, "type": "a"}], "connections": [{"from": 1, "to": 7}]})", &error));
}

TEST_CASE("Connecting replaces an existing connection to the same input", "[graph]")
{
    ShaderGraph g;
    const int a = g.addNode("const.float", {});
    const int b = g.addNode("const.float", {});
    const int add = g.addNode("math.add", {});
    CHECK(g.connect({a, "out", add, "a"}));
    CHECK(g.connect({b, "out", add, "a"}));
    CHECK(g.connections().size() == 1);
    CHECK(g.connectionTo(add, "a")->fromNode == b);
    CHECK_FALSE(g.connect({add, "out", add, "b"})); // self-connection
    g.removeNode(b);
    CHECK(g.connections().isEmpty());
}
