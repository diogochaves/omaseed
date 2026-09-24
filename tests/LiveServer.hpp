#pragma once

// The one place a live test gets its server URL from. Live tests only ever talk to the :8081
// playground or the :8082 strict twin (scripts/dev/test-server.sh); :8080 is the real library
// and no test may touch it. A mistyped QBT_TEST_URL used to be taken at its word, and the
// destructive tests (delete, deleteFiles, setPreferences) would have run against whatever
// answered, so a URL on port 8080 now ends the test binary before a single request.

#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace LiveServer {

inline constexpr int kHomePort = 8080;

/// True for any URL on port 8080, whatever the host: 127.0.0.1, localhost, ::1 or a LAN name
/// can all reach the real library. The raw text is checked too, so a URL QUrl cannot parse
/// ("127.0.0.1:8080", no scheme) is refused as well.
inline bool isHomeUrl(const QString &text)
{
    const QUrl url(text.trimmed());
    const int defaultPort = url.scheme() == QLatin1String("https") ? 443 : 80;
    if (url.isValid() && url.port(defaultPort) == kHomePort) {
        return true;
    }
    static const QRegularExpression homePort(QStringLiteral(":0*8080(?![0-9])"));
    return homePort.match(text).hasMatch();
}

/// `text` as a URL; the process exits with status 2 if it is on port 8080.
inline QUrl guarded(const QString &text)
{
    if (isHomeUrl(text)) {
        // A plain exit, not qFatal: an abort would leave a core dump (and a crash notice on
        // the desktop) behind every time the guard does its job.
        std::fprintf(stderr,
                     "refusing to run a live test against %s: port 8080 is the real library, "
                     "never a test server (use :8081 or :8082)\n",
                     qPrintable(text));
        std::exit(2);
    }
    return QUrl(text);
}

/// The environment variable `name`, or `fallback` when it is unset, behind the :8080 guard.
inline QUrl fromEnv(const char *name, const QString &fallback)
{
    const QString value = QProcessEnvironment::systemEnvironment().value(QString::fromLatin1(name));
    return guarded(value.isEmpty() ? fallback : value);
}

/// QBT_TEST_URL, or the playground's fixed address when dev.env is not loaded.
inline QUrl playground()
{
    return fromEnv("QBT_TEST_URL", QStringLiteral("http://127.0.0.1:8081"));
}

/// QBT_STRICT_URL, or the strict twin's fixed address when dev.env is not loaded.
inline QUrl strict()
{
    return fromEnv("QBT_STRICT_URL", QStringLiteral("http://127.0.0.1:8082"));
}

} // namespace LiveServer
