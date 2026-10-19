#pragma once

#include "diagnostics/LogCapture.h"

#include <QWidget>

class QComboBox;
class QTreeWidget;

namespace pf {

// Shows application log messages captured by LogCapture.
class LogPanel final : public QWidget {
    Q_OBJECT
public:
    explicit LogPanel(QWidget* parent = nullptr);

private:
    void addEntry(const LogEntry& entry);
    void applyFilter();
    bool passesFilter(QtMsgType type) const;

    QComboBox* m_level = nullptr;
    QTreeWidget* m_tree = nullptr;
    quint64 m_lastSequence = 0;
};

} // namespace pf
