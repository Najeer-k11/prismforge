#pragma once

#include "graph/GraphCodegen.h"

#include <QWidget>

#include <functional>

class QMenu;
class QUndoStack;

namespace pf::grapheditor {

class GraphScene;
class GraphView;
class NodePropertiesPanel;

// Node-graph editing surface: canvas + properties, with undo/redo.
// It keeps its own codegen result up to date for visual feedback; the owner
// listens to graphChanged() and reads codegen() to compile.
class GraphEditor final : public QWidget {
    Q_OBJECT
public:
    explicit GraphEditor(QWidget* parent = nullptr);
    ~GraphEditor() override;

    void setRegistry(const graph::NodeRegistry* registry);
    const graph::NodeRegistry* registry() const { return m_registry; }

    // Replaces the document and clears undo history.
    void setGraph(const graph::ShaderGraph& graph);
    const graph::ShaderGraph& graph() const { return m_graph; }
    const graph::CodegenResult& codegen() const { return m_codegen; }

    void selectNode(int id);
    void frameAll();
    QUndoStack* undoStack() const { return m_undo; }

signals:
    void graphChanged();

private:
    friend class SnapshotCommand;
    void applyEdit(const QString& text, const std::function<void(graph::ShaderGraph&)>& edit,
                   const QString& mergeKey = {}, bool refreshProperties = true);
    void setGraphFromUndo(const graph::ShaderGraph& graph);
    void refresh(bool refreshProperties);
    void refreshProperties();
    void populateAddMenu(QMenu* menu, QPointF scenePos);
    void addNode(const QString& definitionId, QPointF scenePos);
    void deleteSelection();
    void duplicateSelection();

    const graph::NodeRegistry* m_registry = nullptr;
    graph::ShaderGraph m_graph;
    graph::CodegenResult m_codegen;
    GraphScene* m_scene = nullptr;
    GraphView* m_view = nullptr;
    NodePropertiesPanel* m_properties = nullptr;
    QUndoStack* m_undo = nullptr;
};

} // namespace pf::grapheditor
