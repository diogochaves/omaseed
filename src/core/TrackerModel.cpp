#include "core/TrackerModel.hpp"

#include "core/Torrent.hpp"

#include <QCollator>
#include <QSet>

#include <algorithm>

namespace omaseed::core {

namespace {

bool hostLessThan(const TrackerGroup &a, const TrackerGroup &b)
{
    // "trackerless" is a bucket, not a tracker: it sits at the foot of the group.
    const bool aLast = a.host == trackerlessKey();
    const bool bLast = b.host == trackerlessKey();
    if (aLast != bLast)
        return bLast;

    static QCollator collator = [] {
        QCollator c;
        c.setCaseSensitivity(Qt::CaseInsensitive);
        c.setNumericMode(true);
        return c;
    }();
    const int compared = collator.compare(a.host, b.host);
    return compared != 0 ? compared < 0 : a.host < b.host;
}

} // namespace

TrackerModel::TrackerModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int TrackerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_groups.size());
}

QVariant TrackerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_groups.size())
        return {};
    const TrackerGroup &group = m_groups.at(index.row());
    switch (role) {
    case Qt::DisplayRole:
    case HostRole:
        return group.host;
    case UrlsRole:
        return group.urls;
    case HashesRole:
        return group.hashes;
    case TorrentCountRole:
        return int(group.hashes.size());
    case IsTrackerlessRole:
        return group.host == trackerlessKey();
    default:
        return {};
    }
}

QHash<int, QByteArray> TrackerModel::roleNames() const
{
    return {{HostRole, "host"},
            {UrlsRole, "urls"},
            {HashesRole, "hashes"},
            {TorrentCountRole, "torrentCount"},
            {IsTrackerlessRole, "isTrackerless"}};
}

QStringList TrackerModel::hosts() const
{
    QStringList list;
    list.reserve(int(m_groups.size()));
    for (const TrackerGroup &group : m_groups)
        list.append(group.host);
    return list;
}

QStringList TrackerModel::hashesFor(const QString &host) const
{
    for (const TrackerGroup &group : m_groups) {
        if (group.host == host)
            return group.hashes;
    }
    return {};
}

void TrackerModel::applyFullUpdate(const QHash<QString, QStringList> &trackers)
{
    m_byUrl = trackers;
    rebuild();
}

void TrackerModel::applyPartial(const QHash<QString, QStringList> &changed,
                                const QStringList &removed)
{
    if (changed.isEmpty() && removed.isEmpty())
        return;

    for (const QString &url : removed)
        m_byUrl.remove(url);
    for (auto it = changed.constBegin(); it != changed.constEnd(); ++it) {
        const QStringList &hashes = it.value();
        if (hashes.isEmpty())
            m_byUrl.remove(it.key());
        else
            m_byUrl.insert(it.key(), hashes);
    }
    rebuild();
}

void TrackerModel::setTrackerlessHashes(const QStringList &hashes)
{
    if (m_trackerless == hashes)
        return;
    m_trackerless = hashes;
    rebuild();
}

void TrackerModel::clear()
{
    m_byUrl.clear();
    m_hostCache.clear();
    m_trackerless.clear();
    rebuild();
}

void TrackerModel::rebuild()
{
    // A set per host, so a tracker with thousands of torrents over several announce URLs
    // stays linear; a QStringList::contains here was quadratic.
    QHash<QString, TrackerGroup> byHost;
    QHash<QString, QSet<QString>> hashesByHost;
    for (auto it = m_byUrl.constBegin(); it != m_byUrl.constEnd(); ++it) {
        auto cached = m_hostCache.constFind(it.key());
        if (cached == m_hostCache.constEnd())
            cached = m_hostCache.insert(it.key(), trackerHost(it.key()));
        const QString &host = *cached;
        TrackerGroup &group = byHost[host];
        group.host = host;
        group.urls.append(it.key());
        QSet<QString> &hashes = hashesByHost[host];
        for (const QString &hash : it.value())
            hashes.insert(hash);
    }

    QList<TrackerGroup> next;
    next.reserve(byHost.size() + 1);
    for (TrackerGroup &group : byHost) {
        const QSet<QString> &hashes = hashesByHost.value(group.host);
        group.hashes = QStringList(hashes.cbegin(), hashes.cend());
        std::sort(group.urls.begin(), group.urls.end());
        std::sort(group.hashes.begin(), group.hashes.end());
        next.append(group);
    }
    if (!m_trackerless.isEmpty()) {
        TrackerGroup group;
        group.host = trackerlessKey();
        group.hashes = m_trackerless;
        std::sort(group.hashes.begin(), group.hashes.end());
        next.append(group);
    }
    std::sort(next.begin(), next.end(), hostLessThan);

    if (next == m_groups)
        return;

    const bool sameHosts =
        next.size() == m_groups.size() &&
        std::equal(next.begin(),
                   next.end(),
                   m_groups.begin(),
                   [](const TrackerGroup &a, const TrackerGroup &b) { return a.host == b.host; });
    if (sameHosts) {
        m_groups = std::move(next);
        Q_EMIT dataChanged(index(0), index(int(m_groups.size()) - 1));
        return;
    }

    beginResetModel();
    m_groups = std::move(next);
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace omaseed::core
