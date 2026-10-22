#include "core/engines/transmission/TransmissionFingerprint.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

namespace omaseed::core::TransmissionFingerprint {

QUrl rpcUrl(const QUrl &address)
{
    QUrl url = address;
    url.setQuery(QString());
    url.setFragment(QString());
    QString path = url.path();
    while (path.endsWith(QLatin1Char('/')))
        path.chop(1);
    const QString marker = QStringLiteral("/transmission");
    const qsizetype at = path.lastIndexOf(marker);
    if (at >= 0 && (at + marker.size() == path.size() || path.at(at + marker.size()) == '/')) {
        // …/transmission, …/transmission/web, …/transmission/rpc: the RPC sits beside the web
        // UI, under whatever a proxy put in front.
        path = path.left(at + marker.size()) + QStringLiteral("/rpc");
    } else {
        path += marker + QStringLiteral("/rpc");
    }
    url.setPath(path);
    return url;
}

QByteArray sessionGetBody()
{
    return QByteArrayLiteral(
        R"({"jsonrpc":"2.0","method":"session_get","params":{"fields":["version","rpc_version_semver"]},"id":1})");
}

Reading read(const HttpAnswer &answer)
{
    Reading reading;
    if (answer.status == 409) {
        reading.sessionId = answer.header("X-Transmission-Session-Id");
        if (reading.sessionId.isEmpty())
            return reading; // a 409 of some other server
        reading.fingerprint = Fingerprint::Itself;
        reading.rpcVersion = QString::fromLatin1(answer.header("X-Transmission-Rpc-Version"));
        return reading;
    }
    if (answer.status == 401) {
        const QByteArray asked = answer.header("WWW-Authenticate");
        if (asked.contains("Transmission")) {
            reading.fingerprint = Fingerprint::Itself;
            reading.wantsLogin = true;
        }
        return reading;
    }
    if (answer.status != 200)
        return reading;

    // session_get's answer: JSON-RPC 2.0 `result`, or the legacy envelope's `arguments` (an
    // old daemon that took the request in its own dialect).
    const QJsonObject root = QJsonDocument::fromJson(answer.body).object();
    QJsonObject fields = root.value(QStringLiteral("result")).toObject();
    if (fields.isEmpty())
        fields = root.value(QStringLiteral("arguments")).toObject();
    const bool rpc2 = root.value(QStringLiteral("jsonrpc")).toString() == QLatin1String("2.0");
    const bool legacy = root.value(QStringLiteral("result")).isString();
    if (!rpc2 && !legacy)
        return reading;
    reading.fingerprint = Fingerprint::Itself;
    // "4.1.3 (6e5b2d1)": the number, without the commit.
    const QString version = fields.value(QStringLiteral("version")).toString();
    static const QRegularExpression number(QStringLiteral("^\\s*(\\d+(\\.\\d+)*)"));
    const QRegularExpressionMatch match = number.match(version);
    reading.version = match.hasMatch() ? match.captured(1) : version.trimmed();
    reading.rpcVersion = fields.value(QStringLiteral("rpc_version_semver")).toString();
    return reading;
}

} // namespace omaseed::core::TransmissionFingerprint
