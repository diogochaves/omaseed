#pragma once

// TorrentPeersModel — the details pane's Peers tab (spec §2 Fig. 4).
//
// `sync/torrentPeers` speaks the same rid protocol as `sync/maindata`: a `full_update` once,
// then only the peers (and inside them only the keys) that moved, plus a `peers_removed`
// list. The map is keyed by "ip:port", which is what `address` is and what the ban and copy
// actions carry.
//
// Rows are sorted by download speed, fastest first, then by address — the order the figure
// draws and the one that keeps the interesting peers on screen. Speeds move on every poll,
// so an update never resets: peers that left are removed, newcomers inserted, the rest
// changed in place and moved (beginMoveRows) to where their speed now puts them. The view
// keeps its delegates and scroll, and the cursor follows its peer by address through
// rowOfAddress() once `applied` fires. Speeds print through
// Format::speed ("1.02 MB/s"), the units the main table prints (docs/11 J2; Fig. 4 drew
// "1.02 M", which read as a different unit beside the table).

#include "core/engines/DetailsData.hpp"

#include <QAbstractListModel>
#include <QHash>
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

    /// Applies one answer about the peers: a full one replaces everything, otherwise each
    /// peer is merged field by field and the removed ones dropped.
    void applyPeers(const PeersDelta &delta);
    /// Forgets the peers and the rid, so the next poll asks for a full update.
    void clear();

    Q_INVOKABLE QString addressAt(int row) const;
    /// The row showing the peer at `address` ("ip:port"), or -1.
    Q_INVOKABLE int rowOfAddress(const QString &address) const;

Q_SIGNALS:
    void countChanged();
    void ridChanged();
    /// One applyPeers() has finished moving rows: the moment to put a cursor back on its peer.
    void applied();

private:
    /// Turns m_rows into `next` (already sorted) with removes, inserts, dataChanged and moves.
    void syncRows(const QList<TorrentPeer> &next);

    QHash<QString, TorrentPeer> m_peers;
    QList<TorrentPeer> m_rows;
    int m_rid = 0;
};

} // namespace omaseed::core
