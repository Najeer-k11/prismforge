#pragma once

#include "diagnostics/Diagnostic.h"

#include <QPlainTextEdit>

namespace pf {

class GlslHighlighter;

// Plain-text GLSL editor with a line-number gutter and inline diagnostic marks.
class CodeEditor final : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget* parent = nullptr);

    // Underlines and gutter-marks the lines referenced by `diagnostics`.
    void setDiagnostics(const QList<Diagnostic>& diagnostics);
    void goToLine(int line, int column = -1);

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    friend class LineNumberArea;
    int gutterWidth() const;
    void paintGutter(QPaintEvent* event);
    void updateGutter(const QRect& rect, int dy);
    void refreshExtraSelections();

    QWidget* m_gutter = nullptr;
    GlslHighlighter* m_highlighter = nullptr;
    QHash<int, Severity> m_lineSeverity; // 1-based line -> worst severity
};

} // namespace pf
