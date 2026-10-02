#include "core/ApiError.hpp"

#include <utility>

namespace omaseed::core {

ApiError::ApiError(Kind kind, QString message, int httpStatus)
    : m_kind(kind)
    , m_message(std::move(message))
    , m_httpStatus(httpStatus)
{}

ApiError ApiError::certificate(QString message, QString sha256)
{
    ApiError error(Kind::Certificate, std::move(message));
    error.m_certificateSha256 = std::move(sha256);
    return error;
}

ApiError ApiError::fromHttpStatus(int httpStatus, QString message)
{
    if (httpStatus == 0 || (httpStatus >= 200 && httpStatus < 300))
        return ApiError();

    Kind kind = Kind::Unknown;
    switch (httpStatus) {
    case 401:
        kind = Kind::Unauthorized;
        break;
    case 403:
        kind = Kind::Forbidden;
        break;
    case 404:
        kind = Kind::NotFound;
        break;
    case 409:
        kind = Kind::Conflict;
        break;
    default:
        if (httpStatus >= 500)
            kind = Kind::Server;
        break;
    }
    return ApiError(kind, std::move(message), httpStatus);
}

bool ApiError::isTransient() const
{
    switch (m_kind) {
    case Kind::Network:
    case Kind::Timeout:
    case Kind::Server:
        return true;
    default:
        return false;
    }
}

QString ApiError::kindName(Kind kind)
{
    switch (kind) {
    case Kind::None:
        return QStringLiteral("none");
    case Kind::Network:
        return QStringLiteral("network");
    case Kind::Certificate:
        return QStringLiteral("certificate");
    case Kind::Timeout:
        return QStringLiteral("timeout");
    case Kind::Unauthorized:
        return QStringLiteral("unauthorized");
    case Kind::Forbidden:
        return QStringLiteral("forbidden");
    case Kind::NotFound:
        return QStringLiteral("not-found");
    case Kind::Conflict:
        return QStringLiteral("conflict");
    case Kind::Server:
        return QStringLiteral("server");
    case Kind::Parse:
        return QStringLiteral("parse");
    case Kind::Cancelled:
        return QStringLiteral("cancelled");
    case Kind::Unknown:
        break;
    }
    return QStringLiteral("unknown");
}

QString ApiError::toString() const
{
    QString text = kindName(m_kind);
    if (m_httpStatus != 0)
        text += QStringLiteral(" (%1)").arg(m_httpStatus);
    if (!m_message.isEmpty())
        text += QStringLiteral(": ") + m_message;
    return text;
}

} // namespace omaseed::core
