#pragma once

// TrackerModel — sync/maindata's `trackers` map, `{trackerUrl: [hashes]}`, grouped the way
// the rail shows it (spec §1: "opentrackr.org 88", "archlinux.org 8", "trackerless 4").
//
// Several URLs of one host (http and udp announce URLs of the same tracker, different ports)
// collapse into one row keyed by QUrl::host(); qBittorrent's pseudo-trackers become the rows
// DHT, PeX and LSD. The trackerless row is not in the API's map — SyncEngine fills it from
// the torrents that have no working tracker — and always sorts last.

#include <QAbstractListModel>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>

namespace omaseed::core {

struct TrackerGroup
{
    QString host;       ///< "tracker.opentrackr.org", "DHT", or "trackerless"
    QStringList urls;   ///< every announce URL that resolved to this host
    QStringList hashes; ///< torrents announcing to it

    bool operator==(const TrackerGroup &other) const = default;
};

class TrackerModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        HostRole = Qt::UserRole + 1,
        UrlsRole,
        HashesRole,
        TorrentCountRole,
        IsTrackerlessRole,
    };
    Q_ENUM(Role)

    explicit TrackerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_groups.size()); }
    QStringList hosts() const;
    Q_INVOKABLE QStringList hashesFor(const QString &host) const;
    /// The API's map as it stands: announce URL → the torrents announcing to it (#146: which
    /// URL a private host's newest torrent uses).
    const QHash<QString, QStringList> &byUrl() const { return m_byUrl; }

    void applyFullUpdate(const QJsonObject &trackers);
    /// `changed` holds only the announce URLs whose hash list moved; `removed` the URLs the
    /// server dropped (qBittorrent sends them as `trackers_removed`).
    void applyPartial(const QJsonObject &changed, const QJsonArray &removed);
    /// The torrents with no tracker at all; an empty list removes the trackerless row.
    void setTrackerlessHashes(const QStringList &hashes);
    void clear();

Q_SIGNALS:
    void countChanged();

private:
    void rebuild();

    QHash<QString, QStringList> m_byUrl; ///< announce URL → hashes, straight from the API
    QStringList m_trackerless;
    QHash<QString, QString> m_hostCache; ///< announce URL → host, so QUrl parses once
    QList<TrackerGroup> m_groups;
};

} // namespace omaseed::core
