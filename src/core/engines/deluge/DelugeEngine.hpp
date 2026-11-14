#pragma once

// DelugeEngine — Deluge 2.1 and later behind EngineClient, through deluge-web's /json (docs/36
// §3, §5, §6 E6; DECISIONS C14 E3, C25; issue #87). It speaks JSON-RPC 1 through DelugeRpc (the
// password, the session cookie, the endpoint), reads answers through DelugeTranslate, and makes
// deltas of Deluge's whole list with SnapshotDiffer:
//   * a poll is web.update_ui (every torrent, the filter tree, the stats: one call), then the
//     session's totals; `diff=True` is not used: through deluge-web its cache is every web
//     session's at once (scripts/dev/research/87-deluge-prefetch);
//   * a torrent's `trackers` and `files` are lists of dictionaries: the poll asks for trackers
//     on its first call, every kTrackersEveryPolls polls and after an edit, and for the files of
//     a torrent only until it knows where its content starts; in between it keeps what they said;
//   * the Label plugin is read at the probe and on every poll (its filter in the tree): its
//     labels are the categories (C25 B), each with its move-completed folder as its path;
//   * deluge-web holds one daemon for all its users (K10, checked in board 47 §3): the engine
//     never switches it. It lists the daemons and connects one only when asked (the daemon
//     step), or, when deluge-web has none at all, to the one stored with the server;
//   * a magnet's metadata (C5, K26): one blocking prefetch at a time per magnet, 30 s, while the
//     lookup's quick polls hear "still looking"; every call starts over, so it is never cut short.
//
// What Deluge can't do settles as "not supported" (EngineClient::unsupported), and its feature
// record (DelugeTranslate::features) says so beforehand, so the views never offer it (C14 E4).

#include "core/engines/EngineClient.hpp"
#include "core/engines/SnapshotDiffer.hpp"

#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>

#include <functional>
#include <memory>
#include <optional>

namespace omaseed::core {

class DelugeRpc;

/// One daemon deluge-web knows (web.get_hosts, web.get_host_status): K10's daemon step.
struct DelugeHost
{
    QString id;
    QString host;
    int port = 0;
    QString user;
    QString status;  ///< "Online", "Offline" or "Connected", as deluge-web says
    QString version; ///< the daemon's, when it is online

    bool operator==(const DelugeHost &other) const = default;
};

class DelugeEngine final : public EngineClient
{
    Q_OBJECT

public:
    explicit DelugeEngine(QObject *parent = nullptr);
    ~DelugeEngine() override;

    /// The wire, for the tests' witnesses.
    DelugeRpc *rpc() const { return m_rpc; }
    /// Milliseconds since the epoch: lastActivity's "now", and when things were last asked.
    /// Tests set their own.
    void setClock(std::function<qint64()> msNow);

    /// A magnet prefetch's own wait (Deluge's default; research/87: every call starts over,
    /// so a shorter one would never finish on a slow swarm).
    static constexpr int kPrefetchSeconds = 30;
    /// The poll asks for every torrent's trackers this often (and after an edit).
    static constexpr int kTrackersEveryPolls = 20;
    /// And for every label's options this often (and when the labels change).
    static constexpr int kLabelsEveryPolls = 40;
    /// A torrent's files are asked again after this long while its content root is unknown.
    static constexpr qint64 kRootRetryMs = 30000;

    // --- the daemon step (C25 K10) ---------------------------------------------------------
    /// The daemons deluge-web knows, each with its status.
    Reply<QList<DelugeHost>> *hosts();
    /// Connects deluge-web to `hostId`, for every one of its users (K10: the step says so
    /// first), and stores it as this server's daemon.
    Reply<Done> *connectDaemon(const QString &hostId);
    /// The daemon stored with the server: connected only when deluge-web has none.
    void setDaemon(const QString &hostId);
    QString daemon() const { return m_hostId; }
    /// deluge-web had a daemon at the last probe or poll.
    bool daemonConnected() const { return m_daemonConnected; }
    /// The Label plugin was on at the last probe or poll (features() follows it).
    bool labelPlugin() const { return m_labelPlugin; }
    /// libtorrent's version, as the probe read it.
    QString libtorrentVersion() const { return m_libtorrentVersion; }

