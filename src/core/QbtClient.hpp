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
//   GET  app/buildInfo               appBuildInfo()
//   POST auth/login                  login()
//   GET  sync/maindata               syncMainData(rid)
//   GET  sync/torrentPeers           torrentsPeers(hash, rid)
//   GET  torrents/info               torrentsInfo(query)
//   GET  torrents/properties         torrentsProperties(hash)
//   GET  torrents/files              torrentsFiles(hash)
//   GET  torrents/trackers           torrentsTrackers(hash)
//   GET  torrents/categories         torrentsCategories()
//   GET  torrents/tags               torrentsTags()
//   GET  transfer/info               transferInfo()
//   GET  transfer/speedLimitsMode    transferSpeedLimitsMode()
//   POST transfer/toggleSpeedLimitsMode  transferToggleSpeedLimitsMode()
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
#include <memory>
#include <optional>

class QNetworkAccessManager;

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
    /// The server root, e.g. http://127.0.0.1:8081 — without the /api/v2 part.
    void setBaseUrl(const QUrl &baseUrl);

    void setApiKey(const QString &apiKey);
    bool hasApiKey() const;

    void setCredentials(const QString &username, const QString &password);
    bool hasCredentials() const;
    /// Forgets key, credentials and any session cookie.
    void clearAuth();

    AuthState authState() const;

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
    QbtReply<QString> *appVersion();              ///< GET app/version
    QbtReply<QString> *webApiVersion();           ///< GET app/webapiVersion
    QbtReply<QJsonObject> *appPreferences();      ///< GET app/preferences
    QbtReply<QJsonObject> *appBuildInfo();        ///< GET app/buildInfo
    QbtReply<QJsonObject> *syncMainData(int rid); ///< GET sync/maindata?rid=
    QbtReply<QJsonArray> *torrentsInfo(const TorrentsInfoQuery &query = TorrentsInfoQuery());
    QbtReply<QJsonObject> *torrentsProperties(const QString &hash); ///< GET torrents/properties
    QbtReply<QJsonArray> *torrentsFiles(const QString &hash);       ///< GET torrents/files
    QbtReply<QJsonArray> *torrentsTrackers(const QString &hash);    ///< GET torrents/trackers
    /// GET sync/torrentPeers?hash=&rid= — the same rid protocol as sync/maindata.
    QbtReply<QJsonObject> *torrentsPeers(const QString &hash, int rid);
    QbtReply<QJsonObject> *torrentsCategories(); ///< GET torrents/categories
    QbtReply<QJsonArray> *torrentsTags();        ///< GET torrents/tags
    QbtReply<QJsonObject> *transferInfo();       ///< GET transfer/info
    /// GET transfer/speedLimitsMode — true when the alternative limits are on.
    QbtReply<bool> *transferSpeedLimitsMode();
    QbtReply<QByteArray> *transferToggleSpeedLimitsMode(); ///< POST transfer/toggleSpeedLimitsMode

    /// POST auth/login with the credentials set earlier. Resolves to true on success;
    /// a rejected password is ApiError::Kind::Unauthorized.
    QbtReply<bool> *login();

    /// app/version + app/webapiVersion; emits serverInfoChanged() when both arrive.
    QbtReply<ServerInfo> *probe();

    /// Drops every queued and in-flight request; each reply finishes with Kind::Cancelled.
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
    };

    template<class T, class Fn>
    QbtReply<T> *submit(RequestSpec spec, Fn parse);

    void enqueue(const JobPtr &job);
    void pump();
    void send(const JobPtr &job);
    void onJobReplyFinished(const JobPtr &job);
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
    AuthState m_authState = AuthState::Unknown;

    std::chrono::milliseconds m_timeout{15000};
    std::chrono::milliseconds m_retryBackoff{500};
    int m_maxRetries = 3;
    int m_maxConcurrent = 4;

    QList<JobPtr> m_queued;
    QList<JobPtr> m_active;
    QList<JobPtr> m_waitingForLogin;
    bool m_loginInFlight = false;
};

} // namespace omaseed::core
