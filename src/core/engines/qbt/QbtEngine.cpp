#include "core/engines/qbt/QbtEngine.hpp"

#include "core/engines/qbt/QbtClient.hpp"
#include "core/engines/qbt/QbtSettingsExport.hpp"
#include "core/engines/qbt/QbtTranslate.hpp"

#include <QJsonDocument>
#include <QPointer>
#include <QUrlQuery>

namespace omaseed::core {

namespace {

// --- the way out: omaseed's values in qBittorrent's words -----------------------------------

/// torrents/filePrio and the add's filePriorities take FilePriority's own numbers (the
/// reference engine's, docs/36 §4.1). Mixed is not a priority a file can be given.
int wirePriority(FilePriority priority)
{
    switch (priority) {
    case FilePriority::Skip:
        return 0;
    case FilePriority::High:
        return 6;
    case FilePriority::Maximal:
        return 7;
    case FilePriority::Normal:
    case FilePriority::Mixed:
        break;
    }
    return 1;
}

/// torrents/setShareLimits spells "the server's default" -2 and "none" -1.
double wireRatio(RatioLimit limit)
{
    switch (limit.mode) {
    case LimitMode::Global:
        return -2.0;
    case LimitMode::Unlimited:
        return -1.0;
    case LimitMode::Value:
        break;
    }
    return limit.ratio;
}

qint64 wireMinutes(MinutesLimit limit)
{
    switch (limit.mode) {
    case LimitMode::Global:
        return -2;
    case LimitMode::Unlimited:
        return -1;
    case LimitMode::Value:
        break;
    }
    return limit.minutes;
}

QbtClient::ContentLayout wireLayout(ContentLayout layout)
{
    switch (layout) {
    case ContentLayout::Subfolder:
        return QbtClient::ContentLayout::Subfolder;
    case ContentLayout::NoSubfolder:
        return QbtClient::ContentLayout::NoSubfolder;
    case ContentLayout::Original:
        break;
    }
    return QbtClient::ContentLayout::Original;
}

QString wireStopCondition(StopCondition condition)
{
    return condition == StopCondition::FilesChecked ? QStringLiteral("FilesChecked")
                                                    : QStringLiteral("MetadataReceived");
}

QbtClient::AddOptions wireAddOptions(const AddOptions &options)
{
    QbtClient::AddOptions wire;
    wire.savepath = options.savePath;
    wire.category = options.category;
    wire.tags = options.tags;
    wire.stopped = options.stopped;
    wire.skipChecking = options.skipChecking;
    if (options.contentLayout)
        wire.contentLayout = wireLayout(*options.contentLayout);
    wire.rename = options.rename;
    wire.upLimit = options.upLimit;
    wire.dlLimit = options.dlLimit;
    wire.autoTMM = options.autoManagement;
    wire.sequentialDownload = options.sequentialDownload;
    wire.firstLastPiecePrio = options.firstLastPiece;
    for (const FilePriority priority : options.filePriorities)
        wire.filePriorities.append(wirePriority(priority));
    if (options.stopCondition)
        wire.stopCondition = wireStopCondition(*options.stopCondition);
    return wire;
}

QList<QbtClient::AddFile> wireFiles(const QList<AddFile> &files)
{
    QList<QbtClient::AddFile> wire;
    wire.reserve(files.size());
    for (const AddFile &file : files)
        wire.append(QbtClient::AddFile{file.fileName, file.bytes});
    return wire;
}

/// torrents/add's answer: 5.x lists the ids it added; 4.x says "Ok." or, refusing the
/// payload, "Fails." with a 200 (5.x answers 409 instead, an error before this).
AddResult addResult(const QByteArray &body)
{
    AddResult result;
    if (QString::fromUtf8(body).trimmed().compare(QStringLiteral("Fails."), Qt::CaseInsensitive) ==
        0) {
        result.refused = true;
        return result;
    }
    const QJsonArray added =
        QJsonDocument::fromJson(body).object().value(QStringLiteral("added_torrent_ids")).toArray();
    for (const QJsonValue &id : added)
        result.addedIds.append(id.toString());
    return result;
}

const QStringList kAll{QStringLiteral("all")};

} // namespace

QbtEngine::QbtEngine(QObject *parent)
    : EngineClient(parent)
    , m_client(new QbtClient(this))
{}

QbtEngine::~QbtEngine() = default;

Reply<Done> *QbtEngine::done(Reply<QByteArray> *reply)
{
    return mapReply<Done>(reply, this, [](const QByteArray &) { return Done{}; });
}

Reply<AddResult> *QbtEngine::added(Reply<QByteArray> *reply)
{
    return mapReply<AddResult>(reply, this, addResult);
}

// --- qBittorrent's own ---------------------------------------------------------------------

Reply<int> *QbtEngine::rssFeedCount()
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("withData"), QStringLiteral("false"));
    return mapReply<int>(
        m_client->get(QStringLiteral("rss/items"), query), this, [](const QByteArray &body) {
            QbtSettingsSnapshot snapshot;
            snapshot.rssItems = QJsonDocument::fromJson(body).object();
            return snapshot.feedCount();
        });
}

