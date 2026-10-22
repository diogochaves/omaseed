#pragma once

// EngineClient — one torrent engine on one server, in omaseed's terms (docs/36 §4, docs/
// DECISIONS.md C14 E2 a). Everything outside src/core/engines/ talks to a server through
// this: the sync, the details pane, the actions, the add, the delete, the limits, the labels,
// the probe and the sign-in. Each engine translates its wire format at this edge and nowhere
// else, both ways: answers come back as SyncDelta, TorrentProperties, ContentFile and the
// rest, and what goes out (file priorities, share limits, add options) leaves as omaseed's
// values, which the engine spells in its own words.
//
// Every call hands back a Reply that settles once (docs/ARCHITECTURE.md: errors are values).
// An action that worked settles with Done. A call the engine cannot make settles with an
// ApiError of kind NotFound ("not supported by this engine"); EngineFeatures says beforehand
// which those are, and the views never offer them (C14 E4).
//
// qBittorrent's engine is QbtEngine (src/core/engines/qbt/). The surfaces only it has (its
// settings pane, settings and torrents export and import, RSS) reach it through the feature
// record and App::qbtEngine(), not through this interface (docs/36 §4.4).

#include "core/Reply.hpp"
#include "core/ServerFolder.hpp"
#include "core/engines/AddOptions.hpp"
#include "core/engines/DetailsData.hpp"
#include "core/engines/EngineFeatures.hpp"
#include "core/engines/LogEntry.hpp"
#include "core/engines/ServerInfo.hpp"
#include "core/engines/ShareLimits.hpp"
#include "core/engines/SyncDelta.hpp"
#include "core/engines/TorrentMetadata.hpp"
#include "core/engines/TorrentPatch.hpp"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <optional>

namespace omaseed::core {

/// What an action answers when it worked: nothing to read.
struct Done
{
    bool operator==(const Done &other) const = default;
};

/// The server-wide speed limits in bytes/s, 0 for none; the alternative ones too. On a set,
/// an absent value is left as it is.
struct GlobalLimits
{
    std::optional<qint64> download;
    std::optional<qint64> upload;
    std::optional<qint64> altDownload;
    std::optional<qint64> altUpload;

    bool operator==(const GlobalLimits &other) const = default;
};

class EngineClient : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    ~EngineClient() override;

    // --- the connection and the sign-in ----------------------------------------------------
    virtual QUrl baseUrl() const = 0;
    /// The server root (no API path). A different URL cancels everything outstanding first.
    virtual void setBaseUrl(const QUrl &baseUrl) = 0;
    /// Sign in with a key the server issued (qBittorrent 5.2's API key).
    virtual void setApiKey(const QString &apiKey) = 0;
    virtual bool hasApiKey() const = 0;
    /// Sign in with a user name and password, when the server asks.
    virtual void setCredentials(const QString &username, const QString &password) = 0;
    /// Forgets the key, the credentials and any session, and cancels everything outstanding.
    virtual void clearAuth() = 0;
    /// After a refused sign-in, lets the next one through (a retry the user asked for).
    virtual void allowLoginAgain() = 0;
    /// The one certificate to accept although the system does not trust it (CertificatePin).
    virtual void setPinnedCertificate(const QString &sha256) = 0;
    virtual void setTimeout(std::chrono::milliseconds timeout) = 0;
    virtual int maxRetries() const = 0;
    virtual void setMaxRetries(int retries) = 0;
    /// Drops every request still queued or in flight; each settles as Cancelled.
    virtual void cancelAll() = 0;
    /// An address under `base` that answers, signed in or not, whenever the engine's API is
    /// up there: for watching a server come back on a new port or scheme.
    virtual QUrl reachableUrl(const QUrl &base) const = 0;

    // --- what the server is ----------------------------------------------------------------
    /// The engine's version and its API's.
    virtual Reply<ServerInfo> *probe() = 0;
    /// What a server that answered `info` can do (docs/36 §4.3).
    virtual EngineFeatures features(const ServerInfo &info) const = 0;

