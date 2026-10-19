#include "diagnostics/LogCapture.h"

#include <QMetaObject>
#include <QMutexLocker>

namespace pf {

namespace {
QtMessageHandler g_previousHandler = nullptr;
}

LogCapture& LogCapture::instance()
{
    static LogCapture capture;
    return capture;
}

void LogCapture::install()
{
    instance(); // construct on the calling (main) thread
    g_previousHandler = qInstallMessageHandler(&LogCapture::handler);
}

void LogCapture::handler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    if (g_previousHandler)
        g_previousHandler(type, context, message);
    instance().append({0, QDateTime::currentDateTime(), type,
                       QString::fromLatin1(context.category ? context.category : "default"), message});
}

void LogCapture::append(LogEntry entry)
{
    {
        QMutexLocker lock(&m_mutex);
        static quint64 nextSequence = 1;
        entry.sequence = nextSequence++;
        m_entries.append(entry);
        if (m_entries.size() > kMaxEntries)
            m_entries.remove(0, m_entries.size() - kMaxEntries);
    }
    // Queued so worker-thread messages reach the UI safely.
    QMetaObject::invokeMethod(this, [this, entry] { emit entryAdded(entry); }, Qt::QueuedConnection);
}

QList<LogEntry> LogCapture::entries() const
{
    QMutexLocker lock(&m_mutex);
    return m_entries;
}

void LogCapture::clear()
{
    QMutexLocker lock(&m_mutex);
    m_entries.clear();
}

QString toString(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg: return QStringLiteral("debug");
    case QtInfoMsg: return QStringLiteral("info");
    case QtWarningMsg: return QStringLiteral("warning");
    case QtCriticalMsg: return QStringLiteral("critical");
    case QtFatalMsg: return QStringLiteral("fatal");
    }
    return {};
}

} // namespace pf
