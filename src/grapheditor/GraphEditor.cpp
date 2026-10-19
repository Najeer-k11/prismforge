#include "grapheditor/GraphEditor.h"

#include "grapheditor/GraphScene.h"
#include "grapheditor/GraphView.h"
#include "grapheditor/NodePropertiesPanel.h"

#include <QAction>
#include <QMenu>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QUndoCommand>
#include <QUndoStack>
#include <QVBoxLayout>

namespace pf::grapheditor {

using namespace pf::graph;

// Undo entry holding the graph before and after one edit. Edits sharing a
// merge key (e.g. dragging a value spin box) collapse into one entry.
class SnapshotCommand final : public QUndoCommand {
public:
    SnapshotCommand(GraphEditor* editor, ShaderGraph before, ShaderGraph after, const QString& text,
                    QString mergeKey)
        : QUndoCommand(text), m_editor(editor), m_before(std::move(before)), m_after(std::move(after)),
          m_mergeKey(std::move(mergeKey))
    {
    }

    void undo() override { m_editor->setGraphFromUndo(m_before); }
    void redo() override
    {
        if (std::exchange(m_firstRedo, false))
            return; // the editor already applied this edit
        m_editor->setGraphFromUndo(m_after);
    }
    int id() const override { return m_mergeKey.isEmpty() ? -1 : 1; }
    bool mergeWith(const QUndoCommand* other) override
    {
        const auto* o = static_cast<const SnapshotCommand*>(other);
        if (o->m_mergeKey != m_mergeKey)
            return false;
        m_after = o->m_after;
        return true;
    }

private:
    GraphEditor* m_editor;
    ShaderGraph m_before;
    ShaderGraph m_after;
    QString m_mergeKey;
    bool m_firstRedo = true;
};

namespace {
const QStringList kCategoryOrder = {"Input", "Constant", "Parameter", "Math", "Vector",
                                    "Colour", "UV", "Noise", "Material", "Output"};
}

GraphEditor::GraphEditor(QWidget* parent) : QWidget(parent)
{
    m_undo = new QUndoStack(this);
    m_scene = new GraphScene(this);
    m_view = new GraphView(m_scene, this);
    m_properties = new NodePropertiesPanel(this);

    auto* toolbar = new QToolBar(this);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextOnly);

    auto* addButton = new QToolButton(toolbar);
    addButton->setText(tr("Add Node"));
    addButton->setPopupMode(QToolButton::InstantPopup);
    auto* addMenu = new QMenu(addButton);
    connect(addMenu, &QMenu::aboutToShow, this, [this, addMenu] {
        addMenu->clear();
        populateAddMenu(addMenu, m_view->mapToScene(m_view->viewport()->rect().center()));
    });
    addButton->setMenu(addMenu);
    toolbar->addWidget(addButton);
    toolbar->addSeparator();

    // Shortcuts are scoped to the graph so they don't clash with the code editor.
    auto makeAction = [&](const QString& text, const QKeySequence& key, auto slot) {
        auto* a = new QAction(text, this);
        a->setShortcut(key);
        a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
        connect(a, &QAction::triggered, this, slot);
        addAction(a);
        toolbar->addAction(a);
        return a;
    };
    QAction* undo = m_undo->createUndoAction(this, tr("Undo"));
    undo->setShortcut(QKeySequence::Undo);
    undo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    QAction* redo = m_undo->createRedoAction(this, tr("Redo"));
    redo->setShortcut(QKeySequence::Redo);
    redo->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    addAction(undo);
    addAction(redo);
    toolbar->addAction(undo);
    toolbar->addAction(redo);
    toolbar->addSeparator();
    QAction* del = makeAction(tr("Delete"), QKeySequence::Delete, [this] { deleteSelection(); });
    del->setShortcuts({QKeySequence::Delete, QKeySequence(Qt::Key_Backspace)});
    makeAction(tr("Duplicate"), QKeySequence(tr("Ctrl+D")), [this] { duplicateSelection(); });
    toolbar->addSeparator();
    makeAction(tr("Frame All"), QKeySequence(tr("F")), [this] { frameAll(); });

    // Properties below the canvas: the canvas keeps the dock's full width.
    auto* splitter = new QSplitter(Qt::Vertical, this);
    splitter->addWidget(m_view);
    splitter->addWidget(m_properties);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({460, 170});

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(toolbar);
    layout->addWidget(splitter, 1);

