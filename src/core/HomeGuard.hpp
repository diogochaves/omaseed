#pragma once

// Port 8080 is the developer's real qBittorrent library. The live tests (tests/LiveServer.hpp)
// and `omaseed --demo sync`, which both take a server URL from the environment, refuse any URL
// on it; the app itself talks to whatever server the user configured.

#include <QLatin1String>
#include <QRegularExpression>
#include <QString>
#include <QUrl>

namespace omaseed::core {

inline constexpr int kHomePort = 8080;

/// True for any URL on port 8080, whatever the host: 127.0.0.1, localhost, ::1 or a LAN name
/// can all reach the real library. The raw text is checked too, so a URL QUrl cannot parse
/// ("127.0.0.1:8080", no scheme) is refused as well.
inline bool isHomeUrl(const QString &text)
{
    const QUrl url(text.trimmed());
    const int defaultPort = url.scheme() == QLatin1String("https") ? 443 : 80;
    if (url.isValid() && url.port(defaultPort) == kHomePort)
        return true;
    static const QRegularExpression homePort(QStringLiteral(":0*8080(?![0-9])"));
    return homePort.match(text).hasMatch();
}

} // namespace omaseed::core
