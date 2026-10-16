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

#include "core/engines/DetailsData.hpp"
#include <QAbstractListModel>

#include <QList>
#include <QString>

namespace omaseed::core {

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

    void applyTrackers(const QList<TrackerEntry> &trackers);
    /// The same, with "now" (seconds since the epoch) given, so tests can pin the clock.
    void applyTrackers(const QList<TrackerEntry> &trackers, qint64 nowSecs);
    void clear();

    /// The announce URL of a row, for `y` copy and for the Phase 4 remove/reannounce actions.
    Q_INVOKABLE QString urlAt(int row) const;

    /// "udp://tracker.example.net:1337/announce" -> "tracker.example.net"; anything
    /// unparseable comes back unchanged.
    static QString hostOf(const QString &url);
    /// What a row of `source` is called: "DHT", "PeX", "LSD"; empty for a real tracker.
    static QString sourceName(TrackerSource source);
    /// "working · 27 min", "timed out · retry 4 min", "not contacted", "updating",
    /// "disabled". `msg` wins over the generic wording when the server sent one.
    static QString statusText(int status, const QString &msg, qint64 nextAnnounce);
    /// The seconds left from `nowSecs` until `nextAnnounce` (both since the epoch): never
    /// negative, 0 when there is no figure.
    static qint64 secondsUntilAnnounce(std::optional<qint64> nextAnnounce, qint64 nowSecs);

Q_SIGNALS:
    void countChanged();

private:
    QList<TorrentTracker> m_rows;
};

} // namespace omaseed::core
