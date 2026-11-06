#pragma once

// DelugeRpc — the wire to one deluge-web: JSON-RPC 1 POSTs to its one endpoint, `/json`
// (docs/36 §3, DECISIONS C14 E3, C25 K7 and K10, issue #87). Everything deluge-web asks of a
// client before a call counts is handled here and nowhere else:
//   * the sign-in: one password, no user name (K7). `auth.login(password)` answers true and
//     sets the `_session_id` cookie, which the network manager's jar keeps and sends; false is
//     a refused password, which is latched: no request goes out again until someone hands a
//     password over (`setPassword`) or asks for another try (`allowLoginAgain`), so a wrong
//     one is never hammered;
//   * a session that ran out (deluge-web's `session_timeout`, an hour by default; a deluge-web
//     restarted): an answer "Not authenticated" (error code 1) signs in again and sends the
//     same call once more, once;
//   * the endpoint: the address may be the web UI's, a bare host:port or a proxy's path in
//     front of it (DelugeFingerprint::jsonUrl);
//   * the certificate pin (Q27), the timeout, and retries of reads on a transient failure.
//
// deluge-web takes one call per POST (an array is refused). An error object comes back as an
// ApiError: "Unknown method" (code 2: a method deluge-web doesn't pass on, or a daemon not
// connected) as NotFound, "Not authenticated" (code 1) as Unauthorized, anything the daemon
// refused (code 3 and 4: a Python exception's text, e.g. "Torrent already in session") as
// Conflict with the exception's own words, cut out of the traceback.

#include "core/Reply.hpp"
#include "core/engines/EngineFingerprint.hpp"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUrl>

#include <chrono>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QSslError;

namespace omaseed::core {

class DelugeRpc : public QObject
{
    Q_OBJECT

public:
    /// Whether a transient failure (no connection, a timeout, a 5xx) is tried again. Only
    /// for calls that change nothing: a mutation that timed out may have happened.
    enum class Retry { No, OnTransient };

    /// deluge-web's error codes (its JSON-RPC errors carry them).
    enum ErrorCode {
        NotAuthenticated = 1,
        UnknownMethod = 2,
        CallFailed = 3,   ///< the call raised in deluge-web
        DaemonFailed = 4, ///< the daemon's call raised (a Failure wrapping its exception)
    };

    explicit DelugeRpc(QObject *parent = nullptr);
    ~DelugeRpc() override;

    /// The server's address as given (no `/json` needed). A different one cancels everything
    /// outstanding and forgets the session.
    QUrl baseUrl() const;
    void setBaseUrl(const QUrl &baseUrl);
    /// Where the calls go: DelugeFingerprint::jsonUrl(baseUrl()).
    QUrl endpoint() const;

    /// deluge-web's one password (K7). Setting one (again) lets a refused sign-in be tried.
    void setPassword(const QString &password);
    bool hasPassword() const { return !m_password.isEmpty(); }
    /// Forgets the password and the session, and cancels everything outstanding.
    void clearAuth();
    /// After a refused password, lets the next call try it again.
    void allowLoginAgain();
    /// The password was refused and nothing is sent until one of the two above.
    bool loginRefused() const { return m_loginRejected; }
    /// Forgets the session cookie: the next call signs in first (another server, a test).
    void forgetSession();
    bool signedIn() const { return m_signedIn; }

    void setPinnedCertificate(const QString &sha256);
    void setTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds timeout() const;
    int maxRetries() const;
    void setMaxRetries(int retries);
    void setRetryBackoff(std::chrono::milliseconds backoff);
    /// Every call still in flight or waiting to be retried settles as Cancelled.
    void cancelAll();

    /// One call: settles with its `result` (any JSON value). Signs in first when no session
    /// is known yet. `timeout`, when given, replaces timeout() for this call alone (a
    /// metadata prefetch, which waits as long as it was told to).
    Reply<QJsonValue> *call(const QString &method,
                            const QJsonArray &params = {},
                            Retry retry = Retry::No,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    /// `{"method": method, "params": params, "id": id}`, compact.
    static QByteArray request(const QString &method, const QJsonArray &params, int id);
    /// One answer as a result or an error (`method` names it).
    static Result<QJsonValue> answerOf(const QString &method, const QByteArray &body);
    /// A deluge-web error object as an ApiError (see the class comment).
    static ApiError errorOf(const QString &method, const QJsonValue &error);
    /// The exception's own words out of a daemon's Failure text ("Torrent already in session
    /// (HASH).", "Invalid label, valid characters:[a-z0-9_-]"); the text as it is when it
    /// holds no traceback.
    static QString failureWords(const QString &message);

Q_SIGNALS:
    /// The password was refused (auth.login answered false): latched until setPassword or
    /// allowLoginAgain.
    void loginRefusedChanged();

private:
    struct Job;
    using JobPtr = std::shared_ptr<Job>;

    void start(const JobPtr &job);
    /// auth.login, then every job waiting for it.
    void login();
    void loginFinished(bool ok, const ApiError &error);
    void send(const JobPtr &job);
    void onSslErrors(const JobPtr &job, QNetworkReply *reply, const QList<QSslError> &errors);
    void onFinished(const JobPtr &job);
    void finish(const JobPtr &job, const Result<QJsonValue> &result);
    void finishLater(const JobPtr &job, const ApiError &error);
    void cancelJob(const JobPtr &job);
    int nextId() { return ++m_nextId; }

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_baseUrl;
    QString m_password;
    bool m_loginRejected = false;
    bool m_signedIn = false;
    bool m_loggingIn = false;
    QPointer<QNetworkReply> m_loginReply;
    QString m_pinnedCertificate;
    std::chrono::milliseconds m_timeout{15000};
    int m_maxRetries = 2;
    std::chrono::milliseconds m_retryBackoff{500};
    int m_nextId = 0;
    QList<JobPtr> m_waitingForLogin;
    QList<JobPtr> m_active;
    QList<JobPtr> m_backingOff;
};

} // namespace omaseed::core
