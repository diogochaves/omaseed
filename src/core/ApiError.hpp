#pragma once

#include <QMetaType>
#include <QString>

namespace omaseed::core {

/// Errors in omaseed are values, never exceptions (see docs/ARCHITECTURE.md).
/// A default-constructed ApiError means "no error", so every call that can fail returns one
/// and the caller checks isError().
class ApiError
{
public:
    enum class Kind {
        None,
        Network,      ///< connection refused, host unreachable, TLS failure
        Certificate,  ///< TLS refused the server's certificate; see certificateSha256()
        Timeout,      ///< the request took longer than the client allows
        Unauthorized, ///< 401: missing or stale credential
        Forbidden,    ///< 403: the credential is not allowed here
        NotFound,     ///< 404: unknown endpoint or torrent
        Conflict,     ///< 409: qBittorrent refused the operation
        Server,       ///< 5xx
        Parse,        ///< the reply was not what the API documents
        Cancelled,    ///< the caller (or QbtClient::cancelAll) dropped the request
        Unknown,
    };

    ApiError() = default;
    ApiError(Kind kind, QString message, int httpStatus = 0);

    /// A Kind::Certificate error for a server that showed the certificate `sha256` (see
    /// CertificatePin::fingerprint) and was refused.
    static ApiError certificate(QString message, QString sha256);

    /// Maps an HTTP status to a Kind. Status 0 or 2xx yields a "no error" value.
    static ApiError fromHttpStatus(int httpStatus, QString message = QString());

    Kind kind() const { return m_kind; }
    QString message() const { return m_message; }
    int httpStatus() const { return m_httpStatus; }
    /// For Kind::Certificate: the refused certificate's SHA-256, 64 lowercase hex digits.
    QString certificateSha256() const { return m_certificateSha256; }

    bool isError() const { return m_kind != Kind::None; }
    /// True when retrying the same request later can plausibly succeed.
    bool isTransient() const;

    /// A one-line form for logs and banners, e.g. "unauthorized (401): bad credential".
    QString toString() const;
    /// toString() read back (GlobalState keeps only the text): the kind, the status and the
    /// message. A certificate's fingerprint does not survive the trip.
    static ApiError fromString(const QString &text);
    static QString kindName(Kind kind);

    bool operator==(const ApiError &other) const = default;

private:
    Kind m_kind = Kind::None;
    QString m_message;
    int m_httpStatus = 0;
    QString m_certificateSha256;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::ApiError)
