#pragma once

// TransmissionRpc — the wire to one Transmission 4.1+ daemon: JSON-RPC 2.0 POSTs to its one
// endpoint (docs/36 §3, DECISIONS C14 E3, issue #85). Everything Transmission asks of a client
// before a request counts is handled here and nowhere else:
//   * the anti-CSRF session id: an answer 409 carries `X-Transmission-Session-Id`, which is
//     kept and the request sent again with it, once (rpc-spec §2.2.1);
//   * HTTP Basic auth, sent with every request when a login is set (§2.2.3); a refused login
//     is not sent again until someone hands one over (Transmission bans after enough misses,
//     `anti_brute_force_enabled`), as QbtClient does with qBittorrent's;
//   * the endpoint: the address may be the web UI's, the RPC's, a bare host:port or a proxy's
//     path in front of either (C25 K6, TransmissionFingerprint::rpcUrl);
//   * the certificate pin (Q27), the timeout and retries of reads on a transient failure.
//
// A JSON-RPC error comes back as an ApiError: "Method not found" as NotFound, a malformed
// request as Parse, anything the daemon refused (invalid params, a path that isn't absolute, a
// system error) as Conflict with the daemon's own `error_string`. An answer that isn't JSON-RPC
// 2.0 (a daemon older than 4.1 answering in its legacy dialect) is a Parse error that says so.

#include "core/Reply.hpp"
#include "core/engines/EngineFingerprint.hpp"

#include <QByteArray>
#include <QJsonObject>
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

class TransmissionRpc : public QObject
{
    Q_OBJECT

public:
    /// One method of a batch: `params` is left out of the request when empty.
    struct Call
    {
        QString method;
        QJsonObject params;
    };

    /// Whether a transient failure (no connection, a timeout, a 5xx) is tried again. Only
    /// for requests that change nothing: a mutation that timed out may have happened.
    enum class Retry { No, OnTransient };

    explicit TransmissionRpc(QObject *parent = nullptr);
    ~TransmissionRpc() override;

    /// The server's address as given (no RPC path needed). A different one cancels
    /// everything outstanding and forgets the session id.
    QUrl baseUrl() const;
    void setBaseUrl(const QUrl &baseUrl);
    /// Where the requests go: TransmissionFingerprint::rpcUrl(baseUrl()).
    QUrl endpoint() const;

    void setCredentials(const QString &username, const QString &password);
    bool hasCredentials() const;
    /// Forgets the login and the session id, and cancels everything outstanding.
    void clearAuth();
    /// After a refused login, lets the next request try it again.
    void allowLoginAgain();
    void setPinnedCertificate(const QString &sha256);
    void setTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds timeout() const;
    int maxRetries() const;
    void setMaxRetries(int retries);
    void setRetryBackoff(std::chrono::milliseconds backoff);
    /// Every request still in flight or waiting to be retried settles as Cancelled.
    void cancelAll();

    /// The session id the daemon handed out last; empty before the first 409.
    QByteArray sessionId() const { return m_sessionId; }
    /// The `X-Transmission-Rpc-Version` of the last 409 (4.1 and later send it), or empty.
    QString rpcVersionHeader() const { return m_rpcVersionHeader; }

    /// One POST of `body` as it is: a 2xx answer, whatever it holds (the probe reads it
    /// with TransmissionFingerprint, which knows both dialects). `what` names it in errors.
    /// `timeout`, when given, replaces timeout() for this request alone (a move's, which
    /// Transmission answers only once the copy ends: C25 K22).
    Reply<HttpAnswer> *post(const QByteArray &body,
                            const QString &what,
                            Retry retry,
                            std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
    /// One method: settles with the answer's `result` object.
    Reply<QJsonObject> *call(const QString &method,
                             const QJsonObject &params = {},
                             Retry retry = Retry::No,
                             std::chrono::milliseconds timeout = std::chrono::milliseconds(0));
    /// Several methods in one POST (a JSON-RPC 2.0 batch): one result per call, in the order
    /// given. The reply itself errs only when the POST did.
    Reply<QList<Result<QJsonObject>>> *batch(const QList<Call> &calls, Retry retry = Retry::No);

    /// A JSON-RPC 2.0 request object.
    static QJsonObject request(const QString &method, const QJsonObject &params, int id);
    /// One answer object as a result or an error (`method` names it).
    static Result<QJsonObject> answerOf(const QString &method, const QJsonObject &answer);

private:
    struct Job;
    using JobPtr = std::shared_ptr<Job>;

    void send(const JobPtr &job);
    void onSslErrors(const JobPtr &job, QNetworkReply *reply, const QList<QSslError> &errors);
    void onFinished(const JobPtr &job);
    void finish(const JobPtr &job, const HttpAnswer &answer, const ApiError &error);
    void finishLater(const JobPtr &job, const ApiError &error);
    void cancelJob(const JobPtr &job);
    int nextId() { return ++m_nextId; }

    QNetworkAccessManager *m_nam = nullptr;
    QUrl m_baseUrl;
    QString m_username;
    QString m_password;
    bool m_loginRejected = false;
    QByteArray m_sessionId;
    QString m_rpcVersionHeader;
    QString m_pinnedCertificate;
    std::chrono::milliseconds m_timeout{15000};
    int m_maxRetries = 2;
    std::chrono::milliseconds m_retryBackoff{500};
    int m_nextId = 0;
    QList<JobPtr> m_active;
    QList<JobPtr> m_backingOff;
};

} // namespace omaseed::core
