#include "core/TorrentDetails.hpp"

#include "core/Format.hpp"
#include "core/PieceStates.hpp"
#include "core/QbtClient.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace omaseed::core {

namespace {

constexpr int kDefaultPollInterval = 2000; ///< spec §2: the details pane refreshes every 2 s.

} // namespace

TorrentDetails::TorrentDetails(QObject *parent)
    : QObject(parent)
    , m_files(new TorrentFilesModel(this))
    , m_trackers(new TorrentTrackersModel(this))
    , m_peers(new TorrentPeersModel(this))
    , m_timer(new QTimer(this))
    , m_peersTimer(new QTimer(this))
{
    m_timer->setInterval(kDefaultPollInterval);
    m_peersTimer->setInterval(kDefaultPollInterval);
    connect(m_timer, &QTimer::timeout, this, &TorrentDetails::poll);
    connect(m_peersTimer, &QTimer::timeout, this, &TorrentDetails::pollPeers);

    connect(m_trackers, &TorrentTrackersModel::countChanged, this, [this]() {
        const bool error = m_trackers->hasError();
        if (error == m_trackerError)
            return;
        m_trackerError = error;
        Q_EMIT hasTrackerErrorChanged();
    });
}

TorrentDetails::~TorrentDetails() = default;

bool TorrentDetails::hasTrackerError() const
{
    return m_trackerError;
}

int TorrentDetails::pollInterval() const
{
    return m_timer->interval();
}

void TorrentDetails::setPollInterval(int milliseconds)
{
    m_timer->setInterval(milliseconds);
    m_peersTimer->setInterval(milliseconds);
}

void TorrentDetails::setClient(QbtClient *client)
{
    m_client = client;
    m_demo = false;
    updateTimers();
}

void TorrentDetails::setTorrentModel(TorrentModel *torrents)
{
    m_torrents = torrents;
}

void TorrentDetails::setHash(const QString &hash)
{
    if (hash == m_hash)
        return;

    m_hash = hash;
    m_gone = false;
    m_rawProperties = QJsonObject();
    m_properties.clear();
    m_errorLogged = false;
    setLastError(QString());
    m_files->clear();
    m_trackers->clear();
    m_peers->clear();
    setPieceStates(QByteArray());

    Q_EMIT hashChanged();
    Q_EMIT propertiesChanged();

    updateTimers();
    if (!m_hash.isEmpty() && !m_demo)
        refreshNow();
}

void TorrentDetails::setPeersActive(bool active)
{
    if (active == m_peersActive)
        return;
    m_peersActive = active;
    if (!active)
        m_peers->clear(); // the next visit asks for a full update
    updateTimers();
    if (active && !m_hash.isEmpty() && !m_demo)
        pollPeers();
}

void TorrentDetails::setGeneralActive(bool active)
{
    if (active == m_generalActive)
        return;
    m_generalActive = active;
    // Nobody looks at the bar from another tab; come back to fresh states instead of
    // old ones for a tick.
    if (!active && !m_demo)
        setPieceStates(QByteArray());
    if (active && !m_hash.isEmpty() && !m_demo)
        pollPieceStates();
}

int TorrentDetails::piecesHaveCount() const
{
    return PieceStates::count(m_pieceStates, PieceStates::Have);
}

QList<int> TorrentDetails::pieceBuckets(int buckets) const
{
    return PieceStates::bucket(m_pieceStates, buckets);
}

void TorrentDetails::setPieceStates(const QByteArray &states)
{
    if (states == m_pieceStates)
        return;
    m_pieceStates = states;
    Q_EMIT pieceStatesChanged();
}

void TorrentDetails::setVisible(bool visible)
{
    if (visible == m_visible)
        return;
    m_visible = visible;
    updateTimers();
    if (visible && !m_hash.isEmpty() && !m_demo)
        refreshNow();
}

void TorrentDetails::updateTimers()
{
    const bool canPoll =
        !m_demo && !m_client.isNull() && !m_hash.isEmpty() && m_visible && !m_gone;
    if (canPoll && !m_timer->isActive())
        m_timer->start();
    else if (!canPoll && m_timer->isActive())
        m_timer->stop();

    const bool canPollPeers = canPoll && m_peersActive;
    if (canPollPeers && !m_peersTimer->isActive())
        m_peersTimer->start();
    else if (!canPollPeers && m_peersTimer->isActive())
        m_peersTimer->stop();
}

void TorrentDetails::refreshNow()
{
    poll();
    if (m_peersActive)
        pollPeers();
}

// --- polling -----------------------------------------------------------------------------

bool TorrentDetails::torrentExists() const
{
    if (m_gone)
        return false;
    // The sync model drops a deleted torrent before the cursor moves off it; a model that
    // has rows but not this hash means there is nothing left to ask about.
    return m_torrents.isNull() || m_torrents->rowCount() == 0
           || m_torrents->indexOfHash(m_hash) >= 0;
}

