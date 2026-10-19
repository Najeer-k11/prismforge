#include "grapheditor/GraphView.h"

#include <QContextMenuEvent>
#include <QGraphicsItem>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace pf::grapheditor {

GraphView::GraphView(QGraphicsScene* scene, QWidget* parent) : QGraphicsView(scene, parent)
{
    setRenderHint(QPainter::Antialiasing);
    setRenderHint(QPainter::TextAntialiasing);
    setDragMode(QGraphicsView::RubberBandDrag);
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
    setFrameShape(QFrame::NoFrame);
    setSceneRect(-5000, -5000, 10000, 10000);
    setFocusPolicy(Qt::StrongFocus);
    setBackgroundBrush(QColor(0x1e, 0x1f, 0x22));
}

void GraphView::resizeEvent(QResizeEvent* event)
{
    QGraphicsView::resizeEvent(event);
    if (m_frameOnResize && viewport()->width() > 100 && viewport()->height() > 100)
        frameAll();
}

void GraphView::frameAll()
{
    m_frameOnResize = !isVisible() || viewport()->width() <= 100 || viewport()->height() <= 100;
    if (m_frameOnResize)
        return;
    QRectF bounds;
    for (QGraphicsItem* item : scene()->items())
        if (!item->parentItem())
            bounds |= item->sceneBoundingRect();
    if (bounds.isEmpty()) {
        resetTransform();
        centerOn(0, 0);
        return;
    }
    fitInView(bounds.adjusted(-40, -40, 40, 40), Qt::KeepAspectRatio);
    // Stay between legible and 1:1 (beyond 1:1 text gets blurry).
    const qreal zoom = std::clamp(transform().m11(), 0.45, 1.0);
    resetTransform();
    scale(zoom, zoom);
    centerOn(bounds.center());
}

void GraphView::drawBackground(QPainter* painter, const QRectF& rect)
{
    QGraphicsView::drawBackground(painter, rect);
    const qreal fine = 20.0;
    const qreal coarse = 100.0;
    auto drawGrid = [&](qreal step, const QColor& color) {
        painter->setPen(QPen(color, 0));
        const qreal left = std::floor(rect.left() / step) * step;
        const qreal top = std::floor(rect.top() / step) * step;
        for (qreal x = left; x < rect.right(); x += step)
            painter->drawLine(QPointF(x, rect.top()), QPointF(x, rect.bottom()));
        for (qreal y = top; y < rect.bottom(); y += step)
            painter->drawLine(QPointF(rect.left(), y), QPointF(rect.right(), y));
    };
    if (transform().m11() > 0.5)
        drawGrid(fine, QColor(0x25, 0x26, 0x2a));
    drawGrid(coarse, QColor(0x2c, 0x2e, 0x33));
}

void GraphView::wheelEvent(QWheelEvent* event)
{
    const qreal factor = std::pow(1.0015, event->angleDelta().y());
    const qreal current = transform().m11();
    const qreal target = std::clamp(current * factor, 0.25, 2.5);
    scale(target / current, target / current);
}

void GraphView::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::MiddleButton) {
        m_panning = true;
        m_lastPan = event->position().toPoint();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void GraphView::mouseMoveEvent(QMouseEvent* event)
{
    if (m_panning) {
        const QPoint pos = event->position().toPoint();
        const QPoint delta = pos - m_lastPan;
        m_lastPan = pos;
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
        event->accept();
        return;
    }
    QGraphicsView::mouseMoveEvent(event);
}

void GraphView::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_panning && event->button() == Qt::MiddleButton) {
        m_panning = false;
        unsetCursor();
        event->accept();
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void GraphView::contextMenuEvent(QContextMenuEvent* event)
{
    QGraphicsItem* item = itemAt(event->pos());
    if (item && !item->isSelected()) {
        // Right-clicking an unselected item selects just that item first.
        while (item->parentItem())
            item = item->parentItem();
        scene()->clearSelection();
        item->setSelected(true);
    }
    emit contextMenuRequested(mapToScene(event->pos()), event->globalPos(), item != nullptr);
}

} // namespace pf::grapheditor
