#pragma once

// QbtClient — one HTTP client per qBittorrent server, written against the public WebUI API
// v2 documentation (qBittorrent 5.x, API 2.15). No qBittorrent, qui or Taurent code is used.
//
// Auth policy
//   * An API key (setApiKey) is sent on every request as `Authorization: Bearer <key>`.
//   * If the server answers 401 or 403 and credentials exist (setCredentials), the client
//     logs in once through `auth/login` and retries that request transparently. The session
//     cookie (SID) then rides along in the QNetworkAccessManager cookie jar.
//   * With neither key nor credentials, requests go out plain: that works on a server whose
//     localhost auth is bypassed and is 403 elsewhere.
//   * `auth/login` answers 200 `Ok.` on older builds and 204 on 5.x; both count as success.
//     200 with a `Fails.` body, 401 and 403 all map to ApiError::Kind::Unauthorized.
//   * TLS: a certificate the system trusts is always accepted. Any other one is refused with
//     ApiError::Kind::Certificate, which carries the certificate's SHA-256 so the person can
//     be shown it, unless it is exactly the one setPinnedCertificate() names (trust on first
//     use, see CertificatePin). Certificate errors are never retried.
//   * Every request also carries `Referer` and `Origin` (qBittorrent's CSRF check) and a
//     `User-Agent: omaseed/<version>`.
//
// Queue: at most maxConcurrentRequests() (4) requests in flight, each with a transfer
// timeout (15 s). Idempotent GETs are retried up to maxRetries() (3) times on network,
// timeout and 5xx failures with a doubling back-off (0.5 s, 1 s, 2 s). POSTs are never
// retried for a transient failure — only once after a re-login, which cannot have taken
// effect on the server.
//
// Endpoints covered by this class (T2.1); torrent actions come with T2.5.
//   GET  app/version                 appVersion()
//   GET  app/webapiVersion           webApiVersion()
//   GET  app/preferences             appPreferences()
//   POST app/setPreferences          appSetPreferences(changes)  -- only the keys given
//   GET  app/getDirectoryContent     appDirectoryContent(dirPath)
//   GET  app/buildInfo               appBuildInfo()
//   POST auth/login                  login()
//   GET  sync/maindata               syncMainData(rid)
//   GET  sync/torrentPeers           torrentsPeers(hash, rid)
//   GET  torrents/info               torrentsInfo(query)
//   GET  torrents/properties         torrentsProperties(hash)
//   GET  torrents/files              torrentsFiles(hash)
//   GET  torrents/trackers           torrentsTrackers(hash)
//   GET  torrents/pieceStates        torrentsPieceStates(hash)
//   GET  torrents/webseeds           torrentsWebSeeds(hash)  -- [{url}], 404 for an unknown hash
//   GET  torrents/categories         torrentsCategories()
//   GET  torrents/tags               torrentsTags()
//   GET  transfer/info               transferInfo()
//   GET  transfer/speedLimitsMode    transferSpeedLimitsMode()
//   POST transfer/toggleSpeedLimitsMode  transferToggleSpeedLimitsMode()
//   POST transfer/setSpeedLimitsMode  transferSetSpeedLimitsMode(on)  -- mode=0|1
//   POST transfer/setDownloadLimit   transferSetDownloadLimit(bytesPerSec)
//   POST transfer/setUploadLimit     transferSetUploadLimit(bytesPerSec)
//
// Torrent actions (T2.5). Verified by hand with curl against the 5.2.3 playground; see the
// report on task/T2.5 for the session. All hashes parameters are QStringList joined with
// '|' (also accepts the literal "all"). Every POST goes through post()/postMultipart(),
// which are never retried for a transient failure — only once after a re-login.
//   POST torrents/start                 torrentsStart(hashes)          -- 5.x name (was resume)
//   POST torrents/stop                  torrentsStop(hashes)           -- 5.x name (was pause)
//   POST torrents/setForceStart         torrentsSetForceStart(hashes, value)
//   POST torrents/recheck               torrentsRecheck(hashes)
//   POST torrents/reannounce            torrentsReannounce(hashes)
//   POST torrents/removeTrackers        torrentsRemoveTrackers(hash, urls)
//   POST torrents/addTrackers           torrentsAddTrackers(hash, urls)  -- urls one per line
//   POST torrents/addPeers              torrentsAddPeers(hashes, peers)  -- both '|'-joined
//   POST torrents/addWebSeeds           torrentsAddWebSeeds(hash, urls)  -- '|'-joined; 400
//                                          names the first URL that is not valid
//   POST torrents/editWebSeed           torrentsEditWebSeed(hash, origUrl, newUrl) -- 409
//                                          when origUrl is not one of the torrent's
//   POST torrents/removeWebSeeds        torrentsRemoveWebSeeds(hash, urls)  -- '|'-joined
//   (the three web seed writes: checked by hand on the 5.2.3 playground, API 2.15.1)
//   POST transfer/banPeers              transferBanPeers(peers)          -- '|'-joined host:port
//   POST torrents/setCategory           torrentsSetCategory(hashes, category)
//   POST torrents/createCategory        torrentsCreateCategory(name, savePath)
//   POST torrents/editCategory          torrentsEditCategory(name, savePath)
//   POST torrents/removeCategories      torrentsRemoveCategories(names)
//   POST torrents/addTags               torrentsAddTags(hashes, tags)
//   POST torrents/removeTags            torrentsRemoveTags(hashes, tags)
//   POST torrents/createTags            torrentsCreateTags(tags)
//   POST torrents/deleteTags            torrentsDeleteTags(tags)
//   POST torrents/setLocation           torrentsSetLocation(hashes, location)
//   POST torrents/setDownloadLimit      torrentsSetDownloadLimit(hashes, bytesPerSec)
//   POST torrents/setUploadLimit        torrentsSetUploadLimit(hashes, bytesPerSec)
//   POST torrents/setShareLimits        torrentsSetShareLimits(hashes, ratio, seedingTime,
//                                          inactiveSeedingTime, shareLimitAction) -- the
//                                          action (API >= 2.15.1: Default | Stop | Remove |
//                                          RemoveWithContent | EnableSuperSeeding) is
//                                          required by 5.2.3 and rejected with 400 when it
//                                          is missing; -2 = global default, -1 = none.
//   POST torrents/rename                torrentsRename(hash, name)
//   POST torrents/renameFile            torrentsRenameFile(hash, oldPath, newPath)
//   POST torrents/renameFolder          torrentsRenameFolder(hash, oldPath, newPath)
//   POST torrents/increasePrio          torrentsIncreasePrio(hashes)
//   POST torrents/decreasePrio          torrentsDecreasePrio(hashes)
//   POST torrents/topPrio               torrentsTopPrio(hashes)
//   POST torrents/bottomPrio            torrentsBottomPrio(hashes)
//   POST torrents/filePrio              torrentsSetFilePrio(hash, fileIds, priority)
//   POST torrents/toggleSequentialDownload    torrentsToggleSequentialDownload(hashes)
//   POST torrents/toggleFirstLastPiecePrio    torrentsToggleFirstLastPiecePrio(hashes)
//   POST torrents/setAutoManagement     torrentsSetAutoManagement(hashes, enable)
//   POST torrents/setSuperSeeding       torrentsSetSuperSeeding(hashes, value)
//   POST torrents/delete                torrentsDelete(hashes, deleteFiles)
//   POST torrents/add (urls=)           torrentsAddUrls(urls, options)
//   POST torrents/add (multipart file)  torrentsAddFile(fileName, bytes, options)
//   POST torrents/add (multipart, many) torrentsAddMany(files, urls, options)
//   POST torrents/fetchMetadata         torrentsFetchMetadata(source)  -- 202 still looking,
//                                          200 the metadata; kept per Web UI session (#44)
// Anything else goes through the generic get() / post().

