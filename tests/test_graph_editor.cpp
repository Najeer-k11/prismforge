// Drives the real graph editor widgets with synthetic mouse/keyboard input.
// Runs on the offscreen Qt platform, so no display is needed.

#include "grapheditor/GraphEditor.h"
#include "grapheditor/GraphScene.h"
#include "grapheditor/GraphView.h"

#include <QApplication>
#include <QMouseEvent>
#include <QTest>
#include <QUndoStack>

#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace pf::graph;
using namespace pf::grapheditor;

namespace {

struct Fixture {
    GraphEditor editor;
    GraphScene* scene = nullptr;
    GraphView* view = nullptr;
    int constant = 0;
    int output = 0;

    Fixture()
    {
        editor.setRegistry(&NodeRegistry::builtin());
        ShaderGraph g;
        constant = g.addNode("const.float", {0, 0});
        output = g.addNode("output.unlit", {320, 0});
        editor.setGraph(g);
        editor.resize(1100, 700);
        editor.show();
        REQUIRE(QTest::qWaitForWindowExposed(&editor));
        scene = editor.findChild<GraphScene*>();
        view = editor.findChild<GraphView*>();
        REQUIRE(scene);
        REQUIRE(view);
        view->resetTransform();
        view->centerOn(160, 40);
    }

    QPoint toView(QPointF scenePos) const { return view->mapFromScene(scenePos); }

    void send(QEvent::Type type, QPoint pos, Qt::MouseButton button, Qt::MouseButtons buttons)
    {
        QMouseEvent event(type, pos, view->viewport()->mapToGlobal(pos), button, buttons, Qt::NoModifier);
        QApplication::sendEvent(view->viewport(), &event);
    }

    void drag(QPointF fromScene, QPointF toScene)
    {
        const QPoint a = toView(fromScene);
        const QPoint b = toView(toScene);
        send(QEvent::MouseButtonPress, a, Qt::LeftButton, Qt::LeftButton);
        send(QEvent::MouseMove, (a + b) / 2, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseMove, b, Qt::NoButton, Qt::LeftButton);
        send(QEvent::MouseButtonRelease, b, Qt::LeftButton, Qt::NoButton);
        QApplication::processEvents(); // edits are applied via queued connections
    }

    QPointF outPort() const { return scene->nodeItem(constant)->outputPort("out")->scenePos(); }
    QPointF alphaPort() const { return scene->nodeItem(output)->inputPort("alpha")->scenePos(); }
};

} // namespace

TEST_CASE("Dragging from an output to an input connects them, with undo", "[editor]")
{
    Fixture f;
    f.drag(f.outPort(), f.alphaPort());
    REQUIRE(f.editor.graph().connectionTo(f.output, "alpha"));
    CHECK(f.editor.graph().connectionTo(f.output, "alpha")->fromNode == f.constant);
    CHECK(f.editor.codegen().success);

    f.editor.undoStack()->undo();
    CHECK_FALSE(f.editor.graph().connectionTo(f.output, "alpha"));
    f.editor.undoStack()->redo();
    CHECK(f.editor.graph().connectionTo(f.output, "alpha"));
}

TEST_CASE("Dragging from an input to an output also connects", "[editor]")
{
    Fixture f;
    f.drag(f.alphaPort(), f.outPort());
    CHECK(f.editor.graph().connectionTo(f.output, "alpha"));
}

TEST_CASE("Dragging a connected input onto empty canvas disconnects it", "[editor]")
{
    Fixture f;
    f.drag(f.outPort(), f.alphaPort());
    REQUIRE(f.editor.graph().connectionTo(f.output, "alpha"));
    f.drag(f.alphaPort(), f.alphaPort() + QPointF(0, 200));
    CHECK_FALSE(f.editor.graph().connectionTo(f.output, "alpha"));
    CHECK(f.editor.undoStack()->count() == 2);
}

TEST_CASE("Dropping a connection on the same node does nothing", "[editor]")
{
    Fixture f;
    const auto inputPort = f.scene->nodeItem(f.output)->inputPort("color")->scenePos();
    f.drag(f.alphaPort(), inputPort);
    CHECK(f.editor.graph().connections().isEmpty());
    CHECK(f.editor.undoStack()->count() == 0);
}

TEST_CASE("Moving a node is recorded as one undoable edit", "[editor]")
{
    Fixture f;
    const QPointF title = f.scene->nodeItem(f.constant)->scenePos() + QPointF(60, 10);
    f.drag(title, title + QPointF(80, 40));
    CHECK(f.editor.graph().node(f.constant)->position == QPointF(80, 40));
    CHECK(f.editor.undoStack()->count() == 1);
    f.editor.undoStack()->undo();
    CHECK(f.editor.graph().node(f.constant)->position == QPointF(0, 0));
}

TEST_CASE("Delete key removes the selected node and its connections", "[editor]")
{
    Fixture f;
    f.drag(f.outPort(), f.alphaPort());
    f.editor.selectNode(f.constant);
    f.view->setFocus();
    QTest::keyClick(f.view, Qt::Key_Delete);
    QApplication::processEvents();
    CHECK_FALSE(f.editor.graph().node(f.constant));
    CHECK(f.editor.graph().connections().isEmpty());
    f.editor.undoStack()->undo();
    CHECK(f.editor.graph().node(f.constant));
    CHECK(f.editor.graph().connections().size() == 1);
}

TEST_CASE("Duplicate copies the node and its values", "[editor]")
{
    Fixture f;
    f.editor.selectNode(f.constant);
    f.view->setFocus();
    QTest::keyClick(f.view, Qt::Key_D, Qt::ControlModifier);
    QApplication::processEvents();
    CHECK(f.editor.graph().nodes().size() == 3);
    CHECK(f.scene->selectedNodeIds().size() == 1);
    CHECK(f.scene->selectedNodeIds().first() != f.constant);
}

int main(int argc, char* argv[])
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    return Catch::Session().run(argc, argv);
}
