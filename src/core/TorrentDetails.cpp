#include "core/TorrentDetails.hpp"

#include "core/FilesViewState.hpp"
#include "core/Format.hpp"
#include "core/PieceStates.hpp"
#include "core/QbtClient.hpp"
#include "core/Torrent.hpp"
#include "core/TorrentModel.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

namespace omaseed::core {

namespace {

constexpr int kDefaultPollInterval = 2000; ///< spec §2: the details pane refreshes every 2 s.
constexpr int kFilterSaveDelay = 800;      ///< a typed Files filter is written once it rests
/// #10: away from the HTTP sources tab, its list is asked for on one tick in this many.
constexpr int kWebSeedsIdleTicks = 10;

} // namespace

TorrentDetails::TorrentDetails(QObject *parent)
    : QObject(parent)
    , m_files(new TorrentFilesModel(this))
    , m_trackers(new TorrentTrackersModel(this))
    , m_peers(new TorrentPeersModel(this))
    , m_webSeeds(new TorrentWebSeedsModel(this))
    , m_timer(new QTimer(this))
    , m_peersTimer(new QTimer(this))
    , m_filterSaveTimer(new QTimer(this))
{
    m_timer->setInterval(kDefaultPollInterval);
    m_peersTimer->setInterval(kDefaultPollInterval);
    connect(m_timer, &QTimer::timeout, this, &TorrentDetails::poll);
    connect(m_peersTimer, &QTimer::timeout, this, &TorrentDetails::pollPeers);

    m_filterSaveTimer->setSingleShot(true);
    m_filterSaveTimer->setInterval(kFilterSaveDelay);
    connect(m_filterSaveTimer, &QTimer::timeout, this, &TorrentDetails::saveFilesFilter);
    connect(m_files, &TorrentFilesModel::filterTextChanged, this, [this]() {
        if (m_rememberViewState && !m_switching)
            m_filterSaveTimer->start();
    });
    connect(m_files, &TorrentFilesModel::hideSkippedChanged, this, [this]() {
        if (m_rememberViewState)
            FilesViewState::setHideSkipped(m_files->hideSkipped());
    });

    connect(m_trackers, &TorrentTrackersModel::countChanged, this, [this]() {
        const bool error = m_trackers->hasError();
        if (error == m_trackerError)
            return;
        m_trackerError = error;
        Q_EMIT hasTrackerErrorChanged();
    });
}

TorrentDetails::~TorrentDetails()
{
    // A filter typed just before quitting is still waiting for its timer.
    if (m_filterSaveTimer->isActive())
        saveFilesFilter();
}

void TorrentDetails::setRememberViewState(bool remember)
{
    if (remember == m_rememberViewState)
        return;
    if (m_filterSaveTimer->isActive())
        saveFilesFilter();
    m_rememberViewState = remember;
    if (remember) {
        m_files->setHideSkipped(FilesViewState::hideSkipped());
        m_switching = true;
        m_filterHash = m_hash;
        m_files->setFilterText(FilesViewState::filterFor(m_hash));
        m_switching = false;
    }
    Q_EMIT rememberViewStateChanged();
}

void TorrentDetails::saveFilesFilter()
{
    m_filterSaveTimer->stop();
    if (m_rememberViewState)
        FilesViewState::rememberFilter(m_filterHash, m_files->filterText());
}

void TorrentDetails::switchModels(const QString &hash)
{
    if (m_filterSaveTimer->isActive())
        saveFilesFilter();
    m_switching = true;
    m_files->clear();
    m_filterHash = hash;
    if (m_rememberViewState)
        m_files->setFilterText(FilesViewState::filterFor(hash));
    m_switching = false;
    m_trackers->clear();
    m_peers->clear();
    m_webSeeds->clear();
}

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
    m_properties.clear();
    m_errorLogged = false;
    setLastError(QString());
    switchModels(hash);
    m_pollTick = 0;
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

void TorrentDetails::setWebSeedsActive(bool active)
{
    if (active == m_webSeedsActive)
        return;
    m_webSeedsActive = active;
    if (active && !m_hash.isEmpty() && !m_demo)
        pollWebSeeds();
}

void TorrentDetails::refreshWebSeeds()
{
    pollWebSeeds();
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
    const bool canPoll = !m_demo && !m_client.isNull() && !m_hash.isEmpty() && m_visible && !m_gone;
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
    return m_torrents.isNull() || m_torrents->rowCount() == 0 ||
           m_torrents->indexOfHash(m_hash) >= 0;
}

void TorrentDetails::poll()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !torrentExists())
        return;

    const QString hash = m_hash;

    m_client->torrentsProperties(hash)->then(this, [this, hash](const Result<QJsonObject> &result) {
        if (hash != m_hash)
            return; // the selection moved on while this was in flight
        if (result.isError()) {
            noteError(QStringLiteral("properties"), result.error());
            return;
        }
        noteSuccess();
        applyProperties(QbtTranslate::properties(result.value()));
        Q_EMIT refreshed();
    });

    m_client->torrentsFiles(hash)->then(this, [this, hash](const Result<QJsonArray> &result) {
        if (hash != m_hash)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("files"), result.error());
            return;
        }
        noteSuccess();
        m_files->applyFiles(QbtTranslate::files(result.value()));
    });

    m_client->torrentsTrackers(hash)->then(this, [this, hash](const Result<QJsonArray> &result) {
        if (hash != m_hash)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("trackers"), result.error());
            return;
        }
        noteSuccess();
        m_trackers->applyTrackers(QbtTranslate::trackers(result.value()));
    });

    if (m_webSeedsActive || m_pollTick % kWebSeedsIdleTicks == 0)
        pollWebSeeds();
    ++m_pollTick;

    if (m_generalActive)
        pollPieceStates();
}

