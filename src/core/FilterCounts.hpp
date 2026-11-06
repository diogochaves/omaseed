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
#include <QPointer>
#include <QString>
#include <QStringList>

namespace omaseed::core {

class LocalDay;
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
    // DECISIONS C24 H2: the rail's five questions, one count each (TorrentFilterModel::
    // questionOf(), so the rail's number and the rows it shows always agree).
    Q_PROPERTY(int needsYou READ needsYou NOTIFY changed)
    Q_PROPERTY(int arriving READ arriving NOTIFY changed)
    Q_PROPERTY(int landedToday READ landedToday NOTIFY changed)
    Q_PROPERTY(int givingBack READ givingBack NOTIFY changed)
    Q_PROPERTY(int resting READ resting NOTIFY changed)
    /// Torrents with the private flag: the hero warns about anonymous mode only when there
    /// is one (docs/16 §5).
    Q_PROPERTY(int privateCount READ privateCount NOTIFY changed)
    /// Moves on every `changed`. The per-name lookups below are methods, which a QML binding
    /// does not re-run by itself; a binding that passes `revision` along does.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit FilterCounts(QObject *parent = nullptr);

    /// Watches `model`'s updated() signal; pass nullptr to detach.
    void setModel(TorrentModel *model);
    TorrentModel *model() const { return m_model; }
    /// Where "today" comes from for landed today; not owned. A new day counts again.
    /// Without one, the system clock at each pass.
    void setDay(LocalDay *day);

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
    int privateCount() const { return m_private; }
    int trackerless() const { return m_trackerCounts.value(trackerlessKey()); }
    int revision() const { return m_revision; }
    int needsYou() const { return m_questionCounts[1]; }
    int arriving() const { return m_questionCounts[2]; }
    int landedToday() const { return m_questionCounts[3]; }
    int givingBack() const { return m_questionCounts[4]; }
    int resting() const { return m_questionCounts[5]; }
    /// A TorrentFilterModel::Question's count; AnyQuestion is the total.
    Q_INVOKABLE int questionCount(int question) const;

    int count(StatusGroup group) const;
    /// The rail's status rows; StatusGroup::NoGroup stands for "all".
    Q_INVOKABLE int statusCount(int group) const;
    /// An empty name is the rail's `none` row.
    Q_INVOKABLE int categoryCount(const QString &name) const;
    /// An empty tag is "untagged".
    Q_INVOKABLE int tagCount(const QString &tag) const;
    /// "trackerless" (no tracker at all, `trackers_count == 0`) is a valid host here; any
    /// other host counts the torrents currently announcing to it. The rail's host rows use
    /// TrackerModel instead, which counts every announce URL.
    Q_INVOKABLE int trackerCount(const QString &host) const;

    QHash<QString, int> categoryCounts() const { return m_categoryCounts; }
    QHash<QString, int> tagCounts() const { return m_tagCounts; }
    QHash<QString, int> trackerCounts() const { return m_trackerCounts; }
    /// The hashes with no tracker at all, for TrackerModel's trackerless row.
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
    int m_private = 0;
    int m_inProgress = 0;
    int m_revision = 0;
    int m_groupCounts[8] = {};
    int m_questionCounts[6] = {}; ///< by TorrentFilterModel::Question; [0] unused
    QPointer<LocalDay> m_day;
    QHash<QString, int> m_categoryCounts;
    QHash<QString, int> m_tagCounts;
    QHash<QString, int> m_trackerCounts;
    QStringList m_trackerlessHashes;
    QHash<QString, QString> m_hostCache; ///< announce URL → host, so QUrl parses once
};

} // namespace omaseed::core
