#pragma once

// SeedConsole — everything the seed console draws (docs/22 §4–5), worked out from the
// library, the server's totals and the ledger, for one scope at a time.
//
// The scope is what the rail selects (the rows of a TorrentFilterModel), or a selection when
// one was handed over (setScopeHashes). Totals over the whole library use the server's own
// all-time counters, which also count torrents since deleted; over a part, the sums of its
// torrents.
//
// Every block is a plain QVariantMap / QVariantList with raw numbers and the texts the view
// prints, so the QML only lays them out. Work is done only while `active` (the console is on
// screen): the cheap blocks every `refreshMs`, the ones that read the ledger (tide, year,
// earning, the trackers' 30 days) when the scope or range changes and every `slowMs`.
//
// Each block has its own change signal, emitted only when a refresh changed what it holds:
// on a library that uploads, a 2 s refresh changes a few numbers, and a signal shared by
// every block made the view re-read and rebuild all of them each time (#50). `refreshed` and
// `slowRefreshed` say that a refresh ran, whatever it changed.
//
// A few invokables serve the library while the console is hidden: the selection bar's summary
// (T1), the last-seed check before a delete (T6), and the hero's last 60 s (T2).

#include <QDateTime>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

namespace omaseed::core {

class GlobalState;
class SeedLedger;
class TorrentFilterModel;
class TorrentModel;
class TrackerModel;
struct Torrent;

class SeedConsole : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    /// "1h", "24h", "7d", "30d" or "1y".
    Q_PROPERTY(QString range READ range WRITE setRange NOTIFY rangeChanged)
    Q_PROPERTY(QStringList ranges READ ranges CONSTANT)
    /// Seeds in the swarm at or under which a torrent counts as rare (the widget's rareSeeds).
    Q_PROPERTY(int rareSeeds READ rareSeeds WRITE setRareSeeds NOTIFY rareSeedsChanged)
    /// True while a selection, not the rail, sets the scope.
    Q_PROPERTY(bool selectionScope READ selectionScope NOTIFY scopeChanged)
    /// The scope line: how many torrents, whether it is a part, and the ledger's age.
    Q_PROPERTY(int scopeCount READ scopeCount NOTIFY scopeLineChanged)
    Q_PROPERTY(bool scoped READ scoped NOTIFY scopeLineChanged)
    Q_PROPERTY(QString ledgerSince READ ledgerSince NOTIFY scopeLineChanged)
    Q_PROPERTY(bool ledgerNew READ ledgerNew NOTIFY scopeLineChanged)

    Q_PROPERTY(QVariantMap attention READ attention NOTIFY attentionChanged)
    Q_PROPERTY(QVariantMap totals READ totals NOTIFY totalsChanged)
    Q_PROPERTY(QVariantMap tide READ tide NOTIFY tideChanged)
    Q_PROPERTY(QVariantMap year READ year NOTIFY yearChanged)
    Q_PROPERTY(QVariantMap alive READ alive NOTIFY aliveChanged)
    /// The ratio ladder's line: `{summary}`.
    Q_PROPERTY(QVariantMap ladder READ ladder NOTIFY ladderChanged)
    /// The ratio ladder's dots, `{ratio, size, name, ratioText, sizeText}` for each complete
    /// torrent in scope, biggest first. Replaced only when a dot would move or read otherwise
    /// (a ratio that moved less than 0.1 % and prints the same keeps its dot): the ladder lays
    /// out every dot again on each change, thousands on a big library (#45).
    Q_PROPERTY(QVariantList ladderPoints READ ladderPoints NOTIFY ladderPointsChanged)
    Q_PROPERTY(QVariantMap trackers READ trackers NOTIFY trackersChanged)
    Q_PROPERTY(QVariantMap earning READ earning NOTIFY earningChanged)
    Q_PROPERTY(QVariantMap holding READ holding NOTIFY holdingChanged)
    Q_PROPERTY(QVariantMap onAir READ onAir NOTIFY onAirChanged)
    Q_PROPERTY(QVariantList lately READ lately NOTIFY latelyChanged)

    /// The last 60 s of upload, oldest first, and today's total: the hero (T2), always.
    Q_PROPERTY(QVariantList heroSpark READ heroSpark NOTIFY heroChanged)
    Q_PROPERTY(QVariantList heroDownSpark READ heroDownSpark NOTIFY heroChanged)
    Q_PROPERTY(QString todayText READ todayText NOTIFY heroChanged)

public:
    explicit SeedConsole(QObject *parent = nullptr);
    ~SeedConsole() override;

    void setSources(TorrentModel *torrents,
                    TorrentFilterModel *scope,
                    TrackerModel *trackers,
                    GlobalState *state,
                    SeedLedger *ledger);
    /// The time "now" means; the system clock until set (tests).
    void setClock(std::function<QDateTime()> clock);
    /// Completions and milestones, newest first (the status file's `recent`).
    void setRecentProvider(std::function<QVariantList()> provider);
    void setIntervals(int refreshMs, int slowMs);

