#include "core/TorrentTrackersModel.hpp"

#include "core/Format.hpp"

#include <QDateTime>
#include <QHash>
#include <QUrl>

#include <algorithm>

namespace omaseed::core {

namespace {

/// The Seeds and Peers columns of spec §2 Fig. 3: a grouped count, or an em dash where the
/// tracker sent -1 ("it did not say").
QString countOrDash(int value)
{
    return value < 0 ? QStringLiteral("—") : Format::count(value);
}

} // namespace

QString TorrentTrackersModel::sourceName(TrackerSource source)
{
    switch (source) {
    case TrackerSource::Dht:
        return QStringLiteral("DHT");
    case TrackerSource::Pex:
        return QStringLiteral("PeX");
    case TrackerSource::Lsd:
        return QStringLiteral("LSD");
    case TrackerSource::Announce:
        break;
    }
    return {};
}

QString TorrentTrackersModel::hostOf(const QString &url)
{
    const QUrl parsed(url);
    const QString host = parsed.host();
    return host.isEmpty() ? url : host;
}

QString TorrentTrackersModel::statusText(int status, const QString &msg, qint64 nextAnnounce)
{
    switch (status) {
    case int(TrackerStatus::Disabled):
        return msg.isEmpty() ? QStringLiteral("disabled") : msg;
    case int(TrackerStatus::NotContacted):
        return QStringLiteral("not contacted");
    case int(TrackerStatus::Working):
        return nextAnnounce > 0 ? QStringLiteral("working · %1").arg(Format::duration(nextAnnounce))
                                : QStringLiteral("working");
    case int(TrackerStatus::Updating):
        return QStringLiteral("updating");
    case int(TrackerStatus::NotWorking):
    case int(TrackerStatus::TrackerError):
    case int(TrackerStatus::Unreachable): {
        // The figure's "timed out · retry 4 min": the server's own words, then when it will
        // try again. `msg` is what a real tracker sends back ("timed out", "unregistered
        // torrent"); without one there is nothing to say but how it failed.
        QString reason = msg;
        if (reason.isEmpty()) {
            reason = status == int(TrackerStatus::TrackerError)  ? QStringLiteral("tracker error")
                     : status == int(TrackerStatus::Unreachable) ? QStringLiteral("unreachable")
                                                                 : QStringLiteral("not working");
        }
        return nextAnnounce > 0
                   ? QStringLiteral("%1 · retry %2").arg(reason, Format::duration(nextAnnounce))
                   : reason;
    }
    default:
        return msg.isEmpty() ? QStringLiteral("unknown") : msg;
    }
}

qint64 TorrentTrackersModel::secondsUntilAnnounce(std::optional<qint64> nextAnnounce,
                                                  qint64 nowSecs)
{
    if (!nextAnnounce)
        return 0;
    return std::max<qint64>(0, *nextAnnounce - nowSecs);
}

TorrentTrackersModel::TorrentTrackersModel(QObject *parent)
    : QAbstractListModel(parent)
{}

int TorrentTrackersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QHash<int, QByteArray> TorrentTrackersModel::roleNames() const
{
    return {
        {UrlRole, "url"},
        {HostRole, "host"},
        {TierRole, "tier"},
        {StatusRole, "status"},
        {StatusTextRole, "statusText"},
        {HealthyRole, "healthy"},
        {DotRoleRole, "dotRole"},
        {SeedsRole, "seeds"},
        {SeedsTextRole, "seedsText"},
        {PeersRole, "peers"},
        {LeechesRole, "leeches"},
        {LeechesTextRole, "leechesText"},
        {DownloadedRole, "downloaded"},
        {MsgRole, "msg"},
        {IsPseudoRole, "isPseudo"},
    };
}

QVariant TorrentTrackersModel::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= int(m_rows.size()))
        return {};
    const TorrentTracker &row = m_rows.at(index.row());

    switch (role) {
    case UrlRole:
        return row.url;
    case HostRole:
        return row.host;
    case TierRole:
        return row.tier;
    case StatusRole:
        return row.status;
    case StatusTextRole:
        return statusText(row.status, row.msg, row.nextAnnounce);
    case HealthyRole:
        return row.status == int(TrackerStatus::Working);
    case DotRoleRole:
        // Spec §2 Fig. 3: green for a working announce URL, `.dot.warn` yellow for the one
        // that timed out, `.dot.off` dim for DHT/PeX and for anything not contacted yet.
        if (row.isPseudo)
            return QStringLiteral("dimForeground");
        if (row.status == int(TrackerStatus::Working))
            return QStringLiteral("green");
        if (isTrackerFailure(row.status))
            return QStringLiteral("yellow");
        return QStringLiteral("dimForeground");
    case SeedsRole:
        return row.seeds;
    case SeedsTextRole:
        return countOrDash(row.seeds);
    case PeersRole:
        return row.peers;
    case LeechesRole:
        return row.leeches;
    case LeechesTextRole:
        return countOrDash(row.leeches);
    case DownloadedRole:
        return row.downloaded;
    case MsgRole:
        return row.msg;
    case IsPseudoRole:
        return row.isPseudo;
    default:
        return {};
    }
}

