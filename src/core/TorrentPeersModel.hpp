#pragma once

// TorrentPeersModel — the details pane's Peers tab (spec §2 Fig. 4).
//
// `sync/torrentPeers` speaks the same rid protocol as `sync/maindata`: a `full_update` once,
// then only the peers (and inside them only the keys) that moved, plus a `peers_removed`
// list. The map is keyed by "ip:port", which is what `address` is and what the ban and copy
// actions carry.
//
// Rows are sorted by download speed, fastest first, then by address — the order the figure
// draws and the one that keeps the interesting peers on screen. Speeds print through
// Format::speed ("1.02 MB/s"), the units the main table prints (docs/11 J2; Fig. 4 drew
// "1.02 M", which read as a different unit beside the table).

#include <QAbstractListModel>
#include <QHash>
#include <QJsonObject>
#include <QList>
#include <QString>

namespace omaseed::core {

struct TorrentPeer
{
    QString address; ///< the map key, "185.21.216.140:51413"
    QString ip;
    int port = 0;
    QString country;
    QString countryCode;
    QString client;
    QString connection;
    QString flags;
    QString flagsDesc;
    qint64 dlSpeed = 0;
    qint64 upSpeed = 0;
    qint64 downloaded = 0;
    qint64 uploaded = 0;
    double progress = 0.0;
    double relevance = 0.0;

    bool operator==(const TorrentPeer &other) const = default;
};

class TorrentPeersModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int rid READ rid NOTIFY ridChanged)

public:
    enum Role {
        AddressRole = Qt::UserRole + 1,
        IpRole,
        PortRole,
        CountryRole,
        CountryCodeRole,
        ClientRole,
        DlSpeedRole,
        DlSpeedTextRole,
        UpSpeedRole,
        UpSpeedTextRole,
        ProgressRole,
        ProgressTextRole,
        ConnectionRole,
        FlagsRole,
        FlagsDescRole,
        DownloadedRole,
        UploadedRole,
        RelevanceRole,
    };
    Q_ENUM(Role)

    explicit TorrentPeersModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    /// The rid to send with the next sync/torrentPeers call; 0 until the first answer.
    int rid() const { return m_rid; }

    /// Applies one `sync/torrentPeers` answer: `full_update` replaces everything, otherwise
    /// `peers` is merged key by key and `peers_removed` dropped.
    void applyPeers(const QJsonObject &answer);
    /// Forgets the peers and the rid, so the next poll asks for a full update.
    void clear();

    Q_INVOKABLE QString addressAt(int row) const;

Q_SIGNALS:
    void countChanged();
    void ridChanged();

private:
    void rebuildRows();

    QHash<QString, TorrentPeer> m_peers;
    QList<TorrentPeer> m_rows;
    int m_rid = 0;
};

} // namespace omaseed::core
