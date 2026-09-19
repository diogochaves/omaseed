#include "core/TorrentDetails.hpp"

#include "core/Format.hpp"
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
    m_rawProperties = QJsonObject();
    m_properties.clear();
    m_errorLogged = false;
    setLastError(QString());
    m_files->clear();
    m_trackers->clear();
    m_peers->clear();

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
    const bool canPoll = !m_demo && !m_client.isNull() && !m_hash.isEmpty() && m_visible;
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

void TorrentDetails::poll()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty())
        return;

    const QString hash = m_hash;

    m_client->torrentsProperties(hash)->then(
        this, [this, hash](const QbtResult<QJsonObject> &result) {
            if (hash != m_hash)
                return; // the selection moved on while this was in flight
            if (result.isError()) {
                noteError(QStringLiteral("properties"), result.error().toString());
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
            noteError(QStringLiteral("files"), result.error().toString());
            return;
        }
        noteSuccess();
        m_files->applyFiles(result.value());
    });

    m_client->torrentsTrackers(hash)->then(this, [this, hash](const QbtResult<QJsonArray> &result) {
        if (hash != m_hash)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("trackers"), result.error().toString());
            return;
        }
        noteSuccess();
        m_trackers->applyTrackers(result.value());
    });
}

void TorrentDetails::pollPeers()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !m_peersActive)
        return;

    const QString hash = m_hash;
    m_client->torrentsPeers(hash, m_peers->rid())
        ->then(this, [this, hash](const QbtResult<QJsonObject> &result) {
            if (hash != m_hash || !m_peersActive)
                return;
            if (result.isError()) {
                noteError(QStringLiteral("peers"), result.error().toString());
                return;
            }
            noteSuccess();
            m_peers->applyPeers(result.value());
        });
}

void TorrentDetails::noteError(const QString &what, const QString &message)
{
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
    map.insert(QStringLiteral("pieces"),
               Format::pieces(
                   int(number("pieces_num")), number("piece_size"), int(number("pieces_have"))));
    map.insert(QStringLiteral("availability"), Format::ratio(real("availability")));
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

    // torrents/properties has no share limits; the torrent row of sync/maindata does.
    double ratioLimit = -2.0;
    qint64 seedingTimeLimit = -2;
    if (!m_torrents.isNull() && !m_hash.isEmpty()) {
        const Torrent &row = m_torrents->torrent(m_hash);
        ratioLimit = row.ratioLimit;
        seedingTimeLimit = row.seedingTimeLimit;
    }
    map.insert(QStringLiteral("shareLimit"), Format::shareLimit(ratioLimit, seedingTimeLimit));

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
    Q_EMIT refreshed();
}

} // namespace omaseed::core
