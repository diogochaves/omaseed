#pragma once

// ServerListModel — the QML face of `ServerStore` (T4.5, spec v2 §5 Fig. 16's Servers pane
// and §3 Fig. 9's server sheet).
//
// `ServerStore` is a plain value container: a list of `Server`, a default id and a JSON file.
// The settings modal needs a `QAbstractListModel` with the *texts* the figure prints
// ("http://127.0.0.1:8080 · API key · 142 torrents", "/downloads → /data/torrents",
// "yes · 3 ms"), so this is a thin model over the store rather than a change to it: the store
// stays the single owner of the list and of `servers.json`, and every mutation here goes
// through it.
//
// Two kinds of state live in a row:
//   - what the store persists: id, name, url, auth mode, username, path map, default flag;
//   - what a probe found and nothing persists: reachable, latency, version, API version,
//     torrent count, last error. `setProbeResult()` writes those; they are cleared when the
//     server is removed. Its argument order is `App::serverTested()`'s, so a sheet that
//     tested a server before it had an id can hand the answer straight on once it does
//     (docs/DECISIONS.md Q23).
//
// Secrets never travel through `Server` or `servers.json`. `add()`/`update()` take the secret
// separately and hand it to `SecretStore` (asynchronously, keyed by the server's id);
// `remove()` deletes both kinds of secret for that id. A model built without a `SecretStore`
// simply skips that half, which is what the JSON-only unit tests use.

#include "core/Server.hpp"

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantMap>

namespace omaseed::core {

class SecretStore;
class ServerStore;

class ServerListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(QString defaultId READ defaultId NOTIFY changed)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        UrlRole,          ///< the full URL, "http://127.0.0.1:8080"
        UrlTextRole,      ///< "127.0.0.1:8080" — host:port, for the dim line
        AuthModeRole,     ///< "apiKey" | "cookie" | "none"
        AuthModeTextRole, ///< "API key" | "cookie" | "none"
        UsernameRole,
        IsDefaultRole,
        PathMapTextRole,   ///< "/downloads → /data/torrents", empty when there is none
        ReachableRole,     ///< the last probe's verdict; false until one ran
        ReachableTextRole, ///< "yes · 3 ms" / "no · connection refused"; empty until one ran
        TorrentCountRole,  ///< -1 until a probe counted them
        LastErrorRole,
        VersionRole,     ///< "5.2.3" (the server's leading "v" dropped); empty until a probe ran
        ApiVersionRole,  ///< "2.15.1"; empty until a probe ran
        VersionTextRole, ///< "v5.2.3" for the dim line; empty until a probe ran
        CertificateRole, ///< the trusted certificate's SHA-256 (CertificatePin), or empty
    };
    Q_ENUM(Role)

    /// `store` is required and not owned; `secrets` is optional and not owned.
    explicit ServerListModel(ServerStore *store,
                             SecretStore *secrets = nullptr,
                             QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return int(m_rows.size()); }
    QString defaultId() const;

    /// Row index of `id`, or -1.
    Q_INVOKABLE int indexOfId(const QString &id) const;
    /// The id of row `row`, or "" — QML cannot call `data()`/`index()` on a model.
    Q_INVOKABLE QString idAt(int row) const;
    /// Every role of one row as a map, the shape `add()`/`update()` take back. An unknown id
    /// gives an empty map.
    Q_INVOKABLE QVariantMap serverMap(const QString &id) const;

    /// Appends a server built from `fields` (see `serverFromMap()`) and saves the store.
    /// A missing or empty `id` gets a fresh one. Returns the id, or an empty string when the
    /// fields do not describe a usable server (no URL). A non-empty `secret` is written to
    /// `SecretStore` under the auth mode's kind.
    Q_INVOKABLE QString add(const QVariantMap &fields, const QString &secret = QString());
    /// Replaces the server `fields["id"]` names. `certificateSha256` left out of `fields`
    /// keeps the trusted certificate; given (empty included), it replaces it. An empty
    /// `secret` leaves the stored one
    /// alone — that is what "edit without retyping the key" needs — unless the auth mode
    /// changed: the old mode's secret is then removed (both, for "none"), so a stale key is
    /// neither kept nor silently reused on a switch back.
    Q_INVOKABLE bool update(const QVariantMap &fields, const QString &secret = QString());
    /// Removes the server and both of its secrets.
    Q_INVOKABLE bool remove(const QString &id);
    /// Makes `id` the default (empty clears it).
    Q_INVOKABLE bool setDefault(const QString &id);

    /// What a `testServer()` round trip found — the arguments of `App::serverTested()` after
    /// its id. `torrentCount` < 0 leaves the count alone; empty versions leave the last known
    /// ones alone (a failed probe has none, and the server did not change version by being
    /// unreachable).
    Q_INVOKABLE void setProbeResult(const QString &id,
                                    bool reachable,
                                    const QString &version,
                                    const QString &apiVersion,
                                    int torrentCount,
                                    int latencyMs,
                                    const QString &error);

    // ---- the pure parts, so a test needs no store -------------------------------------

    /// Builds a `Server` from the QML field map: `id`, `name`, `url`, `authMode`
    /// ("apiKey" | "cookie" | "none"), `username`, `pathMapRemote`, `pathMapLocal`,
    /// `certificateSha256` (anything but a SHA-256 reads as none). User info
    /// typed into the URL never stays there: the user name fills an empty `username`, and for
    /// a cookie server the password comes back through `urlPassword` (dropped otherwise).
    static Server serverFromMap(const QVariantMap &fields, QString *urlPassword = nullptr);
    /// "API key" / "cookie" / "none" — what Fig. 9 and Fig. 16 print.
    static QString authModeText(AuthMode mode);
    /// "127.0.0.1:8080", the host:port half of the dim line.
    static QString urlText(const QUrl &url);
    /// "/downloads → /data/torrents" for the first mapping, empty when there is none.
    static QString pathMapText(const QList<PathMapping> &mappings);

Q_SIGNALS:
    void countChanged();
    void changed();

private:
    struct Probe
    {
        bool reachable = false;
        bool probed = false;
        int torrentCount = -1;
        int latencyMs = -1;
        QString version;
        QString apiVersion;
        QString error;
    };

    void reload();
    void writeSecret(const Server &server, const QString &secret);

    ServerStore *m_store = nullptr;
    SecretStore *m_secrets = nullptr;
    QList<Server> m_rows;
    QHash<QString, Probe> m_probes;
};

} // namespace omaseed::core