Reply<int> *QbtEngine::rssRuleCount()
{
    return mapReply<int>(
        m_client->get(QStringLiteral("rss/rules")), this, [](const QByteArray &body) {
            return int(QJsonDocument::fromJson(body).object().size());
        });
}

// --- the connection ------------------------------------------------------------------------

QUrl QbtEngine::baseUrl() const
{
    return m_client->baseUrl();
}

void QbtEngine::setBaseUrl(const QUrl &baseUrl)
{
    m_client->setBaseUrl(baseUrl);
}

void QbtEngine::setApiKey(const QString &apiKey)
{
    m_client->setApiKey(apiKey);
}

bool QbtEngine::hasApiKey() const
{
    return m_client->hasApiKey();
}

void QbtEngine::setCredentials(const QString &username, const QString &password)
{
    m_client->setCredentials(username, password);
}

void QbtEngine::clearAuth()
{
    m_client->clearAuth();
}

void QbtEngine::allowLoginAgain()
{
    m_client->allowLoginAgain();
}

void QbtEngine::setPinnedCertificate(const QString &sha256)
{
    m_client->setPinnedCertificate(sha256);
}

void QbtEngine::setTimeout(std::chrono::milliseconds timeout)
{
    m_client->setTimeout(timeout);
}

int QbtEngine::maxRetries() const
{
    return m_client->maxRetries();
}

void QbtEngine::setMaxRetries(int retries)
{
    m_client->setMaxRetries(retries);
}

void QbtEngine::cancelAll()
{
    m_client->cancelAll();
}

QUrl QbtEngine::reachableUrl(const QUrl &base) const
{
    // app/version answers 403 to a stranger, which is still an answer.
    QUrl url = base;
    QString path = url.path();
    if (!path.endsWith(QLatin1Char('/')))
        path += QLatin1Char('/');
    url.setPath(path + QStringLiteral("api/v2/app/version"));
    return url;
}

// --- what the server is --------------------------------------------------------------------

Reply<ServerInfo> *QbtEngine::probe()
{
    return mapReply<ServerInfo>(
        m_client->probe(), this, [](const ServerInfo &info) { return info; });
}

EngineFeatures QbtEngine::features(const ServerInfo &info) const
{
    return QbtTranslate::features(info.apiVersion);
}

// --- sync and details ----------------------------------------------------------------------

Reply<SyncDelta> *QbtEngine::sync(int cursor)
{
    return mapReply<SyncDelta>(m_client->syncMainData(cursor), this, QbtTranslate::syncDelta);
}

Reply<QList<TorrentPatch>> *QbtEngine::torrents(const QStringList &hashes)
{
    TorrentsInfoQuery query;
    query.hashes = hashes;
    return mapReply<QList<TorrentPatch>>(
        m_client->torrentsInfo(query), this, [](const QJsonArray &array) {
            QList<TorrentPatch> patches;
            patches.reserve(array.size());
            for (const QJsonValue &value : array) {
                const QJsonObject json = value.toObject();
                patches.append(
                    QbtTranslate::torrent(json.value(QStringLiteral("hash")).toString(), json));
            }
            return patches;
        });
}

