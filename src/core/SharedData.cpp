#include "core/SharedData.hpp"

#include <QSet>

#include <algorithm>

namespace omaseed::core {

namespace {

/// A tiny union-find over row indexes, for the groups that share data.
class Groups
{
public:
    explicit Groups(qsizetype count)
        : m_parent(count)
    {
        for (qsizetype i = 0; i < count; ++i)
            m_parent[i] = i;
    }

    qsizetype root(qsizetype i)
    {
        while (m_parent[i] != i) {
            m_parent[i] = m_parent[m_parent[i]];
            i = m_parent[i];
        }
        return i;
    }

    void join(qsizetype a, qsizetype b) { m_parent[root(a)] = root(b); }

private:
    QList<qsizetype> m_parent;
};

/// Sum of `members`' sizes with each connected group counted once, at its largest member: a
/// nested torrent's data is inside its parent's, and equal ones are the same data.
qint64
sizeOnce(const QList<qsizetype> &members, const QList<SharedDataRow> &library, Groups &groups)
{
    QHash<qsizetype, qint64> largest;
    for (const qsizetype i : members) {
        const qsizetype r = groups.root(i);
        largest[r] = std::max(largest.value(r, 0), library.at(i).size);
    }
    qint64 total = 0;
    for (const qint64 size : std::as_const(largest))
        total += size;
    return total;
}

} // namespace

QString SharedData::normalize(const QString &path)
{
    QString result = path;
    result.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (result.size() > 1 && result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

bool SharedData::isUnder(const QString &path, const QString &parent)
{
    if (parent.isEmpty() || !path.startsWith(parent))
        return false;
    if (path.size() == parent.size())
        return true;
    if (parent.endsWith(QLatin1Char('/'))) // "/" itself
        return true;
    return path.at(parent.size()) == QLatin1Char('/');
}

bool SharedData::hasNoRoot(const SharedDataRow &row)
{
    const QString content = normalize(row.contentPath);
    return !content.isEmpty() && content == normalize(row.savePath);
}

SharedData::Relation SharedData::relate(const SharedDataRow &a, const SharedDataRow &b)
{
    if (a.hash == b.hash)
        return Relation::None;
    const QString ca = normalize(a.contentPath);
    const QString cb = normalize(b.contentPath);
    // A magnet still fetching its metadata has no content yet: nothing on disk to share.
    if (ca.isEmpty() || cb.isEmpty())
        return Relation::None;

    const bool aNoRoot = hasNoRoot(a);
    const bool bNoRoot = hasNoRoot(b);
    const bool sameSave = normalize(a.savePath) == normalize(b.savePath);

    // A torrent without a root folder "contains" its whole save folder by path, so a path
    // test alone would make it share with every neighbour. Beside another torrent in the same
    // save path, only the two file lists can tell.
    if ((aNoRoot || bNoRoot) && sameSave)
        return Relation::NeedsFiles;
    if (ca == cb)
        return Relation::Same;
    // Nested only counts when the outer one has a root of its own: a no-root torrent's
    // content path is a folder it merely saves into.
    if ((isUnder(cb, ca) && !aNoRoot) || (isUnder(ca, cb) && !bNoRoot))
        return Relation::Nested;
    return Relation::None;
}

bool SharedData::filesOverlap(const SharedDataRow &a,
                              const QStringList &aFiles,
                              const SharedDataRow &b,
                              const QStringList &bFiles)
{
    const auto absolute = [](const SharedDataRow &row, const QString &name) {
        return normalize(normalize(row.savePath) + QLatin1Char('/') + normalize(name));
    };
    QSet<QString> seen;
    seen.reserve(aFiles.size());
    for (const QString &name : aFiles)
        seen.insert(absolute(a, name));
    for (const QString &name : bFiles) {
        if (seen.contains(absolute(b, name)))
            return true;
    }
    return false;
}

SharedData::Result SharedData::find(const QStringList &selected,
                                    const QList<SharedDataRow> &library,
                                    const QHash<QString, QStringList> &files)
{
    Result result;

    QHash<QString, qsizetype> indexOf;
    for (qsizetype i = 0; i < library.size(); ++i)
        indexOf.insert(library.at(i).hash, i);

    QList<qsizetype> chosen; // selected rows that are in the library, in selection order
    QSet<qsizetype> chosenSet;
    for (const QString &hash : selected) {
        const qsizetype i = indexOf.value(hash, -1);
        if (i < 0 || chosenSet.contains(i))
            continue;
        chosen.append(i);
        chosenSet.insert(i);
    }

    Groups groups(library.size());
    QSet<qsizetype> outsideSet;
    QSet<QString> missing;

    const auto shares = [&](qsizetype i, qsizetype j) {
        const SharedDataRow &a = library.at(i);
        const SharedDataRow &b = library.at(j);
        switch (relate(a, b)) {
        case Relation::None:
            return false;
        case Relation::Same:
        case Relation::Nested:
            return true;
        case Relation::NeedsFiles:
            break;
        }
        const bool haveA = files.contains(a.hash);
        const bool haveB = files.contains(b.hash);
        if (!haveA)
            missing.insert(a.hash);
        if (!haveB)
            missing.insert(b.hash);
        return haveA && haveB && filesOverlap(a, files.value(a.hash), b, files.value(b.hash));
    };

    for (const qsizetype i : std::as_const(chosen)) {
        QStringList outsideOfThis;
        for (qsizetype j = 0; j < library.size(); ++j) {
            if (j == i)
                continue;
            // Each selected pair is settled once, from its earlier member.
            if (chosenSet.contains(j) && j < i)
                continue;
            if (!shares(i, j))
                continue;
            groups.join(i, j);
            if (!chosenSet.contains(j)) {
                outsideOfThis.append(library.at(j).hash);
                outsideSet.insert(j);
            }
        }
        if (!outsideOfThis.isEmpty()) {
            const QString &hash = library.at(i).hash;
            result.sharing.append(hash);
            result.outsideOf.insert(hash, outsideOfThis);
        }
    }

    QList<qsizetype> outsideRows(outsideSet.cbegin(), outsideSet.cend());
    std::sort(outsideRows.begin(), outsideRows.end());
    for (const qsizetype j : std::as_const(outsideRows))
        result.outside.append(library.at(j).hash);

    // Groups made of selected torrents only, through selected-to-selected links: joined
    // again on their own, so an outside sharer does not glue two unrelated ones together.
    Groups insideGroups(library.size());
    for (qsizetype a = 0; a < chosen.size(); ++a) {
        for (qsizetype b = a + 1; b < chosen.size(); ++b) {
            const qsizetype i = chosen.at(a);
            const qsizetype j = chosen.at(b);
            if (groups.root(i) == groups.root(j) && shares(i, j))
                insideGroups.join(i, j);
        }
    }
    QHash<qsizetype, QStringList> byRoot;
    QList<qsizetype> rootOrder;
    for (const qsizetype i : std::as_const(chosen)) {
        const qsizetype r = insideGroups.root(i);
        if (!byRoot.contains(r))
            rootOrder.append(r);
        byRoot[r].append(library.at(i).hash);
    }
    qint64 plainSize = 0;
    for (const qsizetype i : std::as_const(chosen))
        plainSize += library.at(i).size;
    for (const qsizetype r : std::as_const(rootOrder)) {
        if (byRoot.value(r).size() >= 2)
            result.inside.append(byRoot.value(r));
    }

    result.diskSize = sizeOnce(chosen, library, insideGroups);
    result.insideSize = plainSize - result.diskSize;
    result.diskSizeWithOutside = sizeOnce(chosen + outsideRows, library, groups);

    for (const SharedDataRow &row : library) {
        if (missing.contains(row.hash))
            result.needFiles.append(row.hash);
    }
    return result;
}

} // namespace omaseed::core
