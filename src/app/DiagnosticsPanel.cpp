#include "app/DiagnosticsPanel.h"

#include <QHeaderView>
#include <QLabel>
#include <QStyle>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pf {

namespace {
enum Column { ColSeverity, ColStage, ColLocation, ColMessage, ColOrigin, ColCount };
constexpr int kIndexRole = Qt::UserRole + 1;
} // namespace

DiagnosticsPanel::DiagnosticsPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_summary = new QLabel(tr("No compilation yet"), this);
    m_summary->setObjectName("diagnosticsSummary");
    m_summary->setContentsMargins(8, 4, 8, 4);
    layout->addWidget(m_summary);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("Severity"), tr("Stage"), tr("Line"), tr("Message"), tr("Source")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setAlternatingRowColors(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(ColMessage, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(ColSeverity, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColStage, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColLocation, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(ColOrigin, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree);

    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* item) {
        const int index = item->data(0, kIndexRole).toInt();
        if (index >= 0 && index < m_diagnostics.size())
            emit diagnosticActivated(m_diagnostics[index]);
    });
}

void DiagnosticsPanel::setDiagnostics(const QList<Diagnostic>& diagnostics, const QString& summary,
                                      bool success)
{
    m_diagnostics = diagnostics;
    m_summary->setText(summary);
    m_summary->setProperty("state", success ? "ok" : "error");
    m_summary->style()->unpolish(m_summary);
    m_summary->style()->polish(m_summary);
    m_tree->clear();

    for (int i = 0; i < diagnostics.size(); ++i) {
        const Diagnostic& d = diagnostics[i];
        auto* item = new QTreeWidgetItem(m_tree);
        item->setText(ColSeverity, toString(d.severity));
        item->setText(ColStage, d.stage ? toString(*d.stage) : tr("program"));
        if (d.line > 0)
            item->setText(ColLocation, d.column > 0 ? QStringLiteral("%1:%2").arg(d.line).arg(d.column)
                                                    : QString::number(d.line));
        item->setText(ColMessage, d.message);
        item->setToolTip(ColMessage, d.message);
        item->setText(ColOrigin, d.origin);
        item->setData(0, kIndexRole, i);

        const QColor color = d.severity == Severity::Error     ? QColor(0xe0, 0x6c, 0x6c)
                             : d.severity == Severity::Warning ? QColor(0xd8, 0xa6, 0x4a)
                                                               : QColor(0x9a, 0x9d, 0xa3);
        item->setForeground(ColSeverity, color);
    }
}

} // namespace pf
