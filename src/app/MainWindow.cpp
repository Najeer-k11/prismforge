#include "app/MainWindow.h"

#include "app/DiagnosticsPanel.h"
#include "app/LogPanel.h"
#include "app/MaterialPanel.h"
#include "app/ViewportWidget.h"
#include "diagnostics/Logging.h"
#include "editor/CodeEditor.h"
#include "shader/GlslangValidator.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QSaveFile>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QtConcurrent>

#include <algorithm>

namespace pf {

namespace {

constexpr int kAutoCompileDelayMs = 350;

QString readTextFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, file.errorString());
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

QString builtinShader(ShaderStage stage)
{
    const QString path = stage == ShaderStage::Vertex ? QStringLiteral(":/shaders/default.vert")
                                                      : QStringLiteral(":/shaders/default.frag");
    QString error;
    const QString text = readTextFile(path, &error);
    if (!error.isEmpty())
        qCCritical(lcApp).noquote() << "Missing built-in shader resource:" << error;
    return text;
}

QString stageSuffix(ShaderStage stage)
{
    return stage == ShaderStage::Vertex ? QStringLiteral("vert") : QStringLiteral("frag");
}

constexpr int tabIndex(ShaderStage stage) { return stage == ShaderStage::Fragment ? 0 : 1; }

} // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent)
{
    setObjectName("MainWindow");
    setDockNestingEnabled(true);

    m_viewport = new ViewportWidget(this);
    setCentralWidget(m_viewport);

    m_autoCompileTimer = new QTimer(this);
    m_autoCompileTimer->setSingleShot(true);
    m_autoCompileTimer->setInterval(kAutoCompileDelayMs);
    connect(m_autoCompileTimer, &QTimer::timeout, this, &MainWindow::requestCompile);

    createDocks();
    createActions();

    m_statusCompile = new QLabel(this);
    m_statusFrame = new QLabel(this);
    statusBar()->addWidget(m_statusCompile, 1);
    statusBar()->addPermanentWidget(m_statusFrame);

    connect(&m_validation, &QFutureWatcher<QList<Diagnostic>>::finished, this,
            &MainWindow::onValidationFinished);
    connect(m_viewport, &ViewportWidget::programBuilt, this, &MainWindow::onProgramBuilt);
    connect(m_viewport, &ViewportWidget::rendererReady, this, &MainWindow::showRendererInfo);
    connect(m_viewport, &ViewportWidget::statsUpdated, this, &MainWindow::showStats);
    connect(m_viewport, &ViewportWidget::rendererFailed, this, [this](const QString& reason) {
        m_rendererLabel->setText(tr("<b>Renderer unavailable</b><br>%1").arg(reason.toHtmlEscaped()));
        QMessageBox::critical(this, tr("Renderer unavailable"),
                              tr("The preview could not be started:\n\n%1").arg(reason));
        if (m_compileInFlight)
            finishCompile(false, {{Severity::Error, std::nullopt, -1, -1, reason, "renderer"}});
    });

    loadDefaultShaders();
    restoreLayout();
    updateWindowTitle();
    requestCompile();
}

MainWindow::~MainWindow()
{
    m_validation.waitForFinished();
}

// ---------------------------------------------------------------- UI setup