#include "core/ApiError.hpp"
#include "core/QbtReply.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QUrlQuery>

#include <chrono>
#include <functional>
#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;
class QSslError;
class QHttpMultiPart;

namespace omaseed::core {

/// What a qBittorrent server said about itself, from app/version + app/webapiVersion.
struct ServerInfo
{
    QString version;    ///< e.g. "v5.2.3"
    QString apiVersion; ///< e.g. "2.15.1"

    bool operator==(const ServerInfo &other) const = default;
};

/// Optional narrowing for torrents/info. An unset field is left out of the query, which is
/// not the same as an empty one ("category=" means "torrents with no category").
struct TorrentsInfoQuery
{
    std::optional<QString> filter;   ///< all, downloading, seeding, completed, stopped, …
    std::optional<QString> category; ///< empty string = uncategorised
    std::optional<QString> tag;      ///< empty string = untagged
    QStringList hashes;              ///< joined with '|'
    std::optional<QString> sort;
    bool reverse = false;
    int limit = 0;
    int offset = 0;
};

class QbtClient : public QObject
{
    Q_OBJECT

public:
    enum class AuthState {
        Unknown,         ///< nothing tried yet
        Ok,              ///< the last request was accepted
        NeedsCredential, ///< 401, or a login the server rejected
        Forbidden,       ///< 403 with no usable credential
    };
    Q_ENUM(AuthState)