    bool active() const { return m_active; }
    void setActive(bool active);
    QString range() const { return m_range; }
    void setRange(const QString &range);
    static QStringList ranges();
    int rareSeeds() const { return m_rareSeeds; }
    void setRareSeeds(int seeds);
    bool selectionScope() const { return !m_scopeHashes.isEmpty(); }
    int scopeCount() const { return m_scopeCount; }
    bool scoped() const { return m_scoped; }
    QString ledgerSince() const;
    bool ledgerNew() const;

    QVariantMap attention() const { return m_attention; }
    QVariantMap totals() const { return m_totals; }
    QVariantMap tide() const { return m_tide; }
    QVariantMap year() const { return m_year; }
    QVariantMap alive() const { return m_alive; }
    QVariantMap ladder() const { return m_ladder; }
    QVariantList ladderPoints() const { return m_ladderPoints; }
    QVariantMap trackers() const { return m_trackers; }
    QVariantMap earning() const { return m_earning; }
    QVariantMap holding() const { return m_holding; }
    QVariantMap onAir() const { return m_onAir; }
    QVariantList lately() const { return m_lately; }
    QVariantList heroSpark() const { return m_heroSpark; }
    QVariantList heroDownSpark() const { return m_heroDownSpark; }
    QString todayText() const { return m_todayText; }

    /// Scope the console to these torrents (a selection); an empty list goes back to the rail.
    Q_INVOKABLE void setScopeHashes(const QStringList &hashes);
    /// The next range ("]") or the previous one ("[").
    Q_INVOKABLE void stepRange(int delta);
    /// Recompute everything now.
    Q_INVOKABLE void refresh();

    /// T1: `{count, sizeText, givenText, ratioText, ratioGood}` for the selection bar.
    Q_INVOKABLE QVariantMap selectionSummary(const QStringList &hashes) const;
    /// T6: `{onlyYou, rare, waiting, name, seeds}` over `hashes`: how many have you as their
    /// only seed, how many are rare, how many peers wait for those, and for a single torrent
    /// its name and seeds in the swarm (-1 unknown).
    Q_INVOKABLE QVariantMap rarity(const QStringList &hashes) const;
    /// T3: "only you", "2", "2 418", or "" when the swarm count is unknown.
    Q_INVOKABLE static QString swarmText(int numComplete);

    /// Seconds per bucket and buckets for a range ("24h" → 600 × 144).
    static QPair<qint64, int> rangeShape(const QString &range);
    /// Seeds in the swarm we trust: -1 when the tracker gave no count (or reported an error).
    static int knownSeeds(const Torrent &t);

Q_SIGNALS:
    void activeChanged();
    void rangeChanged();
    void rareSeedsChanged();
    void scopeChanged();
    void scopeLineChanged();
    void attentionChanged();
    void totalsChanged();
    void tideChanged();
    void yearChanged();
    void aliveChanged();
    void ladderChanged();
    void ladderPointsChanged();
    void trackersChanged();
    void earningChanged();
    void holdingChanged();
    void onAirChanged();
    void latelyChanged();
    /// A fast refresh (the cheap blocks) ran; the blocks it changed have signalled first.
    void refreshed();
    /// A slow refresh (the ledger's blocks) ran; the blocks it changed have signalled first.
    void slowRefreshed();
    void heroChanged();

private:
    QDateTime now() const;
    QList<const Torrent *> scopeTorrents() const;
    void scheduleRefresh();
    void refreshFast();
    void refreshSlow();
    void refreshHero();
    /// The scope's signals, coalesced: everything again when its torrents are no longer the
    /// ones the ledger's blocks were worked out for.
    void scopeMoved();
    static quint64 scopeKey(const QList<const Torrent *> &scope);

    QPointer<TorrentModel> m_torrents;
    QPointer<TorrentFilterModel> m_scope;
    QPointer<TrackerModel> m_trackerModel;
    QPointer<GlobalState> m_state;
    SeedLedger *m_ledger = nullptr;
    std::function<QDateTime()> m_clock;
    std::function<QVariantList()> m_recent;

    bool m_active = false;
    QString m_range = QStringLiteral("24h");
    int m_rareSeeds = 2;
    QStringList m_scopeHashes;
    int m_scopeCount = 0;
    bool m_scoped = false;
    QString m_scopeLine; ///< what the scope line last showed, to signal it only when it moves

    QVariantMap m_attention, m_totals, m_tide, m_year, m_alive, m_ladder, m_trackers, m_earning,
        m_holding, m_onAir;
    QVariantList m_lately;
    /// What m_ladderPoints shows, to tell whether a refresh moved any dot.
    struct LadderDot
    {
        QString hash, name;
        double ratio = 0;
        qint64 size = 0;
    };
    QList<LadderDot> m_ladderDots;
    QVariantList m_ladderPoints;
    QVariantList m_heroSpark;
    QVariantList m_heroDownSpark;
    QString m_todayText;
    int m_heroTick = 0;

    QTimer m_refreshTimer;      ///< the cheap blocks, while active
    QTimer m_slowTimer;         ///< the ledger's blocks, while active
    QTimer m_heroTimer;         ///< the hero's spark, always (one ledger read a second)
    QTimer m_coalesce;          ///< a burst of the scope's signals becomes one scopeMoved()
    quint64 m_slowScopeKey = 0; ///< scopeKey() of the scope refreshSlow last worked on
};

} // namespace omaseed::core
