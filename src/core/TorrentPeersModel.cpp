#include "core/TorrentPeersModel.hpp"

#include "core/Format.hpp"

#include <QJsonArray>
#include <QSet>

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
        return Format::speed(row.dlSpeed);
    case UpSpeedRole:
        return row.upSpeed;
    case UpSpeedTextRole:
        return Format::speed(row.upSpeed);
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

int TorrentPeersModel::rowOfAddress(const QString &address) const
{
    if (address.isEmpty())
        return -1;
    for (int row = 0; row < int(m_rows.size()); ++row) {
        if (m_rows.at(row).address == address)
            return row;
    }
    return -1;
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

    QList<TorrentPeer> next = m_peers.values();
    std::sort(next.begin(), next.end(), [](const TorrentPeer &a, const TorrentPeer &b) {
        if (a.dlSpeed != b.dlSpeed)
            return a.dlSpeed > b.dlSpeed;
        return a.address < b.address;
    });

    const int wasCount = int(m_rows.size());
    syncRows(next);
    m_rid = rid;

    if (int(m_rows.size()) != wasCount)
        Q_EMIT countChanged();
    Q_EMIT ridChanged();
    Q_EMIT applied();
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

void TorrentPeersModel::syncRows(const QList<TorrentPeer> &next)
{
    // 1. The peers that left, bottom up so the rows above keep their numbers.
    QSet<QString> nextAddresses;
    nextAddresses.reserve(next.size());
    for (const TorrentPeer &peer : next)
        nextAddresses.insert(peer.address);
    for (int row = int(m_rows.size()) - 1; row >= 0; --row) {
        if (nextAddresses.contains(m_rows.at(row).address))
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_rows.removeAt(row);
        endRemoveRows();
    }

    // 2. The newcomers, at the foot for now; step 4 moves them into place.
    QSet<QString> known;
    known.reserve(m_rows.size());
    for (const TorrentPeer &peer : std::as_const(m_rows))
        known.insert(peer.address);
    QList<TorrentPeer> arrived;
    for (const TorrentPeer &peer : next) {
        if (!known.contains(peer.address))
            arrived.append(peer);
    }
    if (!arrived.isEmpty()) {
        const int first = int(m_rows.size());
        beginInsertRows(QModelIndex(), first, first + int(arrived.size()) - 1);
        m_rows.append(arrived);
        endInsertRows();
    }

    // 3. Fresh values for the peers that stayed.
    for (int row = 0; row < int(m_rows.size()); ++row) {
        const TorrentPeer fresh = m_peers.value(m_rows.at(row).address);
        if (m_rows.at(row) == fresh)
            continue;
        m_rows[row] = fresh;
        Q_EMIT dataChanged(index(row), index(row));
    }

    // 4. Into speed order, one move per peer out of place. A torrent has tens of peers, a
    // few hundred at most, so the linear search per position is cheap.
    for (int target = 0; target < int(next.size()); ++target) {
        if (m_rows.at(target).address == next.at(target).address)
            continue;
        int from = target + 1;
        while (from < int(m_rows.size()) && m_rows.at(from).address != next.at(target).address)
            ++from;
        if (from >= int(m_rows.size()))
            continue; // cannot happen: m_rows holds exactly next's peers by now
        beginMoveRows(QModelIndex(), from, from, QModelIndex(), target);
        m_rows.move(from, target);
        endMoveRows();
    }
}

} // namespace omaseed::core
