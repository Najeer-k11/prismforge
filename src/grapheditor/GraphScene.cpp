#include "grapheditor/GraphScene.h"

#include <QGraphicsSceneMouseEvent>
#include <QPen>

namespace pf::grapheditor {

using namespace pf::graph;

GraphScene::GraphScene(QObject* parent) : QGraphicsScene(parent)
{
    setItemIndexMethod(QGraphicsScene::NoIndex); // items move constantly; graphs are small
}

void GraphScene::rebuild(const ShaderGraph& graph, const NodeRegistry& registry, const CodegenResult* codegen)
{
    const QList<int> selected = selectedNodeIds();
    m_drag.reset();
    clear();
    m_nodes.clear();
    m_connections.clear();

    QHash<int, NodeVisual> visuals;
    for (const GraphNode& n : graph.nodes()) {
        NodeVisual v;
        const NodeDefinition* def = registry.find(n.definitionId);
        const DataType t = codegen ? codegen->genericTypes.value(n.id, DataType::Float) : DataType::Float;
        if (def) {
            for (const InputDefinition& in : def->inputs)
                v.inputTypes.insert(in.id, in.type.generic ? t : in.type.concrete);
            for (const OutputDefinition& out : def->outputs)
                v.outputTypes.insert(out.id, out.type.generic ? t : out.type.concrete);
        }
        visuals.insert(n.id, v);
    }
    for (const Connection& c : graph.connections())
        visuals[c.toNode].connectedInputs.insert(c.toPort);
    if (codegen) {
        for (const GraphIssue& issue : codegen->issues) {
            if (issue.nodeId < 0 || !visuals.contains(issue.nodeId))
                continue;
            NodeVisual& v = visuals[issue.nodeId];
            if (!v.issueSeverity || issue.severity < *v.issueSeverity)
                v.issueSeverity = issue.severity;
            v.issueText += (v.issueText.isEmpty() ? QString() : QStringLiteral("\n")) + issue.message;
        }
    }

    for (const GraphNode& n : graph.nodes()) {
        auto* item = new NodeItem(n, registry.find(n.definitionId), visuals.value(n.id));
        addItem(item);
        m_nodes.insert(n.id, item);
    }
    for (const Connection& c : graph.connections()) {
        NodeItem* from = m_nodes.value(c.fromNode);
        NodeItem* to = m_nodes.value(c.toNode);
        PortItem* out = from ? from->outputPort(c.fromPort) : nullptr;
        PortItem* in = to ? to->inputPort(c.toPort) : nullptr;
        if (!out || !in)
            continue; // invalid connection; reported by validation
        auto* item = new ConnectionItem(c, out->dataType());
        item->setEndpoints(out->scenePos(), in->scenePos());
        addItem(item);
        m_connections.append(item);
    }
    selectNodes(selected);
}

QList<int> GraphScene::selectedNodeIds() const
{
    QList<int> ids;
    for (QGraphicsItem* item : selectedItems())
        if (item->type() == NodeItemType)
            ids.append(static_cast<NodeItem*>(item)->nodeId());
    std::sort(ids.begin(), ids.end());
    return ids;
}

QList<Connection> GraphScene::selectedConnections() const
{
    QList<Connection> list;
    for (QGraphicsItem* item : selectedItems())
        if (item->type() == ConnectionItemType)
            list.append(static_cast<ConnectionItem*>(item)->connection());
    return list;
}

void GraphScene::selectNodes(const QList<int>& ids)
{
    {
        const QSignalBlocker block(this); // one notification instead of one per item
        clearSelection();
        for (int id : ids)
            if (NodeItem* item = m_nodes.value(id))
                item->setSelected(true);
    }
    emit selectionChanged();
}

QPointF GraphScene::anchorOf(const Connection& c, bool fromEnd) const
{
    if (fromEnd) {
        NodeItem* n = m_nodes.value(c.fromNode);
        PortItem* p = n ? n->outputPort(c.fromPort) : nullptr;
        return p ? p->scenePos() : QPointF();
    }
    NodeItem* n = m_nodes.value(c.toNode);
    PortItem* p = n ? n->inputPort(c.toPort) : nullptr;
    return p ? p->scenePos() : QPointF();
}

void GraphScene::updateConnectionsFor(int nodeId)
{
    for (ConnectionItem* item : m_connections) {
        const Connection& c = item->connection();
        if (c.fromNode == nodeId || c.toNode == nodeId)
            item->setEndpoints(anchorOf(c, true), anchorOf(c, false));
    }
}

PortItem* GraphScene::portAt(QPointF scenePos) const
{
    for (QGraphicsItem* item : items(scenePos))
        if (item->type() == PortItemType)
            return static_cast<PortItem*>(item);
    return nullptr;
}

void GraphScene::clearHighlights()
{
    for (NodeItem* n : m_nodes)
        for (QGraphicsItem* child : n->childItems())
            if (child->type() == PortItemType)
                static_cast<PortItem*>(child)->setHighlighted(false);
}

void GraphScene::mousePressEvent(QGraphicsSceneMouseEvent* event)
{
    if (event->button() == Qt::LeftButton) {
        if (PortItem* port = portAt(event->scenePos())) {
            Drag drag;
            drag.anchor = port;
            if (!port->isOutput()) {
                // Picking up an existing connection from an input drags its loose end.
                for (ConnectionItem* item : m_connections) {
                    const Connection& c = item->connection();
                    if (c.toNode == port->node()->nodeId() && c.toPort == port->portId()) {
                        drag.detached = c;
                        drag.anchor = m_nodes.value(c.fromNode)->outputPort(c.fromPort);
                        item->setVisible(false);
                        break;
                    }
                }
            }
            drag.preview = addPath(QPainterPath(), QPen(colorForType(drag.anchor->dataType()), 2.0, Qt::DashLine));
            drag.preview->setZValue(10);
            m_drag = drag;
            event->accept();
            return;
        }
        m_pressPositions.clear();
        for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it)
            m_pressPositions.insert(it.key(), (*it)->pos());
    }
    QGraphicsScene::mousePressEvent(event);
}

