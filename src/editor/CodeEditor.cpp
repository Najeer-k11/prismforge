#include "editor/CodeEditor.h"

#include "editor/GlslHighlighter.h"

#include <QFontDatabase>
#include <QPainter>
#include <QTextBlock>

namespace pf {

namespace {
const QColor kGutterBg(0x23, 0x24, 0x27);
const QColor kGutterFg(0x6b, 0x6e, 0x75);
const QColor kGutterFgCurrent(0xc8, 0xca, 0xce);
const QColor kCurrentLine(0x2a, 0x2c, 0x30);
const QColor kErrorColor(0xe0, 0x5a, 0x5a);
const QColor kWarningColor(0xd8, 0xa6, 0x4a);
} // namespace

class LineNumberArea final : public QWidget {
public:
    explicit LineNumberArea(CodeEditor* editor) : QWidget(editor), m_editor(editor) {}
    QSize sizeHint() const override { return {m_editor->gutterWidth(), 0}; }

protected:
    void paintEvent(QPaintEvent* event) override { m_editor->paintGutter(event); }

private:
    CodeEditor* m_editor;
};

CodeEditor::CodeEditor(QWidget* parent) : QPlainTextEdit(parent)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPointSizeF(std::max(font.pointSizeF(), 10.0));
    font.setStyleHint(QFont::Monospace);
    setFont(font);
    setTabStopDistance(fontMetrics().horizontalAdvance(u' ') * 4);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setFrameShape(QFrame::NoFrame);

    m_gutter = new LineNumberArea(this);
    m_highlighter = new GlslHighlighter(document());

    connect(this, &QPlainTextEdit::blockCountChanged, this,
            [this] { setViewportMargins(gutterWidth(), 0, 0, 0); });
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateGutter);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::refreshExtraSelections);
    setViewportMargins(gutterWidth(), 0, 0, 0);
    refreshExtraSelections();
}

void CodeEditor::setDiagnostics(const QList<Diagnostic>& diagnostics)
{
    m_lineSeverity.clear();
    for (const Diagnostic& d : diagnostics) {
        if (d.line < 1)
            continue;
        auto it = m_lineSeverity.find(d.line);
        // Enum order is Error < Warning < Info, so lower is worse.
        if (it == m_lineSeverity.end() || int(d.severity) < int(*it))
            m_lineSeverity[d.line] = d.severity;
    }
    refreshExtraSelections();
    m_gutter->update();
}

void CodeEditor::goToLine(int line, int column)
{
    const QTextBlock block = document()->findBlockByNumber(std::max(line, 1) - 1);
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    if (column > 0)
        cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor,
                            std::min(column - 1, block.length() - 1));
    setTextCursor(cursor);
    centerCursor();
    setFocus();
}

int CodeEditor::gutterWidth() const
{
    int digits = 1;
    for (int n = std::max(1, blockCount()); n >= 10; n /= 10)
        ++digits;
    digits = std::max(digits, 3);
    return 14 + fontMetrics().horizontalAdvance(u'9') * digits;
}

void CodeEditor::resizeEvent(QResizeEvent* event)
{
    QPlainTextEdit::resizeEvent(event);
    const QRect cr = contentsRect();
    m_gutter->setGeometry(QRect(cr.left(), cr.top(), gutterWidth(), cr.height()));
}

void CodeEditor::keyPressEvent(QKeyEvent* event)
{
    // Keep indentation on Enter; a small convenience that matters for GLSL.
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QString line = textCursor().block().text();
        qsizetype i = 0;
        while (i < line.size() && (line[i] == u' ' || line[i] == u'\t'))
            ++i;
        QPlainTextEdit::keyPressEvent(event);
        insertPlainText(line.left(i));
        return;
    }
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        insertPlainText(QStringLiteral("    "));
        return;
    }
    QPlainTextEdit::keyPressEvent(event);
}

void CodeEditor::updateGutter(const QRect& rect, int dy)
{
    if (dy)
        m_gutter->scroll(0, dy);
    else
        m_gutter->update(0, rect.y(), m_gutter->width(), rect.height());
}

void CodeEditor::paintGutter(QPaintEvent* event)
{
    QPainter painter(m_gutter);
    painter.fillRect(event->rect(), kGutterBg);
    painter.setFont(font());

    const int currentBlock = textCursor().blockNumber();
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber();
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int lineHeight = fontMetrics().height();

    while (block.isValid() && top <= event->rect().bottom()) {
        if (block.isVisible() && bottom >= event->rect().top()) {
            const auto sev = m_lineSeverity.constFind(number + 1);
            if (sev != m_lineSeverity.constEnd()) {
                const QColor c = *sev == Severity::Error ? kErrorColor : kWarningColor;
                painter.fillRect(0, top, 3, lineHeight, c);
            }
            painter.setPen(number == currentBlock ? kGutterFgCurrent : kGutterFg);
            painter.drawText(0, top, m_gutter->width() - 6, lineHeight, Qt::AlignRight,
                             QString::number(number + 1));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::refreshExtraSelections()
{
    QList<QTextEdit::ExtraSelection> selections;

    QTextEdit::ExtraSelection current;
    current.format.setBackground(kCurrentLine);
    current.format.setProperty(QTextFormat::FullWidthSelection, true);
    current.cursor = textCursor();
    current.cursor.clearSelection();
    selections.append(current);

    for (auto it = m_lineSeverity.cbegin(); it != m_lineSeverity.cend(); ++it) {
        const QTextBlock block = document()->findBlockByNumber(it.key() - 1);
        if (!block.isValid())
            continue;
        QTextEdit::ExtraSelection mark;
        mark.format.setUnderlineStyle(QTextCharFormat::WaveUnderline);
        mark.format.setUnderlineColor(it.value() == Severity::Error ? kErrorColor : kWarningColor);
        mark.cursor = QTextCursor(block);
        mark.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        selections.append(mark);
    }
    setExtraSelections(selections);
    m_gutter->update();
}

} // namespace pf
