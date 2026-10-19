#include "grapheditor/GraphItems.h"

#include "grapheditor/GraphScene.h"

#include <QFontMetricsF>
#include <QGraphicsScene>
#include <QPainter>
#include <QPainterPathStroker>
#include <QStyleOptionGraphicsItem>

#include <algorithm>
#include <cmath>

namespace pf::grapheditor {

using namespace pf::graph;

QColor colorForType(DataType type)
{
    switch (type) {
    case DataType::Float: return QColor(0xa0, 0xa4, 0xab);
    case DataType::Vec2: return QColor(0x7f, 0xc0, 0x8a);
    case DataType::Vec3: return QColor(0xe0, 0xb0, 0x5a);
    case DataType::Vec4: return QColor(0xb4, 0x8c, 0xe0);
    }
    return Qt::gray;
}

QColor colorForCategory(const QString& category)
{
    static const QHash<QString, QColor> colors = {
        {"Input", QColor(0x34, 0x52, 0x6b)},     {"Constant", QColor(0x46, 0x48, 0x4e)},
        {"Parameter", QColor(0x52, 0x45, 0x6e)}, {"Math", QColor(0x37, 0x55, 0x45)},
        {"Vector", QColor(0x3a, 0x55, 0x58)},    {"Colour", QColor(0x66, 0x52, 0x34)},
        {"UV", QColor(0x34, 0x56, 0x66)},        {"Noise", QColor(0x5c, 0x44, 0x44)},
        {"Material", QColor(0x58, 0x54, 0x36)},  {"Output", QColor(0x6a, 0x38, 0x38)},
    };
    return colors.value(category, QColor(0x48, 0x4a, 0x50));
}

// ---------------------------------------------------------------- PortItem

PortItem::PortItem(NodeItem* node, QString portId, bool isOutput, DataType type)
    : QGraphicsItem(node), m_node(node), m_portId(std::move(portId)), m_isOutput(isOutput), m_type(type)
{
    setAcceptHoverEvents(true);
    setCursor(Qt::CrossCursor);
    setZValue(1);
}

QRectF PortItem::boundingRect() const
{
    const qreal r = kRadius + 3.0; // generous hit area
    return {-r, -r, 2 * r, 2 * r};
}

void PortItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QColor c = colorForType(m_type);
    const qreal r = (m_hovered || m_highlighted) ? kRadius + 1.5 : kRadius;
    painter->setPen(QPen(QColor(0x1a, 0x1b, 0x1e), 1.5));
    painter->setBrush(m_highlighted ? c.lighter(140) : c);
    painter->drawEllipse(QPointF(0, 0), r, r);
}

void PortItem::setHighlighted(bool on)
{
    if (m_highlighted != on) {
        m_highlighted = on;
        update();
    }
}

void PortItem::hoverEnterEvent(QGraphicsSceneHoverEvent*)
{
    m_hovered = true;
    update();
}

void PortItem::hoverLeaveEvent(QGraphicsSceneHoverEvent*)
{
    m_hovered = false;
    update();
}

// ---------------------------------------------------------------- NodeItem

namespace {
QString formatValue(const ParamValue& v, int components)
{
    QStringList parts;
    for (int i = 0; i < components; ++i)
        parts << QString::number(double(v[std::size_t(i)]), 'g', 3);
    return parts.join(QStringLiteral(", "));
}
} // namespace

