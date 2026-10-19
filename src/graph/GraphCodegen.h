#pragma once

#include "diagnostics/Diagnostic.h"
#include "graph/NodeRegistry.h"
#include "graph/ShaderGraph.h"

#include <QHash>
#include <QList>
#include <QString>

namespace pf::graph {

struct GraphIssue {
    Severity severity = Severity::Error;
    int nodeId = -1;  // -1: graph-level
    QString port;     // input or output id, when relevant
    QString message;
};

struct CodegenResult {
    bool success = false;
    QString source;              // complete fragment shader; empty on failure
    QList<GraphIssue> issues;
    QHash<int, DataType> genericTypes; // resolved T per node (for display)
};

// Validates the graph and generates a GLSL 4.50 fragment shader matching the
// PrismForge vertex interface. Output is deterministic: the same graph always
// produces byte-identical source.
//
// Only nodes that contribute to the output node are emitted; disconnected
// nodes produce a warning. Parameter nodes become uniforms carrying editor
// hints, so they appear in the Material panel.
CodegenResult generateFragmentShader(const ShaderGraph& graph, const NodeRegistry& registry);

// Resolved type of an output port (generic ports use the node's resolved T).
std::optional<DataType> resolvedOutputType(const ShaderGraph& graph, const NodeRegistry& registry,
                                           int nodeId, const QString& outputId);

// Converts issues to diagnostics for the Diagnostics panel.
QList<Diagnostic> toDiagnostics(const QList<GraphIssue>& issues, const ShaderGraph& graph);

// A small starter graph: UV -> noise -> colour mix -> surface output.
ShaderGraph makeDefaultGraph();

} // namespace pf::graph
