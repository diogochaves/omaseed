#pragma once

// The one place a live test gets its server URL from. Live tests only ever talk to the :8081
// playground or the :8082 strict twin (scripts/dev/test-server.sh); :8080 is the real library
// and no test may touch it. A mistyped QBT_TEST_URL used to be taken at its word, and the
// destructive tests (delete, deleteFiles, setPreferences) would have run against whatever
// answered, so a URL on port 8080 now ends the test binary before a single request.

#include "core/HomeGuard.hpp"

#include <QProcessEnvironment>
#include <QString>
#include <QUrl>
#include <QtGlobal>

#include <cstdio>
#include <cstdlib>

namespace LiveServer {

using omaseed::core::isHomeUrl;

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