    explicit QbtClient(QObject *parent = nullptr);
    ~QbtClient() override;

    // --- configuration -------------------------------------------------------------------
    QUrl baseUrl() const;
    /// The server root, e.g. http://127.0.0.1:8081 — without the /api/v2 part. A different
    /// URL cancels all outstanding work first (see cancelAll()).
    void setBaseUrl(const QUrl &baseUrl);

    void setApiKey(const QString &apiKey);
    bool hasApiKey() const;

    void setCredentials(const QString &username, const QString &password);
    bool hasCredentials() const;
    /// Forgets key, credentials and any session cookie, and cancels all outstanding work.
    void clearAuth();
    /// Lets the next auth failure log in once more after the server refused a login. For a
    /// deliberate user retry only: the latch exists because repeated failures ban the IP.
    void allowLoginAgain();

    AuthState authState() const;

    /// The one certificate (CertificatePin::fingerprint form) to accept for this server even
    /// though the system does not trust it; empty for none. A change drops every pooled
    /// connection, so nothing accepted under the old pin is reused.
    void setPinnedCertificate(const QString &sha256);
    QString pinnedCertificate() const;

    std::chrono::milliseconds timeout() const;
    void setTimeout(std::chrono::milliseconds timeout);

    int maxRetries() const;
    void setMaxRetries(int retries);

    std::chrono::milliseconds retryBackoff() const;
    /// First back-off step; it doubles for every further attempt.
    void setRetryBackoff(std::chrono::milliseconds backoff);

    int maxConcurrentRequests() const;
    void setMaxConcurrentRequests(int count);

    // --- generic ------------------------------------------------------------------------
    /// `path` is relative to /api/v2, e.g. "torrents/info".
    QbtReply<QByteArray> *get(const QString &path, const QUrlQuery &query = QUrlQuery());
    /// Form-encoded POST. Never retried for transient failures: it may have mutated state.
    QbtReply<QByteArray> *post(const QString &path, const QUrlQuery &form = QUrlQuery());