    // --- EngineClient ----------------------------------------------------------------------
    QUrl baseUrl() const override;
    void setBaseUrl(const QUrl &baseUrl) override;
    /// deluge-web has no API keys: ignored.
    void setApiKey(const QString &apiKey) override;
    bool hasApiKey() const override;
    /// deluge-web's one password (K7); the user name is ignored.
    void setCredentials(const QString &username, const QString &password) override;
    void clearAuth() override;
    void allowLoginAgain() override;
    void setPinnedCertificate(const QString &sha256) override;
    void setTimeout(std::chrono::milliseconds timeout) override;
    int maxRetries() const override;
    void setMaxRetries(int retries) override;
    void cancelAll() override;
    QUrl reachableUrl(const QUrl &base) const override;

    /// Signs in, makes sure deluge-web has a daemon (the stored one when it has none), and
    /// reads its version, libtorrent's and whether the Label plugin is on. Below 2.1 refused
    /// in EngineKind's words (K8); no daemon at all: Conflict, daemonConnected() false.
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
    /// A magnet's metadata by core.prefetch_magnet_metadata (K26): answers "still looking"
    /// at once while one call runs. A link to a .torrent: not supported.
    Reply<FetchedMetadata> *fetchMetadata(const QString &source,
                                          const QString &downloader = QString()) override;
    /// core.glob of the folder's entries; core.get_path_size tells an empty folder from none.
    Reply<ServerFolder> *folderContent(const QString &path) override;
    /// core.get_free_space on `path` or, while Deluge can't read it (-1), its parents.
    Reply<FreeSpaceAt> *freeSpace(const QString &path) override;

    Reply<Done> *start(const QStringList &hashes) override;
    Reply<Done> *stop(const QStringList &hashes) override;
    Reply<Done> *startAll() override;
    Reply<Done> *stopAll() override;
    Reply<Done> *setForceStart(const QStringList &hashes, bool on) override;
    Reply<Done> *recheck(const QStringList &hashes) override;
    Reply<Done> *reannounce(const QStringList &hashes) override;
    /// The name shown only (K28): Deluge's `name` option.
    Reply<Done> *rename(const QString &hash, const QString &name) override;
    /// core.move_storage: Deluge answers at once and reports the move as it goes (K21).
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

    /// The Label plugin's label (lower case; empty takes it off).
    Reply<Done> *setCategory(const QStringList &hashes, const QString &category) override;
    /// label.add; a path makes the label move its torrents there when they finish.
    Reply<Done> *createCategory(const QString &name, const QString &savePath = {}) override;
    Reply<Done> *removeCategories(const QStringList &names) override;
    Reply<Done> *addTags(const QStringList &hashes, const QStringList &tags) override;
    Reply<Done> *removeTags(const QStringList &hashes, const QStringList &tags) override;
    Reply<Done> *createTags(const QStringList &tags) override;
    Reply<Done> *deleteTags(const QStringList &tags) override;

    Reply<Done> *setDownloadLimit(const QStringList &hashes, qint64 bytesPerSec) override;
    Reply<Done> *setUploadLimit(const QStringList &hashes, qint64 bytesPerSec) override;
    /// A ratio, then stop or remove keeping the files (K18). "The server's" copies the
    /// server's defaults onto the torrents (a Deluge torrent keeps its own).
    Reply<Done> *setShareLimits(const QStringList &hashes,
                                RatioLimit ratio,
                                MinutesLimit seedingTime,
                                MinutesLimit inactiveSeedingTime,
                                const std::optional<QString> &action) override;
    Reply<GlobalLimits> *globalLimits() override;
    /// The alternative limits are dropped: Deluge has none (K20).
    Reply<Done> *setGlobalLimits(const GlobalLimits &limits) override;
    Reply<QVariantMap> *sessionPreferences() override;
    Reply<Done> *setSessionPreferences(const QVariantMap &changes) override;
    /// core.test_listen_port: Deluge asks its own checker on the internet.
    Reply<bool> *testPort() override;
    Reply<Done> *toggleAltSpeed() override;
    Reply<Done> *setAltSpeed(bool on) override;

