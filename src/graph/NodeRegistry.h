#pragma once

#include "graph/GraphTypes.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

namespace pf::graph {

struct InputDefinition {
    QString id;
    QString label;
    PortType type;
    ParamValue defaultValue{};
    bool connectable = true; // false: a per-node setting (e.g. a parameter default)
    bool color = false;      // edit with a colour picker
    // GLSL used when unconnected instead of a literal (e.g. "vUV"); such
    // inputs have no editable value.
    QString defaultExpression;
};

struct OutputDefinition {
    QString id;
    QString label;
    PortType type;
    QString expression; // GLSL expression; {inputId} and {T} placeholders
};

enum class NodeRole {
    Function,  // pure expression node
    Parameter, // becomes an editable uniform; {name} is the uniform name
    Output,    // writes fragColor via `mainTemplate`; exactly one per graph
};

struct NodeDefinition {
    QString id; // e.g. "math.add"
    QString title;
    QString category;
    QString description;
    NodeRole role = NodeRole::Function;
    QList<InputDefinition> inputs;
    QList<OutputDefinition> outputs;
    QString functions;    // helper GLSL emitted once per graph (prefix names with pf_)
    QString mainTemplate; // statements for Output nodes
    QString origin;       // file the definition came from

    const InputDefinition* input(const QString& id) const;
    const OutputDefinition* output(const QString& id) const;
    bool hasGenericPorts() const;
};

// Data-driven node library. Definitions are loaded from JSON documents:
//   { "formatVersion": 1, "nodes": [ { "id": ..., "inputs": [...], ... } ] }
// A later definition with the same id replaces an earlier one, so user
// libraries can override built-ins.
class NodeRegistry {
public:
    // Returns the number of definitions loaded; problems are appended to
    // `errors` (one message per rejected definition or file problem).
    int loadJson(const QByteArray& json, const QString& origin, QStringList* errors);
    int loadFile(const QString& path, QStringList* errors);
    int loadDirectory(const QString& directory, QStringList* errors);

    // The library bundled with PrismForge (Qt resource :/nodes/builtin.json).
    static const NodeRegistry& builtin();

    const NodeDefinition* find(const QString& id) const;
    QList<const NodeDefinition*> all() const; // sorted by category, then title
    int size() const { return int(m_definitions.size()); }

private:
    QHash<QString, NodeDefinition> m_definitions;
};

} // namespace pf::graph
