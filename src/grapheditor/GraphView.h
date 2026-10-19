#pragma once

#include <QGraphicsView>

namespace pf::grapheditor {

// Canvas with a background grid, wheel zoom and middle-button panning.
class GraphView final : public QGraphicsView {
    Q_OBJECT
public:
    explicit GraphView(QGraphicsScene* scene, QWidget* parent = nullptr);

    // Frames all items now, or on the first resize if the view has no size yet.
    void frameAll();

signals:
    // Right click: on empty canvas (`onItem` false) or on a node/connection.
    void contextMenuRequested(QPointF scenePos, QPoint globalPos, bool onItem);

protected:
    void drawBackground(QPainter* painter, const QRectF& rect) override;
    void wheelEvent(QWheelEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    bool m_panning = false;
    bool m_frameOnResize = false;
    QPoint m_lastPan;
};

} // namespace pf::grapheditor
