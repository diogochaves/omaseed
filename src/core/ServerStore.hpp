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
/// `load()` tolerates a missing file (nothing to load, not an error). A file whose
/// `version` is newer than this build understands, or whose JSON does not parse, is left
/// untouched on disk and `loadFailed()` is emitted; nothing already in memory is discarded.
/// The next save never overwrites such a file: it renames it to
/// `servers.json.unreadable-<timestamp>` first, and writes nothing if that rename fails.
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
    /// none). Cleared once a save has moved that file aside.
    QString loadError() const { return m_loadError; }
    /// Where the unreadable file was moved by the last save, if one was.
    QString setAsidePath() const { return m_setAsidePath; }

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
    bool m_persistent = true;
    // Set while the file on disk is one load() could not use; save() moves it aside first.
    mutable QString m_loadError;
    mutable QString m_setAsidePath;

    void failLoad(const QString &message);
    static QString filePath();
    int indexOf(const QString &id) const;
};

} // namespace omaseed::core
