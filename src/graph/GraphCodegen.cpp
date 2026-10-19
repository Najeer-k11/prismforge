#include "graph/GraphCodegen.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <set>

namespace pf::graph {

namespace {

const QRegularExpression kPlaceholder(QStringLiteral(R"(\{(\w+)\})"));
const QRegularExpression kIdentifier(QStringLiteral("^[A-Za-z_][A-Za-z0-9_]*$"));

bool isReservedName(const QString& name)
{
    static const QSet<QString> keywords = {
        "float", "int", "uint", "bool", "vec2", "vec3", "vec4", "mat2", "mat3", "mat4", "void",
        "in", "out", "inout", "uniform", "const", "if", "else", "for", "while", "do", "return",
        "break", "continue", "discard", "struct", "true", "false", "main", "fragColor",
        "vWorldPosition", "vWorldNormal", "vUV"};
    return keywords.contains(name) || name.startsWith(u"gl_") || name.startsWith(u"pf_") ||
           name.contains(u"__") || isBuiltinUniform(name);
}

QString variableName(int nodeId, const QString& port)
{
    return QStringLiteral("n%1_%2").arg(nodeId).arg(port);
}

// Everything derived from the graph that both validation and emission need.
struct Analysis {
    QHash<int, const NodeDefinition*> defs; // nodes with a known definition
    QList<Connection> edges;                // structurally valid connections
    QList<int> order;                       // topological order (acyclic part)
    QHash<int, DataType> genericTypes;
    QHash<int, QString> parameterNames;
    int outputNode = -1;
    QSet<int> reachable; // nodes contributing to the output
    QList<GraphIssue> issues;

