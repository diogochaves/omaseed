#pragma once

#include "core/engines/EngineKind.hpp"

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

namespace omaseed::core {

class ApiError;

/// How a `Server` authenticates. The secret itself (API key or password) is never stored
/// here; it lives in `SecretStore`, keyed by `Server::id`. Which modes an engine offers
/// (C25 K7) is `authModesFor()`.
enum class AuthMode {
    ApiKey, ///< qBittorrent's API key (Bearer)
    Cookie, ///< a username and password: qBittorrent's cookie login, Transmission's Basic
    None,
    Password, ///< one password and no username: deluge-web's
};

/// The sign-in modes `engine` offers, in the order its chips show them (C25 K7): qBittorrent
/// API key, username and password, none; Transmission username and password, none; Deluge
/// its one password. Unknown (not recognised yet) offers qBittorrent's three.
QList<AuthMode> authModesFor(EngineKind engine);

QString authModeToString(AuthMode mode);
/// The modes whose secret is a password (SecretStore::Kind::Password): username and password,
/// and Deluge's lone password. The API key is the other kind.
inline bool usesPassword(AuthMode mode)
{
    return mode == AuthMode::Cookie || mode == AuthMode::Password;
}
AuthMode authModeFromString(const QString &text, bool *ok = nullptr);

/// What a person reads when the server turned the credential down: "wrong username or
/// password" for a cookie login, "the server refused this API key" for a key. Empty for any
/// other failure (unreachable, timed out, …) and for `AuthMode::None`, where the raw error is
/// all there is to say.
QString credentialRefusedText(const ApiError &error, AuthMode mode);

/// What a person reads when TLS refused the server's certificate (ApiError::Kind::Certificate):
/// "untrusted certificate" while none is trusted for the server, "the certificate changed"
/// when `pinned` (Server::certificateSha256) names another one.
QString certificateRefusedText(const QString &pinned);

/// One remote-to-local path rewrite, for servers whose download root is mounted at a
/// different path locally (see the charter's `PathMap`). T2.3 may adapt the shape; kept
/// here as a plain value so `Server` does not depend on the parser module.
struct PathMapping
{
    QString remote;
    QString local;

    bool operator==(const PathMapping &other) const = default;

    QJsonObject toJson() const;
    static PathMapping fromJson(const QJsonObject &object);
};

/// A configured server. Value type: copyable, comparable, JSON round-trippable. Secrets (API
/// key, password) are intentionally absent; see `SecretStore`.
class Server
{
public:
    QString id; ///< stable UUID, generated once with `newId()`
    QString name;
    QUrl url;
    AuthMode authMode = AuthMode::None;
    QString username;
    QList<PathMapping> pathMap;
    bool isDefault = false;
    QDateTime addedAt;
    /// The certificate the person chose to trust for this server (trust on first use, see
    /// CertificatePin), 64 lowercase hex digits; empty when none was. Not a secret.
    QString certificateSha256;
    /// The engine it runs (servers.json v2; a v1 entry is qBittorrent), and the version it
    /// reported at the last contact ("5.2.3"; empty before one). C25 K12.
    EngineKind engine = EngineKind::Qbittorrent;
    QString engineVersion;

    Server() = default;

    /// A fresh, random, stable identifier for a new server.
    static QString newId();

    QJsonObject toJson() const;

    /// Parses a `Server` from `object`. Unknown keys are ignored. `ok`, if given, is set to
    /// false when the object is missing a usable `id` (the only field this refuses to guess).
    static Server fromJson(const QJsonObject &object, bool *ok = nullptr);

    bool operator==(const Server &other) const = default;
};

} // namespace omaseed::core
