#include "core/AutoBackup.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace omaseed::core {

namespace {

/// `auto-yyyy-MM-dd-HHmmss.omaseed`: sorting the names sorts the copies by time.
const QRegularExpression &copyName()
{
    static const QRegularExpression re(QStringLiteral("^auto-(\\d{4}-\\d{2}-\\d{2}-\\d{6})\\.omaseed$"));
    return re;
}

} // namespace

AutoBackup::AutoBackup(QObject *parent)
    : QObject(parent)
{}

void AutoBackup::setMode(const QString &mode)
{
    m_mode = (mode == QLatin1String("off") || mode == QLatin1String("daily")) ? mode
                                                                              : QStringLiteral("weekly");
}

qint64 AutoBackup::interval() const
{
    if (m_mode == QLatin1String("off"))
        return 0;
    return m_mode == QLatin1String("daily") ? 86400 : 7 * 86400;
}

QStringList AutoBackup::copies() const
{
    QStringList out;
    const QDir dir(m_sources.backupsDir);
    const QStringList names = dir.entryList({kPrefix + QStringLiteral("-*") + OmaseedBackup::kSuffix},
                                            QDir::Files, QDir::Name | QDir::Reversed);
    for (const QString &name : names) {
        if (copyName().match(name).hasMatch())
            out.append(dir.filePath(name));
    }
    return out;
}

QDateTime AutoBackup::lastCopyTime() const
{
    const QStringList all = copies();
    if (all.isEmpty())
        return {};
    const QRegularExpressionMatch m = copyName().match(QFileInfo(all.constFirst()).fileName());
    return QDateTime::fromString(m.captured(1), QStringLiteral("yyyy-MM-dd-HHmmss"));
}

bool AutoBackup::runIfDue(qint64 now)
{
    const qint64 every = interval();
    if (every <= 0)
        return false;
    const QDateTime last = lastCopyTime();
    // A copy "from the future" (the clock stepped back) counts as due, so copies never stop.
    if (last.isValid() && last.toSecsSinceEpoch() <= now && now - last.toSecsSinceEpoch() < every)
        return false;
    return runNow(now);
}

bool AutoBackup::runNow(qint64 now)
{
    if (m_beforeCopy)
        m_beforeCopy();
    const QString path = OmaseedBackup::datedName(m_sources.backupsDir, kPrefix, now);
    QString why;
    if (!OmaseedBackup::createFile(m_sources, path, now, &why)) {
        QFile::remove(path);
        if (!m_failureReported) {
            m_failureReported = true;
            Q_EMIT failed(why);
        }
        return false;
    }
    m_failureReported = false;
    prune();
    Q_EMIT copied(path);
    return true;
}

void AutoBackup::prune()
{
    const QStringList all = copies();
    for (int i = kKeep; i < all.size(); ++i)
        QFile::remove(all.at(i));
}

} // namespace omaseed::core
