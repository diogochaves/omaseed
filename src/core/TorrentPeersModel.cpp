#include "core/TorrentPeersModel.hpp"

#include "core/Format.hpp"

#include <QJsonArray>

#include <algorithm>
#include <type_traits>

namespace omaseed::core {

namespace {

/// Merges one peer object into `peer`; a partial update only carries the keys that moved,
/// so every field is left alone unless the answer actually names it.
void merge(TorrentPeer &peer, const QJsonObject &json)
{
    const auto take = [&json](const char *key, auto &field) {
        const auto value = json.constFind(QLatin1StringView(key));
        if (value == json.constEnd())
            return;
        using Field = std::decay_t<decltype(field)>;
        if constexpr (std::is_same_v<Field, QString>)
            field = value->toString();
        else if constexpr (std::is_same_v<Field, double>)
            field = value->toDouble();
        else if constexpr (std::is_same_v<Field, int>)
            field = value->toInt();
        else
            field = qint64(value->toDouble());
    };

    take("ip", peer.ip);
    take("port", peer.port);
    take("country", peer.country);
    take("country_code", peer.countryCode);
    take("client", peer.client);
    take("connection", peer.connection);
    take("flags", peer.flags);
    take("flags_desc", peer.flagsDesc);
    take("dl_speed", peer.dlSpeed);
    take("up_speed", peer.upSpeed);
    take("downloaded", peer.downloaded);
    take("uploaded", peer.uploaded);
    take("progress", peer.progress);
    take("relevance", peer.relevance);
}

} // namespace

TorrentPeersModel::TorrentPeersModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int TorrentPeersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> TorrentPeersModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {IpRole, "ip"},
        {PortRole, "port"},
        {CountryRole, "country"},
        {CountryCodeRole, "countryCode"},
        {ClientRole, "client"},
        {DlSpeedRole, "dlSpeed"},
        {DlSpeedTextRole, "dlSpeedText"},
        {UpSpeedRole, "upSpeed"},
        {UpSpeedTextRole, "upSpeedText"},
        {ProgressRole, "progress"},
        {ProgressTextRole, "progressText"},
        {ConnectionRole, "connection"},
        {FlagsRole, "flags"},
        {FlagsDescRole, "flagsDesc"},
        {DownloadedRole, "downloaded"},
        {UploadedRole, "uploaded"},
        {RelevanceRole, "relevance"},
    };
}

QVariant TorrentPeersModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const TorrentPeer &row = m_rows.at(index.row());

    switch (role) {
    case AddressRole:
        return row.address;
    case IpRole:
        return row.ip;
    case PortRole:
        return row.port;
    case CountryRole:
        return row.country;
    case CountryCodeRole:
        return row.countryCode;
    case ClientRole:
        return row.client;
    case DlSpeedRole:
        return row.dlSpeed;
    case DlSpeedTextRole:
        return Format::speedShort(row.dlSpeed);
    case UpSpeedRole:
        return row.upSpeed;
    case UpSpeedTextRole:
        return Format::speedShort(row.upSpeed);
    case ProgressRole:
        return row.progress;
    case ProgressTextRole:
        return Format::percent(row.progress);
    case ConnectionRole:
        return row.connection;
    case FlagsRole:
        return row.flags;
    case FlagsDescRole:
        return row.flagsDesc;
    case DownloadedRole:
        return row.downloaded;
    case UploadedRole:
        return row.uploaded;
    case RelevanceRole:
        return row.relevance;
    default:
        return {};
    }
}

QString TorrentPeersModel::addressAt(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows.at(row).address : QString();
}

void TorrentPeersModel::applyPeers(const QJsonObject &answer)
{
    const int rid = answer.value(QStringLiteral("rid")).toInt(m_rid);
    if (answer.value(QStringLiteral("full_update")).toBool())
        m_peers.clear();

    const QJsonObject peers = answer.value(QStringLiteral("peers")).toObject();
    for (auto it = peers.constBegin(); it != peers.constEnd(); ++it) {
        TorrentPeer &peer = m_peers[it.key()];
        peer.address = it.key();
        merge(peer, it->toObject());
        if (peer.ip.isEmpty()) {
            // A partial update may leave `ip` out; the key is "ip:port", so the address's
            // last colon separates them (an IPv6 address has colons of its own).
            const qsizetype colon = peer.address.lastIndexOf(QLatin1Char(':'));
            peer.ip = colon > 0 ? peer.address.left(colon) : peer.address;
        }
    }

    const QJsonArray removed = answer.value(QStringLiteral("peers_removed")).toArray();
    for (const QJsonValue &value : removed)
        m_peers.remove(value.toString());

    const int wasCount = int(m_rows.size());
    beginResetModel();
    rebuildRows();
    m_rid = rid;
    endResetModel();

    if (int(m_rows.size()) != wasCount)
        Q_EMIT countChanged();
    Q_EMIT ridChanged();
}

void TorrentPeersModel::clear()
{
    const bool hadRows = !m_rows.isEmpty();
    beginResetModel();
    m_peers.clear();
    m_rows.clear();
    m_rid = 0;
    endResetModel();
    if (hadRows)
        Q_EMIT countChanged();
    Q_EMIT ridChanged();
}

void TorrentPeersModel::rebuildRows()
{
    m_rows = m_peers.values();
    std::sort(m_rows.begin(), m_rows.end(), [](const TorrentPeer &a, const TorrentPeer &b) {
        if (a.dlSpeed != b.dlSpeed)
            return a.dlSpeed > b.dlSpeed;
        return a.address < b.address;
    });
}

} // namespace omaseed::core