void MainWindow::createDocks()
{
    m_editorTabs = new QTabWidget(this);
    m_editorTabs->setDocumentMode(true);
    for (ShaderStage stage : {ShaderStage::Fragment, ShaderStage::Vertex}) {
        StageDocument& doc = m_documents[tabIndex(stage)];
        doc.stage = stage;
        doc.editor = new CodeEditor(m_editorTabs);
        m_editorTabs->addTab(doc.editor, QString());
        connect(doc.editor, &QPlainTextEdit::textChanged, this, [this] {
            if (m_autoCompileAction && m_autoCompileAction->isChecked())
                m_autoCompileTimer->start();
        });
        connect(doc.editor->document(), &QTextDocument::modificationChanged, this, [this] {
            updateTabTitles();
            updateWindowTitle();
        });
    }

    auto* editorDock = new QDockWidget(tr("Shader Editor"), this);
    editorDock->setObjectName("ShaderEditorDock");
    editorDock->setWidget(m_editorTabs);
    addDockWidget(Qt::RightDockWidgetArea, editorDock);

    m_diagnostics = new DiagnosticsPanel(this);
    auto* diagDock = new QDockWidget(tr("Diagnostics"), this);
    diagDock->setObjectName("DiagnosticsDock");
    diagDock->setWidget(m_diagnostics);
    addDockWidget(Qt::BottomDockWidgetArea, diagDock);
    connect(m_diagnostics, &DiagnosticsPanel::diagnosticActivated, this, [this](const Diagnostic& d) {
        // Program-level messages have no stage; show the fragment shader.
        const ShaderStage stage = d.stage.value_or(ShaderStage::Fragment);
        m_editorTabs->setCurrentIndex(tabIndex(stage));
        if (d.line > 0)
            document(stage).editor->goToLine(d.line, d.column);
    });

    m_materialPanel = new MaterialPanel(this);
    auto* materialDock = new QDockWidget(tr("Material"), this);
    materialDock->setObjectName("MaterialDock");
    materialDock->setWidget(m_materialPanel);
    addDockWidget(Qt::LeftDockWidgetArea, materialDock);
    resizeDocks({materialDock}, {380}, Qt::Horizontal);
    connect(m_materialPanel, &MaterialPanel::parameterChanged, this, [this](const MaterialParameter& p) {
        m_parameterOverrides.insert(p.name, p);
        m_viewport->setParameter(p);
        setSettingsModified(true);
    });
    connect(m_materialPanel, &MaterialPanel::parameterReset, this, [this](const QString& name) {
        m_parameterOverrides.remove(name);
        setSettingsModified(true);
    });

    auto* infoWidget = new QWidget(this);
    auto* infoLayout = new QVBoxLayout(infoWidget);
    infoLayout->setContentsMargins(8, 6, 8, 6);
    m_rendererLabel = new QLabel(tr("Initialising renderer…"), infoWidget);
    m_statsLabel = new QLabel(infoWidget);
    for (QLabel* l : {m_rendererLabel, m_statsLabel}) {
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        l->setAlignment(Qt::AlignTop | Qt::AlignLeft);
        infoLayout->addWidget(l);
    }
    m_capabilities = new QTreeWidget(this);
    m_capabilities->setColumnCount(2);
    m_capabilities->setHeaderLabels({tr("Capability"), tr("Value")});
    m_capabilities->setFrameShape(QFrame::NoFrame);
    m_capabilities->setUniformRowHeights(true);
    m_capabilities->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    auto* infoDock = new QDockWidget(tr("Renderer"), this);
    infoDock->setObjectName("RendererDock");
    infoDock->setWidget(infoWidget);
    addDockWidget(Qt::BottomDockWidgetArea, infoDock);
    splitDockWidget(diagDock, infoDock, Qt::Horizontal);

    auto* logDock = new QDockWidget(tr("Log"), this);
    logDock->setObjectName("LogDock");
    logDock->setWidget(new LogPanel(logDock));
    tabifyDockWidget(diagDock, logDock);

    auto* capsDock = new QDockWidget(tr("GPU Capabilities"), this);
    capsDock->setObjectName("CapabilitiesDock");
    capsDock->setWidget(m_capabilities);
    tabifyDockWidget(diagDock, capsDock);
    diagDock->raise();

    resizeDocks({editorDock}, {560}, Qt::Horizontal);
    resizeDocks({diagDock}, {240}, Qt::Vertical);
    resizeDocks({diagDock, infoDock}, {700, 300}, Qt::Horizontal);
}