void GraphScene::mouseMoveEvent(QGraphicsSceneMouseEvent* event)
{
    if (!m_drag) {
        QGraphicsScene::mouseMoveEvent(event);
        return;
    }
    const QPointF anchor = m_drag->anchor->scenePos();
    const QPointF cursor = event->scenePos();
    m_drag->preview->setPath(m_drag->anchor->isOutput() ? ConnectionItem::pathBetween(anchor, cursor)
                                                        : ConnectionItem::pathBetween(cursor, anchor));
    clearHighlights();
    if (PortItem* target = portAt(cursor))
        if (target->isOutput() != m_drag->anchor->isOutput() && target->node() != m_drag->anchor->node())
            target->setHighlighted(true);
}

void GraphScene::mouseReleaseEvent(QGraphicsSceneMouseEvent* event)
{
    if (!m_drag) {
        QGraphicsScene::mouseReleaseEvent(event);
        QHash<int, QPointF> moved;
        for (auto it = m_nodes.cbegin(); it != m_nodes.cend(); ++it)
            if (m_pressPositions.contains(it.key()) && m_pressPositions[it.key()] != (*it)->pos())
                moved.insert(it.key(), (*it)->pos());
        m_pressPositions.clear();
        if (!moved.isEmpty())
            emit nodesMoved(moved);
        return;
    }

    const Drag drag = *m_drag;
    m_drag.reset();
    clearHighlights();
    removeItem(drag.preview);
    delete drag.preview;

    std::optional<Connection> add;
    PortItem* target = portAt(event->scenePos());
    if (target && target->isOutput() != drag.anchor->isOutput() && target->node() != drag.anchor->node()) {
        PortItem* out = drag.anchor->isOutput() ? drag.anchor : target;
        PortItem* in = drag.anchor->isOutput() ? target : drag.anchor;
        add = Connection{out->node()->nodeId(), out->portId(), in->node()->nodeId(), in->portId()};
    }
    if (add == drag.detached) {
        // Dropped back where it was: nothing changed.
        for (ConnectionItem* item : m_connections)
            item->setVisible(true);
        return;
    }
    if (add || drag.detached)
        emit connectionEditRequested(drag.detached, add);
    else
        for (ConnectionItem* item : m_connections)
            item->setVisible(true);
}

} // namespace pf::grapheditor
