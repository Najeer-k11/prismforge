#include "app/LogPanel.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QScrollBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace pf {

namespace {
enum Column { ColTime, ColLevel, ColCategory, ColMessage, ColCount };
constexpr int kTypeRole = Qt::UserRole + 1;
constexpr int kMaxRows = 5000;

// Severity rank used for filtering: debug < info < warning < critical/fatal.
int rank(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return 0;
    case QtInfoMsg: return 1;
    case QtWarningMsg: return 2;
    case QtCriticalMsg:
    case QtFatalMsg: return 3;
    }
    return 1;
}
} // namespace

LogPanel::LogPanel(QWidget* parent) : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto* bar = new QWidget(this);
    auto* barLayout = new QHBoxLayout(bar);
    barLayout->setContentsMargins(8, 3, 8, 3);
    barLayout->addWidget(new QLabel(tr("Show"), bar));
    m_level = new QComboBox(bar);
    m_level->addItem(tr("Debug and above"), 0);
    m_level->addItem(tr("Info and above"), 1);
    m_level->addItem(tr("Warnings and errors"), 2);
    m_level->addItem(tr("Errors only"), 3);
    m_level->setCurrentIndex(1);
    barLayout->addWidget(m_level);
    barLayout->addStretch();
    auto* clear = new QToolButton(bar);
    clear->setText(tr("Clear"));
    barLayout->addWidget(clear);
    layout->addWidget(bar);

    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(ColCount);
    m_tree->setHeaderLabels({tr("Time"), tr("Level"), tr("Category"), tr("Message")});
    m_tree->setRootIsDecorated(false);
    m_tree->setUniformRowHeights(true);
    m_tree->setFrameShape(QFrame::NoFrame);
    m_tree->header()->setStretchLastSection(true);
    for (int c : {ColTime, ColLevel, ColCategory})
        m_tree->header()->setSectionResizeMode(c, QHeaderView::ResizeToContents);
    layout->addWidget(m_tree);

    connect(m_level, &QComboBox::currentIndexChanged, this, &LogPanel::applyFilter);
    connect(clear, &QToolButton::clicked, this, [this] {
        LogCapture::instance().clear();
        m_tree->clear();
    });

    LogCapture& capture = LogCapture::instance();
    connect(&capture, &LogCapture::entryAdded, this, &LogPanel::addEntry);
    for (const LogEntry& entry : capture.entries())
        addEntry(entry);
}

bool LogPanel::passesFilter(QtMsgType type) const
{
    return rank(type) >= m_level->currentData().toInt();
}

void LogPanel::addEntry(const LogEntry& entry)
{
    if (entry.sequence <= m_lastSequence)
        return; // already shown (delivered both by the snapshot and the queued signal)
    m_lastSequence = entry.sequence;

    QScrollBar* scroll = m_tree->verticalScrollBar();
    const bool atBottom = scroll->value() == scroll->maximum();

    auto* item = new QTreeWidgetItem(m_tree);
    item->setText(ColTime, entry.time.toString(QStringLiteral("HH:mm:ss.zzz")));
    item->setText(ColLevel, toString(entry.type));
    item->setText(ColCategory, entry.category);
    item->setText(ColMessage, entry.message);
    item->setToolTip(ColMessage, entry.message);
    item->setData(0, kTypeRole, int(entry.type));
    const int r = rank(entry.type);
    if (r >= 2)
        item->setForeground(ColLevel, r == 2 ? QColor(0xd8, 0xa6, 0x4a) : QColor(0xe0, 0x6c, 0x6c));
    item->setHidden(!passesFilter(entry.type));

    while (m_tree->topLevelItemCount() > kMaxRows)
        delete m_tree->takeTopLevelItem(0);
    if (atBottom)
        m_tree->scrollToBottom();
}

void LogPanel::applyFilter()
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* item = m_tree->topLevelItem(i);
        item->setHidden(!passesFilter(QtMsgType(item->data(0, kTypeRole).toInt())));
    }
}

} // namespace pf