    // Queued: the scene emits these from inside its own mouse handlers, and
    // applying an edit rebuilds (deletes) the scene's items.
    connect(m_scene, &GraphScene::connectionEditRequested, this,
            [this](std::optional<Connection> remove, std::optional<Connection> add) {
                applyEdit(add ? tr("Connect") : tr("Disconnect"), [=](ShaderGraph& g) {
                    if (remove)
                        g.disconnectInput(remove->toNode, remove->toPort);
                    if (add)
                        g.connect(*add);
                });
            },
            Qt::QueuedConnection);
    connect(m_scene, &GraphScene::nodesMoved, this,
            [this](const QHash<int, QPointF>& positions) {
                applyEdit(tr("Move"), [=](ShaderGraph& g) {
                    for (auto it = positions.begin(); it != positions.end(); ++it)
                        if (GraphNode* n = g.node(it.key()))
                            n->position = it.value();
                });
            },
            Qt::QueuedConnection);
    connect(m_scene, &QGraphicsScene::selectionChanged, this, &GraphEditor::refreshProperties);
    connect(m_view, &GraphView::contextMenuRequested, this, [this](QPointF scenePos, QPoint globalPos, bool onItem) {
        QMenu menu;
        if (onItem) {
            menu.addAction(tr("Duplicate"), this, &GraphEditor::duplicateSelection);
            menu.addAction(tr("Delete"), this, &GraphEditor::deleteSelection);
        } else {
            populateAddMenu(&menu, scenePos);
        }
        menu.exec(globalPos);
    });

    connect(m_properties, &NodePropertiesPanel::inputValueEdited, this,
            [this](int nodeId, const QString& inputId, const ParamValue& value) {
                applyEdit(tr("Edit Value"),
                          [=](ShaderGraph& g) {
                              if (GraphNode* n = g.node(nodeId))
                                  n->inputValues.insert(inputId, value);
                          },
                          QStringLiteral("value:%1:%2").arg(nodeId).arg(inputId), false);
            });
    connect(m_properties, &NodePropertiesPanel::parameterNameEdited, this, [this](int nodeId, const QString& name) {
        const GraphNode* n = m_graph.node(nodeId);
        if (!n || n->parameterName == name)
            return;
        applyEdit(tr("Rename Parameter"), [=](ShaderGraph& g) { g.node(nodeId)->parameterName = name; }, {}, false);
    });
    connect(m_properties, &NodePropertiesPanel::rangeEdited, this,
            [this](int nodeId, std::optional<float> lo, std::optional<float> hi) {
                applyEdit(tr("Edit Range"),
                          [=](ShaderGraph& g) {
                              g.node(nodeId)->rangeMin = lo;
                              g.node(nodeId)->rangeMax = hi;
                          },
                          QStringLiteral("range:%1").arg(nodeId), false);
            });
}

GraphEditor::~GraphEditor()
{
    // The scene is a child QObject and outlives this destructor; deleting its
    // selected items would otherwise call back into a destroyed editor.
    m_scene->disconnect(this);
}

void GraphEditor::setRegistry(const NodeRegistry* registry)
{
    m_registry = registry;
    refresh(true);
}

void GraphEditor::setGraph(const ShaderGraph& graph)
{
    m_undo->clear();
    m_graph = graph;
    refresh(true);
    frameAll();
}

void GraphEditor::setGraphFromUndo(const ShaderGraph& graph)
{
    m_graph = graph;
    refresh(true);
    emit graphChanged();
}

void GraphEditor::applyEdit(const QString& text, const std::function<void(ShaderGraph&)>& edit,
                            const QString& mergeKey, bool refreshProps)
{
    ShaderGraph after = m_graph;
    edit(after);
    if (after == m_graph)
        return;
    const ShaderGraph before = m_graph;
    // Apply now (optionally keeping the properties panel, so a spin box being
    // edited keeps focus), then record. The command's first redo() is a no-op.
    m_graph = after;
    refresh(refreshProps);
    m_undo->push(new SnapshotCommand(this, before, after, text, mergeKey));
    emit graphChanged();
}

void GraphEditor::refresh(bool refreshProps)
{
    if (!m_registry)
        return;
    m_codegen = generateFragmentShader(m_graph, *m_registry);
    m_scene->rebuild(m_graph, *m_registry, &m_codegen);
    if (refreshProps)
        refreshProperties();
}

