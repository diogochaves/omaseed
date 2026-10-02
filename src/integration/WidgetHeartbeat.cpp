#include "integration/WidgetHeartbeat.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLoggingCategory>
#include <QTimer>

namespace omaseed::integration {

namespace {

Q_LOGGING_CATEGORY(lcHeartbeat, "omaseed.widget")

const char *kFileName = "widget.alive";

} // namespace

WidgetHeartbeat::WidgetHeartbeat(QObject *parent, const QString &directory)
    : QObject(parent)
    , m_directory(directory.isEmpty() ? defaultDirectory() : directory)
    , m_watcher(new QFileSystemWatcher(this))
    , m_staleTimer(new QTimer(this))
{
    m_staleTimer->setSingleShot(true);
    connect(m_staleTimer, &QTimer::timeout, this, &WidgetHeartbeat::refresh);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, &WidgetHeartbeat::refresh);
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, &WidgetHeartbeat::refresh);

    if (m_directory.isEmpty()) {
        qCInfo(lcHeartbeat) << "no XDG_RUNTIME_DIR: the bar widget's heartbeat is not watched";
        return;
    }
    m_filePath = QDir(m_directory).filePath(QString::fromLatin1(kFileName));
    // The status file lives here too (StatusWriter); whichever side starts first makes it.
    if (!QFileInfo::exists(m_directory)) {
        QDir().mkpath(m_directory);
        QFile::setPermissions(
            m_directory, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    refresh();
}

QString WidgetHeartbeat::defaultDirectory()
{
    const QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        return QString();
    return QDir(runtime).filePath(QStringLiteral("omaseed"));
}

bool WidgetHeartbeat::isFresh(const QDateTime &mtime, const QDateTime &now)
{
    if (!mtime.isValid() || !now.isValid())
        return false;
    // A file stamped in the future (a clock step) counts as fresh: it was touched recently.
    return mtime.msecsTo(now) < qint64(kFreshSeconds) * 1000;
}

bool WidgetHeartbeat::trayIconVisible(const QString &mode,
                                      const QDateTime &mtime,
                                      const QDateTime &now)
{
    return trayIconVisible(mode, isFresh(mtime, now));
}

bool WidgetHeartbeat::trayIconVisible(const QString &mode, bool widgetAlive)
{
    if (mode == QLatin1String("never"))
        return false;
    if (mode == QLatin1String("always"))
        return true;
    return !widgetAlive;
}

void WidgetHeartbeat::refresh()
{
    if (m_filePath.isEmpty())
        return;
    rewatch();

    const QFileInfo info(m_filePath);
    const QDateTime mtime = info.exists() ? info.lastModified() : QDateTime();
    const QDateTime now = QDateTime::currentDateTime();
    const bool alive = isFresh(mtime, now);

    m_staleTimer->stop();
    if (alive) {
        // Wake up just after the file turns stale, unless another touch comes first.
        const qint64 left = qint64(kFreshSeconds) * 1000 - mtime.msecsTo(now);
        m_staleTimer->start(int(qBound<qint64>(0, left, qint64(kFreshSeconds) * 1000) + 50));
    }

    if (alive == m_alive)
        return;
    m_alive = alive;
    qCInfo(lcHeartbeat) << (alive ? "bar widget running" : "bar widget gone");
    Q_EMIT widgetAliveChanged(alive);
}

void WidgetHeartbeat::rewatch()
{
    // The directory may appear after we started (no StatusWriter yet), and a file replaced by
    // rename drops its watch: add whatever is missing, every time.
    const QStringList directories = m_watcher->directories();
    if (directories.isEmpty() && QFileInfo::exists(m_directory))
        m_watcher->addPath(m_directory);
    const bool watchingFile = m_watcher->files().contains(m_filePath);
    if (!watchingFile && QFileInfo::exists(m_filePath))
        m_watcher->addPath(m_filePath);
}

} // namespace omaseed::integration