    // --- sync and details ------------------------------------------------------------------
    /// One poll: everything when `cursor` is 0, else what moved since the poll that gave it.
    virtual Reply<SyncDelta> *sync(int cursor) = 0;
    /// The torrents (all of them, or those of `hashes`), every field the engine has.
    virtual Reply<QList<TorrentPatch>> *torrents(const QStringList &hashes = {}) = 0;
    /// Those of `hashes` that are stopped, whatever else their state says: an engine whose
    /// state word hides a stop behind another (qBittorrent's `error`) answers from its own
    /// record of it (DECISIONS C20, #142).
    virtual Reply<QStringList> *stoppedAmong(const QStringList &hashes) = 0;
    virtual Reply<QList<CategoryPatch>> *categories() = 0;
    virtual Reply<QStringList> *tags() = 0;
    virtual Reply<TorrentProperties> *properties(const QString &hash) = 0;
    virtual Reply<QList<ContentFile>> *files(const QString &hash) = 0;
    virtual Reply<QList<TrackerEntry>> *trackers(const QString &hash) = 0;
    /// The torrent's peers: all of them when `cursor` is 0, else what moved since.
    virtual Reply<PeersDelta> *peers(const QString &hash, int cursor) = 0;
    virtual Reply<QStringList> *webSeeds(const QString &hash) = 0;
    /// One PieceStates::State per piece.
    virtual Reply<QByteArray> *pieceStates(const QString &hash) = 0;
    /// The engine's own log, the lines after `afterId` (-1 for all it keeps); only warnings
    /// and errors when `problemsOnly`. EngineFeatures::engineLog.
    virtual Reply<QList<LogEntry>> *log(int afterId, bool problemsOnly);
    /// A magnet's or a link's metadata, without adding it: ask again until it is ready.
    /// EngineFeatures::fetchMetadata.
    virtual Reply<FetchedMetadata> *fetchMetadata(const QString &source);
    /// What is in `path`, a folder on the server's own disks, for a server whose disks
    /// omaseed can't see (DECISIONS C16 L13). Always settles with a value: its state says
    /// listed, missing, failed, or (the default) that this engine can't list folders.
    virtual Reply<ServerFolder> *folderContent(const QString &path);

    // --- actions ---------------------------------------------------------------------------
    virtual Reply<Done> *start(const QStringList &hashes) = 0;
    virtual Reply<Done> *stop(const QStringList &hashes) = 0;
    virtual Reply<Done> *startAll() = 0;
    virtual Reply<Done> *stopAll() = 0;
    /// Starts past the queue (EngineFeatures::forceStart).
    virtual Reply<Done> *setForceStart(const QStringList &hashes, bool on) = 0;
    virtual Reply<Done> *recheck(const QStringList &hashes) = 0;
    virtual Reply<Done> *reannounce(const QStringList &hashes) = 0;
    virtual Reply<Done> *rename(const QString &hash, const QString &name) = 0;
    virtual Reply<Done> *setLocation(const QStringList &hashes, const QString &path) = 0;
    /// Moves a file (or, with renameFolder, a folder) of the torrent's content; paths are
    /// relative to its save path.
    virtual Reply<Done> *
    renameFile(const QString &hash, const QString &oldPath, const QString &newPath) = 0;
    virtual Reply<Done> *
    renameFolder(const QString &hash, const QString &oldPath, const QString &newPath) = 0;
    /// `fileIds` are ContentFile::index values.
    virtual Reply<Done> *
    setFilePriority(const QString &hash, const QList<int> &fileIds, FilePriority priority) = 0;
    /// The queue (EngineFeatures::queue): one place up or down.
    virtual Reply<Done> *queueUp(const QStringList &hashes) = 0;
    virtual Reply<Done> *queueDown(const QStringList &hashes) = 0;
    /// Each flips the torrents it is given: the caller sends only those to change.
    virtual Reply<Done> *toggleSequentialDownload(const QStringList &hashes) = 0;
    virtual Reply<Done> *toggleFirstLastPiece(const QStringList &hashes) = 0;
    virtual Reply<Done> *setSuperSeeding(const QStringList &hashes, bool on) = 0;
    virtual Reply<Done> *setAutoManagement(const QStringList &hashes, bool on) = 0;
    virtual Reply<Done> *remove(const QStringList &hashes, bool deleteFiles) = 0;