void MainWindow::createActions()
{
    auto* fileMenu = menuBar()->addMenu(tr("&File"));
    auto* shaderMenu = menuBar()->addMenu(tr("&Shader"));
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    auto* helpMenu = menuBar()->addMenu(tr("&Help"));

    fileMenu->addAction(tr("&New Project"), QKeySequence::New, this, &MainWindow::newProject);
    fileMenu->addAction(tr("&Open Project…"), QKeySequence::Open, this, &MainWindow::openProjectDialog);
    fileMenu->addAction(tr("&Save Project"), QKeySequence::Save, this, [this] { saveProject(false); });
    fileMenu->addAction(tr("Save Project &As…"), QKeySequence::SaveAs, this, [this] { saveProject(true); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("New Shader"), this, &MainWindow::newShader);
    fileMenu->addAction(tr("Open Shader…"), QKeySequence(tr("Ctrl+Shift+O")), this,
                        &MainWindow::openShaderDialog);
    fileMenu->addAction(tr("Save Shader"), QKeySequence(tr("Ctrl+Alt+S")), this,
                        [this] { saveShader(currentDocument(), false); });
    fileMenu->addAction(tr("Save Shader As…"), this, [this] { saveShader(currentDocument(), true); });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence::Quit, this, &QWidget::close);

    auto* compileAction = shaderMenu->addAction(tr("&Compile"), QKeySequence(Qt::Key_F5), this,
                                                &MainWindow::requestCompile);
    compileAction->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    compileAction->setToolTip(tr("Compile shaders (F5)"));
    m_autoCompileAction = shaderMenu->addAction(tr("&Auto Compile"));
    m_autoCompileAction->setCheckable(true);
    m_autoCompileAction->setChecked(true);
    m_autoCompileAction->setToolTip(tr("Recompile %1 ms after the last edit").arg(kAutoCompileDelayMs));
    shaderMenu->addSeparator();
    shaderMenu->addAction(tr("Fragment Shader"), QKeySequence(tr("Ctrl+1")), this,
                          [this] { m_editorTabs->setCurrentIndex(tabIndex(ShaderStage::Fragment)); });
    shaderMenu->addAction(tr("Vertex Shader"), QKeySequence(tr("Ctrl+2")), this,
                          [this] { m_editorTabs->setCurrentIndex(tabIndex(ShaderStage::Vertex)); });

    m_meshGroup = new QActionGroup(this);
    const std::pair<PreviewMesh, QString> meshes[] = {
        {PreviewMesh::Sphere, tr("Sphere")}, {PreviewMesh::Plane, tr("Plane")}, {PreviewMesh::Cube, tr("Cube")}};
    int shortcut = 1;
    for (const auto& [mesh, label] : meshes) {
        QAction* a = viewMenu->addAction(label);
        a->setCheckable(true);
        a->setData(int(mesh));
        a->setShortcut(QKeySequence(QStringLiteral("Alt+%1").arg(shortcut++)));
        a->setChecked(mesh == PreviewMesh::Sphere);
        m_meshGroup->addAction(a);
    }
    connect(m_meshGroup, &QActionGroup::triggered, this, [this](QAction* a) {
        m_viewport->setPreviewMesh(PreviewMesh(a->data().toInt()));
    });
    viewMenu->addSeparator();
    auto* resetCamera = viewMenu->addAction(tr("Reset Camera"), QKeySequence(tr("Home")), m_viewport,
                                            &ViewportWidget::resetCamera);
    viewMenu->addSeparator();
    for (QDockWidget* dock : findChildren<QDockWidget*>())
        viewMenu->addAction(dock->toggleViewAction());

    helpMenu->addAction(tr("&About PrismForge"), this, [this] {
        QMessageBox::about(this, tr("About PrismForge"),
                           tr("<b>PrismForge %1</b><br>GPU shader development environment.<br><br>"
                              "Qt %2<br>glslang: %3")
                               .arg(QStringLiteral(PRISMFORGE_VERSION), QString::fromLatin1(qVersion()),
                                    GlslangValidator::isAvailable() ? GlslangValidator::versionString()
                                                                    : tr("not available")));
    });

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName("MainToolbar");
    toolbar->setMovable(false);
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->addAction(compileAction);
    toolbar->addAction(m_autoCompileAction);
    toolbar->addSeparator();
    for (QAction* a : m_meshGroup->actions())
        toolbar->addAction(a);
    toolbar->addSeparator();
    toolbar->addAction(resetCamera);
}

void MainWindow::restoreLayout()
{
    QSettings settings;
    if (const QByteArray geometry = settings.value("window/geometry").toByteArray(); !geometry.isEmpty())
        restoreGeometry(geometry);
    else
        resize(1600, 960);
    restoreState(settings.value("window/state").toByteArray());
}

