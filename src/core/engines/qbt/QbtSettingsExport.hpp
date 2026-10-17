#pragma once

// QbtSettingsExport — a qBittorrent's settings as one JSON file (docs/24 §2, §4; D2 a, D5 a;
// #32).
//
// The file holds what the Web UI API hands out about a server's setup: every preference
// (`app/preferences`), the categories with their folders and share limits, the tags, the RSS
// feeds (`rss/items`) and auto-download rules (`rss/rules`), and the qBittorrent and Web API
// versions it came from:
//
//   { "format": "omaseed-qbittorrent-settings", "version": 1, "created": ISO date,
//     "app": omaseed's version, "server": {"name", "url", "qbittorrent", "webApi"},
//     "preferences": {...}, "categories": {...}, "tags": [...],
//     "rss": {"items": {...}, "rules": {...}} }
//
// Secrets never reach the file (D5 a): the keys qBittorrent returns in clear (proxy, e-mail and
// DynDNS passwords, the Web UI API key), the Web UI password should a version ever return it,
// qBittorrent 4.1's Web UI key, and an RSS rule's torrent `ssl_private_key`. `isSecretKey` is
// the one test, by name and by suffix, so a key a later qBittorrent adds in the same form
// stays out too. `parse` drops them again, so a file edited by hand cannot bring one in.
//
// `collect()` reads all of it from a server through its qBittorrent engine; `toJson` / `parse`
// are pure.

#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>

#include <optional>

namespace omaseed::core {

class QbtClient;
class QbtEngine;

/// One server's settings, as the file holds them.
struct QbtSettingsSnapshot
{
    QDateTime created;
    QString appVersion; ///< omaseed's, at export
    QString serverName;
    QString serverUrl;
    QString version;    ///< qBittorrent's, "v5.2.3"
    QString apiVersion; ///< the Web API's, "2.15.1"
    QJsonObject preferences;
    QJsonObject categories; ///< torrents/categories, keyed by name
    QStringList tags;
    QJsonObject rssItems; ///< rss/items?withData=false: folders hold feeds ({uid, url})
    QJsonObject rssRules; ///< rss/rules, keyed by rule name

    int feedCount() const;
};

class QbtSettingsExport : public QObject
{
    Q_OBJECT

public:
    static constexpr int kVersion = 1;
    static inline const QString kFormat = QStringLiteral("omaseed-qbittorrent-settings");
    static inline const QString kSuffix = QStringLiteral(".json");

    /// Talks to `engine`'s transport (docs/36 §4.4: a qBittorrent surface); null for none.
    explicit QbtSettingsExport(QbtEngine *engine, QObject *parent = nullptr);

    /// A key whose value is a secret (D5 a): never written, never imported.
    static bool isSecretKey(const QString &key);
    /// The secrets qBittorrent 5.2 returns in clear, by name (for the sheet's line and tests).
    static QStringList knownSecretKeys();
    /// `object` without its secret keys, at any depth.
    static QJsonObject withoutSecrets(const QJsonObject &object);

    /// The file's bytes: indented JSON, secrets left out.
    static QByteArray toJson(const QbtSettingsSnapshot &snapshot);
    /// A file's contents, or nothing (with `error`) when it is not a settings export this build
    /// reads. Secrets in it are dropped.
    static std::optional<QbtSettingsSnapshot> parse(const QByteArray &bytes,
                                                    QString *error = nullptr);
    static std::optional<QbtSettingsSnapshot> parseFile(const QString &path,
                                                        QString *error = nullptr);
    /// Writes `toJson(snapshot)` to `path` atomically, private (0600).
    static bool
    writeFile(const QbtSettingsSnapshot &snapshot, const QString &path, QString *error = nullptr);

    /// Reads everything from the server: versions, preferences, categories, tags, RSS. One
    /// collection at a time; `collected` or `failed` ends it.
    void collect(const QString &serverName, const QString &serverUrl, const QString &appVersion);
    bool isBusy() const { return m_busy; }

Q_SIGNALS:
    void collected(const omaseed::core::QbtSettingsSnapshot &snapshot);
    void failed(const QString &error);

private:
    void step();
    void fail(const QString &error);

    QPointer<QbtClient> m_client;
    QbtSettingsSnapshot m_snapshot;
    int m_step = 0;
    bool m_busy = false;
};

} // namespace omaseed::core

Q_DECLARE_METATYPE(omaseed::core::QbtSettingsSnapshot)