    // --- typed calls --------------------------------------------------------------------
    QbtReply<QString> *appVersion();         ///< GET app/version
    QbtReply<QString> *webApiVersion();      ///< GET app/webapiVersion
    QbtReply<QJsonObject> *appPreferences(); ///< GET app/preferences
    /// GET app/getDirectoryContent (mode=all): a folder on the server's own disks, as full
    /// paths (DECISIONS C16 L13). Its 4xx carry qBittorrent's text ("Directory does not
    /// exist", "Endpoint does not exist"): ServerFolder::fromReply reads them.
    QbtReply<QJsonArray> *appDirectoryContent(const QString &dirPath);
    /// POST app/setPreferences with `json=<changes>`; the server leaves every other key alone.
    QbtReply<QByteArray> *appSetPreferences(const QJsonObject &changes);
    QbtReply<QJsonObject> *appBuildInfo();        ///< GET app/buildInfo
    QbtReply<QJsonObject> *syncMainData(int rid); ///< GET sync/maindata?rid=
    QbtReply<QJsonArray> *torrentsInfo(const TorrentsInfoQuery &query = TorrentsInfoQuery());
    QbtReply<QJsonObject> *torrentsProperties(const QString &hash); ///< GET torrents/properties
    QbtReply<QJsonArray> *torrentsFiles(const QString &hash);       ///< GET torrents/files
    QbtReply<QJsonArray> *torrentsTrackers(const QString &hash);    ///< GET torrents/trackers
    /// GET torrents/pieceStates — one number per piece: 0 missing, 1 downloading, 2 had.
    QbtReply<QJsonArray> *torrentsPieceStates(const QString &hash);
    /// GET torrents/webseeds — the torrent's HTTP sources, `[{"url": …}]`.
    QbtReply<QJsonArray> *torrentsWebSeeds(const QString &hash);
    /// GET sync/torrentPeers?hash=&rid= — the same rid protocol as sync/maindata.
    QbtReply<QJsonObject> *torrentsPeers(const QString &hash, int rid);
    QbtReply<QJsonObject> *torrentsCategories(); ///< GET torrents/categories
    QbtReply<QJsonArray> *torrentsTags();        ///< GET torrents/tags
    QbtReply<QJsonObject> *transferInfo();       ///< GET transfer/info
    /// GET log/main: warnings and critical lines only, those after `lastKnownId` (-1 for
    /// all the server keeps). qBittorrent keeps them in memory: they start again from id 0
    /// when it restarts (docs/28 §2).
    QbtReply<QJsonArray> *logMainProblems(int lastKnownId);
    /// GET log/main with all four levels, the lines after `lastKnownId` (-1 for all the
    /// server keeps): the log face (docs/32 §6, #56).
    QbtReply<QJsonArray> *logMain(int lastKnownId);
    /// GET transfer/speedLimitsMode — true when the alternative limits are on.
    QbtReply<bool> *transferSpeedLimitsMode();
    QbtReply<QByteArray> *transferToggleSpeedLimitsMode(); ///< POST transfer/toggleSpeedLimitsMode
    /// POST transfer/setSpeedLimitsMode with `mode` 1 (alternative limits on) or 0. Unlike the
    /// toggle it says what it wants, so two quick calls cannot cancel each other out.
    QbtReply<QByteArray> *transferSetSpeedLimitsMode(bool on);

    /// POST auth/login with the credentials set earlier. Resolves to true on success;
    /// a rejected password is ApiError::Kind::Unauthorized.
    QbtReply<bool> *login();

    /// app/version + app/webapiVersion; emits serverInfoChanged() when both arrive.
    QbtReply<ServerInfo> *probe();