// ---------------------------------------------------------------- compile pipeline

ShaderSources MainWindow::currentSources() const
{
    ShaderSources sources;
    for (const StageDocument& doc : m_documents)
        sources.get(doc.stage) = doc.editor->toPlainText();
    return sources;
}

void MainWindow::requestCompile()
{
    m_autoCompileTimer->stop();
    if (m_compileInFlight) {
        m_compileQueued = true; // picked up when the current compile finishes
        return;
    }
    startCompile();
}

void MainWindow::startCompile()
{
    m_compileInFlight = true;
    m_compileQueued = false;
    m_compilingSources = currentSources();
    m_validationDiagnostics.clear();
    m_statusCompile->setText(tr("Compiling…"));

    if (GlslangValidator::isAvailable()) {
        m_validation.setFuture(QtConcurrent::run(&GlslangValidator::validate, m_compilingSources));
    } else {
        m_viewport->buildProgram(m_compilingSources);
    }
}

void MainWindow::onValidationFinished()
{
    const QList<Diagnostic> diagnostics = m_validation.result();
    if (m_compileQueued) {
        // The sources changed while validating; this result is already stale.
        startCompile();
        return;
    }
    if (hasErrors(diagnostics)) {
        // glslang rejected the sources; the driver is not asked to build them.
        finishCompile(false, diagnostics);
        return;
    }
    m_validationDiagnostics = diagnostics;
    m_viewport->buildProgram(m_compilingSources);
}

void MainWindow::onProgramBuilt(const ProgramBuildResult& result)
{
    if (!m_compileInFlight)
        return;
    if (result.success)
        applyParameters(result.parameters);
    finishCompile(result.success, m_validationDiagnostics + result.diagnostics);
}

void MainWindow::applyParameters(QList<MaterialParameter> parameters)
{
    QHash<QString, ParamHints> hints = parseParameterHints(m_compilingSources.vertex);
    hints.insert(parseParameterHints(m_compilingSources.fragment));
    for (MaterialParameter& p : parameters) {
        p.hints = hints.value(p.name);
        const auto it = m_parameterOverrides.constFind(p.name);
        if (it != m_parameterOverrides.constEnd() && it->type == p.type) {
            p.value = it->value;
            m_viewport->setParameter(p);
        }
    }
    m_materialPanel->setParameters(parameters);
}

void MainWindow::setSettingsModified(bool modified)
{
    m_settingsModified = modified;
    updateWindowTitle();
}

bool MainWindow::hasUnsavedChanges() const
{
    return m_settingsModified ||
           std::any_of(m_documents.begin(), m_documents.end(), [](const StageDocument& d) {
               return d.editor && d.editor->document()->isModified();
           });
}

void MainWindow::finishCompile(bool success, const QList<Diagnostic>& diagnostics)
{
    m_compileInFlight = false;
    if (success)
        m_hasWorkingProgram = true;

    int errors = 0, warnings = 0;
    for (const Diagnostic& d : diagnostics) {
        errors += d.severity == Severity::Error;
        warnings += d.severity == Severity::Warning;
    }

    QString summary;
    if (success) {
        summary = warnings ? tr("Compiled successfully with %n warning(s)", nullptr, warnings)
                           : tr("Compiled successfully");
    } else {
        summary = tr("Compilation failed: %n error(s)", nullptr, std::max(errors, 1));
        summary += m_hasWorkingProgram ? tr(" — preview shows the last working shader")
                                       : tr(" — no working shader yet");
    }
    m_diagnostics->setDiagnostics(diagnostics, summary, success);
    m_statusCompile->setText(summary);

    for (StageDocument& doc : m_documents) {
        QList<Diagnostic> stageDiagnostics;
        for (const Diagnostic& d : diagnostics)
            if (d.stage == doc.stage)
                stageDiagnostics.append(d);
        doc.editor->setDiagnostics(stageDiagnostics);
    }

    qCInfo(lcShader).noquote() << summary;
    emit compileFinished(success);

    if (success && !m_pendingSmokeFragment.isEmpty()) {
        document(ShaderStage::Fragment).editor->setPlainText(std::exchange(m_pendingSmokeFragment, {}));
        m_compileQueued = true;
    }
    if (m_compileQueued)
        requestCompile();
}

