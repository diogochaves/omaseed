#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <optional>

namespace omaseed::core {

/// The result of one `SecretStore` operation. A free-standing (not nested) `QObject` because
/// moc does not support meta-object features on nested classes. Deletes itself once
/// `finished()` fires.
class SecretStoreJob : public QObject
{
    Q_OBJECT

public:
    explicit SecretStoreJob(QObject *parent = nullptr)
        : QObject(parent)
    {
        connect(this, &SecretStoreJob::finished, this, &QObject::deleteLater);
    }

Q_SIGNALS:
    void finished(bool ok, const QString &value, const QString &error);
};

/// Stores per-server secrets (API keys, passwords) that never belong in `Server` or
/// `servers.json`. Two backends:
///
/// - **Keychain**: libsecret, through `qtkeychain`'s `WritePasswordJob` / `ReadPasswordJob` /
///   `DeletePasswordJob`, service `io.github.diogochaves.omaseed` (or a caller-supplied
///   service name, so tests can use a `-test` suffix that never touches real entries), key
///   `<serverId>/<kind>`.
/// - **File**: `<configHome>/omaseed/secrets/<serverId>.json` (dir 0700, file 0600),
///   `{ "version": 1, "apiKey": "...", "password": "..." }`, used when the keychain backend
///   is unavailable.
///
/// Which backend is in use is detected once per process (a throwaway probe write/read/
/// delete), cached, and exposed as `backendName()`. `setForcedBackend()` skips the probe
/// for tests.
///
/// All operations are asynchronous: they return a `Job*` (parented to `this` unless a
/// `parent` is given) that emits `finished(bool ok, QString value, QString error)` exactly
/// once and then deletes itself. `value` is only meaningful for `secret()`.
class SecretStore : public QObject
{
    Q_OBJECT

public:
    enum class Kind {
        ApiKey,
        Password,
    };

    enum class Backend {
        Keychain,
        File,
    };

    using Job = SecretStoreJob;

    /// `service` is the keychain service name; tests pass one suffixed `-test`.
    explicit SecretStore(QString service = defaultServiceName(), QObject *parent = nullptr);

    static QString defaultServiceName();

    Job *
    setSecret(const QString &serverId, Kind kind, const QString &value, QObject *parent = nullptr);
    Job *secret(const QString &serverId, Kind kind, QObject *parent = nullptr);
    Job *removeSecret(const QString &serverId, Kind kind, QObject *parent = nullptr);

    /// "libsecret" or "file". Triggers backend detection if it has not run yet.
    QString backendName();
    Backend backend();

    /// Skips the probe and pins the backend, for tests. Pass `std::nullopt` to go back to
    /// auto-detection on the next call.
    void setForcedBackend(std::optional<Backend> backend);

private:
    QString m_service;
    std::optional<Backend> m_detectedBackend;
    std::optional<Backend> m_forcedBackend;
    QList<std::function<void()>> m_queue;
    bool m_draining = false;

    Backend resolveBackend();
    Backend probeKeychain() const;

    static QString keyFor(Kind kind);
    static QString fieldFor(Kind kind);

    void runKeychainWrite(Job *job, const QString &serverId, Kind kind, const QString &value) const;
    void runKeychainRead(Job *job, const QString &serverId, Kind kind) const;
    void runKeychainRemove(Job *job, const QString &serverId, Kind kind) const;

    /// One queue for every operation: the keychain backend blocks in a nested event loop,
    /// so two operations must never overlap (see the note in SecretStore.cpp).
    void enqueue(std::function<void()> operation);
    void drain();

    void runFileWrite(Job *job, const QString &serverId, Kind kind, const QString &value) const;
    void runFileRead(Job *job, const QString &serverId, Kind kind) const;
    void runFileRemove(Job *job, const QString &serverId, Kind kind) const;

    static QString secretFilePath(const QString &serverId);
};

} // namespace omaseed::core
