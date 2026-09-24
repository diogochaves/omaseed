#pragma once

// FilterCounts — the numbers the rail prints next to every row (spec §1: "downloading 6",
// "movies 41", "trackerless 4").
//
// One pass over TorrentModel per poll, triggered by its updated() signal. A poll changes so
// many rows at once that an incremental scheme would cost more bookkeeping than the pass:
// the pass is O(n) with one hash lookup per category, tag and tracker, and tst_filtercounts
// holds it to 20 ms at 10 000 torrents in a Release build.

#include "core/Torrent.hpp"

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>

namespace omaseed::core {

class TorrentModel;

class FilterCounts : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int total READ total NOTIFY changed)
    Q_PROPERTY(int downloading READ downloading NOTIFY changed)
    Q_PROPERTY(int inProgress READ inProgress NOTIFY changed)
    Q_PROPERTY(int seeding READ seeding NOTIFY changed)
    Q_PROPERTY(int completed READ completed NOTIFY changed)
    Q_PROPERTY(int stopped READ stopped NOTIFY changed)
    Q_PROPERTY(int active READ active NOTIFY changed)
    Q_PROPERTY(int stalled READ stalled NOTIFY changed)
    Q_PROPERTY(int checking READ checking NOTIFY changed)
    Q_PROPERTY(int errored READ errored NOTIFY changed)
    Q_PROPERTY(int uncategorised READ uncategorised NOTIFY changed)
    Q_PROPERTY(int untagged READ untagged NOTIFY changed)
    Q_PROPERTY(int trackerless READ trackerless NOTIFY changed)
    /// Moves on every `changed`. The per-name lookups below are methods, which a QML binding
    /// does not re-run by itself; a binding that passes `revision` along does.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit FilterCounts(QObject *parent = nullptr);

    /// Watches `model`'s updated() signal; pass nullptr to detach.
    void setModel(TorrentModel *model);
    TorrentModel *model() const { return m_model; }

    int total() const { return m_total; }
    int downloading() const { return count(StatusGroup::Downloading); }
    /// Downloading and not stopped: what the footer's "downloading" fact counts. The rail's
    /// row keeps qBittorrent's filter, which also takes stoppedDL (Q16); the footer is a
    /// status line, where a stopped torrent does not read as downloading.
    int inProgress() const { return m_inProgress; }
    int seeding() const { return count(StatusGroup::Seeding); }
    int completed() const { return count(StatusGroup::Completed); }
    int stopped() const { return count(StatusGroup::Stopped); }
    int active() const { return count(StatusGroup::Active); }
    int stalled() const { return count(StatusGroup::Stalled); }
    int checking() const { return count(StatusGroup::Checking); }
    int errored() const { return count(StatusGroup::Errored); }
    int uncategorised() const { return m_categoryCounts.value(QString()); }
    int untagged() const { return m_untagged; }
    int trackerless() const { return m_trackerCounts.value(trackerlessKey()); }
    int revision() const { return m_revision; }

    int count(StatusGroup group) const;
    /// The rail's status rows; StatusGroup::NoGroup stands for "all".
    Q_INVOKABLE int statusCount(int group) const;
    /// An empty name is the rail's `none` row.
    Q_INVOKABLE int categoryCount(const QString &name) const;
    /// An empty tag is "untagged".
    Q_INVOKABLE int tagCount(const QString &tag) const;
    /// "trackerless" is a valid host here.
    Q_INVOKABLE int trackerCount(const QString &host) const;

    QHash<QString, int> categoryCounts() const { return m_categoryCounts; }
    QHash<QString, int> tagCounts() const { return m_tagCounts; }
    QHash<QString, int> trackerCounts() const { return m_trackerCounts; }
    /// The hashes with no working tracker, for TrackerModel's trackerless row.
    QStringList trackerlessHashes() const { return m_trackerlessHashes; }

public Q_SLOTS:
    /// One pass over the model. Safe to call by hand; SyncEngine never has to.
    void recompute();

Q_SIGNALS:
    void changed();

private:
    TorrentModel *m_model = nullptr;

    int m_total = 0;
    int m_untagged = 0;
    int m_inProgress = 0;
    int m_revision = 0;
    int m_groupCounts[8] = {};
    QHash<QString, int> m_categoryCounts;
    QHash<QString, int> m_tagCounts;
    QHash<QString, int> m_trackerCounts;
    QStringList m_trackerlessHashes;
    QHash<QString, QString> m_hostCache; ///< announce URL → host, so QUrl parses once
};

} // namespace omaseed::core