    bool hasErrors() const
    {
        return std::any_of(issues.begin(), issues.end(),
                           [](const GraphIssue& i) { return i.severity == Severity::Error; });
    }
    void error(int node, const QString& port, const QString& message)
    {
        issues.append({Severity::Error, node, port, message});
    }
};

std::optional<DataType> outputType(const Analysis& a, int nodeId, const QString& port)
{
    const NodeDefinition* def = a.defs.value(nodeId);
    const OutputDefinition* out = def ? def->output(port) : nullptr;
    if (!out)
        return std::nullopt;
    return out->type.generic ? a.genericTypes.value(nodeId, DataType::Float) : out->type.concrete;
}

Analysis analyze(const ShaderGraph& graph, const NodeRegistry& registry)
{
    Analysis a;

    for (const GraphNode& n : graph.nodes()) {
        if (const NodeDefinition* def = registry.find(n.definitionId))
            a.defs.insert(n.id, def);
        else
            a.error(n.id, {}, QStringLiteral("Unknown node type '%1'").arg(n.definitionId));
    }

    for (const Connection& c : graph.connections()) {
        const NodeDefinition* from = a.defs.value(c.fromNode);
        const NodeDefinition* to = a.defs.value(c.toNode);
        if (!from || !to)
            continue; // already reported as unknown node
        const InputDefinition* in = to->input(c.toPort);
        if (!from->output(c.fromPort)) {
            a.error(c.fromNode, c.fromPort, QStringLiteral("Connection from unknown output '%1'").arg(c.fromPort));
            continue;
        }
        if (!in || !in->connectable) {
            a.error(c.toNode, c.toPort, QStringLiteral("Input '%1' does not accept connections").arg(c.toPort));
            continue;
        }
        a.edges.append(c);
    }

    // Exactly one output node.
    QList<int> outputs;
    for (auto it = a.defs.cbegin(); it != a.defs.cend(); ++it)
        if ((*it)->role == NodeRole::Output)
            outputs.append(it.key());
    std::sort(outputs.begin(), outputs.end());
    if (outputs.isEmpty())
        a.error(-1, {}, QStringLiteral("The graph needs an output node (e.g. Surface Output)"));
    for (int i = 1; i < outputs.size(); ++i)
        a.error(outputs[i], {}, QStringLiteral("Only one output node is allowed"));
    if (!outputs.isEmpty())
        a.outputNode = outputs.first();

    // Topological sort (Kahn); the smallest ready id goes first, which makes
    // the order — and therefore the generated source — deterministic.
    QHash<int, int> inDegree;
    QHash<int, QList<int>> successors;
    for (int id : a.defs.keys())
        inDegree.insert(id, 0);
    for (const Connection& c : a.edges) {
        ++inDegree[c.toNode];
        successors[c.fromNode].append(c.toNode);
    }
    std::set<int> ready;
    for (auto it = inDegree.cbegin(); it != inDegree.cend(); ++it)
        if (it.value() == 0)
            ready.insert(it.key());
    while (!ready.empty()) {
        const int id = *ready.begin();
        ready.erase(ready.begin());
        a.order.append(id);
        for (int next : successors.value(id))
            if (--inDegree[next] == 0)
                ready.insert(next);
    }
    if (a.order.size() != a.defs.size()) {
        QList<int> cyclic;
        for (auto it = inDegree.cbegin(); it != inDegree.cend(); ++it)
            if (it.value() > 0)
                cyclic.append(it.key());
        std::sort(cyclic.begin(), cyclic.end());
        for (int id : cyclic)
            a.error(id, {}, QStringLiteral("Node is part of a cycle"));
    }

    // Resolve generic types and check connection types in dependency order.
    for (int id : a.order) {
        const NodeDefinition* def = a.defs.value(id);
        DataType t = DataType::Float;
        for (const InputDefinition& in : def->inputs) {
            if (!in.type.generic)
                continue;
            if (const auto c = graph.connectionTo(id, in.id))
                if (const auto src = outputType(a, c->fromNode, c->fromPort))
                    t = std::max(t, *src);
        }
        if (def->hasGenericPorts())
            a.genericTypes.insert(id, t);

        for (const InputDefinition& in : def->inputs) {
            const auto c = graph.connectionTo(id, in.id);
            if (!c || !a.edges.contains(*c))
                continue;
            const auto src = outputType(a, c->fromNode, c->fromPort);
            const DataType dst = in.type.generic ? t : in.type.concrete;
            if (src && !canConvert(*src, dst))
                a.error(id, in.id,
                        QStringLiteral("Cannot connect %1 to %2 input '%3'; use a Split node to pick a component")
                            .arg(glslName(*src), glslName(dst), in.label));
        }
    }

    // Parameter names: valid, unreserved, unique.
    QHash<QString, int> seen;
    for (auto it = a.defs.cbegin(); it != a.defs.cend(); ++it) {
        if ((*it)->role != NodeRole::Parameter)
            continue;
        const GraphNode* n = graph.node(it.key());
        const QString name = n->parameterName.isEmpty() ? QStringLiteral("uParam%1").arg(n->id)
                                                        : n->parameterName;
        a.parameterNames.insert(n->id, name);
        if (!kIdentifier.match(name).hasMatch())
            a.error(n->id, {}, QStringLiteral("'%1' is not a valid GLSL identifier").arg(name));
        else if (isReservedName(name))
            a.error(n->id, {}, QStringLiteral("Parameter name '%1' is reserved").arg(name));
        else if (seen.contains(name))
            a.error(n->id, {}, QStringLiteral("Parameter name '%1' is already used by node %2").arg(name).arg(seen[name]));
        seen.insert(name, n->id);
    }

    // Reachability from the output node (walk inputs backwards).
    if (a.outputNode >= 0) {
        QList<int> stack{a.outputNode};
        while (!stack.isEmpty()) {
            const int id = stack.takeLast();
            if (a.reachable.contains(id))
                continue;
            a.reachable.insert(id);
            for (const Connection& c : a.edges)
                if (c.toNode == id)
                    stack.append(c.fromNode);
        }
        for (int id : a.defs.keys())
            if (!a.reachable.contains(id))
                a.issues.append({Severity::Warning, id, {},
                                 QStringLiteral("Not connected to the output; ignored")});
    }
    return a;
}

QString inputExpression(const Analysis& a, const ShaderGraph& graph, const GraphNode& node,
                        const InputDefinition& in)
{
    const DataType dst = in.type.generic ? a.genericTypes.value(node.id, DataType::Float) : in.type.concrete;
    if (const auto c = graph.connectionTo(node.id, in.id); c && a.edges.contains(*c)) {
        const DataType src = outputType(a, c->fromNode, c->fromPort).value_or(dst);
        return convertExpression(variableName(c->fromNode, c->fromPort), src, dst);
    }
    if (!in.defaultExpression.isEmpty())
        return in.defaultExpression;
    return glslLiteral(dst, node.inputValues.value(in.id, in.defaultValue));
}

QString substitute(const QString& text, const Analysis& a, const ShaderGraph& graph, const GraphNode& node)
{
    const NodeDefinition* def = a.defs.value(node.id);
    QString out;
    qsizetype last = 0;
    auto it = kPlaceholder.globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString key = m.captured(1);
        if (key == u"T")
            out += glslName(a.genericTypes.value(node.id, DataType::Float));
        else if (key == u"name")
            out += a.parameterNames.value(node.id);
        else if (const InputDefinition* in = def->input(key))
            out += QStringLiteral("(%1)").arg(inputExpression(a, graph, node, *in));
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

QString sanitizeComment(QString s)
{
    s.replace(u'\n', u' ');
    s.replace(QStringLiteral("*/"), QStringLiteral("* /"));
    return s;
}

} // namespace

CodegenResult generateFragmentShader(const ShaderGraph& graph, const NodeRegistry& registry)
{
    Analysis a = analyze(graph, registry);
    CodegenResult result;
    result.issues = a.issues;
    result.genericTypes = a.genericTypes;
    if (a.hasErrors())
        return result;

    QStringList lines;
    lines << QStringLiteral("#version 450 core")
          << QStringLiteral("// Generated by PrismForge from shader graph \"%1\".").arg(sanitizeComment(graph.name))
          << QStringLiteral("// Do not edit: this source is regenerated whenever the graph changes.")
          << QString()
          << QStringLiteral("in vec3 vWorldPosition;")
          << QStringLiteral("in vec3 vWorldNormal;")
          << QStringLiteral("in vec2 vUV;")
          << QString()
          << QStringLiteral("uniform vec3 uCameraPosition;")
          << QStringLiteral("uniform float uTime;")
          << QStringLiteral("uniform vec2 uResolution;")
          << QString();

    // Parameters, in node id order.
    bool anyParameter = false;
    for (const GraphNode& n : graph.nodes()) {
        const NodeDefinition* def = a.defs.value(n.id);
        if (!a.reachable.contains(n.id) || def->role != NodeRole::Parameter)
            continue;
        const InputDefinition* in = def->input(QStringLiteral("default"));
        const DataType type = def->outputs[0].type.concrete;
        QStringList hints;
        if (n.rangeMin && n.rangeMax && *n.rangeMin < *n.rangeMax)
            hints << QStringLiteral("@range(%1, %2)").arg(glslLiteral(DataType::Float, {*n.rangeMin}),
                                                          glslLiteral(DataType::Float, {*n.rangeMax}));
        if (in->color)
            hints << QStringLiteral("@color");
        QString line = QStringLiteral("uniform %1 %2 = %3;")
                           .arg(glslName(type), a.parameterNames.value(n.id),
                                glslLiteral(type, n.inputValues.value(in->id, in->defaultValue)));
        if (!hints.isEmpty())
            line += QStringLiteral(" // ") + hints.join(u' ');
        lines << line;
        anyParameter = true;
    }
    if (anyParameter)
        lines << QString();
    lines << QStringLiteral("out vec4 fragColor;") << QString();

    // Helper functions: once per definition, in definition id order.
    QStringList defIds;
    for (int id : a.reachable)
        if (!a.defs.value(id)->functions.trimmed().isEmpty() && !defIds.contains(a.defs.value(id)->id))
            defIds << a.defs.value(id)->id;
    defIds.sort();
    for (const QString& id : defIds) {
        lines << QStringLiteral("// --- %1").arg(id);
        lines << registry.find(id)->functions.trimmed() << QString();
    }

    // Used outputs, so unused ones produce no dead variables.
    QSet<QPair<int, QString>> used;
    for (const Connection& c : a.edges)
        if (a.reachable.contains(c.toNode))
            used.insert({c.fromNode, c.fromPort});

    lines << QStringLiteral("void main()") << QStringLiteral("{");
    for (int id : a.order) {
        if (!a.reachable.contains(id))
            continue;
        const GraphNode& node = *graph.node(id);
        const NodeDefinition* def = a.defs.value(id);
        lines << QStringLiteral("    // [%1] %2").arg(id).arg(sanitizeComment(def->title));
        for (const OutputDefinition& out : def->outputs) {
            if (!used.contains({id, out.id}))
                continue;
            const DataType t = out.type.generic ? a.genericTypes.value(id, DataType::Float) : out.type.concrete;
            lines << QStringLiteral("    %1 %2 = %3;")
                         .arg(glslName(t), variableName(id, out.id), substitute(out.expression, a, graph, node));
        }
        if (def->role == NodeRole::Output)
            for (const QString& statement : substitute(def->mainTemplate, a, graph, node).split(u'\n'))
                lines << QStringLiteral("    ") + statement.trimmed();
    }
    lines << QStringLiteral("}");

    result.source = lines.join(u'\n') + u'\n';
    result.success = true;
    return result;
}

std::optional<DataType> resolvedOutputType(const ShaderGraph& graph, const NodeRegistry& registry,
                                           int nodeId, const QString& outputId)
{
    return outputType(analyze(graph, registry), nodeId, outputId);
}

QList<Diagnostic> toDiagnostics(const QList<GraphIssue>& issues, const ShaderGraph& graph)
{
    QList<Diagnostic> out;
    for (const GraphIssue& issue : issues) {
        QString message = issue.message;
        if (issue.nodeId >= 0) {
            const GraphNode* n = graph.node(issue.nodeId);
            const QString type = n ? n->definitionId : QString();
            message = issue.port.isEmpty()
                          ? QStringLiteral("Node %1 (%2): %3").arg(issue.nodeId).arg(type, issue.message)
                          : QStringLiteral("Node %1 (%2), port '%3': %4")
                                .arg(issue.nodeId).arg(type, issue.port, issue.message);
        }
        out.append({issue.severity, ShaderStage::Fragment, -1, -1, message, QStringLiteral("graph")});
    }
    return out;
}

ShaderGraph makeDefaultGraph()
{
    ShaderGraph g;
    g.name = QStringLiteral("Default graph");
    const int output = g.addNode(QStringLiteral("output.surface"), {520, 0});
    const int uv = g.addNode(QStringLiteral("input.uv"), {-460, -40});
    const int noise = g.addNode(QStringLiteral("noise.fbm"), {-240, -40});
    const int colorA = g.addNode(QStringLiteral("param.color"), {-240, 120});
    const int colorB = g.addNode(QStringLiteral("param.color"), {-240, 220});
    const int mix = g.addNode(QStringLiteral("math.lerp"), {40, 40});
    const int rough = g.addNode(QStringLiteral("param.float"), {40, 200});

    g.node(colorA)->parameterName = QStringLiteral("uDeepColor");
    g.node(colorA)->inputValues.insert(QStringLiteral("default"), {0.05f, 0.18f, 0.35f, 1.0f});
    g.node(colorB)->parameterName = QStringLiteral("uHighlightColor");
    g.node(colorB)->inputValues.insert(QStringLiteral("default"), {0.95f, 0.62f, 0.28f, 1.0f});
    g.node(rough)->parameterName = QStringLiteral("uRoughness");
    g.node(rough)->inputValues.insert(QStringLiteral("default"), {0.45f, 0, 0, 0});
    g.node(rough)->rangeMin = 0.0f;
    g.node(rough)->rangeMax = 1.0f;
    g.node(noise)->inputValues.insert(QStringLiteral("scale"), {4.0f, 0, 0, 0});

    g.connect({uv, QStringLiteral("out"), noise, QStringLiteral("uv")});
    g.connect({colorA, QStringLiteral("value"), mix, QStringLiteral("a")});
    g.connect({colorB, QStringLiteral("value"), mix, QStringLiteral("b")});
    g.connect({noise, QStringLiteral("out"), mix, QStringLiteral("t")});
    g.connect({mix, QStringLiteral("out"), output, QStringLiteral("baseColor")});
    g.connect({rough, QStringLiteral("value"), output, QStringLiteral("roughness")});
    return g;
}

} // namespace pf::graph
