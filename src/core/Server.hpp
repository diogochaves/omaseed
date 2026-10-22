#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QUrl>

namespace omaseed::core {

class ApiError;

/// How a `Server` authenticates. The secret itself (API key or password) is never stored
/// here; it lives in `SecretStore`, keyed by `Server::id`.
enum class AuthMode {
    ApiKey,
    Cookie,
    None,
};

QString authModeToString(AuthMode mode);
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

/// A configured server: a qBittorrent, or one a newer build recorded with another engine
/// (`isQbittorrent()`). Value type: copyable, comparable, JSON round-trippable.
/// Secrets (API key, password) are intentionally absent; see `SecretStore`.
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
    /// The keys of this server's servers.json entry that this build doesn't read, as they
    /// were: a newer build's (the engines build's `engine` and `engineVersion`, #84), kept so
    /// a save here never drops them. An `authMode` this build can't read is kept here too, and
    /// written back while `authMode` is still the `None` it fell back to. DECISIONS C28.
    QJsonObject extra;

    Server() = default;

    /// A fresh, random, stable identifier for a new server.
    static QString newId();

    /// The engine a newer build recorded for this server (`engine` in `extra`, spelled as
    /// there: "qbittorrent", "transmission", "deluge"); empty when none is recorded.
    QString engine() const;
    /// True for a qBittorrent: no engine recorded (every server before #84), or the engines
    /// build's "qbittorrent". Any other value is listed but never connected to (C28).
    bool isQbittorrent() const;
    /// The recorded engine's name for a sentence: "Transmission", "Deluge", an engine this
    /// build doesn't know as it is spelled; empty for a qBittorrent or an unreadable value.
    QString engineName() const;

    QJsonObject toJson() const;

    /// Parses a `Server` from `object`. Unknown keys are kept, as they are, in `extra`. `ok`,
    /// if given, is set to false when the object is missing a usable `id` (the only field this
    /// refuses to guess).
    static Server fromJson(const QJsonObject &object, bool *ok = nullptr);

    bool operator==(const Server &other) const = default;
};

} // namespace omaseed::core