Reply<QStringList> *QbtEngine::stoppedAmong(const QStringList &hashes)
{
    // qBittorrent ranks an error above a stop, so `state` says only `error`; its `stopped`
    // filter tests the stop itself (scripts/dev/research/107-errored-stopped).
    TorrentsInfoQuery query;
    query.filter = QStringLiteral("stopped");
    query.hashes = hashes;
    return mapReply<QStringList>(m_client->torrentsInfo(query), this, [](const QJsonArray &array) {
        QStringList stopped;
        stopped.reserve(array.size());
        for (const QJsonValue &value : array)
            stopped.append(value.toObject().value(QStringLiteral("hash")).toString());
        return stopped;
    });
}

Reply<QList<CategoryPatch>> *QbtEngine::categories()
{
    return mapReply<QList<CategoryPatch>>(
        m_client->torrentsCategories(), this, QbtTranslate::categories);
}

Reply<QStringList> *QbtEngine::tags()
{
    return mapReply<QStringList>(m_client->torrentsTags(), this, [](const QJsonArray &array) {
        QStringList tags;
        for (const QJsonValue &tag : array)
            tags.append(tag.toString());
        return tags;
    });
}

Reply<TorrentProperties> *QbtEngine::properties(const QString &hash)
{
    return mapReply<TorrentProperties>(
        m_client->torrentsProperties(hash), this, QbtTranslate::properties);
}

Reply<QList<ContentFile>> *QbtEngine::files(const QString &hash)
{
    return mapReply<QList<ContentFile>>(m_client->torrentsFiles(hash), this, QbtTranslate::files);
}

Reply<QList<TrackerEntry>> *QbtEngine::trackers(const QString &hash)
{
    return mapReply<QList<TrackerEntry>>(
        m_client->torrentsTrackers(hash), this, [](const QJsonArray &array) {
            return QbtTranslate::trackers(array);
        });
}

Reply<PeersDelta> *QbtEngine::peers(const QString &hash, int cursor)
{
    return mapReply<PeersDelta>(m_client->torrentsPeers(hash, cursor), this, QbtTranslate::peers);
}

Reply<QStringList> *QbtEngine::webSeeds(const QString &hash)
{
    return mapReply<QStringList>(m_client->torrentsWebSeeds(hash), this, QbtTranslate::webSeeds);
}

Reply<QByteArray> *QbtEngine::pieceStates(const QString &hash)
{
    return mapReply<QByteArray>(
        m_client->torrentsPieceStates(hash), this, QbtTranslate::pieceStates);
}

Reply<QList<LogEntry>> *QbtEngine::log(int afterId, bool problemsOnly)
{
    Reply<QJsonArray> *reply =
        problemsOnly ? m_client->logMainProblems(afterId) : m_client->logMain(afterId);
    return mapReply<QList<LogEntry>>(reply, this, QbtTranslate::log);
}

Reply<FetchedMetadata> *QbtEngine::fetchMetadata(const QString &source)
{
    return mapReply<FetchedMetadata>(m_client->torrentsFetchMetadata(source),
                                     this,
                                     [](const QbtClient::FetchedMetadata &answer) {
                                         FetchedMetadata fetched;
                                         fetched.ready = answer.ready;
                                         if (answer.ready)
                                             fetched.metadata = QbtTranslate::metadata(answer.body);
                                         return fetched;
                                     });
}

Reply<ServerFolder> *QbtEngine::folderContent(const QString &path)
{
    // A refusal is an answer here too: it says the folder is missing, or the endpoint is.
    auto *listed = new Reply<ServerFolder>(this);
    QPointer<Reply<QJsonArray>> source = m_client->appDirectoryContent(path);
    connect(listed, &ReplyBase::abortRequested, listed, [source]() {
        if (!source.isNull())
            source->abort();
    });
    source->then(listed, [listed](const Result<QJsonArray> &result) {
        listed->complete(Result<ServerFolder>(QbtTranslate::serverFolder(result)));
    });
    return listed;
}

// --- actions -------------------------------------------------------------------------------

Reply<Done> *QbtEngine::start(const QStringList &hashes)
{
    return done(m_client->torrentsStart(hashes));
}

Reply<Done> *QbtEngine::stop(const QStringList &hashes)
{
    return done(m_client->torrentsStop(hashes));
}

