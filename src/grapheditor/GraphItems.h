#pragma once

#include "diagnostics/Diagnostic.h"
#include "graph/NodeRegistry.h"
#include "graph/ShaderGraph.h"

#include <QGraphicsItem>
#include <QGraphicsPathItem>
#include <QSet>

namespace pf::grapheditor {

enum ItemType {
    NodeItemType = QGraphicsItem::UserType + 1,
    PortItemType,
    ConnectionItemType,
};

QColor colorForType(graph::DataType type);
QColor colorForCategory(const QString& category);

class NodeItem;

class PortItem final : public QGraphicsItem {
public:
    static constexpr qreal kRadius = 5.0;

    PortItem(NodeItem* node, QString portId, bool isOutput, graph::DataType type);

    int type() const override { return PortItemType; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    NodeItem* node() const { return m_node; }
    const QString& portId() const { return m_portId; }
    bool isOutput() const { return m_isOutput; }
    graph::DataType dataType() const { return m_type; }
    void setHighlighted(bool on);

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent* event) override;
    void hoverLeaveEvent(QGraphicsSceneHoverEvent* event) override;

private:
    NodeItem* m_node;
    QString m_portId;
    bool m_isOutput;
    graph::DataType m_type;
    bool m_hovered = false;
    bool m_highlighted = false;
};

// Visual state for one node, derived from the model and the last codegen.
struct NodeVisual {
    QHash<QString, graph::DataType> inputTypes;  // resolved per input
    QHash<QString, graph::DataType> outputTypes; // resolved per output
    QSet<QString> connectedInputs;
    std::optional<Severity> issueSeverity;
    QString issueText;
};

class NodeItem final : public QGraphicsItem {
public:
    static constexpr qreal kWidth = 190.0;
    static constexpr qreal kTitleHeight = 24.0;
    static constexpr qreal kRowHeight = 20.0;

    NodeItem(const graph::GraphNode& node, const graph::NodeDefinition* definition, const NodeVisual& visual);

    int type() const override { return NodeItemType; }
    QRectF boundingRect() const override;
    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;

    int nodeId() const { return m_node.id; }
    PortItem* inputPort(const QString& id) const { return m_inputs.value(id); }
    PortItem* outputPort(const QString& id) const { return m_outputs.value(id); }

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant& value) override;

private:
    struct Row {
        QString label;
        QString value;     // inline value text for unconnected inputs
        QColor swatch;     // valid for colour inputs
        bool output = false;
    };

    graph::GraphNode m_node;
    const graph::NodeDefinition* m_definition;
    NodeVisual m_visual;
    QList<Row> m_rows;
    QHash<QString, PortItem*> m_inputs;
    QHash<QString, PortItem*> m_outputs;
    qreal m_height = 0;
};

class ConnectionItem final : public QGraphicsPathItem {
public:
    ConnectionItem(const graph::Connection& connection, graph::DataType type);
    int type() const override { return ConnectionItemType; }
    const graph::Connection& connection() const { return m_connection; }

    void setEndpoints(QPointF from, QPointF to);
    static QPainterPath pathBetween(QPointF from, QPointF to);

    void paint(QPainter* painter, const QStyleOptionGraphicsItem* option, QWidget* widget) override;
    QPainterPath shape() const override;

private:
    graph::Connection m_connection;
    QColor m_color;
};

} // namespace pf::grapheditor
