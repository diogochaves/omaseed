#include "core/TrackerMerge.hpp"

#include <QSet>
#include <QUrl>

namespace omaseed::core::TrackerMerge {

namespace {

int defaultPort(const QString &scheme)
{
    if (scheme == QLatin1String("http"))
        return 80;
    if (scheme == QLatin1String("https"))
        return 443;
    return -1;
}

} // namespace

QString key(const QString &url)
{
    const QUrl parsed(url.trimmed(), QUrl::StrictMode);
    if (!parsed.isValid() || parsed.scheme().isEmpty() || parsed.host().isEmpty())
        return QString();
    const QString scheme = parsed.scheme().toLower();
    const int port = parsed.port(defaultPort(scheme));
    QString path = parsed.path(QUrl::FullyEncoded);
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    QString result = scheme + QStringLiteral("://") + parsed.host().toLower();
    if (port >= 0)
        result += QLatin1Char(':') + QString::number(port);
    result += path;
    if (parsed.hasQuery())
        result += QLatin1Char('?') + parsed.query(QUrl::FullyEncoded);
    return result;
}

QStringList missing(const QStringList &incoming, const QStringList &existing)
{
    QSet<QString> have;
    for (const QString &url : existing) {
        const QString k = key(url);
        if (!k.isEmpty())
            have.insert(k);
    }
    QStringList result;
    for (const QString &url : incoming) {
        const QString k = key(url);
        if (k.isEmpty() || have.contains(k))
            continue;
        have.insert(k);
        result.append(url.trimmed());
    }
    return result;
}

} // namespace omaseed::core::TrackerMerge