    virtual Reply<Done> *addTrackers(const QString &hash, const QStringList &urls) = 0;
    virtual Reply<Done> *removeTrackers(const QString &hash, const QStringList &urls) = 0;
    virtual Reply<Done> *addWebSeeds(const QString &hash, const QStringList &urls) = 0;
    virtual Reply<Done> *removeWebSeeds(const QString &hash, const QStringList &urls) = 0;
    /// `peers` as `host:port` or `[v6]:port`.
    virtual Reply<Done> *addPeers(const QStringList &hashes, const QStringList &peers) = 0;
    /// Bans `peers` (`host:port`) from every torrent.
    virtual Reply<Done> *banPeers(const QStringList &peers) = 0;

    // --- labels (C14 E5: categories one per torrent, tags many) ---------------------------
    virtual Reply<Done> *setCategory(const QStringList &hashes, const QString &category) = 0;
    virtual Reply<Done> *createCategory(const QString &name, const QString &savePath = {}) = 0;
    virtual Reply<Done> *removeCategories(const QStringList &names) = 0;
    virtual Reply<Done> *addTags(const QStringList &hashes, const QStringList &tags) = 0;
    virtual Reply<Done> *removeTags(const QStringList &hashes, const QStringList &tags) = 0;
    virtual Reply<Done> *createTags(const QStringList &tags) = 0;
    virtual Reply<Done> *deleteTags(const QStringList &tags) = 0;

    // --- limits ----------------------------------------------------------------------------
    /// Bytes/s, 0 for none.
    virtual Reply<Done> *setDownloadLimit(const QStringList &hashes, qint64 bytesPerSec) = 0;
    virtual Reply<Done> *setUploadLimit(const QStringList &hashes, qint64 bytesPerSec) = 0;
    /// `action` names what happens at the limit (one of EngineFeatures::shareLimitActions);
    /// absent leaves it out of the request, for a server that does not know it.
    virtual Reply<Done> *setShareLimits(const QStringList &hashes,
                                        RatioLimit ratio,
                                        MinutesLimit seedingTime,
                                        MinutesLimit inactiveSeedingTime,
                                        const std::optional<QString> &action) = 0;
    virtual Reply<GlobalLimits> *globalLimits() = 0;
    virtual Reply<Done> *setGlobalLimits(const GlobalLimits &limits) = 0;
    virtual Reply<Done> *toggleAltSpeed() = 0;
    /// Says what it wants, so two quick calls cannot cancel each other out.
    virtual Reply<Done> *setAltSpeed(bool on) = 0;

    // --- adding ----------------------------------------------------------------------------
    /// Magnets or links to `.torrent`s, which the server fetches itself.
    virtual Reply<AddResult> *addUrls(const QStringList &urls, const AddOptions &options) = 0;
    virtual Reply<AddResult> *
    addFile(const QString &fileName, const QByteArray &bytes, const AddOptions &options) = 0;
    /// Several `.torrent`s and links at once, under one set of options.
    virtual Reply<AddResult> *
    addMany(const QList<AddFile> &files, const QStringList &urls, const AddOptions &options) = 0;

protected:
    /// A reply that settles, on the next turn of the event loop, as "not supported".
    template<class T>
    Reply<T> *unsupported(const QString &what)
    {
        auto *reply = new Reply<T>(this);
        const ApiError error(ApiError::Kind::NotFound,
                             QStringLiteral("%1: not supported by this engine").arg(what));
        QTimer::singleShot(0, reply, [reply, error]() { reply->complete(Result<T>(error)); });
        return reply;
    }
};

} // namespace omaseed::core