void TorrentDetails::poll()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !torrentExists())
        return;

    const QString hash = m_hash;

    m_client->torrentsProperties(hash)->then(
        this, [this, hash](const QbtResult<QJsonObject> &result) {
            if (hash != m_hash)
                return; // the selection moved on while this was in flight
            if (result.isError()) {
                noteError(QStringLiteral("properties"), result.error());
                return;
            }
            noteSuccess();
            applyProperties(result.value());
            Q_EMIT refreshed();
        });

    m_client->torrentsFiles(hash)->then(this, [this, hash](const QbtResult<QJsonArray> &result) {
        if (hash != m_hash)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("files"), result.error());
            return;
        }
        noteSuccess();
        m_files->applyFiles(result.value());
    });

    m_client->torrentsTrackers(hash)->then(this, [this, hash](const QbtResult<QJsonArray> &result) {
        if (hash != m_hash)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("trackers"), result.error());
            return;
        }
        noteSuccess();
        m_trackers->applyTrackers(result.value());
    });

    if (m_generalActive)
        pollPieceStates();
}

void TorrentDetails::pollPieceStates()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !m_generalActive || !torrentExists())
        return;

    const QString hash = m_hash;
    m_client->torrentsPieceStates(hash)->then(
        this, [this, hash](const QbtResult<QJsonArray> &result) {
            if (hash != m_hash || !m_generalActive)
                return;
            if (result.isError()) {
                noteError(QStringLiteral("pieceStates"), result.error());
                return;
            }
            noteSuccess();
            setPieceStates(PieceStates::fromJson(result.value()));
        });
}

void TorrentDetails::pollPeers()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !m_peersActive || !torrentExists())
        return;

    const QString hash = m_hash;
    m_client->torrentsPeers(hash, m_peers->rid())
        ->then(this, [this, hash](const QbtResult<QJsonObject> &result) {
            if (hash != m_hash || !m_peersActive)
                return;
            if (result.isError()) {
                noteError(QStringLiteral("peers"), result.error());
                return;
            }
            noteSuccess();
            m_peers->applyPeers(result.value());
        });
}

void TorrentDetails::noteError(const QString &what, const ApiError &error)
{
    // 404: the torrent was deleted (here or elsewhere) while the pane still showed it. That
    // is not a failure to report; stop asking until the cursor lands on another torrent.
    if (error.kind() == ApiError::Kind::NotFound) {
        m_gone = true;
        updateTimers();
        return;
    }
    const QString message = error.toString();
    if (!m_errorLogged) {
        m_errorLogged = true;
        qWarning().noquote()
            << QStringLiteral("details: %1 for %2 failed: %3").arg(what, m_hash.left(8), message);
    }
    setLastError(message);
}

void TorrentDetails::noteSuccess()
{
    m_errorLogged = false;
    setLastError(QString());
}

void TorrentDetails::setLastError(const QString &message)
{
    if (message == m_lastError)
        return;
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}

// --- the General tab -----------------------------------------------------------------------

