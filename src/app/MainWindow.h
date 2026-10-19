#pragma once

#include "diagnostics/Diagnostic.h"
#include "graph/NodeRegistry.h"
#include "project/Project.h"
#include "render/Renderer.h"

#include <QFutureWatcher>
#include <QMainWindow>

#include <array>

class QAction;
class QDockWidget;
class QActionGroup;
class QLabel;
class QTabWidget;
class QTimer;
class QTreeWidget;

namespace pf {

namespace grapheditor {
class GraphEditor;
}

class CodeEditor;
class DiagnosticsPanel;
class MaterialPanel;
class ViewportWidget;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openProject(const QString& filePath);
    // Replaces the fragment source once the first program has built
    // successfully; used by the --smoke-replace-fragment command line option.
    void replaceFragmentAfterFirstBuild(const QString& source);
    void enableGraphMode();

signals:
    void compileFinished(bool success);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct StageDocument {
        ShaderStage stage;
        CodeEditor* editor = nullptr;
        QString filePath; // empty for unsaved buffers
    };

    void createActions();
    void createDocks();
    void restoreLayout();

    // Compile pipeline: glslang (worker thread) -> GL driver (GUI thread).
    void requestCompile();
    void startCompile();
    void onValidationFinished();
    void onProgramBuilt(const ProgramBuildResult& result);
    void finishCompile(bool success, const QList<Diagnostic>& compileDiagnostics);
    ShaderSources currentSources() const;

    // Documents and projects
    StageDocument& document(ShaderStage stage);
    StageDocument& currentDocument();
    void loadDefaultShaders();
    bool loadShaderFile(ShaderStage stage, const QString& path, QString* error);
    bool saveDocument(StageDocument& doc, const QString& path);
    bool saveShader(StageDocument& doc, bool saveAs);
    bool saveProject(bool saveAs);
    void newProject();
    void openProjectDialog();
    void openShaderDialog();
    void newShader();
    bool maybeSaveChanges();
    void updateWindowTitle();
    void updateTabTitles();
    void applyProjectSettings(const Project& project);

    void applyParameters(QList<MaterialParameter> parameters);

    // Shader graph
    bool graphMode() const;
    void setGraphMode(bool enabled);
    void reloadNodeLibraries();
    static QString userNodeLibraryDir();
    void setSettingsModified(bool modified);
    bool hasUnsavedChanges() const;

    void showRendererInfo(const RendererInfo& info);
    void showStats(double frameIntervalMs, const FrameStats& stats);

    ViewportWidget* m_viewport = nullptr;
    QTabWidget* m_editorTabs = nullptr;
    std::array<StageDocument, 2> m_documents;
    DiagnosticsPanel* m_diagnostics = nullptr;
    MaterialPanel* m_materialPanel = nullptr;
    QHash<QString, MaterialParameter> m_parameterOverrides; // user-edited values by name
    bool m_settingsModified = false; // project state outside the shader buffers
    QLabel* m_rendererLabel = nullptr;
    QLabel* m_statsLabel = nullptr;
    QTreeWidget* m_capabilities = nullptr;

    grapheditor::GraphEditor* m_graphEditor = nullptr;
    QDockWidget* m_graphDock = nullptr;
    CodeEditor* m_generatedEditor = nullptr; // read-only view of graph output
    QAction* m_graphModeAction = nullptr;
    graph::NodeRegistry m_registry;
    QString m_graphPath; // absolute; empty until saved
    QList<Diagnostic> m_graphDiagnostics; // codegen warnings for the compile in flight
    QLabel* m_statusCompile = nullptr;
    QLabel* m_statusFrame = nullptr;

    QAction* m_autoCompileAction = nullptr;
    QActionGroup* m_meshGroup = nullptr;
    QTimer* m_autoCompileTimer = nullptr;

    QString m_projectPath;
    QString m_projectName = QStringLiteral("Untitled");
    RendererInfo m_rendererInfo;

    QFutureWatcher<QList<Diagnostic>> m_validation;
    ShaderSources m_compilingSources;
    QList<Diagnostic> m_validationDiagnostics;
    bool m_compileInFlight = false;
    bool m_compileQueued = false;
    bool m_hasWorkingProgram = false;
    QString m_pendingSmokeFragment;
};

} // namespace pf
