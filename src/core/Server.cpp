#include "core/Server.hpp"

#include "core/ApiError.hpp"
#include "core/CertificatePin.hpp"

#include <QJsonArray>
#include <QUuid>

namespace omaseed::core {

QString authModeToString(AuthMode mode)
{
    switch (mode) {
    case AuthMode::ApiKey:
        return QStringLiteral("apiKey");
    case AuthMode::Cookie:
        return QStringLiteral("cookie");
    case AuthMode::None:
        break;
    }
    return QStringLiteral("none");
}

AuthMode authModeFromString(const QString &text, bool *ok)
{
    if (ok)
        *ok = true;
    if (text == QStringLiteral("apiKey"))
        return AuthMode::ApiKey;
    if (text == QStringLiteral("cookie"))
        return AuthMode::Cookie;
    if (text == QStringLiteral("none")) {
        return AuthMode::None;
    }
    if (ok)
        *ok = false;
    return AuthMode::None;
}

QString credentialRefusedText(const ApiError &error, AuthMode mode)
{
    // qBittorrent answers a bad login with 401 and a bad Bearer key with 403 (5.2), but the
    // two have moved between versions, so either counts as "turned down".
    const bool refused =
        error.kind() == ApiError::Kind::Unauthorized || error.kind() == ApiError::Kind::Forbidden;
    if (!refused)
        return {};
    switch (mode) {
    case AuthMode::Cookie:
        return QStringLiteral("wrong username or password");
    case AuthMode::ApiKey:
        return QStringLiteral("the server refused this API key");
    case AuthMode::None:
        // No credential went out, and the server wants one.
        return QStringLiteral("this server asks for a login");
    }
    return {};
}

QString certificateRefusedText(const QString &pinned)
{
    return pinned.isEmpty() ? QStringLiteral("untrusted certificate")
                            : QStringLiteral("the certificate changed");
}

QJsonObject PathMapping::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("remote")] = remote;
    object[QStringLiteral("local")] = local;
    return object;
}

PathMapping PathMapping::fromJson(const QJsonObject &object)
{
    PathMapping mapping;
    mapping.remote = object.value(QStringLiteral("remote")).toString();
    mapping.local = object.value(QStringLiteral("local")).toString();
    return mapping;
}

namespace {

/// The keys toJson() writes: anything else in an entry is a newer build's, kept in `extra`.
bool isKnownKey(const QString &key)
{
    static const QStringList keys{
        QStringLiteral("id"),
        QStringLiteral("name"),
        QStringLiteral("url"),
        QStringLiteral("authMode"),
        QStringLiteral("username"),
        QStringLiteral("pathMap"),
        QStringLiteral("isDefault"),
        QStringLiteral("addedAt"),
        QStringLiteral("certificateSha256"),
    };
    return keys.contains(key);
}

} // namespace

QString Server::newId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString Server::engine() const
{
    return extra.value(QStringLiteral("engine")).toString();
}

bool Server::isQbittorrent() const
{
    // The engines build's spelling (#84, engineKindToString). Any other value, an empty or
    // a non-string one included, is not taken for a qBittorrent.
    const QJsonValue value = extra.value(QStringLiteral("engine"));
    return value.isUndefined() || value == QJsonValue(QStringLiteral("qbittorrent"));
}

QString Server::engineName() const
{
    if (isQbittorrent())
        return {};
    const QString stored = engine();
    // "qBittorrent" isn't the engines build's spelling, and naming it would read as nonsense.
    if (stored.compare(QStringLiteral("qbittorrent"), Qt::CaseInsensitive) == 0)
        return {};
    if (stored == QStringLiteral("transmission"))
        return QStringLiteral("Transmission");
    if (stored == QStringLiteral("deluge"))
        return QStringLiteral("Deluge");
    return stored;
}

QJsonObject Server::toJson() const
{
    QJsonObject object;
    object[QStringLiteral("id")] = id;
    object[QStringLiteral("name")] = name;
    object[QStringLiteral("url")] = url.toString(QUrl::RemoveUserInfo);
    object[QStringLiteral("authMode")] = authModeToString(authMode);
    object[QStringLiteral("username")] = username;

    QJsonArray map;
    for (const PathMapping &mapping : pathMap)
        map.append(mapping.toJson());
    object[QStringLiteral("pathMap")] = map;

    object[QStringLiteral("isDefault")] = isDefault;
    if (addedAt.isValid())
        object[QStringLiteral("addedAt")] = addedAt.toUTC().toString(Qt::ISODate);
    if (!certificateSha256.isEmpty())
        object[QStringLiteral("certificateSha256")] = certificateSha256;

    // A newer build's keys, as they were read (C28). An authMode this build couldn't read
    // goes back as it was, unless a mode was picked here since.
    for (auto it = extra.constBegin(); it != extra.constEnd(); ++it) {
        if (it.key() == QStringLiteral("authMode")) {
            if (authMode == AuthMode::None)
                object.insert(it.key(), it.value());
        } else if (!object.contains(it.key())) {
            object.insert(it.key(), it.value());
        }
    }
    return object;
}

Server Server::fromJson(const QJsonObject &object, bool *ok)
{
    Server server;
    server.id = object.value(QStringLiteral("id")).toString();
    server.name = object.value(QStringLiteral("name")).toString();
    server.url = QUrl(object.value(QStringLiteral("url")).toString());
    bool authModeRead = false;
    server.authMode =
        authModeFromString(object.value(QStringLiteral("authMode")).toString(), &authModeRead);
    server.username = object.value(QStringLiteral("username")).toString();

    for (const QJsonValue &value : object.value(QStringLiteral("pathMap")).toArray()) {
        if (value.isObject())
            server.pathMap.append(PathMapping::fromJson(value.toObject()));
    }

    server.isDefault = object.value(QStringLiteral("isDefault")).toBool();

    const QString addedAtText = object.value(QStringLiteral("addedAt")).toString();
    if (!addedAtText.isEmpty())
        server.addedAt = QDateTime::fromString(addedAtText, Qt::ISODate);

    // Anything but a well-formed SHA-256 reads as no pin: a damaged value must never turn
    // into "accept whatever certificate".
    server.certificateSha256 =
        CertificatePin::normalized(object.value(QStringLiteral("certificateSha256")).toString());

    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (!isKnownKey(it.key()))
            server.extra.insert(it.key(), it.value());
    }
    // A newer build's mode (the engines build's "password", Deluge's) reads as None here and
    // is written back as it was.
    if (!authModeRead && object.contains(QStringLiteral("authMode")))
        server.extra.insert(QStringLiteral("authMode"), object.value(QStringLiteral("authMode")));

    if (ok)
        *ok = !server.id.isEmpty();
    return server;
}

} // namespace omaseed::core
