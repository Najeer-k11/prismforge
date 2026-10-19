#pragma once

#include "diagnostics/Diagnostic.h"

#include <QWidget>

class QLabel;
class QTreeWidget;

namespace pf {

class DiagnosticsPanel final : public QWidget {
    Q_OBJECT
public:
    explicit DiagnosticsPanel(QWidget* parent = nullptr);

    void setDiagnostics(const QList<Diagnostic>& diagnostics, const QString& summary, bool success);

signals:
    void diagnosticActivated(const pf::Diagnostic& diagnostic);

private:
    QLabel* m_summary = nullptr;
    QTreeWidget* m_tree = nullptr;
    QList<Diagnostic> m_diagnostics;
};

} // namespace pf
