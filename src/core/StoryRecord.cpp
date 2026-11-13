#include "core/StoryRecord.hpp"

#include "core/TagModel.hpp"

#include <QStringList>

#include <algorithm>
#include <optional>

namespace omaseed::core {

namespace {

using What = StoryEntry::What;
using By = StoryEntry::By;

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
    case What::Category:
        return QStringLiteral("category");
    case What::Tags:
        return QStringLiteral("tags");
    case What::Place:
        return QStringLiteral("place");
    }
    return {};
}

std::optional<What> whatOf(const QString &key)
{
    for (What what : {What::Torrent,
                      What::File,
                      What::Folder,
                      What::Files,
                      What::Category,
                      What::Tags,
                      What::Place}) {
        if (key == whatKey(what))
            return what;
    }
    return std::nullopt;
}

QString byKey(By by)
{
    switch (by) {
    case By::Omaseed:
        return QStringLiteral("omaseed");
    case By::Elsewhere:
        return QStringLiteral("elsewhere");
    case By::Server:
        return QStringLiteral("server");
    }
    return {};
}

std::optional<By> byOf(const QString &key)
{
    for (By by : {By::Omaseed, By::Elsewhere, By::Server}) {
        if (key == byKey(by))
            return by;
    }
    return std::nullopt;
}

/// DECISIONS C43 Y10: the groups each keep their own last 10 per torrent.
enum class Group { Names, Filing, Places };

Group groupOf(What what)
{
    switch (what) {
    case What::Category:
    case What::Tags:
        return Group::Filing;
    case What::Place:
        return Group::Places;
    case What::Torrent:
    case What::File:
    case What::Folder:
    case What::Files:
        break;
    }
    return Group::Names;
}

/// Drops the entries of `group` past the newest kMaxPerKind (the list is newest first).
void cap(QList<StoryEntry> &lines, Group group)
{
    int seen = 0;
    for (qsizetype i = 0; i < lines.size();) {
        if (groupOf(lines.at(i).what) == group && ++seen > StoryRecord::kMaxPerKind)
            lines.removeAt(i);
        else
            ++i;
    }
}

/// Each name once, in the tag list's order (C43 Y9, TagModel::lessThan).
QStringList tagOrder(QStringList names)
{
    names.removeDuplicates();
    std::sort(names.begin(), names.end(), TagModel::lessThan);
    return names;
}

QStringList minus(const QStringList &a, const QStringList &b)
{
    QStringList out;
    for (const QString &name : a) {
        if (!b.contains(name))
            out << name;
    }
    return out;
}

QJsonArray toArray(const QStringList &names)
{
    return QJsonArray::fromStringList(names);
}

QStringList fromArray(const QJsonValue &value)
{
    QStringList out;
    for (const QJsonValue &name : value.toArray()) {
        if (name.isString())
            out << name.toString();
    }
    return out;
}

/// DECISIONS C38 R6, C43 Y9: does `next` fold into `newest`, the torrent's newest entry of any
/// kind? The same thing edited again (the same name renamed again, the category changed again,
/// tags again), by the same hand (a poll's never folds with omaseed's own), within 10
/// minutes. A Files run is a sheet run, each its own line; a place, a server's doing and a
/// removed category or tag are each their own line too.
///
/// "No other line between them" is only what this record knows: a tag line between two
/// renames stops their fold, a move from the move history doesn't (it isn't kept here).
bool folds(const StoryEntry &newest, const StoryEntry &next)
{
    if (newest.what != next.what || newest.by != next.by)
        return false;
    if (next.what == What::Place || next.what == What::Files || next.by == By::Server ||
        newest.removed || next.removed)
        return false;
    const qint64 apart = newest.at.secsTo(next.at);
    if (apart < 0 || apart > StoryRecord::kFoldSeconds)
        return false;
    switch (next.what) {
    case What::Tags:
        return true;
    case What::Category:
        return newest.now == next.was;
    case What::Torrent:
    case What::File:
    case What::Folder:
        return newest.in == next.in && newest.now == next.was;
    case What::Files:
    case What::Place:
        break;
    }
    return false;
}