    Reply<AddResult> *addUrls(const QStringList &urls, const AddOptions &options) override;
    Reply<AddResult> *
    addFile(const QString &fileName, const QByteArray &bytes, const AddOptions &options) override;
    Reply<AddResult> *addMany(const QList<AddFile> &files,
                              const QStringList &urls,
                              const AddOptions &options) override;

Q_SIGNALS:
    void daemonConnectedChanged();
    /// The Label plugin went on or off: features() changed.
    void labelPluginChanged();

private:
    using Results = QList<Result<QJsonValue>>;
    struct Prefetch;

    template<class T>
    Reply<T> *settled(Result<T> result);
    Reply<Done> *doneNow() { return settled<Done>(Done{}); }
    Reply<Done> *refused(const QString &why);
    /// One call, its result thrown away.
    Reply<Done> *done(Reply<QJsonValue> *reply);
    /// Every one of `replies` went through, or the first that didn't.
    Reply<Done> *doneAll(Reply<Results> *reply);
    /// All of `replies` at once: one result each, in order. Aborting it aborts them all.
    Reply<Results> *all(const QList<Reply<QJsonValue> *> &replies);
    /// `first`, then what `next` makes of its value: one reply for both.
    template<class T, class U>
    Reply<U> *then(Reply<T> *first, std::function<Reply<U> *(const T &)> next);
    /// `method(ids, …)` on `hashes`; never on none.
    Reply<Done> *
    forTorrents(const QString &method, const QStringList &hashes, QJsonArray more = {});
    /// core.set_torrent_options on `hashes`.
    Reply<Done> *setOptions(const QStringList &hashes, const QJsonObject &options);
    /// core.get_torrents_status of `fields` for `hashes` (every torrent when empty), as rows.
    Reply<QList<QJsonObject>> *torrentRows(const QStringList &hashes, const QStringList &fields);
    /// One torrent's `fields`; NotFound when Deluge has no such torrent.
    Reply<QJsonObject> *oneTorrent(const QString &hash, const QStringList &fields);
    /// Reads `key` of `hashes` and sets it to the opposite, per group that ends up alike.
    Reply<Done> *
    toggle(const QStringList &hashes, const QString &readKey, const QString &optionKey);
    Reply<Done> *trackersEdit(const QString &hash,
                              std::function<QJsonArray(const QJsonArray &)> change);
    Reply<AddResult> *add(const QList<QPair<QString, QJsonArray>> &calls,
                          const AddOptions &options);
    /// web.connect to `hostId` when deluge-web has no daemon, at most every 10 s.
    void reconnectStored();
    void setDaemonConnected(bool connected);
    void setLabelPlugin(bool on);
    /// Entries for the differ, with what the engine keeps of each (content root, trackers).
    QList<SnapshotDiffer::Entry> entries(const QList<QJsonObject> &rows);
    /// The categories delta of this poll: all of them when `full`, else what moved.
    void putCategories(SyncDelta &delta, bool full);
    void freeSpaceUp(const QString &path, Reply<FreeSpaceAt> *reply, int depth);
    void forget();

    DelugeRpc *m_rpc = nullptr;
    SnapshotDiffer m_differ;
    std::function<qint64()> m_now;
    int m_cursor = 0;
    int m_polls = 0;
    QString m_hostId;
    bool m_daemonConnected = true;
    qint64 m_lastReconnectAt = 0;
    bool m_labelPlugin = false;
    bool m_labelPluginKnown = false; ///< m_labelPlugin has been read once
    int m_filingResetDeltas = 0;     ///< deltas still to mark SyncDelta::filingReset (C43)
    QString m_libtorrentVersion;
    bool m_trackersDirty = true;
    bool m_labelsDirty = true;
    QHash<QString, QString> m_roots;           ///< hash → where its content starts below its folder
    QHash<QString, qint64> m_rootAskedAt;      ///< hash → when its files were last asked (ms)
    QHash<QString, QStringList> m_trackers;    ///< hash → its trackers' URLs
    QMap<QString, CategoryPatch> m_labels;     ///< the labels and their paths, last read
    QMap<QString, CategoryPatch> m_sentLabels; ///< what the last delta handed out
    QHash<QString, std::shared_ptr<Prefetch>> m_prefetches; ///< magnet → its lookup
};

} // namespace omaseed::core