Reply<Done> *QbtEngine::startAll()
{
    return done(m_client->torrentsStart(kAll));
}

Reply<Done> *QbtEngine::stopAll()
{
    return done(m_client->torrentsStop(kAll));
}

Reply<Done> *QbtEngine::setForceStart(const QStringList &hashes, bool on)
{
    return done(m_client->torrentsSetForceStart(hashes, on));
}

Reply<Done> *QbtEngine::recheck(const QStringList &hashes)
{
    return done(m_client->torrentsRecheck(hashes));
}

Reply<Done> *QbtEngine::reannounce(const QStringList &hashes)
{
    return done(m_client->torrentsReannounce(hashes));
}

Reply<Done> *QbtEngine::rename(const QString &hash, const QString &name)
{
    return done(m_client->torrentsRename(hash, name));
}

Reply<Done> *QbtEngine::setLocation(const QStringList &hashes, const QString &path)
{
    return done(m_client->torrentsSetLocation(hashes, path));
}

Reply<Done> *
QbtEngine::renameFile(const QString &hash, const QString &oldPath, const QString &newPath)
{
    return done(m_client->torrentsRenameFile(hash, oldPath, newPath));
}

Reply<Done> *
QbtEngine::renameFolder(const QString &hash, const QString &oldPath, const QString &newPath)
{
    return done(m_client->torrentsRenameFolder(hash, oldPath, newPath));
}

Reply<Done> *
QbtEngine::setFilePriority(const QString &hash, const QList<int> &fileIds, FilePriority priority)
{
    return done(m_client->torrentsSetFilePrio(hash, fileIds, wirePriority(priority)));
}

Reply<Done> *QbtEngine::queueUp(const QStringList &hashes)
{
    return done(m_client->torrentsIncreasePrio(hashes));
}

Reply<Done> *QbtEngine::queueDown(const QStringList &hashes)
{
    return done(m_client->torrentsDecreasePrio(hashes));
}

Reply<Done> *QbtEngine::toggleSequentialDownload(const QStringList &hashes)
{
    return done(m_client->torrentsToggleSequentialDownload(hashes));
}

Reply<Done> *QbtEngine::toggleFirstLastPiece(const QStringList &hashes)
{
    return done(m_client->torrentsToggleFirstLastPiecePrio(hashes));
}

Reply<Done> *QbtEngine::setSuperSeeding(const QStringList &hashes, bool on)
{
    return done(m_client->torrentsSetSuperSeeding(hashes, on));
}

Reply<Done> *QbtEngine::setAutoManagement(const QStringList &hashes, bool on)
{
    return done(m_client->torrentsSetAutoManagement(hashes, on));
}

Reply<Done> *QbtEngine::remove(const QStringList &hashes, bool deleteFiles)
{
    return done(m_client->torrentsDelete(hashes, deleteFiles));
}

Reply<Done> *QbtEngine::addTrackers(const QString &hash, const QStringList &urls)
{
    return done(m_client->torrentsAddTrackers(hash, urls));
}

Reply<Done> *QbtEngine::removeTrackers(const QString &hash, const QStringList &urls)
{
    return done(m_client->torrentsRemoveTrackers(hash, urls));
}

Reply<Done> *QbtEngine::addWebSeeds(const QString &hash, const QStringList &urls)
{
    return done(m_client->torrentsAddWebSeeds(hash, urls));
}

Reply<Done> *QbtEngine::removeWebSeeds(const QString &hash, const QStringList &urls)
{
    return done(m_client->torrentsRemoveWebSeeds(hash, urls));
}

Reply<Done> *QbtEngine::addPeers(const QStringList &hashes, const QStringList &peers)
{
    return done(m_client->torrentsAddPeers(hashes, peers));
}

Reply<Done> *QbtEngine::banPeers(const QStringList &peers)
{
    return done(m_client->transferBanPeers(peers));
}

// --- labels --------------------------------------------------------------------------------

Reply<Done> *QbtEngine::setCategory(const QStringList &hashes, const QString &category)
{
    return done(m_client->torrentsSetCategory(hashes, category));
}

Reply<Done> *QbtEngine::createCategory(const QString &name, const QString &savePath)
{
    return done(m_client->torrentsCreateCategory(name, savePath));
}

