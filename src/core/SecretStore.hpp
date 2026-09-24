#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include <chrono>
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
/// Which backend is in use is detected once per store, asynchronously: the first operation
/// queues a throwaway probe (write, then delete, of a `__omaseed_probe__` entry) ahead of
/// itself. A successful write decides Keychain; a failed write, an unavailable keychain or a
/// probe that has not answered within `probeTimeout()` (3 s by default, Q4) decides File.
/// The result is cached, announced by `backendResolved()` and readable through `backend()`
/// / `backendName()`, neither of which blocks. `setForcedBackend()` skips the probe for
/// tests. A probe that timed out keeps listening: if the keychain answers late (the user
/// unlocked it), the store switches to Keychain for the rest of the session.
///
/// The choice is made per session, so a secret may sit in the backend not in use (stored
/// while the keyring was locked, or the other way round). A read that misses therefore looks
/// in the other backend too: a secret found in the file while on Keychain is answered and
/// moved into the keychain; one found only in the keychain while on File is answered as is
/// (that lookup is bounded by the probe timeout). A write on Keychain drops any stale file
/// copy, and a remove clears both.
///
/// Nothing in the store spins a nested event loop: every operation — the probe included —
/// runs through one queue, strictly one at a time, and the next one starts only once the
/// current one has finished. That is what keeps a read queued right behind a write from
/// missing the entry the write is still storing (T4.5).
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
    Q_ENUM(Backend)

    using Job = SecretStoreJob;

    /// Test hook: a replacement for the keychain probe. It receives a completion callback to
    /// call with `true` when the keychain answered (Backend::Keychain) or `false` when it did
    /// not (Backend::File); a runner that never calls it lets the timeout decide.
    using ProbeRunner = std::function<void(std::function<void(bool keychainOk)> done)>;

    /// `service` is the keychain service name; tests pass one suffixed `-test`.
    explicit SecretStore(QString service = defaultServiceName(), QObject *parent = nullptr);

    static QString defaultServiceName();

    Job *
    setSecret(const QString &serverId, Kind kind, const QString &value, QObject *parent = nullptr);
    Job *secret(const QString &serverId, Kind kind, QObject *parent = nullptr);
    Job *removeSecret(const QString &serverId, Kind kind, QObject *parent = nullptr);

    /// The backend in use, or `std::nullopt` while it is still being detected. Asking
    /// queues the probe if it has not run yet; `backendResolved()` reports the answer.
    std::optional<Backend> backend();
    /// "libsecret", "file" or "detecting" (same semantics as `backend()`).
    QString backendName();

    /// Skips the probe and pins the backend, for tests. Pass `std::nullopt` to go back to
    /// auto-detection.
    void setForcedBackend(std::optional<Backend> backend);

    /// How long the probe may take before the store settles on the file backend. Applies to
    /// the probe only; regular operations have no timeout.
    void setProbeTimeout(std::chrono::milliseconds timeout);
    std::chrono::milliseconds probeTimeout() const;

    /// Test hook, see `ProbeRunner`. An empty runner restores the real keychain probe.
    void setProbeRunner(ProbeRunner runner);

    /// How a keychain operation ended.
    enum class KeychainStatus {
        Ok,
        NotFound,
        Failed,
    };
    using KeychainAnswer =
        std::function<void(KeychainStatus status, const QString &value, const QString &error)>;
    /// Test hook: stands in for libsecret, so the cross-backend paths can be tested without
    /// the desktop's keyring. `key` is `<serverId>/<kind>`. Each function must call its
    /// answer exactly once (or never, to play a keychain that hangs).
    struct KeychainHooks
    {
        std::function<void(const QString &key, KeychainAnswer answer)> read;
        std::function<void(const QString &key, const QString &value, KeychainAnswer answer)>
            write;
        std::function<void(const QString &key, KeychainAnswer answer)> remove;
    };
    /// Empty hooks restore the real keychain.
    void setKeychainHooks(KeychainHooks hooks);

Q_SIGNALS:
    /// Fired when auto-detection settles (never for a forced backend), and once more if a
    /// keychain that missed the probe timeout answers after all: File then becomes Keychain.
    void backendResolved(omaseed::core::SecretStore::Backend backend);

private:
    /// Called by an operation exactly once, when it is done and the next may start.
    using Done = std::function<void()>;
    using Operation = std::function<void(Done done)>;

    QString m_service;
    std::optional<Backend> m_detectedBackend;
    std::optional<Backend> m_forcedBackend;
    std::chrono::milliseconds m_probeTimeout{3000};
    ProbeRunner m_probeRunner;
    bool m_probeQueued = false;

    QList<Operation> m_queue;
    bool m_busy = false;
    bool m_startScheduled = false;

    /// The backend an operation runs on. Only valid inside a queued operation (the probe,
    /// if needed, has finished by then) or when a backend is forced.
    Backend resolvedBackend() const;
    /// Queues the probe (at the front) once, if no backend is known or forced.
    void ensureBackendKnown();
    void runProbe(Done done);
    void settleBackend(Backend backend);

    static QString keyFor(Kind kind);
    static QString fieldFor(Kind kind);
    static QString keychainKey(const QString &serverId, Kind kind);

    // The keychain, as three callbacks (libsecret, or the test hooks).
    void keychainRead(const QString &key, KeychainAnswer answer);
    void keychainWrite(const QString &key, const QString &value, KeychainAnswer answer);
    void keychainRemove(const QString &key, KeychainAnswer answer);
    /// Whether a read may also look in the backend that is not in use (INT-7): a session
    /// that settled on the other backend (a locked keyring at login, say) must still find
    /// what an earlier session stored. Always on in the app; a forced backend (tests) turns
    /// it off unless keychain hooks stand in for the real keyring.
    bool crossBackendFallback() const;

    void enqueue(Operation operation);
    void scheduleNext();
    void startNext();

    // The file backend, synchronous. A status of NotFound covers a missing file or field.
    struct FileAnswer
    {
        KeychainStatus status = KeychainStatus::NotFound;
        QString value;
        QString error;
    };
    static FileAnswer fileRead(const QString &serverId, Kind kind);
    static FileAnswer fileWrite(const QString &serverId, Kind kind, const QString &value);
    static FileAnswer fileRemove(const QString &serverId, Kind kind);

    void runRead(const QPointer<Job> &job, const QString &serverId, Kind kind, Done done);

    static QString secretFilePath(const QString &serverId);

    KeychainHooks m_keychainHooks;
};

} // namespace omaseed::core
