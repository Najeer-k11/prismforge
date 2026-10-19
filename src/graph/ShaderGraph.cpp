#include "graph/ShaderGraph.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <algorithm>
#include <cmath>
#include <tuple>

namespace pf::graph {

namespace {
bool connectionLess(const Connection& a, const Connection& b)
{
    return std::tie(a.toNode, a.toPort, a.fromNode, a.fromPort) <
           std::tie(b.toNode, b.toPort, b.fromNode, b.fromPort);
}
} // namespace

const GraphNode* ShaderGraph::node(int id) const
{
    const auto it = m_nodes.constFind(id);
    return it == m_nodes.constEnd() ? nullptr : &*it;
}

GraphNode* ShaderGraph::node(int id)
{
    const auto it = m_nodes.find(id);
    return it == m_nodes.end() ? nullptr : &*it;
}

int ShaderGraph::addNode(const QString& definitionId, QPointF position)
{
    GraphNode n;
    n.id = m_nextId++;
    n.definitionId = definitionId;
    n.position = position;
    m_nodes.insert(n.id, n);
    return n.id;
}

void ShaderGraph::removeNode(int id)
{
    m_nodes.remove(id);
    m_connections.removeIf([id](const Connection& c) { return c.fromNode == id || c.toNode == id; });
}

bool ShaderGraph::connect(const Connection& connection)
{
    if (connection.fromNode == connection.toNode || !m_nodes.contains(connection.fromNode) ||
        !m_nodes.contains(connection.toNode))
        return false;
    disconnectInput(connection.toNode, connection.toPort);
    // Kept sorted so equality and serialisation don't depend on edit history.
    const auto pos = std::lower_bound(m_connections.begin(), m_connections.end(), connection, connectionLess);
    m_connections.insert(pos, connection);
    return true;
}

void ShaderGraph::disconnectInput(int toNode, const QString& toPort)
{
    m_connections.removeIf(
        [&](const Connection& c) { return c.toNode == toNode && c.toPort == toPort; });
}

std::optional<Connection> ShaderGraph::connectionTo(int toNode, const QString& toPort) const
{
    for (const Connection& c : m_connections)
        if (c.toNode == toNode && c.toPort == toPort)
            return c;
    return std::nullopt;
}

QJsonObject ShaderGraph::toJson() const
{
    QJsonArray nodes;
    for (const GraphNode& n : m_nodes) {
        QJsonObject values;
        // Sorted keys for stable output.
        QStringList keys = n.inputValues.keys();
        keys.sort();
        for (const QString& key : keys) {
            const ParamValue& v = n.inputValues[key];
            values.insert(key, QJsonArray{toJsonNumber(v[0]), toJsonNumber(v[1]), toJsonNumber(v[2]),
                                          toJsonNumber(v[3])});
        }
        QJsonObject o{
            {"id", n.id},
            {"type", n.definitionId},
            {"x", std::round(n.position.x())},
            {"y", std::round(n.position.y())},
        };
        if (!values.isEmpty())
            o.insert("values", values);
        if (!n.parameterName.isEmpty())
            o.insert("parameterName", n.parameterName);
        if (n.rangeMin)
            o.insert("rangeMin", toJsonNumber(*n.rangeMin));
        if (n.rangeMax)
            o.insert("rangeMax", toJsonNumber(*n.rangeMax));
        nodes.append(o);
    }

    QJsonArray connections;
    for (const Connection& c : m_connections)
        connections.append(QJsonObject{{"from", c.fromNode}, {"fromPort", c.fromPort},
                                       {"to", c.toNode}, {"toPort", c.toPort}});

    return QJsonObject{{"formatVersion", kFormatVersion}, {"name", name}, {"nextId", m_nextId},
                       {"nodes", nodes}, {"connections", connections}};
}

std::optional<ShaderGraph> ShaderGraph::fromJson(const QJsonObject& json, QString* error)
{
    auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return std::nullopt;
    };
    const int version = json.value("formatVersion").toInt(-1);
    if (version < 1 || version > kFormatVersion)
        return fail(QStringLiteral("Unsupported graph format version %1").arg(version));

    ShaderGraph g;
    g.name = json.value("name").toString(g.name);
    int maxId = 0;
    for (const QJsonValue& v : json.value("nodes").toArray()) {
        const QJsonObject o = v.toObject();
        GraphNode n;
        n.id = o.value("id").toInt();
        n.definitionId = o.value("type").toString();
        n.position = QPointF(o.value("x").toDouble(), o.value("y").toDouble());
        if (n.id <= 0 || n.definitionId.isEmpty() || g.m_nodes.contains(n.id))
            return fail(QStringLiteral("Invalid or duplicate node entry (id %1)").arg(n.id));
        const QJsonObject values = o.value("values").toObject();
        for (auto it = values.begin(); it != values.end(); ++it) {
            const QJsonArray a = it.value().toArray();
            ParamValue pv{};
            for (int i = 0; i < std::min<qsizetype>(a.size(), 4); ++i)
                pv[std::size_t(i)] = float(a[i].toDouble());
            n.inputValues.insert(it.key(), pv);
        }
        n.parameterName = o.value("parameterName").toString();
        if (o.contains("rangeMin"))
            n.rangeMin = float(o.value("rangeMin").toDouble());
        if (o.contains("rangeMax"))
            n.rangeMax = float(o.value("rangeMax").toDouble());
        maxId = std::max(maxId, n.id);
        g.m_nodes.insert(n.id, n);
    }
    for (const QJsonValue& v : json.value("connections").toArray()) {
        const QJsonObject o = v.toObject();
        const Connection c{o.value("from").toInt(), o.value("fromPort").toString(), o.value("to").toInt(),
                           o.value("toPort").toString()};
        if (!g.m_nodes.contains(c.fromNode) || !g.m_nodes.contains(c.toNode))
            return fail(QStringLiteral("Connection references a missing node"));
        g.connect(c);
    }
    g.m_nextId = std::max(json.value("nextId").toInt(1), maxId + 1);
    return g;
}

QByteArray ShaderGraph::serialize() const
{
    return QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
}

std::optional<ShaderGraph> ShaderGraph::deserialize(const QByteArray& data, QString* error)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error)
            *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        return std::nullopt;
    }
    return fromJson(doc.object(), error);
}

} // namespace pf::graph