    // --- torrent actions (T2.5) ----------------------------------------------------------
    QbtReply<QByteArray> *torrentsStart(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsStop(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsSetForceStart(const QStringList &hashes, bool value);
    QbtReply<QByteArray> *torrentsRecheck(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsReannounce(const QStringList &hashes);
    /// POST torrents/removeTrackers: one torrent, `urls` joined with '|'.
    QbtReply<QByteArray> *torrentsRemoveTrackers(const QString &hash, const QStringList &urls);
    /// POST torrents/addTrackers: one torrent, `urls` joined with a newline (sent as %0A).
    QbtReply<QByteArray> *torrentsAddTrackers(const QString &hash, const QStringList &urls);
    /// POST torrents/addWebSeeds: one torrent, `urls` joined with '|'.
    QbtReply<QByteArray> *torrentsAddWebSeeds(const QString &hash, const QStringList &urls);
    /// POST torrents/editWebSeed: replaces `origUrl` with `newUrl` on one torrent.
    QbtReply<QByteArray> *
    torrentsEditWebSeed(const QString &hash, const QString &origUrl, const QString &newUrl);
    /// POST torrents/removeWebSeeds: one torrent, `urls` joined with '|'.
    QbtReply<QByteArray> *torrentsRemoveWebSeeds(const QString &hash, const QStringList &urls);
    /// POST torrents/addPeers: `hashes` and `peers` (`host:port`, `[v6]:port`) each joined
    /// with '|'.
    QbtReply<QByteArray> *torrentsAddPeers(const QStringList &hashes, const QStringList &peers);
    /// POST transfer/banPeers: `peers` (`host:port`) joined with '|'. The ban is qBittorrent's
    /// global IP filter: every torrent, until removed in its settings.
    QbtReply<QByteArray> *transferBanPeers(const QStringList &peers);

    QbtReply<QByteArray> *torrentsSetCategory(const QStringList &hashes, const QString &category);
    QbtReply<QByteArray> *torrentsCreateCategory(const QString &name,
                                                 const QString &savePath = QString());
    QbtReply<QByteArray> *torrentsEditCategory(const QString &name, const QString &savePath);
    QbtReply<QByteArray> *torrentsRemoveCategories(const QStringList &names);

    QbtReply<QByteArray> *torrentsAddTags(const QStringList &hashes, const QStringList &tags);
    QbtReply<QByteArray> *torrentsRemoveTags(const QStringList &hashes, const QStringList &tags);
    QbtReply<QByteArray> *torrentsCreateTags(const QStringList &tags);
    QbtReply<QByteArray> *torrentsDeleteTags(const QStringList &tags);

    QbtReply<QByteArray> *torrentsSetLocation(const QStringList &hashes, const QString &location);
    QbtReply<QByteArray> *torrentsSetDownloadLimit(const QStringList &hashes, qint64 bytesPerSec);
    QbtReply<QByteArray> *torrentsSetUploadLimit(const QStringList &hashes, qint64 bytesPerSec);
    /// What qBittorrent does to a torrent once one of its share limits is reached, as the
    /// `shareLimitAction` of torrents/setShareLimits and the `share_limit_action` of
    /// torrents/info spell it (API 2.15.1, qBittorrent 5.2). `Default` leaves the choice to
    /// the server's global setting.
    enum class ShareLimitAction { Default, Stop, Remove, RemoveWithContent, EnableSuperSeeding };
    Q_ENUM(ShareLimitAction)

    /// The wire name of `action`, e.g. "RemoveWithContent".
    static QString shareLimitActionName(ShareLimitAction action);
    /// The reverse; anything unknown (an empty string included) reads as Default.
    static ShareLimitAction shareLimitActionFromName(QStringView name);
    /// True when a server with this app/webapiVersion (e.g. "2.15.1") knows the action:
    /// the field was added in 2.15.1 and an older server rejects an unknown parameter.
    static bool supportsShareLimitAction(const QString &apiVersion);

    /// ratioLimit/seedingTimeLimit/inactiveSeedingTimeLimit: -2 = global default, -1 = none,
    /// >=0 the limit itself. `shareLimitAction` engaged sends the field (5.2.3 answers 400
    /// without it, `Default` included, so a >= 2.15.1 caller always passes one); nullopt
    /// leaves it off for the pre-2.15.1 servers that do not know it. With the action goes
    /// `shareLimitsMode=Default`, which Web API 2.15.3 made required (qBittorrent 5.3.0 reports
    /// 2.16.2) and 5.2 (2.15.1) accepts and ignores.
    QbtReply<QByteArray> *torrentsSetShareLimits(
        const QStringList &hashes,
        double ratioLimit,
        qint64 seedingTimeLimit,
        qint64 inactiveSeedingTimeLimit,
        std::optional<ShareLimitAction> shareLimitAction = ShareLimitAction::Default);

    QbtReply<QByteArray> *torrentsRename(const QString &hash, const QString &name);
    QbtReply<QByteArray> *
    torrentsRenameFile(const QString &hash, const QString &oldPath, const QString &newPath);
    QbtReply<QByteArray> *
    torrentsRenameFolder(const QString &hash, const QString &oldPath, const QString &newPath);

    QbtReply<QByteArray> *torrentsIncreasePrio(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsDecreasePrio(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsTopPrio(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsBottomPrio(const QStringList &hashes);
    /// priority: 0 = do not download, 1 = normal, 6 = high, 7 = maximal.
    QbtReply<QByteArray> *
    torrentsSetFilePrio(const QString &hash, const QList<int> &fileIds, int priority);

    QbtReply<QByteArray> *torrentsToggleSequentialDownload(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsToggleFirstLastPiecePrio(const QStringList &hashes);
    QbtReply<QByteArray> *torrentsSetAutoManagement(const QStringList &hashes, bool enable);
    QbtReply<QByteArray> *torrentsSetSuperSeeding(const QStringList &hashes, bool value);

    QbtReply<QByteArray> *torrentsDelete(const QStringList &hashes, bool deleteFiles);

    /// Matches qBittorrent's `contentLayout` values for a newly added torrent.
    enum class ContentLayout { Original, Subfolder, NoSubfolder };

    /// Options for torrentsAddUrls / torrentsAddFile. An unset optional is left off the
    /// request entirely, so the server falls back to its own default (or the category's,
    /// for the TMM-managed fields) instead of an explicit override.
    struct AddOptions
    {
        std::optional<QString> savepath;
        /// The incomplete folder (`downloadPath`) and whether to use one (`useDownloadPath`,
        /// Web API 2.8.4): a torrents import (#33) says both, so the server's own setting can't
        /// send a half-done torrent's check to a folder its files were never in.
        std::optional<QString> downloadPath;
        std::optional<bool> useDownloadPath;
        std::optional<QString> category;
        QStringList tags;
        /// Sent as `stopped` (5.x). `paused` (the pre-5.0 name) is sent alongside it too;
        /// a 5.2.3 server ignores the unknown extra parameter.
        std::optional<bool> stopped;
        /// Add without checking the files already on disk. Spelled by `apiVersion`: see
        /// supportsSeedMode().
        std::optional<bool> skipChecking;
        std::optional<ContentLayout> contentLayout;
        std::optional<QString> rename;
        std::optional<qint64> upLimit;
        std::optional<qint64> dlLimit;
        std::optional<double> ratioLimit;
        std::optional<qint64> seedingTimeLimit;
        std::optional<bool> autoTMM;
        std::optional<bool> sequentialDownload;
        std::optional<bool> firstLastPiecePrio;
        std::optional<QString> cookie;
        /// One per file, in file-id order (0 skip, 1 normal, 6 high, 7 maximal), sent as
        /// `filePriorities=1,0,7`. Only for one magnet or link whose metadata this client's
        /// session already fetched (torrentsFetchMetadata); qBittorrent answers 400 otherwise,
        /// and with a file upload (docs/11 D1). Empty leaves it off.
        QList<int> filePriorities;
        /// qBittorrent's `stopCondition`: "MetadataReceived" stops the torrent as soon as its
        /// metadata is in (docs/29 D5), "FilesChecked" after the check. Unset leaves it off.
        std::optional<QString> stopCondition;
        /// The server's app/webapiVersion ("2.16.2"). It only decides how `skipChecking` is
        /// spelled: `seedMode` when supportsSeedMode(), `skip_checking` otherwise, and both
        /// when it is empty or unreadable, since each server reads only its own key and
        /// qBittorrent ignores a parameter it does not know (as with `stopped`/`paused`).
        QString apiVersion;
    };

    /// What torrents/fetchMetadata answered: `ready` false is 202, the lookup still running
    /// (`body` holds the hashes, or nothing for a link still downloading); true is 200, with
    /// the torrent's metadata in `body` (`info.name`, `info.files[].path` and `.length`, …).
    struct FetchedMetadata
    {
        bool ready = false;
        QJsonObject body;
    };

    /// POST torrents/fetchMetadata (Web API 2.11.9, qBittorrent 5.2): asks qBittorrent for the
    /// metadata of `source` (a magnet, a v1 or v2 hash, an http(s) link to a .torrent) without
    /// adding it. It answers 202 while it looks and 200 once it has it; the caller asks again
    /// (docs/29 §3). The result is kept in the Web UI session, so every poll and the add that
    /// follows must go through this one client. A 404 is a server
    /// without the endpoint.
    QbtReply<FetchedMetadata> *torrentsFetchMetadata(const QString &source);
    /// True when a server with this app/webapiVersion has torrents/fetchMetadata and takes
    /// `filePriorities` on an add: 2.11.9 and later.
    static bool supportsFetchMetadata(const QString &apiVersion);
    /// True when a server with this app/webapiVersion skips the add's check through
    /// `seedMode`: Web API 2.16.0 (qBittorrent 5.3) added it and stopped reading
    /// `skip_checking`, which every earlier version reads instead.
    static bool supportsSeedMode(const QString &apiVersion);

    /// POST torrents/add with one or more magnet links or .torrent URLs (the server fetches
    /// them itself), newline-separated.
    QbtReply<QByteArray> *torrentsAddUrls(const QStringList &urls,
                                          const AddOptions &options = AddOptions());
    /// POST torrents/add, multipart, with the raw bytes of a .torrent file.
    QbtReply<QByteArray> *torrentsAddFile(const QString &fileName,
                                          const QByteArray &bytes,
                                          const AddOptions &options = AddOptions());

    /// One `.torrent` of a batch: the name it is uploaded under and its bytes.
    struct AddFile
    {
        QString fileName;
        QByteArray bytes;
    };
    /// POST torrents/add, multipart, with several `.torrent` files and several links at once,
    /// all under one set of options (#40, the add sheet's batch). qBittorrent takes a
    /// `torrents` part per file and the links newline-separated in one `urls` part.
    QbtReply<QByteArray> *torrentsAddMany(const QList<AddFile> &files,
                                          const QStringList &urls,
                                          const AddOptions &options = AddOptions());

    QbtReply<QByteArray> *transferSetDownloadLimit(qint64 bytesPerSec);
    QbtReply<QByteArray> *transferSetUploadLimit(qint64 bytesPerSec);

    /// A POST whose body is a multipart form (e.g. torrentsAddFile). `buildMultiPart` makes
    /// a fresh, unparented QHttpMultiPart for every attempt (the client parents it to the
    /// QNetworkReply that sends it), so the one-shot re-login retry can resend the body
    /// after an expired session. Never retried for a transient failure.
    QbtReply<QByteArray> *postMultipart(const QString &path,
                                        std::function<QHttpMultiPart *()> buildMultiPart);

    /// Drops every queued, backing-off, login-waiting and in-flight request; each reply
    /// finishes with Kind::Cancelled. setBaseUrl() (to a new URL) and clearAuth() do this
    /// too, so nothing meant for one server is ever sent to the next.
    void cancelAll();

Q_SIGNALS:
    void authStateChanged(omaseed::core::QbtClient::AuthState state);
    void serverInfoChanged(const QString &version, const QString &apiVersion);

private:
    struct Job;
    using JobPtr = std::shared_ptr<Job>;

    /// What to send. `retryOnTransient` is false for anything that may change server state.
    struct RequestSpec
    {
        QString verb = QStringLiteral("GET");
        QString path;
        QUrlQuery query;
        QUrlQuery form;
        bool retryOnTransient = true;
        bool isLogin = false;
        bool allowReloginRetry = true;
        /// A 403 whose body is qBittorrent's own text (not its bare "Forbidden" for a session
        /// it doesn't know) refuses the request, not the sign-in: setLocation's "can't write
        /// to directory" (C15 M19). No re-login, no auth state; the body becomes the message.
        bool forbiddenMayNameRequest = false;
        /// A 4xx other than 401/403 keeps the first line of qBittorrent's own text in the
        /// message, where the status alone doesn't say what went wrong (getDirectoryContent's
        /// 404 is a missing folder or a missing endpoint).
        bool errorKeepsBody = false;
        /// Set instead of `form` for a multipart POST: called once per attempt, see
        /// postMultipart().
        std::function<QHttpMultiPart *()> buildMultiPart;
    };

    template<class T, class Fn>
    QbtReply<T> *submit(RequestSpec spec, Fn parse);

    void enqueue(const JobPtr &job);
    void pump();
    void send(const JobPtr &job);
    void onJobReplyFinished(const JobPtr &job);
    void
    onSslErrors(const JobPtr &job, QNetworkReply *networkReply, const QList<QSslError> &errors);
    void finishJob(const JobPtr &job, const QByteArray &body, const ApiError &error);
    void cancelJob(const JobPtr &job);
    ApiError mapError(const JobPtr &job, int httpStatus, const QString &reason) const;
    void startLogin();
    void onLoginFinished(const ApiError &error);
    void setAuthState(AuthState state);
    QUrl endpointUrl(const QString &path, const QUrlQuery &query) const;

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_baseUrl;
    QString m_apiKey;
    QString m_username;
    QString m_password;
    QString m_pinnedCertificate;
    AuthState m_authState = AuthState::Unknown;

    std::chrono::milliseconds m_timeout{15000};
    std::chrono::milliseconds m_retryBackoff{500};
    int m_maxRetries = 3;
    int m_maxConcurrent = 4;

    QList<JobPtr> m_queued;
    QList<JobPtr> m_active;
    QList<JobPtr> m_waitingForLogin;
    QList<JobPtr> m_backingOff; ///< GETs waiting out a retry delay
    bool m_loginInFlight = false;
    /// The server rejected our username/password. No automatic re-login until
    /// setCredentials(), clearAuth() or allowLoginAgain(): repeated failures get the IP banned.
    bool m_loginRejected = false;
    /// What that rejected login said (Unauthorized for a wrong password, Forbidden for a ban).
    /// While the latch holds, a 401/403 on a normal call is reported as this: the credential
    /// is what is wrong, not the call.
    ApiError::Kind m_loginRejectedKind = ApiError::Kind::Unauthorized;
};

} // namespace omaseed::core
