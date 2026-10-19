#pragma once

#include "graph/GraphCodegen.h"
#include "grapheditor/GraphItems.h"

#include <QGraphicsScene>

#include <optional>

namespace pf::grapheditor {

// Mirrors a ShaderGraph as graphics items and turns mouse gestures into edit
// requests. It never modifies the model itself: the editor applies requests
// (with undo) and calls rebuild().
class GraphScene final : public QGraphicsScene {
    Q_OBJECT
public:
    explicit GraphScene(QObject* parent = nullptr);

    void rebuild(const graph::ShaderGraph& graph, const graph::NodeRegistry& registry,
                 const graph::CodegenResult* codegen);

    NodeItem* nodeItem(int id) const { return m_nodes.value(id); }
    QList<int> selectedNodeIds() const;
    QList<graph::Connection> selectedConnections() const;
    void selectNodes(const QList<int>& ids);

    void updateConnectionsFor(int nodeId);

signals:
    // Remove `remove` (if set) and add `add` (if set) as one edit.
    void connectionEditRequested(std::optional<graph::Connection> remove, std::optional<graph::Connection> add);
    void nodesMoved(const QHash<int, QPointF>& positions);

protected:
    void mousePressEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent* event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent* event) override;

private:
    struct Drag {
        PortItem* anchor = nullptr;                 // fixed end
        std::optional<graph::Connection> detached;  // connection picked up from an input
        QGraphicsPathItem* preview = nullptr;
    };

    PortItem* portAt(QPointF scenePos) const;
    void clearHighlights();
    QPointF anchorOf(const graph::Connection& c, bool fromEnd) const;

    QHash<int, NodeItem*> m_nodes;
    QList<ConnectionItem*> m_connections;
    std::optional<Drag> m_drag;
    QHash<int, QPointF> m_pressPositions;
};

} // namespace pf::grapheditor
