#include "core/RenameHistory.hpp"

#include <QStringList>

#include <algorithm>
#include <optional>

namespace omaseed::core {

namespace {

using What = RenameRecord::What;
using By = RenameRecord::By;

QString whatKey(What what)
{
    switch (what) {
    case What::Torrent:
        return QStringLiteral("torrent");
    case What::File:
        return QStringLiteral("file");
    case What::Folder:
        return QStringLiteral("folder");
    case What::Files:
        return QStringLiteral("files");
    }
    return {};
}

std::optional<What> whatOf(const QString &key)
{
    if (key == QLatin1String("torrent"))
        return What::Torrent;
    if (key == QLatin1String("file"))
        return What::File;
    if (key == QLatin1String("folder"))
        return What::Folder;
    if (key == QLatin1String("files"))
        return What::Files;
    return std::nullopt;
}

QString byKey(By by)
{
    return by == By::Omaseed ? QStringLiteral("omaseed") : QStringLiteral("elsewhere");
}

std::optional<By> byOf(const QString &key)
{
    if (key == QLatin1String("omaseed"))
        return By::Omaseed;
    if (key == QLatin1String("elsewhere"))
        return By::Elsewhere;
    return std::nullopt;
}

/// DECISIONS C38 R6: does `next` fold into `newest`, the torrent's newest line? The same name
/// renamed again (the torrent's, or the same file or folder: the last new name is this one's
/// old, in the same folder), by the same hand (a poll's never folds with omaseed's own),
/// within 10 minutes. A Files run is a sheet run, each its own line.
///
/// "No other line between them" is only what this history knows: `newest` is the torrent's
/// newest rename. The story's other lines (moves, failures…) aren't known here, so a move
/// between two renames doesn't stop them folding.
bool folds(const RenameRecord &newest, const RenameRecord &next)
{
    if (next.what == What::Files || newest.what != next.what || newest.by != next.by)
        return false;
    if (newest.in != next.in || newest.now != next.was)
        return false;
    const qint64 apart = newest.at.secsTo(next.at);
    return apart >= 0 && apart <= RenameHistory::kFoldSeconds;
}

} // namespace

QJsonObject RenameRecord::toJson() const
{
    QJsonObject out{{QStringLiteral("id"), id},
                    {QStringLiteral("at"), at.toUTC().toString(Qt::ISODate)},
                    {QStringLiteral("hash"), hash},
                    {QStringLiteral("what"), whatKey(what)},
                    {QStringLiteral("was"), was},
                    {QStringLiteral("now"), now},
                    {QStringLiteral("count"), count},
                    {QStringLiteral("in"), in},
                    {QStringLiteral("by"), byKey(by)},
                    {QStringLiteral("disk"), disk}};
    if (folders > 1)
        out.insert(QStringLiteral("folders"), folders);
    return out;
}

RenameRecord RenameRecord::fromJson(const QJsonObject &object, bool *ok)
{
    RenameRecord record;
    record.id = object.value(QStringLiteral("id")).toInt();
    record.at =
        QDateTime::fromString(object.value(QStringLiteral("at")).toString(), Qt::ISODate).toUTC();
    record.hash = object.value(QStringLiteral("hash")).toString();
    const auto what = whatOf(object.value(QStringLiteral("what")).toString());
    record.what = what.value_or(What::Torrent);
    record.was = object.value(QStringLiteral("was")).toString();
    record.now = object.value(QStringLiteral("now")).toString();
    record.count = std::max(1, object.value(QStringLiteral("count")).toInt(1));
    record.in = object.value(QStringLiteral("in")).toString();
    record.folders = std::max(1, object.value(QStringLiteral("folders")).toInt(1));
    const auto by = byOf(object.value(QStringLiteral("by")).toString());
    record.by = by.value_or(By::Omaseed);
    record.disk = object.value(QStringLiteral("disk")).toBool();
    if (ok)
        *ok = !record.hash.isEmpty() && record.at.isValid() && what && by;
    return record;
}

int RenameHistory::record(const RenameRecord &rename)
{
    if (rename.hash.isEmpty() || rename.was == rename.now)
        return 0;
    QList<RenameRecord> &lines = m_byHash[rename.hash];
    if (!lines.isEmpty() && folds(lines.constFirst(), rename)) {
        // C38 R6: the first one's old name, the last one's new name and time.
        RenameRecord &newest = lines.first();
        newest.now = rename.now;
        newest.at = rename.at.toUTC();
        newest.disk = newest.disk || rename.disk;
        if (newest.was != newest.now)
            return newest.id;
        // A run that ends on the name it started with leaves no line.
        lines.removeFirst();
        if (lines.isEmpty())
            m_byHash.remove(rename.hash);
        return 0;
    }
    RenameRecord line = rename;
    line.id = m_nextId++;
    line.at = rename.at.toUTC();
    lines.prepend(line);
    // C38 R9: the last 10 per torrent.
    while (lines.size() > kMaxPerTorrent)
        lines.removeLast();
    return line.id;
}

bool RenameHistory::remove(const QString &hash, int id)
{
    const auto it = m_byHash.find(hash);
    if (it == m_byHash.end())
        return false;
    const qsizetype removed =
        it->removeIf([id](const RenameRecord &record) { return record.id == id; });
    if (it->isEmpty())
        m_byHash.erase(it);
    return removed > 0;
}

bool RenameHistory::prune(const QSet<QString> &hashesInLibrary)
{
    return m_byHash.removeIf(
               [&hashesInLibrary](const std::pair<const QString &, QList<RenameRecord> &> &entry) {
                   return !hashesInLibrary.contains(entry.first);
               }) > 0;
}

QList<RenameRecord> RenameHistory::recordsFor(const QString &hash) const
{
    return m_byHash.value(hash);
}

QJsonArray RenameHistory::toJson() const
{
    QStringList hashes = m_byHash.keys();
    hashes.sort();
    QJsonArray out;
    for (const QString &hash : std::as_const(hashes)) {
        for (const RenameRecord &record : m_byHash.value(hash))
            out.append(record.toJson());
    }
    return out;
}

RenameHistory RenameHistory::fromJson(const QJsonArray &array)
{
    RenameHistory history;
    QSet<int> ids;
    QList<RenameRecord *> needId;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        bool ok = false;
        RenameRecord record = RenameRecord::fromJson(value.toObject(), &ok);
        if (!ok || record.was == record.now)
            continue;
        history.m_byHash[record.hash].append(std::move(record));
    }
    for (auto it = history.m_byHash.begin(); it != history.m_byHash.end(); ++it) {
        QList<RenameRecord> &lines = *it;
        std::stable_sort(
            lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.at > b.at; });
        while (lines.size() > kMaxPerTorrent)
            lines.removeLast();
        for (RenameRecord &record : lines) {
            if (record.id <= 0 || ids.contains(record.id))
                needId << &record;
            else
                ids.insert(record.id);
        }
    }
    int top = 0;
    for (int id : std::as_const(ids))
        top = std::max(top, id);
    history.m_nextId = top + 1;
    // A record saved without an id, or with one already taken, gets a fresh one.
    for (RenameRecord *record : std::as_const(needId))
        record->id = history.m_nextId++;
    return history;
}

} // namespace omaseed::core
