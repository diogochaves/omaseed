#include "core/MoveHistory.hpp"

#include "core/SharedData.hpp"

#include <algorithm>

namespace omaseed::core {

namespace {

QJsonObject pathsJson(const QHash<QString, QString> &paths)
{
    QJsonObject out;
    for (auto it = paths.cbegin(); it != paths.cend(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QHash<QString, QString> pathsOf(const QJsonValue &value)
{
    QHash<QString, QString> out;
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it)
        out.insert(it.key(), it.value().toString());
    return out;
}

QString norm(const QString &path)
{
    return SharedData::normalize(path);
}

} // namespace

QJsonObject MoveRecord::toJson() const
{
    QJsonObject out{{QStringLiteral("id"), id},
                    {QStringLiteral("kind"), kind},
                    {QStringLiteral("title"), title},
                    {QStringLiteral("newPath"), newPath},
                    {QStringLiteral("startedAt"), startedAt.toString(Qt::ISODate)},
                    {QStringLiteral("finishedAt"), finishedAt.toString(Qt::ISODate)},
                    {QStringLiteral("from"), pathsJson(from)},
                    {QStringLiteral("to"), pathsJson(to)},
                    {QStringLiteral("names"), pathsJson(names)}};
    if (!hadAutoTmm.isEmpty())
        out.insert(QStringLiteral("hadAutoTmm"), QJsonArray::fromStringList(hadAutoTmm));
    if (carried > 0) {
        out.insert(QStringLiteral("carried"), carried);
        out.insert(QStringLiteral("carriedTo"), carriedTo);
    }
    if (!replaced.isEmpty())
        out.insert(QStringLiteral("replaced"), QJsonArray::fromStringList(replaced));
    return out;
}

MoveRecord MoveRecord::fromJson(const QJsonObject &object)
{
    MoveRecord record;
    record.id = object.value(QStringLiteral("id")).toString();
    record.kind = object.value(QStringLiteral("kind")).toString();
    record.title = object.value(QStringLiteral("title")).toString();
    record.newPath = object.value(QStringLiteral("newPath")).toString();
    record.startedAt =
        QDateTime::fromString(object.value(QStringLiteral("startedAt")).toString(), Qt::ISODate);
    record.finishedAt =
        QDateTime::fromString(object.value(QStringLiteral("finishedAt")).toString(), Qt::ISODate);
    record.from = pathsOf(object.value(QStringLiteral("from")));
    record.to = pathsOf(object.value(QStringLiteral("to")));
    record.names = pathsOf(object.value(QStringLiteral("names")));
    record.hadAutoTmm = object.value(QStringLiteral("hadAutoTmm")).toVariant().toStringList();
    record.carried = object.value(QStringLiteral("carried")).toInt();
    record.carriedTo = object.value(QStringLiteral("carriedTo")).toString();
    record.replaced = object.value(QStringLiteral("replaced")).toVariant().toStringList();
    return record;
}

void MoveHistory::add(MoveRecord record, const QDateTime &now)
{
    if (record.from.isEmpty())
        return;
    m_records.prepend(std::move(record));
    prune(now);
}

bool MoveHistory::prune(const QDateTime &now)
{
    const qsizetype before = m_records.size();
    const QDateTime oldest = now.addDays(-kMaxDays);
    m_records.removeIf([&oldest](const MoveRecord &record) {
        return record.finishedAt.isValid() && record.finishedAt < oldest;
    });
    while (m_records.size() > kMaxMoves)
        m_records.removeLast();
    return m_records.size() != before;
}

int MoveHistory::indexOf(const QString &id) const
{
    for (int i = 0; i < m_records.size(); ++i) {
        if (m_records.at(i).id == id)
            return i;
    }
    return -1;
}

const MoveRecord *MoveHistory::find(const QString &id) const
{
    const int at = indexOf(id);
    return at < 0 ? nullptr : &m_records.at(at);
}

MoveRecord *MoveHistory::find(const QString &id)
{
    const int at = indexOf(id);
    return at < 0 ? nullptr : &m_records[at];
}

const MoveRecord *MoveHistory::newest() const
{
    return m_records.isEmpty() ? nullptr : &m_records.constFirst();
}

const MoveRecord *MoveHistory::lastFor(const QString &hash) const
{
    for (const MoveRecord &record : m_records) {
        if (record.from.contains(hash))
            return &record;
    }
    return nullptr;
}

bool MoveHistory::backDone(const QString &id, const QStringList &hashes)
{
    const int at = indexOf(id);
    if (at < 0)
        return false;
    MoveRecord &record = m_records[at];
    bool changed = false;
    for (const QString &hash : hashes) {
        changed = record.from.remove(hash) > 0 || changed;
        record.to.remove(hash);
        record.names.remove(hash);
        record.hadAutoTmm.removeAll(hash);
    }
    if (record.from.isEmpty()) {
        m_records.removeAt(at);
        return true;
    }
    return changed;
}

int MoveHistory::BackCheck::stay() const
{
    int count = int(outside.size() + gone.size());
    for (const auto &group : movedAgain)
        count += int(group.second.size());
    return count;
}

MoveHistory::BackCheck MoveHistory::check(const QString &id,
                                          const QHash<QString, QString> &savePaths,
                                          const QStringList &only) const
{
    BackCheck out;
    const int at = indexOf(id);
    if (at < 0)
        return out;
    const MoveRecord &record = m_records.at(at);
    QStringList hashes = record.from.keys();
    hashes.sort();
    for (const QString &hash : std::as_const(hashes)) {
        if (!only.isEmpty() && !only.contains(hash))
            continue;
        const auto now = savePaths.constFind(hash);
        if (now == savePaths.cend()) {
            out.gone << hash;
            continue;
        }
        const QString here = norm(*now);
        if (here == norm(record.to.value(hash))) {
            out.goBack << hash;
            continue;
        }
        // The later move that put it where it is now: newer records come first.
        QString by;
        for (int i = 0; i < at; ++i) {
            const MoveRecord &later = m_records.at(i);
            if (later.from.contains(hash) && norm(later.to.value(hash)) == here) {
                by = later.id;
                break;
            }
        }
        if (by.isEmpty()) {
            out.outside << hash;
            continue;
        }
        auto group = std::find_if(out.movedAgain.begin(),
                                  out.movedAgain.end(),
                                  [&by](const auto &entry) { return entry.first == by; });
        if (group == out.movedAgain.end())
            out.movedAgain.append({by, {hash}});
        else
            group->second << hash;
    }
    // Newest move first, as the history reads.
    std::sort(out.movedAgain.begin(), out.movedAgain.end(), [this](const auto &a, const auto &b) {
        return indexOf(a.first) < indexOf(b.first);
    });
    return out;
}

QJsonArray MoveHistory::toJson() const
{
    QJsonArray out;
    for (const MoveRecord &record : m_records)
        out.append(record.toJson());
    return out;
}

MoveHistory MoveHistory::fromJson(const QJsonArray &array)
{
    MoveHistory history;
    for (const QJsonValue &value : array) {
        MoveRecord record = MoveRecord::fromJson(value.toObject());
        if (!record.id.isEmpty() && !record.from.isEmpty())
            history.m_records.append(std::move(record));
    }
    return history;
}

} // namespace omaseed::core
