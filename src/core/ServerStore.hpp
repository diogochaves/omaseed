#pragma once

#include "core/Server.hpp"

#include <QList>
#include <QObject>
#include <QString>
#include <optional>

namespace omaseed::core {

/// Holds the configured server list and persists it to
/// `<configHome>/omaseed/servers.json` (`configHome` follows `XDG_CONFIG_HOME`, see
/// `paths::configHome()`). File shape:
/// `{ "version": 1, "servers": [ Server::toJson()... ], "defaultId": "<id or empty>" }`.
///
/// Version 2 is the engines build's (#84): each server's `engine` and `engineVersion`. This
/// build reads it and keeps it (DECISIONS C28): the keys it doesn't know stay in each
/// `Server::extra` and on the file's root, and a save writes the highest version it read, so
/// a v2 file stays v2. A file this build starts writes version 1.
///
/// `load()` tolerates a missing file (nothing to load, not an error). A file whose
/// `version` is newer than this build understands (3 on), or whose JSON does not parse, is never
/// overwritten: load() renames it to `servers.json.unreadable-<timestamp>` (with a `-N`
/// suffix if taken) straight away, `keptPath()` says where, and `loadFailed()` is emitted;
/// nothing already in memory is discarded. If that rename fails, the file stays in place,
/// the next save tries again, and writes nothing if it still cannot move it.
class ServerStore : public QObject
{
    Q_OBJECT

public:
    explicit ServerStore(QObject *parent = nullptr);

    /// Reads `servers.json`. Returns false (and emits `loadFailed`) when the file exists
    /// but could not be parsed or is a version this build cannot read.
    bool load();

    /// Writes `servers.json` atomically (via `QSaveFile`), then sets the directory to 0700
    /// and the file to 0600. A store that is not persistent writes nothing and says it did.
    bool save() const;

    /// False keeps every change in memory only: a `--demo` run lists its fictional server
    /// and may add or remove more, but must never touch the user's real `servers.json`.
    void setPersistent(bool persistent) { m_persistent = persistent; }
    bool isPersistent() const { return m_persistent; }

    /// Why the last load() could not use the file on disk; empty when it could (or there was
    /// none). Kept until a load succeeds, so the first-run page can still say it.
    QString loadError() const { return m_loadError; }
    /// Where the file the last load() could not use is now: the `.unreadable-<timestamp>`
    /// path it was moved to, or `servers.json` itself while it could not be moved. Empty
    /// when the last load succeeded.
    QString keptPath() const { return m_keptPath; }

    QList<Server> servers() const;
    int count() const;

    std::optional<Server> byId(const QString &id) const;
    /// The server whose id matches the store's `defaultId`, if any.
    std::optional<Server> defaultServer() const;
    QString defaultId() const { return m_defaultId; }

    /// Appends `server` (its `id` must be non-empty and not already present) and saves.
    bool add(const Server &server);
    /// Replaces the server with a matching `id` and saves. Returns false if not found.
    bool update(const Server &server);
    /// Removes the server with `id` and saves. Clears `defaultId` if it pointed at it.
    bool remove(const QString &id);
    /// Sets `defaultId` to `id` (must be a known server, or empty to clear) and saves.
    bool setDefault(const QString &id);

Q_SIGNALS:
    /// Emitted after any change to the in-memory list: load, add, update, remove, setDefault.
    void changed();
    void added(const QString &id);
    void removed(const QString &id);
    void updated(const QString &id);
    /// The file existed but could not be used; it was left untouched on disk.
    void loadFailed(const QString &message);

private:
    QList<Server> m_servers;
    QString m_defaultId;
    /// The version save() writes: 1, or the version of the file load() read (2, #84).
    int m_fileVersion = 1;
    /// The root keys of the file load() read that this build doesn't know, written back.
    QJsonObject m_rootExtra;
    bool m_persistent = true;
    QString m_loadError;
    // Where the unreadable file is kept; see keptPath().
    mutable QString m_keptPath;
    // True while servers.json itself is the unreadable file: save() must move it first.
    mutable bool m_fileUnreadable = false;

    void failLoad(const QString &message);
    /// Renames an unreadable servers.json to `.unreadable-<timestamp>`; false while it is
    /// still in place. Const because save() calls it.
    bool moveUnreadableAside() const;
    static QString filePath();
    int indexOf(const QString &id) const;
};

} // namespace omaseed::core
