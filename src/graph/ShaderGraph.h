#pragma once

#include "material/MaterialParameter.h"

#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QMap>
#include <QPointF>
#include <QString>

#include <optional>

namespace pf::graph {

struct GraphNode {
    int id = 0;
    QString definitionId;
    QPointF position;
    // Values for unconnected inputs; missing entries use the definition default.
    QHash<QString, ParamValue> inputValues;
    // Parameter nodes only.
    QString parameterName;
    std::optional<float> rangeMin;
    std::optional<float> rangeMax;

    bool operator==(const GraphNode&) const = default;
};

struct Connection {
    int fromNode = 0;
    QString fromPort; // output id
    int toNode = 0;
    QString toPort;   // input id
    bool operator==(const Connection&) const = default;
};

// The editable graph document. Pure data: validation and code generation
// live in GraphCodegen, so a graph can hold invalid states while the user
// is still editing.
class ShaderGraph {
public:
    static constexpr int kFormatVersion = 1;

    QString name = QStringLiteral("Graph");

    const QMap<int, GraphNode>& nodes() const { return m_nodes; }
    const QList<Connection>& connections() const { return m_connections; }
    const GraphNode* node(int id) const;
    GraphNode* node(int id);

    int addNode(const QString& definitionId, QPointF position);
    void removeNode(int id); // also removes its connections

    // Connects an output to an input, replacing any existing connection to
    // that input. Rejects self-connections. Type and cycle checks are done by
    // validation so the editor can report them.
    bool connect(const Connection& connection);
    void disconnectInput(int toNode, const QString& toPort);
    std::optional<Connection> connectionTo(int toNode, const QString& toPort) const;

    bool isEmpty() const { return m_nodes.isEmpty(); }
    bool operator==(const ShaderGraph&) const = default;

    QJsonObject toJson() const;
    static std::optional<ShaderGraph> fromJson(const QJsonObject& json, QString* error);

    QByteArray serialize() const;
    static std::optional<ShaderGraph> deserialize(const QByteArray& data, QString* error);

private:
    QMap<int, GraphNode> m_nodes; // ordered by id: keeps output deterministic
    QList<Connection> m_connections;
    int m_nextId = 1;
};

} // namespace pf::graph