void TorrentDetails::pollWebSeeds()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !torrentExists())
        return;

    const QString hash = m_hash;
    m_client->torrentsWebSeeds(hash)->then(this, [this, hash](const Result<QJsonArray> &result) {
        if (hash != m_hash)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("webseeds"), result.error());
            return;
        }
        noteSuccess();
        m_webSeeds->applyWebSeeds(QbtTranslate::webSeeds(result.value()));
    });
}

void TorrentDetails::pollPieceStates()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !m_generalActive || !torrentExists())
        return;

    const QString hash = m_hash;
    m_client->torrentsPieceStates(hash)->then(this, [this, hash](const Result<QJsonArray> &result) {
        if (hash != m_hash || !m_generalActive)
            return;
        if (result.isError()) {
            noteError(QStringLiteral("pieceStates"), result.error());
            return;
        }
        noteSuccess();
        setPieceStates(QbtTranslate::pieceStates(result.value()));
    });
}

void TorrentDetails::pollPeers()
{
    if (m_demo || m_client.isNull() || m_hash.isEmpty() || !m_peersActive || !torrentExists())
        return;

    const QString hash = m_hash;
    m_client->torrentsPeers(hash, m_peers->rid())
        ->then(this, [this, hash](const Result<QJsonObject> &result) {
            if (hash != m_hash || !m_peersActive)
                return;
            if (result.isError()) {
                noteError(QStringLiteral("peers"), result.error());
                return;
            }
            noteSuccess();
            m_peers->applyPeers(QbtTranslate::peers(result.value()));
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

void TorrentDetails::applyProperties(const TorrentProperties &p)
{
    QVariantMap map;
    map.insert(QStringLiteral("name"), p.name);
    map.insert(QStringLiteral("savePath"), p.savePath);
    map.insert(QStringLiteral("hashV1"), p.infohashV1);
    map.insert(QStringLiteral("hashV2"), p.infohashV2);
    map.insert(QStringLiteral("totalSizeText"), Format::size(p.totalSize));
    map.insert(
        QStringLiteral("downloaded"),
        QStringLiteral("%1 of %2").arg(Format::size(p.totalDownloaded), Format::size(p.totalSize)));
    map.insert(QStringLiteral("uploaded"), Format::size(p.totalUploaded));
    map.insert(QStringLiteral("wasted"), Format::size(p.totalWasted));
    map.insert(QStringLiteral("pieces"), Format::pieceLayout(p.piecesNum, p.pieceSize));
    map.insert(QStringLiteral("piecesHave"), Format::piecesHave(p.piecesHave, p.piecesNum));
    // No figure (every seeding torrent has none): no row, rather than a "0.00" that reads as
    // "no peer has the data".
    map.insert(QStringLiteral("availability"),
               p.availability ? Format::ratio(*p.availability) : QString());
    map.insert(QStringLiteral("seeds"), Format::connectedTotal(p.seeds, p.seedsTotal));
    map.insert(QStringLiteral("peers"), Format::connectedTotal(p.peers, p.peersTotal));
    map.insert(QStringLiteral("timeActive"), Format::duration(p.timeElapsed));
    map.insert(QStringLiteral("createdBy"), p.createdBy);
    map.insert(QStringLiteral("comment"), p.comment);

    const auto when = [](const QDateTime &at) {
        return at.isValid() ? Format::relativeTime(at) : QString();
    };
    map.insert(QStringLiteral("createdOn"), when(p.createdOn));

    // docs/12 G2: a figure the server does not know turns into an empty string, and the view
    // drops the row.
    map.insert(QStringLiteral("connections"),
               p.connections ? Format::connections(*p.connections, p.connectionsLimit) : QString());
    // "Never" is the answer that matters for a stalled torrent, but only once there is
    // metadata to be complete against.
    QString lastSeen = p.lastSeen ? when(*p.lastSeen) : QString();
    if (lastSeen.isEmpty() && p.lastSeen && p.piecesNum > 0)
        lastSeen = QStringLiteral("never");
    map.insert(QStringLiteral("lastSeen"), lastSeen);
    map.insert(QStringLiteral("seedingTime"),
               p.seedingTime > 0 ? Format::duration(p.seedingTime) : QString());
    map.insert(QStringLiteral("completedOn"), when(p.completedOn));
    map.insert(QStringLiteral("avgSpeed"),
               p.dlSpeedAvg || p.upSpeedAvg
                   ? Format::averageSpeeds(p.dlSpeedAvg.value_or(-1), p.upSpeedAvg.value_or(-1))
                   : QString());
    map.insert(QStringLiteral("reannounce"), Format::reannounceIn(p.reannounce));

    QString privateText;
    if (p.isPrivate)
        privateText = *p.isPrivate ? QStringLiteral("yes") : QStringLiteral("no");
    map.insert(QStringLiteral("private"), privateText);

    // torrents/properties has no share limits and no magnet; the torrent row of
    // sync/maindata has both.
    RatioLimit ratioLimit;
    MinutesLimit seedingTimeLimit;
    QString magnet;
    if (!m_torrents.isNull() && !m_hash.isEmpty() && m_torrents->indexOfHash(m_hash) >= 0) {
        const Torrent &row = m_torrents->torrent(m_hash);
        ratioLimit = row.ratioLimit;
        seedingTimeLimit = row.seedingTimeLimit;
        magnet = row.magnetUri;
    }
    map.insert(QStringLiteral("shareLimit"),
               Format::shareLimit(ratioLimit.code(), seedingTimeLimit.code()));
    map.insert(QStringLiteral("magnet"), magnet);
    map.insert(QStringLiteral("magnetText"), Format::shortMagnet(magnet));

    if (map == m_properties)
        return;
    m_properties = map;
    Q_EMIT propertiesChanged();
}

// --- the demo ------------------------------------------------------------------------------

void TorrentDetails::applyDemo(const TorrentDetailsData &details)
{
    m_demo = true;
    m_client.clear();
    m_timer->stop();
    m_peersTimer->stop();

    const QString &hash = details.hash;
    if (hash != m_hash) {
        m_hash = hash;
        switchModels(hash);
        Q_EMIT hashChanged();
    }

    m_errorLogged = false;
    setLastError(QString());

    applyProperties(details.properties);
    m_files->applyFiles(details.files);
    m_trackers->applyTrackers(details.trackers);
    m_peers->applyPeers(details.peers);
    m_webSeeds->applyWebSeeds(details.webSeeds);
    setPieceStates(details.pieceStates);
    Q_EMIT refreshed();
}

} // namespace omaseed::core
