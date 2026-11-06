#pragma once

// TransmissionEngine — Transmission 4.1 and later behind EngineClient (docs/36 §3, §5, §6 E4;
// DECISIONS C14 E3, C25; issue #85). It speaks JSON-RPC 2.0 through TransmissionRpc (the
// session id, Basic auth, the endpoint), reads answers through TransmissionTranslate, and
// makes deltas of Transmission's whole objects with SnapshotDiffer:
//   * a poll asks for the torrents active in Transmission's last 60 s (`recently_active`, with
//     the `removed` ids), its session figures and its free space, in one batch; the first poll,
//     a cursor the engine didn't hand out, or a gap over 50 s since the last answered poll asks
//     for every torrent instead (C14, #85), so nothing that moved in the gap is missed;
//   * Transmission's integer ids stay in here (only `removed` needs them, mapped back to
//     hashes); everything else is asked by hash, which Transmission takes as an id;
//   * an empty `ids` means every torrent to Transmission, so a call for no torrents never
//     reaches the wire;
//   * labels, tracker lists and the sequential switch are replaced whole: read, changed, and
//     written back per group of torrents that end up alike.
//
// What Transmission can't do settles as "not supported" (EngineClient::unsupported), and its
// feature record (TransmissionTranslate::features) says so beforehand, so the views never
// offer it (C14 E4).

#include "core/engines/EngineClient.hpp"
#include "core/engines/SnapshotDiffer.hpp"
#include "core/engines/transmission/TransmissionTranslate.hpp"

#include <functional>

namespace omaseed::core {

class TransmissionRpc;

class TransmissionEngine final : public EngineClient
{
    Q_OBJECT

public:
    explicit TransmissionEngine(QObject *parent = nullptr);
    ~TransmissionEngine() override;

    /// The wire, for the tests' witnesses.
    TransmissionRpc *rpc() const { return m_rpc; }
    /// Milliseconds on a steady clock: what the 50 s gap is measured with. Tests set their own.
    void setClock(std::function<qint64()> msNow);
    /// How long (ms) polls may go unanswered before the next one asks for everything
    /// (Transmission's `recently_active` covers 60 s; C14, #85).
    static constexpr qint64 kFullListGapMs = 50000;

    // --- EngineClient ----------------------------------------------------------------------
    QUrl baseUrl() const override;
    void setBaseUrl(const QUrl &baseUrl) override;
    /// Transmission has no API keys: ignored.
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
    Reply<QList<RunningTorrent>> *running() override;
    Reply<QList<CategoryPatch>> *categories() override;
    Reply<QStringList> *tags() override;
    Reply<TorrentProperties> *properties(const QString &hash) override;
    Reply<QList<ContentFile>> *files(const QString &hash) override;
    Reply<QList<TrackerEntry>> *trackers(const QString &hash) override;
    Reply<PeersDelta> *peers(const QString &hash, int cursor) override;
    Reply<QStringList> *webSeeds(const QString &hash) override;
    Reply<QByteArray> *pieceStates(const QString &hash) override;

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
    Reply<QVariantMap> *sessionPreferences() override;
    Reply<Done> *setSessionPreferences(const QVariantMap &changes) override;
    Reply<Done> *toggleAltSpeed() override;
    Reply<Done> *setAltSpeed(bool on) override;

    Reply<AddResult> *addUrls(const QStringList &urls, const AddOptions &options) override;
    Reply<AddResult> *
    addFile(const QString &fileName, const QByteArray &bytes, const AddOptions &options) override;
    Reply<AddResult> *addMany(const QList<AddFile> &files,
                              const QStringList &urls,
                              const AddOptions &options) override;

private:
    using Results = QList<Result<QJsonObject>>;

    /// A reply that settles on the next turn with `result`.
    template<class T>
    Reply<T> *settled(Result<T> result);
    Reply<Done> *doneNow() { return settled<Done>(Done{}); }
    Reply<Done> *refused(const QString &why);
    /// `method` on `hashes` (never on none: an empty `ids` is every torrent to Transmission).
    Reply<Done> *
    forTorrents(const QString &method, const QStringList &hashes, QJsonObject params = {});
    Reply<Done> *done(Reply<QJsonObject> *reply);
    /// Every call of a batch went through, or the first that didn't.
    Reply<Done> *doneAll(Reply<Results> *reply);
    /// torrent_get of `fields` for `hashes` (every torrent when empty), as objects.
    Reply<QList<QJsonObject>> *torrentRows(const QStringList &hashes, const QStringList &fields);
    /// One torrent's `fields`; NotFound when Transmission has no such torrent.
    Reply<QJsonObject> *oneTorrent(const QString &hash, const QStringList &fields);
    /// `first`, then what `next` makes of its value: one reply for both.
    template<class T, class U>
    Reply<U> *then(Reply<T> *first, std::function<Reply<U> *(const T &)> next);
    /// Reads the labels of `hashes` (all torrents when empty), changes each with `change`,
    /// and writes back those that changed, one torrent_set per resulting set.
    Reply<Done> *relabel(const QStringList &hashes,
                         std::function<QStringList(const QStringList &)> change);
    Reply<Done> *renamePath(const QString &hash, const QString &oldPath, const QString &newPath);
    Reply<Done> *speedLimit(const QStringList &hashes, qint64 bytesPerSec, const char *which);
    Reply<AddResult> *add(const QList<QJsonObject> &sources, const AddOptions &options);
    QList<SnapshotDiffer::Entry> entries(const QList<QJsonObject> &rows);
    void forget();

    TransmissionRpc *m_rpc = nullptr;
    SnapshotDiffer m_differ;
    TransmissionTranslate::Session m_session;
    QString m_downloadDir;           ///< the session's, for the poll's free_space
    QHash<qint64, QString> m_hashOf; ///< Transmission's id → hash, for `removed`
    int m_cursor = 0;
    std::optional<qint64> m_lastPollAt; ///< when the last answered poll was asked (ms)
    std::function<qint64()> m_now;
};

} // namespace omaseed::core
