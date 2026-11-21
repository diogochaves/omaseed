#include "core/RemovedKeep.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimeZone>

#include <algorithm>

namespace omaseed::core {

namespace {

const QRegularExpression &idPattern()
{
    static const QRegularExpression pattern(
        QStringLiteral("^(\\d{8})-([0-9a-f]{8})(?:-([1-9]\\d{0,3}))?$"));
    return pattern;
}

/// The day in a folder's name, for one whose file can't be read.
QDateTime dayOf(const QString &id)
{
    const QRegularExpressionMatch m = idPattern().match(id);
    if (!m.hasMatch())
        return {};
    const QDate day = QDate::fromString(m.captured(1), QStringLiteral("yyyyMMdd"));
    return day.isValid() ? QDateTime(day, QTime(0, 0), QTimeZone::UTC) : QDateTime();
}

constexpr QFileDevice::Permissions kPrivateDir =
    QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;

} // namespace

QString RemovedKeep::root()
{
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/omaseed/removed");
}

bool RemovedKeep::isId(const QString &id)
{
    return idPattern().match(id).hasMatch();
}

QString RemovedKeep::prepare(const QString &hash, const QDateTime &at, QString *error)
{
    const QString lower = hash.toLower();
    if (!TorrentsBackup::isTorrentId(lower)) {
        if (error)
            *error = QStringLiteral("not a torrent's hash: %1").arg(hash);
        return {};
    }
    const QString base = root();
    if (!QDir().mkpath(base)) {
        if (error)
            *error = QStringLiteral("cannot make %1").arg(base);
        return {};
    }
    QFile::setPermissions(base, kPrivateDir);
    const QString stem =
        at.toUTC().date().toString(QStringLiteral("yyyyMMdd")) + QLatin1Char('-') + lower.left(8);
    QDir dir(base);
    for (int n = 1; n < 1000; ++n) {
        const QString id = n == 1 ? stem : stem + QLatin1Char('-') + QString::number(n);
        if (dir.exists(id))
            continue;
        if (!dir.mkdir(id))
            break;
        QFile::setPermissions(dir.filePath(id), kPrivateDir);
        return dir.filePath(id) + QLatin1Char('/') + kFile;
    }
    if (error)
        *error = QStringLiteral("cannot make a folder for %1 in %2").arg(lower.left(8), base);
    return {};
}

std::optional<RemovedKeep::Entry> RemovedKeep::keep(const TorrentsManifest &manifest,
                                                    const TorrentRecord &record,
                                                    const QByteArray &torrent,
                                                    QString *error)
{
    if (!record.isMagnet() && torrent.isEmpty()) {
        if (error)
            *error = QStringLiteral("no .torrent to keep for %1").arg(record.name);
        return std::nullopt;
    }
    TorrentsManifest one = manifest;
    if (!one.created.isValid())
        one.created = QDateTime::currentDateTimeUtc();
    one.count = 1;
    one.magnets = record.isMagnet() ? 1 : 0;
    const QString path = prepare(record.id, one.created, error);
    if (path.isEmpty())
        return std::nullopt;
    const QString id = QFileInfo(path).dir().dirName();
    QSaveFile file(path);
    const auto failed = [&](const QString &why) -> std::optional<Entry> {
        file.cancelWriting();
        forget(id);
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (!file.open(QIODevice::WriteOnly))
        return failed(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    TarWriter writer(&file);
    const qint64 now = one.created.toSecsSinceEpoch();
    QString why;
    if (!writer.add(TorrentsBackup::kManifest,
                    QJsonDocument(one.toJson()).toJson(QJsonDocument::Indented),
                    now,
                    &why) ||
        !writer.add(TorrentsBackup::recordEntry(record.id),
                    QJsonDocument(record.toJson()).toJson(QJsonDocument::Indented),
                    now,
                    &why) ||
        (!record.isMagnet() &&
         !writer.add(TorrentsBackup::torrentEntry(record.id), torrent, now, &why)) ||
        !writer.finish(&why))
        return failed(why);
    if (!file.commit())
        return failed(QStringLiteral("cannot write %1: %2").arg(path, file.errorString()));
    const std::optional<Entry> kept = entry(id);
    if (!kept)
        return failed(QStringLiteral("cannot read back %1").arg(path));
    return kept;
}

std::optional<RemovedKeep::Entry> RemovedKeep::entry(const QString &id)
{
    if (!isId(id))
        return std::nullopt;
    const QString path = root() + QLatin1Char('/') + id + QLatin1Char('/') + kFile;
    if (!QFileInfo::exists(path))
        return std::nullopt;
    const std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(path);
    if (!contents || contents->records.size() != 1 || !contents->manifest.created.isValid())
        return std::nullopt;
    const TorrentRecord &record = contents->records.constFirst();
    Entry e;
    e.id = id;
    e.hash = record.infohashV1.isEmpty() ? record.id : record.infohashV1;
    e.name = record.name;
    e.savePath = record.savePath;
    e.serverName = contents->manifest.serverName;
    e.serverUrl = contents->manifest.serverUrl;
    e.engine = contents->manifest.engine;
    e.removedAt = contents->manifest.created;
    e.keptUntil = e.removedAt.addDays(kDays);
    e.path = path;
    e.link = record.isMagnet();
    return e;
}

QList<RemovedKeep::Entry> RemovedKeep::list()
{
    QList<Entry> entries;
    const QDir dir(root());
    for (const QString &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (const std::optional<Entry> e = entry(id))
            entries.append(*e);
    }
    std::stable_sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
        return a.removedAt > b.removedAt;
    });
    return entries;
}

int RemovedKeep::prune(const QDateTime &now)
{
    const QDateTime limit = now.addDays(-kDays);
    const QDir dir(root());
    int gone = 0;
    for (const QString &id : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (!isId(id))
            continue; // never ours: left alone
        const std::optional<Entry> e = entry(id);
        // A damaged one goes by the day in its name (a whole day later, to be sure).
        const QDateTime at = e ? e->removedAt : dayOf(id).addDays(1);
        if (at.isValid() && at < limit && forget(id))
            ++gone;
    }
    return gone;
}

std::optional<TorrentsBackup::Contents> RemovedKeep::take(const QString &id, QString *error)
{
    const std::optional<Entry> e = entry(id);
    if (!e) {
        if (error)
            *error = QStringLiteral("nothing is kept as %1").arg(id);
        return std::nullopt;
    }
    std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(e->path, error);
    if (!contents)
        return std::nullopt;
    // C50 GT6: back paused, never checked or started, wherever it was.
    for (TorrentRecord &record : contents->records)
        record.state = QStringLiteral("stopped");
    return contents;
}

bool RemovedKeep::forget(const QString &id)
{
    if (!isId(id))
        return false;
    QDir dir(root() + QLatin1Char('/') + id);
    return dir.exists() && dir.removeRecursively();
}

} // namespace omaseed::core
