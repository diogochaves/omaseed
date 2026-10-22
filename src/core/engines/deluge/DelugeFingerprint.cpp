#include "core/engines/deluge/DelugeFingerprint.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace omaseed::core::DelugeFingerprint {

QUrl jsonUrl(const QUrl &address)
{
    QUrl url = address;
    url.setQuery(QString());
    url.setFragment(QString());
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    if (!path.endsWith(QLatin1String("/json")))
        path += QStringLiteral("/json");
    url.setPath(path);
    return url;
}

QByteArray call(const QString &method, const QJsonArray &params, int id)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("method"), method},
                                     {QStringLiteral("params"), params},
                                     {QStringLiteral("id"), id}})
        .toJson(QJsonDocument::Compact);
}

Reading read(const HttpAnswer &answer)
{
    Reading reading;
    if (answer.status != 200)
        return reading;
    const QJsonDocument document = QJsonDocument::fromJson(answer.body);
    if (!document.isObject())
        return reading;
    const QJsonObject root = document.object();
    // JSON-RPC v1: all three keys, every time.
    if (!root.contains(QStringLiteral("id")) || !root.contains(QStringLiteral("result")) ||
        !root.contains(QStringLiteral("error")))
        return reading;
    reading.fingerprint = Fingerprint::Itself;
    reading.result = root.value(QStringLiteral("result"));
    const QJsonValue error = root.value(QStringLiteral("error"));
    if (error.isObject())
        reading.errorMessage = error.toObject().value(QStringLiteral("message")).toString();
    return reading;
}

QStringList hostNames(const QJsonValue &hosts, QStringList *ids)
{
    QStringList names;
    for (const QJsonValue &row : hosts.toArray()) {
        const QJsonArray host = row.toArray();
        if (host.size() < 3)
            continue;
        names.append(QStringLiteral("%1:%2").arg(host.at(1).toString()).arg(host.at(2).toInt()));
        if (ids)
            ids->append(host.at(0).toString());
    }
    return names;
}

} // namespace omaseed::core::DelugeFingerprint