/// Folds `next` into `newest`: the first one's start, the last one's end and time.
void fold(StoryEntry &newest, const StoryEntry &next)
{
    newest.at = next.at.toUTC();
    if (next.what == What::Tags) {
        // C43 Y9: the net change of the two.
        const QStringList tagged =
            minus(newest.tagged, next.untagged) + minus(next.tagged, newest.untagged);
        const QStringList untagged =
            minus(newest.untagged, next.tagged) + minus(next.untagged, newest.tagged);
        newest.tagged = tagOrder(tagged);
        newest.untagged = tagOrder(untagged);
        return;
    }
    newest.now = next.now;
    newest.disk = newest.disk || next.disk;
}

} // namespace

bool StoryEntry::isNoChange() const
{
    if (what == What::Tags)
        return tagged.isEmpty() && untagged.isEmpty();
    return was == now;
}

QJsonObject StoryEntry::toJson() const
{
    QJsonObject out{{QStringLiteral("id"), id},
                    {QStringLiteral("at"), at.toUTC().toString(Qt::ISODate)},
                    {QStringLiteral("hash"), hash},
                    {QStringLiteral("what"), whatKey(what)},
                    {QStringLiteral("by"), byKey(by)}};
    switch (what) {
    case What::Torrent:
    case What::File:
    case What::Folder:
    case What::Files:
        // Exactly C38's fields, so its file reads as it did (C43 Y10).
        out.insert(QStringLiteral("was"), was);
        out.insert(QStringLiteral("now"), now);
        out.insert(QStringLiteral("count"), count);
        out.insert(QStringLiteral("in"), in);
        out.insert(QStringLiteral("disk"), disk);
        if (folders > 1)
            out.insert(QStringLiteral("folders"), folders);
        break;
    case What::Category:
        out.insert(QStringLiteral("was"), was);
        out.insert(QStringLiteral("now"), now);
        if (removed)
            out.insert(QStringLiteral("removed"), true);
        break;
    case What::Tags:
        out.insert(QStringLiteral("tagged"), toArray(tagged));
        out.insert(QStringLiteral("untagged"), toArray(untagged));
        if (removed)
            out.insert(QStringLiteral("removed"), true);
        break;
    case What::Place:
        out.insert(QStringLiteral("was"), was);
        out.insert(QStringLiteral("now"), now);
        if (!landed)
            out.insert(QStringLiteral("landed"), false);
        if (!cause.isEmpty()) {
            out.insert(QStringLiteral("cause"), cause);
            out.insert(QStringLiteral("causeName"), causeName);
        }
        break;
    }
    return out;
}

StoryEntry StoryEntry::fromJson(const QJsonObject &object, bool *ok)
{
    StoryEntry entry;
    entry.id = object.value(QStringLiteral("id")).toInt();
    entry.at =
        QDateTime::fromString(object.value(QStringLiteral("at")).toString(), Qt::ISODate).toUTC();
    entry.hash = object.value(QStringLiteral("hash")).toString();
    const auto what = whatOf(object.value(QStringLiteral("what")).toString());
    entry.what = what.value_or(What::Torrent);
    entry.was = object.value(QStringLiteral("was")).toString();
    entry.now = object.value(QStringLiteral("now")).toString();
    entry.count = std::max(1, object.value(QStringLiteral("count")).toInt(1));
    entry.in = object.value(QStringLiteral("in")).toString();
    entry.folders = std::max(1, object.value(QStringLiteral("folders")).toInt(1));
    const auto by = byOf(object.value(QStringLiteral("by")).toString());
    entry.by = by.value_or(By::Omaseed);
    entry.disk = object.value(QStringLiteral("disk")).toBool();
    entry.tagged = tagOrder(fromArray(object.value(QStringLiteral("tagged"))));
    entry.untagged = tagOrder(fromArray(object.value(QStringLiteral("untagged"))));
    entry.landed = object.value(QStringLiteral("landed")).toBool(true);
    entry.cause = object.value(QStringLiteral("cause")).toString();
    entry.causeName = object.value(QStringLiteral("causeName")).toString();
    entry.removed = object.value(QStringLiteral("removed")).toBool(false);
    if (ok)
        *ok = !entry.hash.isEmpty() && entry.at.isValid() && what && by;
    return entry;
}

