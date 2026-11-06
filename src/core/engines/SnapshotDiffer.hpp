#pragma once

// SnapshotDiffer — makes deltas for an engine that has none (docs/36 §4.2, DECISIONS C14 E2 a:
// "engines without them go through a shared differ"). Transmission answers with whole torrent
// objects (every torrent, or those active in its last 60 s); Deluge with the whole list every
// time. The differ keeps what it last handed out and turns each answer into the SyncDelta
// SyncEngine already takes from qBittorrent's maindata: only the fields that moved, the
// torrents that went, and the labels and tracker URLs that came or went with them.
//
// Labels go in the tags place (C14 E5); an engine whose labels exist only on torrents
// (Transmission) has a label for as long as one torrent carries it. The tracker map is URL →
// torrents, as maindata's `trackers`.

#include "core/engines/SyncDelta.hpp"

#include <QHash>
#include <QStringList>

namespace omaseed::core {

class SnapshotDiffer
{
public:
    /// One torrent as the engine has it now: a patch with every field it carries, and the
    /// announce URLs of its trackers (for the rail's map).
    struct Entry
    {
        TorrentPatch patch;
        QStringList trackers;
    };

    /// Every torrent the engine has. `full` hands it all out as a full delta (what the models
    /// hold is replaced); otherwise only what moved since the last answer, and every torrent
    /// it knew that is not listed now as removed.
    SyncDelta everything(const QList<Entry> &entries, bool full);
    /// Some torrents, whole, and the hashes of those removed since: what moved among them.
    SyncDelta some(const QList<Entry> &entries, const QStringList &removed);

    /// Forgets everything (another server, a sign-in): the next answer starts over.
    void clear();
    bool isEmpty() const { return m_known.isEmpty(); }
    bool knows(const QString &hash) const { return m_known.contains(hash); }
    /// The hashes it holds, in the order they first came.
    QStringList hashes() const { return m_order; }
    /// What it holds of one torrent (a default Torrent when it holds nothing).
    Torrent torrent(const QString &hash) const { return m_known.value(hash).torrent; }

private:
    struct Known
    {
        Torrent torrent;
        QStringList trackers;
    };

    /// Takes one entry: what changed of it goes into `delta`.
    void take(const Entry &entry, SyncDelta &delta);
    void drop(const QString &hash);
    void countLabels(const QStringList &labels, int by);
    void countTrackers(const QString &hash, const QStringList &urls, bool add);
    /// The labels and URLs that came or went since `labels`/`urls`, into `delta`.
    void finish(SyncDelta &delta,
                const QHash<QString, int> &labelsBefore,
                const QHash<QString, QStringList> &trackersBefore) const;

    QHash<QString, Known> m_known;
    QStringList m_order;
    QHash<QString, int> m_labels;           ///< label → how many torrents carry it
    QHash<QString, QStringList> m_trackers; ///< announce URL → torrents
};

} // namespace omaseed::core