void TorrentDetails::applyProperties(const QJsonObject &json)
{
    m_rawProperties = json;

    const auto number = [&json](const char *key) {
        return qint64(json.value(QLatin1StringView(key)).toDouble());
    };
    const auto real = [&json](const char *key) {
        return json.value(QLatin1StringView(key)).toDouble();
    };
    const auto text = [&json](const char *key) {
        return json.value(QLatin1StringView(key)).toString();
    };

    const qint64 totalSize = number("total_size");

    QVariantMap map;
    map.insert(QStringLiteral("name"), text("name"));
    map.insert(QStringLiteral("savePath"), text("save_path"));
    map.insert(QStringLiteral("hashV1"), text("infohash_v1"));
    map.insert(QStringLiteral("hashV2"), text("infohash_v2"));
    map.insert(QStringLiteral("totalSizeText"), Format::size(totalSize));
    map.insert(QStringLiteral("downloaded"),
               QStringLiteral("%1 of %2")
                   .arg(Format::size(number("total_downloaded")), Format::size(totalSize)));
    map.insert(QStringLiteral("uploaded"), Format::size(number("total_uploaded")));
    map.insert(QStringLiteral("wasted"), Format::size(number("total_wasted")));
    const int piecesNum = int(number("pieces_num"));
    map.insert(QStringLiteral("pieces"), Format::pieceLayout(piecesNum, number("piece_size")));
    map.insert(QStringLiteral("piecesHave"),
               Format::piecesHave(int(number("pieces_have")), piecesNum));
    // qBittorrent sends -1 when it has no figure (every seeding torrent does): no row, rather
    // than a "0.00" that reads as "no peer has the data".
    const double availability = real("availability");
    map.insert(QStringLiteral("availability"),
               availability < 0 ? QString() : Format::ratio(availability));
    map.insert(QStringLiteral("seeds"),
               Format::connectedTotal(int(number("seeds")), int(number("seeds_total"))));
    map.insert(QStringLiteral("peers"),
               Format::connectedTotal(int(number("peers")), int(number("peers_total"))));
    map.insert(QStringLiteral("timeActive"), Format::duration(number("time_elapsed")));
    map.insert(QStringLiteral("createdBy"), text("created_by"));
    map.insert(QStringLiteral("comment"), text("comment"));

    const qint64 created = number("creation_date");
    map.insert(QStringLiteral("createdOn"),
               created > 0 ? Format::relativeTime(QDateTime::fromSecsSinceEpoch(created))
                           : QString());

    // docs/12 G2. qBittorrent marks "not known" with -1 (or leaves the key out on an older
    // server); every such value turns into an empty string, and the view drops the row.
    const auto has = [&json](const char *key) {
        const QJsonValue value = json.value(QLatin1StringView(key));
        return !value.isUndefined() && !value.isNull();
    };
    const auto when = [&](const char *key) {
        const qint64 seconds = number(key);
        return seconds > 0 ? Format::relativeTime(QDateTime::fromSecsSinceEpoch(seconds))
                           : QString();
    };

    map.insert(QStringLiteral("connections"),
               has("nb_connections") ? Format::connections(int(number("nb_connections")),
                                                           int(number("nb_connections_limit")))
                                     : QString());
    // "Never" is the answer that matters for a stalled torrent, but only once there is
    // metadata to be complete against.
    QString lastSeen = when("last_seen");
    if (lastSeen.isEmpty() && has("last_seen") && piecesNum > 0)
        lastSeen = QStringLiteral("never");
    map.insert(QStringLiteral("lastSeen"), lastSeen);
    const qint64 seedingTime = number("seeding_time");
    map.insert(QStringLiteral("seedingTime"),
               seedingTime > 0 ? Format::duration(seedingTime) : QString());
    map.insert(QStringLiteral("completedOn"), when("completion_date"));
    map.insert(QStringLiteral("avgSpeed"),
               has("dl_speed_avg") || has("up_speed_avg")
                   ? Format::averageSpeeds(has("dl_speed_avg") ? number("dl_speed_avg") : -1,
                                           has("up_speed_avg") ? number("up_speed_avg") : -1)
                   : QString());
    map.insert(QStringLiteral("reannounce"), Format::reannounceIn(number("reannounce")));

    // `private` (5.0+) is null until the metadata says; older servers only send
    // `is_private`, which reads false for a magnet that has no metadata yet.
    QString privateText;
    const QJsonValue privateValue = json.value(QLatin1StringView("private"));
    const QJsonValue isPrivate = json.value(QLatin1StringView("is_private"));
    if (privateValue.isBool())
        privateText = privateValue.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    else if (privateValue.isUndefined() && isPrivate.isBool() && piecesNum > 0)
        privateText = isPrivate.toBool() ? QStringLiteral("yes") : QStringLiteral("no");
    map.insert(QStringLiteral("private"), privateText);

    // torrents/properties has no share limits and no magnet; the torrent row of
    // sync/maindata has both.
    double ratioLimit = -2.0;
    qint64 seedingTimeLimit = -2;
    QString magnet;
    if (!m_torrents.isNull() && !m_hash.isEmpty() && m_torrents->indexOfHash(m_hash) >= 0) {
        const Torrent &row = m_torrents->torrent(m_hash);
        ratioLimit = row.ratioLimit;
        seedingTimeLimit = row.seedingTimeLimit;
        magnet = row.magnetUri;
    }
    map.insert(QStringLiteral("shareLimit"), Format::shareLimit(ratioLimit, seedingTimeLimit));
    map.insert(QStringLiteral("magnet"), magnet);
    map.insert(QStringLiteral("magnetText"), Format::shortMagnet(magnet));

    if (map == m_properties)
        return;
    m_properties = map;
    Q_EMIT propertiesChanged();
}

// --- the demo ------------------------------------------------------------------------------

void TorrentDetails::applyDemo(const QJsonObject &details)
{
    m_demo = true;
    m_client.clear();
    m_timer->stop();
    m_peersTimer->stop();

    const QString hash = details.value(QStringLiteral("hash")).toString();
    if (hash != m_hash) {
        m_hash = hash;
        m_files->clear();
        m_trackers->clear();
        m_peers->clear();
        Q_EMIT hashChanged();
    }

    m_errorLogged = false;
    setLastError(QString());

    applyProperties(details.value(QStringLiteral("properties")).toObject());
    m_files->applyFiles(details.value(QStringLiteral("files")).toArray());
    m_trackers->applyTrackers(details.value(QStringLiteral("trackers")).toArray());
    m_peers->applyPeers(details.value(QStringLiteral("peers")).toObject());
    setPieceStates(PieceStates::fromJson(details.value(QStringLiteral("pieceStates")).toArray()));
    Q_EMIT refreshed();
}

} // namespace omaseed::core
