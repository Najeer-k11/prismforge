#pragma once

#include <QDateTime>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>

namespace pf {

struct LogEntry {
    quint64 sequence = 0; // strictly increasing; lets views skip duplicates
    QDateTime time;
    QtMsgType type = QtInfoMsg;
    QString category;
    QString message;
};

// Collects Qt log output (qDebug/qCInfo/... from any thread) so the UI can
// show it. Messages are still forwarded to the previous handler (stderr or
// journald). Install once, early in main().
class LogCapture final : public QObject {
    Q_OBJECT
public:
    static LogCapture& instance();
    static void install();

    // Entries logged so far (bounded), oldest first.
    QList<LogEntry> entries() const;
    void clear();

signals:
    // Always delivered on the LogCapture object's (GUI) thread.
    void entryAdded(const pf::LogEntry& entry);

private:
    LogCapture() = default;
    void append(LogEntry entry);
    static void handler(QtMsgType type, const QMessageLogContext& context, const QString& message);

    static constexpr int kMaxEntries = 5000;
    mutable QMutex m_mutex;
    QList<LogEntry> m_entries;
};

QString toString(QtMsgType type);

} // namespace pf