Reply<Done> *QbtEngine::removeCategories(const QStringList &names)
{
    return done(m_client->torrentsRemoveCategories(names));
}

Reply<Done> *QbtEngine::addTags(const QStringList &hashes, const QStringList &tags)
{
    return done(m_client->torrentsAddTags(hashes, tags));
}

Reply<Done> *QbtEngine::removeTags(const QStringList &hashes, const QStringList &tags)
{
    return done(m_client->torrentsRemoveTags(hashes, tags));
}

Reply<Done> *QbtEngine::createTags(const QStringList &tags)
{
    return done(m_client->torrentsCreateTags(tags));
}

Reply<Done> *QbtEngine::deleteTags(const QStringList &tags)
{
    return done(m_client->torrentsDeleteTags(tags));
}

// --- limits --------------------------------------------------------------------------------

Reply<Done> *QbtEngine::setDownloadLimit(const QStringList &hashes, qint64 bytesPerSec)
{
    return done(m_client->torrentsSetDownloadLimit(hashes, bytesPerSec));
}

Reply<Done> *QbtEngine::setUploadLimit(const QStringList &hashes, qint64 bytesPerSec)
{
    return done(m_client->torrentsSetUploadLimit(hashes, bytesPerSec));
}

Reply<Done> *QbtEngine::setShareLimits(const QStringList &hashes,
                                       RatioLimit ratio,
                                       MinutesLimit seedingTime,
                                       MinutesLimit inactiveSeedingTime,
                                       const std::optional<QString> &action)
{
    std::optional<QbtClient::ShareLimitAction> wireAction;
    if (action)
        wireAction = QbtClient::shareLimitActionFromName(*action);
    return done(m_client->torrentsSetShareLimits(hashes,
                                                 wireRatio(ratio),
                                                 wireMinutes(seedingTime),
                                                 wireMinutes(inactiveSeedingTime),
                                                 wireAction));
}

Reply<GlobalLimits> *QbtEngine::globalLimits()
{
    // app/preferences carries these in bytes/s on 5.x (checked live on :8081).
    return mapReply<GlobalLimits>(m_client->appPreferences(), this, [](const QJsonObject &prefs) {
        GlobalLimits limits;
        limits.download = prefs.value(QStringLiteral("dl_limit")).toInteger();
        limits.upload = prefs.value(QStringLiteral("up_limit")).toInteger();
        limits.altDownload = prefs.value(QStringLiteral("alt_dl_limit")).toInteger();
        limits.altUpload = prefs.value(QStringLiteral("alt_up_limit")).toInteger();
        return limits;
    });
}

Reply<Done> *QbtEngine::setGlobalLimits(const GlobalLimits &limits)
{
    QJsonObject changes;
    const auto put = [&changes](const char *key, const std::optional<qint64> &value) {
        if (value)
            changes.insert(QLatin1String(key), *value);
    };
    put("dl_limit", limits.download);
    put("up_limit", limits.upload);
    put("alt_dl_limit", limits.altDownload);
    put("alt_up_limit", limits.altUpload);
    return done(m_client->appSetPreferences(changes));
}

Reply<Done> *QbtEngine::toggleAltSpeed()
{
    return done(m_client->transferToggleSpeedLimitsMode());
}

Reply<Done> *QbtEngine::setAltSpeed(bool on)
{
    return done(m_client->transferSetSpeedLimitsMode(on));
}

// --- adding --------------------------------------------------------------------------------

Reply<AddResult> *QbtEngine::addUrls(const QStringList &urls, const AddOptions &options)
{
    return added(m_client->torrentsAddUrls(urls, wireAddOptions(options)));
}

Reply<AddResult> *
QbtEngine::addFile(const QString &fileName, const QByteArray &bytes, const AddOptions &options)
{
    return added(m_client->torrentsAddFile(fileName, bytes, wireAddOptions(options)));
}

Reply<AddResult> *
QbtEngine::addMany(const QList<AddFile> &files, const QStringList &urls, const AddOptions &options)
{
    return added(m_client->torrentsAddMany(wireFiles(files), urls, wireAddOptions(options)));
}

} // namespace omaseed::core
