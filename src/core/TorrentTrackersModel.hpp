#pragma once

// TorrentTrackersModel — the details pane's Trackers tab (spec §2 Fig. 3).
//
// `torrents/trackers` sends one entry per announce URL plus qBittorrent's three
// pseudo-trackers, `** [DHT] **`, `** [PeX] **` and `** [LSD] **`. They are kept as the API
// sends them, only shortened to `DHT`, `PeX`, `LSD`; every other row drops the protocol
// prefix and the path, so `udp://tracker.example.net:1337/announce` reads
// `tracker.example.net` (the full URL is still in `url`, for the tooltip and for `y` copy).
// When two rows share a host, those rows keep their port (`127.0.0.1:26996`, `127.0.0.1:9`)
// so they can be told apart.
//
// The figure's Status column carries the reason as well as the state: "working · 27 min",
// "timed out · retry 4 min". `status` is the API's number; `statusText` is what the column
// prints; `healthy` and `dotRole` drive the dot in front of the host.

#include <QAbstractListModel>
#include <QJsonArray>
#include <QList>
#include <QString>

namespace omaseed::core {

/// The `status` field of a `torrents/trackers` entry (qBittorrent WebUI API v2.15).
enum class TrackerStatus {
    Disabled = 0,     ///< the pseudo-trackers when the feature is off
    NotContacted = 1, ///< the tracker has not been reached yet
    Working = 2,
    Updating = 3,
    NotWorking = 4,
    TrackerError = 5, ///< qBittorrent 5.0+: the tracker answered with an error
    Unreachable = 6,  ///< qBittorrent 5.0+: the tracker could not be reached
};

/// NotWorking, TrackerError and Unreachable: the three ways an announce can fail.
constexpr bool isTrackerFailure(int status)
{
    return status == int(TrackerStatus::NotWorking) ||
           status == int(TrackerStatus::TrackerError) ||
           status == int(TrackerStatus::Unreachable);
}

struct TorrentTracker
{
    QString url;
    QString host;
    int tier = 0;
    int status = int(TrackerStatus::NotContacted);
    int seeds = -1;
    int peers = -1;
    int leeches = -1;
    int downloaded = -1;
    qint64 nextAnnounce = 0; ///< seconds left until the next announce, 0 when unknown
    QString msg;
    bool isPseudo = false; ///< DHT / PeX / LSD

    bool operator==(const TorrentTracker &other) const = default;
};

class TorrentTrackersModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    /// True when any real tracker is failing (isTrackerFailure) — the tab's error badge.
    Q_PROPERTY(bool hasError READ hasError NOTIFY countChanged)

public:
    enum Role {
        UrlRole = Qt::UserRole + 1,
        HostRole,
        TierRole,
        StatusRole,
        StatusTextRole,
        HealthyRole,
        DotRoleRole, ///< the Theme colour role for the dot: "green", "yellow", "dimForeground"
        SeedsRole,
        SeedsTextRole, ///< "1 204", or an em dash when the tracker has no figure (-1)
        PeersRole,
        LeechesRole,
        LeechesTextRole,
        DownloadedRole,
        MsgRole,
        IsPseudoRole,
    };
    Q_ENUM(Role)

    explicit TorrentTrackersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    bool hasError() const;

    void applyTrackers(const QJsonArray &trackers);
    /// The same, with "now" (seconds since the epoch) given, so tests can pin the clock.
    void applyTrackers(const QJsonArray &trackers, qint64 nowSecs);
    void clear();

    /// The announce URL of a row, for `y` copy and for the Phase 4 remove/reannounce actions.
    Q_INVOKABLE QString urlAt(int row) const;

    /// `** [DHT] **` -> "DHT"; "udp://tracker.example.net:1337/announce" ->
    /// "tracker.example.net"; anything unparseable comes back unchanged.
    static QString hostOf(const QString &url);
    /// True for `** [DHT] **` and its two siblings.
    static bool isPseudoTracker(const QString &url);
    /// "working · 27 min", "timed out · retry 4 min", "not contacted", "updating",
    /// "disabled". `msg` wins over the generic wording when the server sent one.
    static QString statusText(int status, const QString &msg, qint64 nextAnnounce);
    /// The seconds left until `next_announce`. qBittorrent 5.x sends an absolute Unix time;
    /// older servers sent the seconds left. A value past 1e9 (September 2001) can only be
    /// the former, so it is measured against `nowSecs`; anything smaller is taken as already
    /// relative. Never negative; 0 means "no figure".
    static qint64 secondsUntilAnnounce(qint64 nextAnnounce, qint64 nowSecs);

Q_SIGNALS:
    void countChanged();

private:
    QList<TorrentTracker> m_rows;
};

} // namespace omaseed::core