bool TorrentTrackersModel::hasError() const
{
    for (const TorrentTracker &row : m_rows) {
        if (!row.isPseudo && isTrackerFailure(row.status))
            return true;
    }
    return false;
}

QString TorrentTrackersModel::urlAt(int row) const
{
    return row >= 0 && row < int(m_rows.size()) ? m_rows.at(row).url : QString();
}

void TorrentTrackersModel::applyTrackers(const QList<TrackerEntry> &trackers)
{
    applyTrackers(trackers, QDateTime::currentSecsSinceEpoch());
}

void TorrentTrackersModel::applyTrackers(const QList<TrackerEntry> &trackers, qint64 nowSecs)
{
    QList<TorrentTracker> rows;
    rows.reserve(trackers.size());

    for (const TrackerEntry &entry : trackers) {
        TorrentTracker row;
        row.url = entry.url;
        if (row.url.isEmpty())
            continue;
        row.isPseudo = entry.source != TrackerSource::Announce;
        row.host = row.isPseudo ? sourceName(entry.source) : hostOf(row.url);
        row.tier = entry.tier;
        row.status = entry.status;
        row.seeds = entry.seeds.value_or(-1);
        row.peers = entry.peers.value_or(-1);
        row.leeches = entry.leeches.value_or(-1);
        row.downloaded = entry.downloaded.value_or(-1);
        // Kept as seconds left, worked out now: each poll then moves the figure along.
        row.nextAnnounce = secondsUntilAnnounce(entry.nextAnnounce, nowSecs);
        row.msg = entry.message;
        rows.append(row);
    }

    // Two announce URLs on one host (a tracker's second port, the swarm's live tracker and
    // its dead twin) would read the same, so those rows add their port; the rest stay as
    // the figure draws them.
    QHash<QString, int> perHost;
    for (const TorrentTracker &row : std::as_const(rows))
        ++perHost[row.host];
    for (TorrentTracker &row : rows) {
        const int port = row.isPseudo ? -1 : QUrl(row.url).port();
        if (perHost.value(row.host) > 1 && port > 0)
            row.host = QStringLiteral("%1:%2").arg(row.host).arg(port);
    }

    if (rows == m_rows)
        return;

    // The list is short (a handful of rows) and the server sends it whole every time, so a
    // reset is cheaper than diffing — and the cursor lives in QML, keyed by row index.
    const bool sameSize = rows.size() == m_rows.size();
    if (sameSize) {
        m_rows = rows;
        Q_EMIT dataChanged(index(0), index(int(m_rows.size()) - 1));
        Q_EMIT countChanged(); // hasError may have flipped
        return;
    }

    beginResetModel();
    m_rows = rows;
    endResetModel();
    Q_EMIT countChanged();
}

void TorrentTrackersModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    endResetModel();
    Q_EMIT countChanged();
}

} // namespace omaseed::core