int StoryRecord::record(const StoryEntry &entry)
{
    StoryEntry line = entry;
    line.at = entry.at.toUTC();
    if (line.what == What::Tags) {
        line.tagged = tagOrder(line.tagged);
        line.untagged = tagOrder(line.untagged);
    }
    if (line.hash.isEmpty() || line.isNoChange())
        return 0;
    QList<StoryEntry> &lines = m_byHash[line.hash];
    if (!lines.isEmpty() && folds(lines.constFirst(), line)) {
        StoryEntry &newest = lines.first();
        fold(newest, line);
        if (!newest.isNoChange())
            return newest.id;
        // A run that ends where it started leaves no line (C38 R6, C43 Y9).
        lines.removeFirst();
        if (lines.isEmpty())
            m_byHash.remove(line.hash);
        return 0;
    }
    line.id = m_nextId++;
    lines.prepend(line);
    cap(lines, groupOf(line.what));
    return line.id;
}

bool StoryRecord::remove(const QString &hash, int id)
{
    const auto it = m_byHash.find(hash);
    if (it == m_byHash.end())
        return false;
    const qsizetype removed =
        it->removeIf([id](const StoryEntry &entry) { return entry.id == id; });
    if (it->isEmpty())
        m_byHash.erase(it);
    return removed > 0;
}

bool StoryRecord::prune(const QSet<QString> &hashesInLibrary)
{
    return m_byHash.removeIf(
               [&hashesInLibrary](const std::pair<const QString &, QList<StoryEntry> &> &entry) {
                   return !hashesInLibrary.contains(entry.first);
               }) > 0;
}

QList<StoryEntry> StoryRecord::recordsFor(const QString &hash) const
{
    return m_byHash.value(hash);
}

QJsonArray StoryRecord::toJson() const
{
    QStringList hashes = m_byHash.keys();
    hashes.sort();
    QJsonArray out;
    for (const QString &hash : std::as_const(hashes)) {
        for (const StoryEntry &entry : m_byHash.value(hash))
            out.append(entry.toJson());
    }
    return out;
}

StoryRecord StoryRecord::fromJson(const QJsonArray &array)
{
    StoryRecord record;
    QSet<int> ids;
    QList<StoryEntry *> needId;
    for (const QJsonValue &value : array) {
        if (!value.isObject())
            continue;
        bool ok = false;
        StoryEntry entry = StoryEntry::fromJson(value.toObject(), &ok);
        // An unknown kind is skipped, as an older omaseed skips this one's new kinds (C43 Y10).
        if (!ok || entry.isNoChange())
            continue;
        record.m_byHash[entry.hash].append(std::move(entry));
    }
    for (auto it = record.m_byHash.begin(); it != record.m_byHash.end(); ++it) {
        QList<StoryEntry> &lines = *it;
        std::stable_sort(
            lines.begin(), lines.end(), [](const auto &a, const auto &b) { return a.at > b.at; });
        for (Group group : {Group::Names, Group::Filing, Group::Places})
            cap(lines, group);
        for (StoryEntry &entry : lines) {
            if (entry.id <= 0 || ids.contains(entry.id))
                needId << &entry;
            else
                ids.insert(entry.id);
        }
    }
    int top = 0;
    for (int id : std::as_const(ids))
        top = std::max(top, id);
    record.m_nextId = top + 1;
    // An entry saved without an id, or with one already taken, gets a fresh one.
    for (StoryEntry *entry : std::as_const(needId))
        entry->id = record.m_nextId++;
    return record;
}

} // namespace omaseed::core
