#include "core/engines/SnapshotDiffer.hpp"

#include <QSet>

namespace omaseed::core {

SyncDelta SnapshotDiffer::everything(const QList<Entry> &entries, bool full)
{
    if (full)
        clear();
    const QHash<QString, int> labelsBefore = m_labels;
    const QHash<QString, QStringList> trackersBefore = m_trackers;

    SyncDelta delta;
    delta.full = full;
    QSet<QString> listed;
    for (const Entry &entry : entries) {
        listed.insert(entry.patch.hash);
        take(entry, delta);
    }
    if (!full) {
        const QStringList known = m_order;
        for (const QString &hash : known) {
            if (!listed.contains(hash)) {
                drop(hash);
                delta.removedTorrents.append(hash);
            }
        }
    }
    finish(delta, labelsBefore, trackersBefore);
    return delta;
}

SyncDelta SnapshotDiffer::some(const QList<Entry> &entries, const QStringList &removed)
{
    const QHash<QString, int> labelsBefore = m_labels;
    const QHash<QString, QStringList> trackersBefore = m_trackers;

    SyncDelta delta;
    for (const QString &hash : removed) {
        if (!m_known.contains(hash))
            continue;
        drop(hash);
        delta.removedTorrents.append(hash);
    }
    for (const Entry &entry : entries)
        take(entry, delta);
    finish(delta, labelsBefore, trackersBefore);
    return delta;
}

void SnapshotDiffer::clear()
{
    m_known.clear();
    m_order.clear();
    m_labels.clear();
    m_trackers.clear();
}

void SnapshotDiffer::take(const Entry &entry, SyncDelta &delta)
{
    const QString &hash = entry.patch.hash;
    if (hash.isEmpty())
        return;
    auto it = m_known.find(hash);
    if (it == m_known.end()) {
        Known known;
        known.torrent.hash = hash;
        known.torrent.apply(entry.patch);
        known.trackers = entry.trackers;
        countLabels(known.torrent.tags, +1);
        countTrackers(hash, known.trackers, true);
        m_known.insert(hash, known);
        m_order.append(hash);
        delta.torrents.append(entry.patch); // new: everything it has
        return;
    }

    const QStringList labelsBefore = it->torrent.tags;
    const TorrentFields changed = it->torrent.apply(entry.patch);
    if (changed.has(TorrentField::Tags)) {
        countLabels(labelsBefore, -1);
        countLabels(it->torrent.tags, +1);
    }
    if (it->trackers != entry.trackers) {
        countTrackers(hash, it->trackers, false);
        it->trackers = entry.trackers;
        countTrackers(hash, it->trackers, true);
    }
    if (changed.isEmpty())
        return;
    TorrentPatch moved(hash);
    moved.fields = changed;
    moved.values = it->torrent;
    delta.torrents.append(moved);
}

void SnapshotDiffer::drop(const QString &hash)
{
    const auto it = m_known.constFind(hash);
    if (it == m_known.constEnd())
        return;
    countLabels(it->torrent.tags, -1);
    countTrackers(hash, it->trackers, false);
    m_known.remove(hash);
    m_order.removeOne(hash);
}

void SnapshotDiffer::countLabels(const QStringList &labels, int by)
{
    for (const QString &label : labels) {
        const int count = m_labels.value(label) + by;
        if (count > 0)
            m_labels.insert(label, count);
        else
            m_labels.remove(label);
    }
}

void SnapshotDiffer::countTrackers(const QString &hash, const QStringList &urls, bool add)
{
    for (const QString &url : urls) {
        QStringList &hashes = m_trackers[url];
        if (add) {
            if (!hashes.contains(hash))
                hashes.append(hash);
        } else {
            hashes.removeAll(hash);
        }
        if (hashes.isEmpty())
            m_trackers.remove(url);
    }
}

void SnapshotDiffer::finish(SyncDelta &delta,
                            const QHash<QString, int> &labelsBefore,
                            const QHash<QString, QStringList> &trackersBefore) const
{
    if (delta.full) {
        delta.tags = m_labels.keys();
        delta.tags.sort();
        delta.trackers = m_trackers;
        return;
    }
    for (auto it = m_labels.constBegin(); it != m_labels.constEnd(); ++it) {
        if (!labelsBefore.contains(it.key()))
            delta.tags.append(it.key());
    }
    for (auto it = labelsBefore.constBegin(); it != labelsBefore.constEnd(); ++it) {
        if (!m_labels.contains(it.key()))
            delta.removedTags.append(it.key());
    }
    delta.tags.sort();
    delta.removedTags.sort();
    for (auto it = m_trackers.constBegin(); it != m_trackers.constEnd(); ++it) {
        if (trackersBefore.value(it.key()) != it.value())
            delta.trackers.insert(it.key(), it.value());
    }
    for (auto it = trackersBefore.constBegin(); it != trackersBefore.constEnd(); ++it) {
        if (!m_trackers.contains(it.key()))
            delta.removedTrackers.append(it.key());
    }
    delta.removedTrackers.sort();
}

} // namespace omaseed::core