void MainWindow::replaceFragmentAfterFirstBuild(const QString& source)
{
    m_pendingSmokeFragment = source;
}

// ---------------------------------------------------------------- documents

MainWindow::StageDocument& MainWindow::document(ShaderStage stage)
{
    return m_documents[tabIndex(stage)];
}

MainWindow::StageDocument& MainWindow::currentDocument()
{
    return m_documents[std::max(m_editorTabs->currentIndex(), 0)];
}

void MainWindow::loadDefaultShaders()
{
    for (StageDocument& doc : m_documents) {
        doc.editor->setPlainText(builtinShader(doc.stage));
        doc.editor->document()->setModified(false);
        doc.filePath.clear();
    }
    updateTabTitles();
}

bool MainWindow::loadShaderFile(ShaderStage stage, const QString& path, QString* error)
{
    QString readError;
    const QString text = readTextFile(path, &readError);
    if (!readError.isEmpty()) {
        if (error)
            *error = readError;
        return false;
    }
    StageDocument& doc = document(stage);
    doc.editor->setPlainText(text);
    doc.editor->document()->setModified(false);
    doc.filePath = QFileInfo(path).absoluteFilePath();
    updateTabTitles();
    return true;
}

bool MainWindow::saveDocument(StageDocument& doc, const QString& path)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Save failed"), tr("%1: %2").arg(path, file.errorString()));
        return false;
    }
    file.write(doc.editor->toPlainText().toUtf8());
    if (!file.commit()) {
        QMessageBox::warning(this, tr("Save failed"), tr("%1: %2").arg(path, file.errorString()));
        return false;
    }
    doc.filePath = QFileInfo(path).absoluteFilePath();
    doc.editor->document()->setModified(false);
    updateTabTitles();
    return true;
}

bool MainWindow::saveShader(StageDocument& doc, bool saveAs)
{
    QString path = doc.filePath;
    if (saveAs || path.isEmpty()) {
        const QString suffix = stageSuffix(doc.stage);
        const QString startDir = m_projectPath.isEmpty() ? QDir::homePath()
                                                         : QFileInfo(m_projectPath).absolutePath();
        path = QFileDialog::getSaveFileName(this, tr("Save %1 Shader").arg(toString(doc.stage)),
                                            startDir + "/untitled." + suffix,
                                            tr("GLSL shader (*.%1);;All files (*)").arg(suffix));
        if (path.isEmpty())
            return false;
    }
    return saveDocument(doc, path);
}

bool MainWindow::saveProject(bool saveAs)
{
    QString projectPath = m_projectPath;
    if (saveAs || projectPath.isEmpty()) {
        projectPath = QFileDialog::getSaveFileName(
            this, tr("Save Project"), QDir::homePath() + "/" + m_projectName + ".pfproj",
            tr("PrismForge project (*.pfproj)"));
        if (projectPath.isEmpty())
            return false;
        if (QFileInfo(projectPath).suffix().isEmpty())
            projectPath += QStringLiteral(".pfproj");
    }

    const QFileInfo projectInfo(projectPath);
    Project project;
    project.name = projectInfo.completeBaseName();

    // Unsaved buffers are written next to the project under shaders/.
    for (StageDocument& doc : m_documents) {
        QString path = doc.filePath;
        if (path.isEmpty())
            path = projectInfo.absoluteDir().filePath(
                QStringLiteral("shaders/%1.%2").arg(project.name, stageSuffix(doc.stage)));
        if (!saveDocument(doc, path))
            return false;
        const QString relative = ProjectIO::toRelativePath(projectPath, doc.filePath);
        (doc.stage == ShaderStage::Vertex ? project.vertexShaderPath : project.fragmentShaderPath) = relative;
    }

    if (QAction* checked = m_meshGroup->checkedAction())
        project.previewMesh = toString(PreviewMesh(checked->data().toInt()));
    project.autoCompile = m_autoCompileAction->isChecked();
    const OrbitCamera& cam = m_viewport->camera();
    project.camera = {cam.yaw(), cam.pitch(), cam.distance()};
    project.parameters = m_parameterOverrides;

    QString error;
    if (!ProjectIO::save(project, projectPath, &error)) {
        QMessageBox::warning(this, tr("Save failed"), tr("Could not save project:\n%1").arg(error));
        return false;
    }
    m_projectPath = projectInfo.absoluteFilePath();
    m_projectName = project.name;
    setSettingsModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(m_projectPath), 4000);
    return true;
}