void GraphEditor::refreshProperties()
{
    const QList<int> selected = m_scene->selectedNodeIds();
    if (selected.size() != 1 || !m_registry) {
        m_properties->showNode(nullptr, nullptr, DataType::Float, {});
        return;
    }
    const GraphNode* node = m_graph.node(selected.first());
    QSet<QString> connected;
    for (const Connection& c : m_graph.connections())
        if (c.toNode == node->id)
            connected.insert(c.toPort);
    m_properties->showNode(node, m_registry->find(node->definitionId),
                           m_codegen.genericTypes.value(node->id, DataType::Float), connected);
}

void GraphEditor::populateAddMenu(QMenu* menu, QPointF scenePos)
{
    if (!m_registry)
        return;
    QMap<QString, QMenu*> submenus;
    QStringList categories;
    for (const NodeDefinition* def : m_registry->all())
        if (!categories.contains(def->category))
            categories << def->category;
    std::sort(categories.begin(), categories.end(), [](const QString& a, const QString& b) {
        const qsizetype ia = kCategoryOrder.indexOf(a), ib = kCategoryOrder.indexOf(b);
        if (ia != ib)
            return (ia < 0 ? 1000 : ia) < (ib < 0 ? 1000 : ib);
        return a < b;
    });
    for (const QString& category : categories)
        submenus.insert(category, menu->addMenu(category));
    for (const NodeDefinition* def : m_registry->all()) {
        QAction* a = submenus[def->category]->addAction(def->title);
        a->setToolTip(def->description);
        const QString id = def->id;
        connect(a, &QAction::triggered, this, [this, id, scenePos] { addNode(id, scenePos); });
    }
}

void GraphEditor::addNode(const QString& definitionId, QPointF scenePos)
{
    int created = 0;
    applyEdit(tr("Add Node"), [&](ShaderGraph& g) {
        created = g.addNode(definitionId, scenePos);
        const NodeDefinition* def = m_registry->find(definitionId);
        if (def && def->role == NodeRole::Parameter) {
            // A readable, unique default uniform name, e.g. uColor5.
            QString word = def->outputs[0].label;
            word.remove(u' ');
            g.node(created)->parameterName = QStringLiteral("u%1%2").arg(word).arg(created);
        }
    });
    selectNode(created);
}

void GraphEditor::deleteSelection()
{
    const QList<int> nodes = m_scene->selectedNodeIds();
    const QList<Connection> connections = m_scene->selectedConnections();
    if (nodes.isEmpty() && connections.isEmpty())
        return;
    applyEdit(tr("Delete"), [&](ShaderGraph& g) {
        for (const Connection& c : connections)
            g.disconnectInput(c.toNode, c.toPort);
        for (int id : nodes)
            g.removeNode(id);
    });
}

void GraphEditor::duplicateSelection()
{
    const QList<int> nodes = m_scene->selectedNodeIds();
    if (nodes.isEmpty())
        return;
    QList<int> created;
    applyEdit(tr("Duplicate"), [&](ShaderGraph& g) {
        QHash<int, int> mapping;
        for (int id : nodes) {
            const GraphNode original = *g.node(id);
            const int copy = g.addNode(original.definitionId, original.position + QPointF(30, 30));
            GraphNode* n = g.node(copy);
            n->inputValues = original.inputValues;
            n->rangeMin = original.rangeMin;
            n->rangeMax = original.rangeMax;
            if (!original.parameterName.isEmpty())
                n->parameterName = original.parameterName + QStringLiteral("_%1").arg(copy);
            mapping.insert(id, copy);
            created.append(copy);
        }
        // Keep connections between duplicated nodes.
        for (const Connection& c : QList<Connection>(g.connections()))
            if (mapping.contains(c.fromNode) && mapping.contains(c.toNode))
                g.connect({mapping[c.fromNode], c.fromPort, mapping[c.toNode], c.toPort});
    });
    m_scene->selectNodes(created);
}

void GraphEditor::selectNode(int id)
{
    m_scene->selectNodes({id});
    if (NodeItem* item = m_scene->nodeItem(id))
        m_view->ensureVisible(item, 60, 60);
}

void GraphEditor::frameAll()
{
    m_view->frameAll();
}

} // namespace pf::grapheditor