NodeItem::NodeItem(const GraphNode& node, const NodeDefinition* definition, const NodeVisual& visual)
    : m_node(node), m_definition(definition), m_visual(visual)
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setPos(node.position);
    setZValue(2);

    if (!definition) {
        m_rows.append({QStringLiteral("unknown type"), node.definitionId, {}, false});
    } else {
        for (const OutputDefinition& out : definition->outputs) {
            const DataType t = visual.outputTypes.value(out.id, out.type.concrete);
            m_outputs.insert(out.id, new PortItem(this, out.id, true, t));
            m_rows.append({out.label, glslName(t), {}, true});
        }
        for (const InputDefinition& in : definition->inputs) {
            const DataType t = visual.inputTypes.value(in.id, in.type.concrete);
            Row row{in.label, {}, {}, false};
            if (!visual.connectedInputs.contains(in.id)) {
                if (!in.defaultExpression.isEmpty()) {
                    row.value = QStringLiteral("auto");
                } else {
                    const ParamValue v = node.inputValues.value(in.id, in.defaultValue);
                    if (in.color) {
                        auto c = [](float x) { return int(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
                        row.swatch = QColor(c(v[0]), c(v[1]), c(v[2]));
                    } else {
                        row.value = formatValue(v, componentCount(t));
                    }
                }
            }
            if (in.connectable)
                m_inputs.insert(in.id, new PortItem(this, in.id, false, t));
            m_rows.append(row);
        }
    }

    // Place ports on the node edges, one per row.
    const qreal subtitle = definition && definition->role == NodeRole::Parameter ? kRowHeight : 0.0;
    int rowIndex = 0;
    auto rowCenterY = [&](int i) { return kTitleHeight + subtitle + kRowHeight * (i + 0.5) + 4.0; };
    if (definition) {
        for (const OutputDefinition& out : definition->outputs)
            m_outputs[out.id]->setPos(kWidth, rowCenterY(rowIndex++));
        for (const InputDefinition& in : definition->inputs) {
            if (PortItem* p = m_inputs.value(in.id))
                p->setPos(0, rowCenterY(rowIndex));
            ++rowIndex;
        }
    }
    m_height = kTitleHeight + subtitle + kRowHeight * std::max<qsizetype>(m_rows.size(), 1) + 8.0;

    QString tip = definition ? definition->description : QString();
    if (!visual.issueText.isEmpty())
        tip = visual.issueText + (tip.isEmpty() ? QString() : QStringLiteral("\n\n") + tip);
    setToolTip(tip);
}

QRectF NodeItem::boundingRect() const
{
    return QRectF(0, 0, kWidth, m_height).adjusted(-3, -3, 3, 3);
}

void NodeItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    const QRectF body(0, 0, kWidth, m_height);
    const QString category = m_definition ? m_definition->category : QString();

    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(0x2a, 0x2c, 0x30, 245));
    painter->drawRoundedRect(body, 3, 3);

    // Title bar
    QPainterPath title;
    title.addRoundedRect(QRectF(0, 0, kWidth, kTitleHeight), 3, 3);
    title.addRect(QRectF(0, kTitleHeight - 3, kWidth, 3));
    painter->setBrush(colorForCategory(category));
    painter->drawPath(title.simplified());

    QFont font = painter->font();
    font.setPointSizeF(9.0);
    QFont bold = font;
    bold.setWeight(QFont::DemiBold);
    painter->setFont(bold);
    painter->setPen(QColor(0xe8, 0xe9, 0xeb));
    const QString titleText = m_definition ? m_definition->title : m_node.definitionId;
    painter->drawText(QRectF(8, 0, kWidth - 40, kTitleHeight), Qt::AlignVCenter | Qt::AlignLeft,
                      QFontMetricsF(bold).elidedText(titleText, Qt::ElideRight, kWidth - 40));
    painter->setFont(font);
    painter->setPen(QColor(0xc0, 0xc2, 0xc6, 150));
    painter->drawText(QRectF(0, 0, kWidth - 8, kTitleHeight), Qt::AlignVCenter | Qt::AlignRight,
                      QString::number(m_node.id));

    qreal y = kTitleHeight + 4.0;
    if (m_definition && m_definition->role == NodeRole::Parameter) {
        painter->setPen(QColor(0xb4, 0xa8, 0xe0));
        const QString name = m_node.parameterName.isEmpty() ? QStringLiteral("uParam%1").arg(m_node.id)
                                                            : m_node.parameterName;
        painter->drawText(QRectF(10, y, kWidth - 20, kRowHeight), Qt::AlignVCenter, name);
        y += kRowHeight;
    }

    const QFontMetricsF fm(font);
    for (const Row& row : m_rows) {
        const QRectF r(12, y, kWidth - 24, kRowHeight);
        painter->setPen(QColor(0xc8, 0xca, 0xce));
        if (row.output) {
            painter->drawText(r, Qt::AlignVCenter | Qt::AlignRight, row.label);
        } else {
            painter->drawText(r, Qt::AlignVCenter | Qt::AlignLeft, row.label);
            if (row.swatch.isValid()) {
                painter->setPen(QPen(QColor(0x1a, 0x1b, 0x1e), 1));
                painter->setBrush(row.swatch);
                painter->drawRect(QRectF(r.right() - 26, r.center().y() - 5, 26, 10));
            } else if (!row.value.isEmpty()) {
                painter->setPen(QColor(0x88, 0x8c, 0x93));
                const qreal labelWidth = fm.horizontalAdvance(row.label) + 10;
                painter->drawText(r.adjusted(labelWidth, 0, 0, 0), Qt::AlignVCenter | Qt::AlignRight,
                                  fm.elidedText(row.value, Qt::ElideLeft, r.width() - labelWidth));
            }
        }
        y += kRowHeight;
    }

    // Outline: selection > issue > default.
    QColor outline(0x18, 0x19, 0x1c);
    qreal width = 1.0;
    if (m_visual.issueSeverity) {
        outline = *m_visual.issueSeverity == Severity::Error ? QColor(0xe0, 0x5a, 0x5a) : QColor(0xd8, 0xa6, 0x4a);
        width = 1.6;
    }
    if (isSelected()) {
        outline = QColor(0x6c, 0xa6, 0xe0);
        width = 2.0;
    }
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(outline, width));
    painter->drawRoundedRect(body, 3, 3);
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant& value)
{
    if (change == ItemPositionHasChanged)
        if (auto* s = qobject_cast<GraphScene*>(scene()))
            s->updateConnectionsFor(m_node.id);
    return QGraphicsItem::itemChange(change, value);
}

// ---------------------------------------------------------------- ConnectionItem

ConnectionItem::ConnectionItem(const Connection& connection, DataType type)
    : m_connection(connection), m_color(colorForType(type))
{
    setFlag(ItemIsSelectable);
    setZValue(1);
    setPen(QPen(m_color, 2.0));
}

QPainterPath ConnectionItem::pathBetween(QPointF from, QPointF to)
{
    QPainterPath path(from);
    const qreal dx = std::max(40.0, std::abs(to.x() - from.x()) * 0.5);
    path.cubicTo(from + QPointF(dx, 0), to - QPointF(dx, 0), to);
    return path;
}

void ConnectionItem::setEndpoints(QPointF from, QPointF to)
{
    setPath(pathBetween(from, to));
}

QPainterPath ConnectionItem::shape() const
{
    QPainterPathStroker stroker;
    stroker.setWidth(10);
    return stroker.createStroke(path());
}

void ConnectionItem::paint(QPainter* painter, const QStyleOptionGraphicsItem*, QWidget*)
{
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(isSelected() ? QColor(0x6c, 0xa6, 0xe0) : m_color, isSelected() ? 3.0 : 2.0));
    painter->drawPath(path());
}

} // namespace pf::grapheditor