bool MainWindow::openProject(const QString& filePath)
{
    const ProjectLoadResult loaded = ProjectIO::load(filePath);
    if (!loaded.project) {
        QMessageBox::warning(this, tr("Open failed"),
                             tr("Could not open project %1:\n%2").arg(filePath, loaded.error));
        return false;
    }
    const Project& project = *loaded.project;

    QString error;
    const QString vertexPath = ProjectIO::toAbsolutePath(filePath, project.vertexShaderPath);
    const QString fragmentPath = ProjectIO::toAbsolutePath(filePath, project.fragmentShaderPath);
    if (!loadShaderFile(ShaderStage::Vertex, vertexPath, &error) ||
        !loadShaderFile(ShaderStage::Fragment, fragmentPath, &error)) {
        QMessageBox::warning(this, tr("Open failed"), tr("Could not load shader:\n%1").arg(error));
        loadDefaultShaders();
        return false;
    }

    m_projectPath = QFileInfo(filePath).absoluteFilePath();
    m_projectName = project.name;
    applyProjectSettings(project);
    updateWindowTitle();
    requestCompile();
    return true;
}

void MainWindow::applyProjectSettings(const Project& project)
{
    const PreviewMesh mesh = previewMeshFromString(project.previewMesh).value_or(PreviewMesh::Sphere);
    for (QAction* a : m_meshGroup->actions())
        a->setChecked(PreviewMesh(a->data().toInt()) == mesh);
    m_viewport->setPreviewMesh(mesh);
    m_autoCompileAction->setChecked(project.autoCompile);
    m_viewport->camera().set(project.camera.yawDegrees, project.camera.pitchDegrees,
                             project.camera.distance);
    m_parameterOverrides = project.parameters;
    setSettingsModified(false);
}

void MainWindow::newProject()
{
    if (!maybeSaveChanges())
        return;
    m_projectPath.clear();
    m_projectName = QStringLiteral("Untitled");
    loadDefaultShaders();
    applyProjectSettings(Project{});
    updateWindowTitle();
    requestCompile();
}

void MainWindow::openProjectDialog()
{
    if (!maybeSaveChanges())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"), QDir::homePath(),
                                                      tr("PrismForge project (*.pfproj)"));
    if (!path.isEmpty())
        openProject(path);
}

void MainWindow::openShaderDialog()
{
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open Shader"), QDir::homePath(),
        tr("GLSL shaders (*.vert *.frag *.vs *.fs *.glsl);;All files (*)"));
    if (path.isEmpty())
        return;
    const ShaderStage stage =
        stageFromFileSuffix(QFileInfo(path).suffix()).value_or(currentDocument().stage);
    StageDocument& doc = document(stage);
    if (doc.editor->document()->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("Replace shader"),
            tr("The %1 shader has unsaved changes. Replace it?").arg(toString(stage)));
        if (answer != QMessageBox::Yes)
            return;
    }
    QString error;
    if (!loadShaderFile(stage, path, &error)) {
        QMessageBox::warning(this, tr("Open failed"), error);
        return;
    }
    m_editorTabs->setCurrentIndex(tabIndex(stage));
    requestCompile();
}

void MainWindow::newShader()
{
    StageDocument& doc = currentDocument();
    if (doc.editor->document()->isModified()) {
        const auto answer = QMessageBox::question(
            this, tr("New shader"),
            tr("Discard unsaved changes to the %1 shader?").arg(toString(doc.stage)));
        if (answer != QMessageBox::Yes)
            return;
    }
    doc.editor->setPlainText(builtinShader(doc.stage));
    doc.editor->document()->setModified(false);
    doc.filePath.clear();
    updateTabTitles();
    requestCompile();
}

