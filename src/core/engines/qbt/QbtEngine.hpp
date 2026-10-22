#pragma once

// QbtEngine — qBittorrent behind EngineClient (docs/36 §4, docs/DECISIONS.md C14 E2 a). It
// wraps the transport, QbtClient, and translates both ways at this edge: answers through
// QbtTranslate, and on the way out omaseed's values in qBittorrent's spelling (FilePriority's
// numbers, -2 and -1 for a share limit that follows the server or has none, the add's form
// fields, the share-limit action's names).
//
// qBittorrent's own surfaces (its settings pane, settings export and import, the torrents
// export and import, RSS) live beside it under src/core/engines/qbt/ and take this engine:
// App reaches it, when the feature record says the server has them, through
// App::qbtEngine() (docs/36 §4.4). They talk to client(), the one place outside this
// directory never sees.

#include "core/engines/EngineClient.hpp"

namespace omaseed::core {

class QbtClient;

class QbtEngine final : public EngineClient
{
    Q_OBJECT

public:
    explicit QbtEngine(QObject *parent = nullptr);
    ~QbtEngine() override;

    /// The transport, for qBittorrent's own surfaces under src/core/engines/qbt/.
    QbtClient *client() const { return m_client; }

    /// RSS (EngineFeatures::rss): how many feeds and how many download rules the server has.
    Reply<int> *rssFeedCount();
    Reply<int> *rssRuleCount();

    // --- EngineClient ----------------------------------------------------------------------
    QUrl baseUrl() const override;
    void setBaseUrl(const QUrl &baseUrl) override;
    void setApiKey(const QString &apiKey) override;
    bool hasApiKey() const override;
    void setCredentials(const QString &username, const QString &password) override;
    void clearAuth() override;
    void allowLoginAgain() override;
    void setPinnedCertificate(const QString &sha256) override;
    void setTimeout(std::chrono::milliseconds timeout) override;
    int maxRetries() const override;
    void setMaxRetries(int retries) override;
    void cancelAll() override;
    QUrl reachableUrl(const QUrl &base) const override;

    Reply<ServerInfo> *probe() override;
    EngineFeatures features(const ServerInfo &info) const override;

    Reply<SyncDelta> *sync(int cursor) override;
    Reply<QList<TorrentPatch>> *torrents(const QStringList &hashes = {}) override;
    Reply<QStringList> *stoppedAmong(const QStringList &hashes) override;
    Reply<QList<CategoryPatch>> *categories() override;
    Reply<QStringList> *tags() override;
    Reply<TorrentProperties> *properties(const QString &hash) override;
    Reply<QList<ContentFile>> *files(const QString &hash) override;
    Reply<QList<TrackerEntry>> *trackers(const QString &hash) override;
    Reply<PeersDelta> *peers(const QString &hash, int cursor) override;
    Reply<QStringList> *webSeeds(const QString &hash) override;
    Reply<QByteArray> *pieceStates(const QString &hash) override;
    Reply<QList<LogEntry>> *log(int afterId, bool problemsOnly) override;
    Reply<FetchedMetadata> *fetchMetadata(const QString &source) override;
    Reply<ServerFolder> *folderContent(const QString &path) override;

    Reply<Done> *start(const QStringList &hashes) override;
    Reply<Done> *stop(const QStringList &hashes) override;
    Reply<Done> *startAll() override;
    Reply<Done> *stopAll() override;
    Reply<Done> *setForceStart(const QStringList &hashes, bool on) override;
    Reply<Done> *recheck(const QStringList &hashes) override;
    Reply<Done> *reannounce(const QStringList &hashes) override;
    Reply<Done> *rename(const QString &hash, const QString &name) override;
    Reply<Done> *setLocation(const QStringList &hashes, const QString &path) override;
    Reply<Done> *
    renameFile(const QString &hash, const QString &oldPath, const QString &newPath) override;
    Reply<Done> *
    renameFolder(const QString &hash, const QString &oldPath, const QString &newPath) override;
    Reply<Done> *
    setFilePriority(const QString &hash, const QList<int> &fileIds, FilePriority priority) override;
    Reply<Done> *queueUp(const QStringList &hashes) override;
    Reply<Done> *queueDown(const QStringList &hashes) override;
    Reply<Done> *toggleSequentialDownload(const QStringList &hashes) override;
    Reply<Done> *toggleFirstLastPiece(const QStringList &hashes) override;
    Reply<Done> *setSuperSeeding(const QStringList &hashes, bool on) override;
    Reply<Done> *setAutoManagement(const QStringList &hashes, bool on) override;
    Reply<Done> *remove(const QStringList &hashes, bool deleteFiles) override;

    Reply<Done> *addTrackers(const QString &hash, const QStringList &urls) override;
    Reply<Done> *removeTrackers(const QString &hash, const QStringList &urls) override;
    Reply<Done> *addWebSeeds(const QString &hash, const QStringList &urls) override;
    Reply<Done> *removeWebSeeds(const QString &hash, const QStringList &urls) override;
    Reply<Done> *addPeers(const QStringList &hashes, const QStringList &peers) override;
    Reply<Done> *banPeers(const QStringList &peers) override;

    Reply<Done> *setCategory(const QStringList &hashes, const QString &category) override;
    Reply<Done> *createCategory(const QString &name, const QString &savePath = {}) override;
    Reply<Done> *removeCategories(const QStringList &names) override;
    Reply<Done> *addTags(const QStringList &hashes, const QStringList &tags) override;
    Reply<Done> *removeTags(const QStringList &hashes, const QStringList &tags) override;
    Reply<Done> *createTags(const QStringList &tags) override;
    Reply<Done> *deleteTags(const QStringList &tags) override;

    Reply<Done> *setDownloadLimit(const QStringList &hashes, qint64 bytesPerSec) override;
    Reply<Done> *setUploadLimit(const QStringList &hashes, qint64 bytesPerSec) override;
    Reply<Done> *setShareLimits(const QStringList &hashes,
                                RatioLimit ratio,
                                MinutesLimit seedingTime,
                                MinutesLimit inactiveSeedingTime,
                                const std::optional<QString> &action) override;
    Reply<GlobalLimits> *globalLimits() override;
    Reply<Done> *setGlobalLimits(const GlobalLimits &limits) override;
    Reply<Done> *toggleAltSpeed() override;
    Reply<Done> *setAltSpeed(bool on) override;

    Reply<AddResult> *addUrls(const QStringList &urls, const AddOptions &options) override;
    Reply<AddResult> *
    addFile(const QString &fileName, const QByteArray &bytes, const AddOptions &options) override;
    Reply<AddResult> *addMany(const QList<AddFile> &files,
                              const QStringList &urls,
                              const AddOptions &options) override;

private:
    Reply<Done> *done(Reply<QByteArray> *reply);
    Reply<AddResult> *added(Reply<QByteArray> *reply);

    QbtClient *m_client = nullptr;
    /// app/webapiVersion from the last probe of this server, for the add's skip-checking
    /// spelling (QbtClient::supportsSeedMode); empty until then, and both spellings go.
    QString m_apiVersion;
};

} // namespace omaseed::core
