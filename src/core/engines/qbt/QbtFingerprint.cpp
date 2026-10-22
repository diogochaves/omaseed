#include "core/engines/qbt/QbtFingerprint.hpp"

#include <QRegularExpression>

namespace omaseed::core::QbtFingerprint {

QUrl versionUrl(const QUrl &address)
{
    QUrl url = address;
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    url.setPath(path + QStringLiteral("/api/v2/app/version"));
    url.setQuery(QString());
    return url;
}

Reading read(const HttpAnswer &answer)
{
    Reading reading;
    const QString body = QString::fromUtf8(answer.body).trimmed();
    if (answer.status == 200) {
        // "v5.3.0", and nothing else: an HTML page or a JSON error is another server.
        static const QRegularExpression version(QStringLiteral("^v?\\d+\\.\\d+(\\.\\d+)*\\S*$"));
        if (version.match(body).hasMatch()) {
            reading.fingerprint = Fingerprint::Itself;
            reading.version = body;
        }
        return reading;
    }
    // Signed out, qBittorrent answers 403 with the one word.
    if (answer.status == 403 && body.compare(QLatin1String("Forbidden"), Qt::CaseInsensitive) == 0)
        reading.fingerprint = Fingerprint::Itself;
    return reading;
}

} // namespace omaseed::core::QbtFingerprint