bool MainWindow::maybeSaveChanges()
{
    if (!hasUnsavedChanges())
        return true;
    const auto answer = QMessageBox::warning(
        this, tr("Unsaved changes"), tr("The project has unsaved changes."),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    if (answer == QMessageBox::Save)
        return saveProject(false);
    return answer == QMessageBox::Discard;
}

void MainWindow::updateTabTitles()
{
    for (const StageDocument& doc : m_documents) {
        QString title = doc.filePath.isEmpty() ? QStringLiteral("untitled.%1").arg(stageSuffix(doc.stage))
                                               : QFileInfo(doc.filePath).fileName();
        if (doc.editor->document()->isModified())
            title += QStringLiteral(" •");
        const int index = tabIndex(doc.stage);
        m_editorTabs->setTabText(index, title);
        m_editorTabs->setTabToolTip(index, doc.filePath.isEmpty() ? tr("Not saved") : doc.filePath);
    }
}

void MainWindow::updateWindowTitle()
{
    setWindowTitle(QStringLiteral("%1%2 — PrismForge").arg(m_projectName, hasUnsavedChanges() ? "*" : ""));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSaveChanges()) {
        event->ignore();
        return;
    }
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    event->accept();
}

// ---------------------------------------------------------------- renderer info

void MainWindow::showRendererInfo(const RendererInfo& info)
{
    m_rendererInfo = info;
    QString html = QStringLiteral(
                       "<table cellspacing='2'>"
                       "<tr><td style='color:#80848b'>API</td><td>%1 %2</td></tr>"
                       "<tr><td style='color:#80848b'>Device</td><td>%3</td></tr>"
                       "<tr><td style='color:#80848b'>Vendor</td><td>%4</td></tr>"
                       "<tr><td style='color:#80848b'>GLSL</td><td>%5</td></tr>"
                       "<tr><td style='color:#80848b'>Validator</td><td>%6</td></tr>"
                       "</table>")
                       .arg(info.api, info.version.toHtmlEscaped(), info.device.toHtmlEscaped(),
                            info.vendor.toHtmlEscaped(), info.shadingLanguageVersion.toHtmlEscaped(),
                            GlslangValidator::isAvailable()
                                ? QStringLiteral("glslang ") + GlslangValidator::versionString()
                                : tr("none (driver only)"));
    for (const QString& note : info.notes)
        html += QStringLiteral("<p style='color:#d8a64a'>%1</p>").arg(note.toHtmlEscaped());
    m_rendererLabel->setText(html);

    m_capabilities->clear();
    for (const auto& [name, value] : info.capabilities)
        new QTreeWidgetItem(m_capabilities, {name, value});
    auto* extensions = new QTreeWidgetItem(
        m_capabilities, {tr("Extensions"), QString::number(info.extensions.size())});
    for (const QString& extension : info.extensions)
        new QTreeWidgetItem(extensions, {extension, QString()});
}

void MainWindow::showStats(double frameIntervalMs, const FrameStats& stats)
{
    const QString gpu = stats.gpuMs ? QStringLiteral("%1 ms").arg(*stats.gpuMs, 0, 'f', 3)
                        : m_rendererInfo.gpuTimerQueries ? tr("pending")
                                                         : tr("unsupported");
    m_statsLabel->setText(
        QStringLiteral("<table cellspacing='2'>"
                       "<tr><td style='color:#80848b'>Frame interval (CPU)</td><td>%1 ms (%2 fps)</td></tr>"
                       "<tr><td style='color:#80848b'>Submit time (CPU)</td><td>%3 ms</td></tr>"
                       "<tr><td style='color:#80848b'>GPU time (timer query)</td><td>%4</td></tr>"
                       "<tr><td style='color:#80848b'>Triangles / draws</td><td>%5 / %6</td></tr>"
                       "</table>")
            .arg(frameIntervalMs, 0, 'f', 2)
            .arg(frameIntervalMs > 0 ? 1000.0 / frameIntervalMs : 0.0, 0, 'f', 0)
            .arg(stats.cpuSubmitMs, 0, 'f', 3)
            .arg(gpu)
            .arg(stats.triangles)
            .arg(stats.drawCalls));
    m_statusFrame->setText(tr("%1 ms/frame · GPU %2").arg(frameIntervalMs, 0, 'f', 2).arg(gpu));
}

} // namespace pf
