#include "core/SecretStore.hpp"

#include "core/Paths.hpp"

#include <QDir>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLoggingCategory>
#include <QPointer>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QTimer>

#include <qt6keychain/keychain.h>

#include <memory>

namespace omaseed::core {

namespace {

Q_LOGGING_CATEGORY(lcSecrets, "omaseed.secrets")

constexpr int kSecretFileVersion = 1;
constexpr auto kProbeKey = "__omaseed_probe__";

QFileDevice::Permissions ownerReadWrite()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

QFileDevice::Permissions ownerReadWriteExec()
{
    return ownerReadWrite() | QFileDevice::ExeOwner;
}

QString backendLabel(SecretStore::Backend backend)
{
    return backend == SecretStore::Backend::Keychain ? QStringLiteral("libsecret")
                                                     : QStringLiteral("file");
}

} // namespace

QString SecretStore::defaultServiceName()
{
    // OMASEED_SECRET_SERVICE lets a test point the whole process — including the one
    // `SecretStore` App builds for itself — at a keychain service of its own, so a test
    // never writes an entry beside the user's real ones. Unset in normal runs (T4.5).
    const QString override =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("OMASEED_SECRET_SERVICE"));
    return override.isEmpty() ? QStringLiteral("io.github.diogochaves.omaseed") : override;
}

SecretStore::SecretStore(QString service, QObject *parent)
    : QObject(parent)
    , m_service(std::move(service))
{}

QString SecretStore::keyFor(Kind kind)
{
    // The serverId prefix is added by the caller; this is just the `<kind>` suffix.
    return kind == Kind::ApiKey ? QStringLiteral("apiKey") : QStringLiteral("password");
}

QString SecretStore::fieldFor(Kind kind)
{
    return keyFor(kind);
}

// --- backend detection -----------------------------------------------------------------------

SecretStore::Backend SecretStore::resolvedBackend() const
{
    if (m_forcedBackend)
        return *m_forcedBackend;
    // An operation only runs once the probe queued ahead of it has settled, so this holds.
    Q_ASSERT(m_detectedBackend);
    return m_detectedBackend.value_or(Backend::File);
}

void SecretStore::ensureBackendKnown()
{
    if (m_forcedBackend || m_detectedBackend || m_probeQueued)
        return;
    m_probeQueued = true;
    // At the front: the probe depends on nothing, and whatever is queued depends on it.
    m_queue.prepend([this](Done done) {
        runProbe([this, done]() {
            m_probeQueued = false;
            done();
        });
    });
    scheduleNext();
}

void SecretStore::settleBackend(Backend backend)
{
    if (m_detectedBackend)
        return;
    m_detectedBackend = backend;
    qCInfo(lcSecrets).noquote() << QStringLiteral("secret backend: %1").arg(backendLabel(backend));
    Q_EMIT backendResolved(backend);
}

void SecretStore::runProbe(Done done)
{
    // A forced backend set after the probe was queued, or an earlier probe, wins.
    if (m_forcedBackend || m_detectedBackend) {
        done();
        return;
    }
    if (!m_probeRunner && !QKeychain::isAvailable()) {
        settleBackend(Backend::File);
        done();
        return;
    }

    // The probe has three ways to end — the keychain answers, the test runner answers, or
    // the timer fires first — and whichever comes first must be the only one to move the
    // queue on. `settled` is shared by all three so the losers turn into no-ops.
    struct Probe
    {
        bool settled = false;
        Done done;
    };
    auto probe = std::make_shared<Probe>();
    probe->done = std::move(done);

    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    timer->setInterval(m_probeTimeout);

    const auto finish = [this, probe, timer](std::optional<Backend> backend) {
        if (probe->settled)
            return;
        probe->settled = true;
        timer->stop();
        timer->deleteLater();
        if (backend)
            settleBackend(*backend);
        probe->done();
    };

    connect(timer, &QTimer::timeout, this, [this, finish]() {
        if (!m_detectedBackend) {
            qCWarning(lcSecrets).noquote()
                << QStringLiteral(
                       "keychain did not answer the probe within %1 ms; storing secrets in "
                       "0600 files instead")
                       .arg(m_probeTimeout.count());
            finish(Backend::File);
        } else {
            // The write already decided Keychain; only the cleanup delete is late. The probe
            // entry is harmless, so let the queue move on rather than wait on it.
            qCInfo(lcSecrets) << "keychain probe cleanup timed out; leaving the probe entry";
            finish(std::nullopt);
        }
    });
    timer->start();

    // The timeout settled on File, but the keychain answered after all (the user unlocked it
    // at the prompt): use it from now on. Secrets already written to files in between are
    // still found, and moved over, by the cross-backend read.
    const auto answeredLate = [this, probe](bool keychainOk) {
        if (!probe->settled || !keychainOk || m_detectedBackend != Backend::File)
            return false;
        m_detectedBackend = Backend::Keychain;
        qCInfo(lcSecrets) << "the keychain answered after the probe timeout; secrets go to the "
                             "keychain from now on";
        Q_EMIT backendResolved(Backend::Keychain);
        return true;
    };

    if (m_probeRunner) {
        m_probeRunner([finish, answeredLate](bool keychainOk) {
            if (!answeredLate(keychainOk))
                finish(keychainOk ? Backend::Keychain : Backend::File);
        });
        return;
    }

    // Real probe: write, decide on the write's result, then delete before the next operation
    // may start (so the delete can never interleave with a caller's job on the same service).
    // The jobs are deliberately NOT parented to the store: libsecret's GLib callback keeps a
    // raw pointer to a running job, so deleting one mid-flight (a store torn down while the
    // probe is out, which every QML test file does) crashes inside Job::finished. They are
    // left to qtkeychain's own auto-delete once finished; the slots have `this` as their
    // context, so a dead store simply stops listening. A write that answers after the timeout
    // upgrades File to Keychain (answeredLate) instead of being ignored.
    auto *write = new QKeychain::WritePasswordJob(m_service);
    write->setKey(QLatin1String(kProbeKey));
    write->setTextData(QStringLiteral("probe"));
    connect(
        write, &QKeychain::Job::finished, this, [this, finish, answeredLate](QKeychain::Job *job) {
            if (answeredLate(job->error() == QKeychain::NoError)) {
                auto *remove = new QKeychain::DeletePasswordJob(m_service);
                remove->setKey(QLatin1String(kProbeKey));
                remove->start(); // best effort, off the queue: the entry is harmless
                return;
            }
            if (job->error() != QKeychain::NoError) {
                qCInfo(lcSecrets).noquote()
                    << QStringLiteral("keychain probe write failed (%1); storing secrets in 0600 "
                                      "files instead")
                           .arg(job->errorString());
                finish(Backend::File);
                return;
            }
            settleBackend(Backend::Keychain);
            auto *remove = new QKeychain::DeletePasswordJob(m_service);
            remove->setKey(QLatin1String(kProbeKey));
            connect(remove, &QKeychain::Job::finished, this, [finish](QKeychain::Job *) {
                finish(std::nullopt); // best effort: the entry is harmless if this failed
            });
            remove->start();
        });
    write->start();
}

void SecretStore::setForcedBackend(std::optional<Backend> backend)
{
    m_forcedBackend = backend;
}

void SecretStore::setProbeTimeout(std::chrono::milliseconds timeout)
{
    m_probeTimeout = timeout;
}

std::chrono::milliseconds SecretStore::probeTimeout() const
{
    return m_probeTimeout;
}

void SecretStore::setProbeRunner(ProbeRunner runner)
{
    m_probeRunner = std::move(runner);
}

std::optional<SecretStore::Backend> SecretStore::backend()
{
    if (m_forcedBackend)
        return m_forcedBackend;
    ensureBackendKnown();
    return m_detectedBackend;
}

QString SecretStore::backendName()
{
    const std::optional<Backend> resolved = backend();
    return resolved ? backendLabel(*resolved) : QStringLiteral("detecting");
}

QString SecretStore::secretFilePath(const QString &serverId)
{
    return paths::secretsDir() + QStringLiteral("/") + serverId + QStringLiteral(".json");
}

// --- keychain backend --------------------------------------------------------------------
// Every qtkeychain job below is unparented and auto-deletes on finish, for the reason given
// in runProbe(): a running job must outlive the store rather than be deleted under
// libsecret's callback.

QString SecretStore::keychainKey(const QString &serverId, Kind kind)
{
    return serverId + QStringLiteral("/") + keyFor(kind);
}

void SecretStore::setKeychainHooks(KeychainHooks hooks)
{
    m_keychainHooks = std::move(hooks);
}

bool SecretStore::crossBackendFallback() const
{
    if (m_keychainHooks.read)
        return true;
    return !m_forcedBackend && QKeychain::isAvailable();
}

void SecretStore::keychainRead(const QString &key, KeychainAnswer answer)
{
    if (m_keychainHooks.read) {
        m_keychainHooks.read(key, std::move(answer));
        return;
    }
    auto *read = new QKeychain::ReadPasswordJob(m_service);
    read->setKey(key);
    connect(read, &QKeychain::Job::finished, this, [answer, read](QKeychain::Job *) {
        const QKeychain::Error error = read->error();
        if (error == QKeychain::NoError)
            answer(KeychainStatus::Ok, read->textData(), QString());
        else if (error == QKeychain::EntryNotFound)
            answer(KeychainStatus::NotFound, QString(), QStringLiteral("not found"));
        else
            answer(KeychainStatus::Failed, QString(), read->errorString());
    });
    read->start();
}

void SecretStore::keychainWrite(const QString &key, const QString &value, KeychainAnswer answer)
{
    if (m_keychainHooks.write) {
        m_keychainHooks.write(key, value, std::move(answer));
        return;
    }
    auto *write = new QKeychain::WritePasswordJob(m_service);
    write->setKey(key);
    write->setTextData(value);
    connect(write, &QKeychain::Job::finished, this, [answer](QKeychain::Job *finished) {
        if (finished->error() == QKeychain::NoError)
            answer(KeychainStatus::Ok, QString(), QString());
        else
            answer(KeychainStatus::Failed, QString(), finished->errorString());
    });
    write->start();
}

void SecretStore::keychainRemove(const QString &key, KeychainAnswer answer)
{
    if (m_keychainHooks.remove) {
        m_keychainHooks.remove(key, std::move(answer));
        return;
    }
    auto *remove = new QKeychain::DeletePasswordJob(m_service);
    remove->setKey(key);
    connect(remove, &QKeychain::Job::finished, this, [answer](QKeychain::Job *finished) {
        const QKeychain::Error error = finished->error();
        if (error == QKeychain::NoError)
            answer(KeychainStatus::Ok, QString(), QString());
        else if (error == QKeychain::EntryNotFound)
            answer(KeychainStatus::NotFound, QString(), QString());
        else
            answer(KeychainStatus::Failed, QString(), finished->errorString());
    });
    remove->start();
}

// --- file backend --------------------------------------------------------------------------

namespace {

/// Writes `root` to `path` through QSaveFile: a crash or a full disk leaves the old file
/// whole instead of an empty one, and a failed write is reported, not swallowed.
bool writeSecretFile(const QString &path, const QJsonObject &root, QString *error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = file.errorString();
        return false;
    }
    // The temporary file is created with the old file's permissions, or the umask's for a
    // new one; restrict it before the secret is in it. The directory is 0700 either way.
    file.setPermissions(ownerReadWrite());
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString();
        return false;
    }
    QFile::setPermissions(path, ownerReadWrite());
    return true;
}

} // namespace

SecretStore::FileAnswer SecretStore::fileRead(const QString &serverId, Kind kind)
{
    QFile file(secretFilePath(serverId));
    if (!file.open(QIODevice::ReadOnly))
        return {KeychainStatus::NotFound, QString(), QStringLiteral("not found")};

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject())
        return {KeychainStatus::Failed, QString(), QStringLiteral("corrupt secret file")};

    const QJsonValue value = doc.object().value(fieldFor(kind));
    if (!value.isString())
        return {KeychainStatus::NotFound, QString(), QStringLiteral("not found")};
    return {KeychainStatus::Ok, value.toString(), QString()};
}

SecretStore::FileAnswer
SecretStore::fileWrite(const QString &serverId, Kind kind, const QString &value)
{
    const QString dir = paths::secretsDir();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, ownerReadWriteExec());

    const QString path = secretFilePath(serverId);
    QJsonObject root;
    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll(), &parseError);
        existing.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            root = doc.object();
        } else if (existing.size() > 0) {
            // Rewriting would silently drop the other kind of secret held in it.
            return {KeychainStatus::Failed, QString(), QStringLiteral("corrupt secret file")};
        }
    }
    root[QStringLiteral("version")] = kSecretFileVersion;
    root[fieldFor(kind)] = value;

    QString error;
    if (!writeSecretFile(path, root, &error))
        return {KeychainStatus::Failed, QString(), error};
    return {KeychainStatus::Ok, QString(), QString()};
}

SecretStore::FileAnswer SecretStore::fileRemove(const QString &serverId, Kind kind)
{
    const QString path = secretFilePath(serverId);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {KeychainStatus::NotFound, QString(), QString()}; // nothing to remove

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    QJsonObject root = (parseError.error == QJsonParseError::NoError && doc.isObject())
                           ? doc.object()
                           : QJsonObject();
    if (!root.contains(fieldFor(kind)) && !root.isEmpty())
        return {KeychainStatus::NotFound, QString(), QString()};
    root.remove(fieldFor(kind));

    const bool hasApiKey = root.contains(QStringLiteral("apiKey"));
    const bool hasPassword = root.contains(QStringLiteral("password"));
    if (!hasApiKey && !hasPassword) {
        QFile::remove(path);
        return {KeychainStatus::Ok, QString(), QString()};
    }

    root[QStringLiteral("version")] = kSecretFileVersion;
    QString error;
    if (!writeSecretFile(path, root, &error))
        return {KeychainStatus::Failed, QString(), error};
    return {KeychainStatus::Ok, QString(), QString()};
}

// --- public API, dispatch to the resolved backend -------------------------------------------

// One queue for every operation, the probe included, strictly one at a time: an operation
// gets a `done` callback and the next one starts only after it has been called. Nothing here
// waits — the keychain jobs report back through their `finished` signal — so the queue is
// what guarantees that a read queued behind a write sees the entry that write stored (T4.5
// found the opposite: "add a server, then use it" read back nothing).
void SecretStore::enqueue(Operation operation)
{
    m_queue.append(std::move(operation));
    scheduleNext();
}

void SecretStore::scheduleNext()
{
    // Deferred to the next tick so the caller can connect to the job it was just handed,
    // and so a synchronous (file) operation never starts the next one recursively.
    if (m_busy || m_startScheduled)
        return;
    m_startScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_startScheduled = false;
        startNext();
    });
}

void SecretStore::startNext()
{
    if (m_busy || m_queue.isEmpty())
        return;
    ensureBackendKnown(); // puts the probe ahead of the operation that is about to run
    m_busy = true;
    const Operation operation = m_queue.takeFirst();
    operation([this]() {
        m_busy = false;
        scheduleNext();
    });
}

namespace {

void answerJob(const QPointer<SecretStoreJob> &job,
               bool ok,
               const QString &value,
               const QString &error)
{
    if (job)
        Q_EMIT job->finished(ok, value, error);
}

} // namespace

SecretStore::Job *
SecretStore::setSecret(const QString &serverId, Kind kind, const QString &value, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind, value](Done done) {
        if (!alive) {
            done();
            return;
        }
        if (resolvedBackend() == Backend::File) {
            const FileAnswer written = fileWrite(serverId, kind, value);
            answerJob(alive, written.status == KeychainStatus::Ok, QString(), written.error);
            done();
            return;
        }
        keychainWrite(keychainKey(serverId, kind),
                      value,
                      [this, alive, serverId, kind, done](
                          KeychainStatus status, const QString &, const QString &error) {
                          // A copy left in a file by a session that could not reach the
                          // keychain is stale now, and plaintext: drop it.
                          if (status == KeychainStatus::Ok && crossBackendFallback())
                              fileRemove(serverId, kind);
                          answerJob(alive, status == KeychainStatus::Ok, QString(), error);
                          done();
                      });
    });
    return job;
}

void SecretStore::runRead(const QPointer<Job> &alive, const QString &serverId, Kind kind, Done done)
{
    const QString key = keychainKey(serverId, kind);

    if (resolvedBackend() == Backend::Keychain) {
        keychainRead(key,
                     [this, alive, serverId, kind, key, done](
                         KeychainStatus status, const QString &value, const QString &error) {
                         if (status == KeychainStatus::Ok || !crossBackendFallback()) {
                             answerJob(alive, status == KeychainStatus::Ok, value, error);
                             done();
                             return;
                         }
                         const FileAnswer fromFile = fileRead(serverId, kind);
                         if (fromFile.status != KeychainStatus::Ok) {
                             answerJob(alive, false, QString(), error);
                             done();
                             return;
                         }
                         // Stored while this machine's keychain was out of reach: answer it,
                         // then move it where it belongs.
                         answerJob(alive, true, fromFile.value, QString());
                         qCInfo(lcSecrets).noquote()
                             << QStringLiteral("moving the %1 secret from its file into the "
                                               "keychain")
                                    .arg(key);
                         keychainWrite(key,
                                       fromFile.value,
                                       [serverId, kind, done](KeychainStatus written,
                                                              const QString &,
                                                              const QString &) {
                                           if (written == KeychainStatus::Ok)
                                               fileRemove(serverId, kind);
                                           done();
                                       });
                     });
        return;
    }

    const FileAnswer fromFile = fileRead(serverId, kind);
    if (fromFile.status != KeychainStatus::NotFound || !crossBackendFallback()) {
        answerJob(alive, fromFile.status == KeychainStatus::Ok, fromFile.value, fromFile.error);
        done();
        return;
    }

    // Not in the file: maybe an earlier session stored it in the keychain. Ask, but never
    // longer than the probe was allowed to take, since the keychain already failed to answer
    // once this session.
    struct Lookup
    {
        bool settled = false;
    };
    auto lookup = std::make_shared<Lookup>();
    auto *timer = new QTimer(this);
    timer->setSingleShot(true);
    const auto finish = [lookup, timer, alive, done](bool ok, const QString &value) {
        if (lookup->settled)
            return;
        lookup->settled = true;
        timer->stop();
        timer->deleteLater();
        answerJob(alive, ok, value, ok ? QString() : QStringLiteral("not found"));
        done();
    };
    connect(timer, &QTimer::timeout, this, [finish]() { finish(false, QString()); });
    timer->start(m_probeTimeout);
    keychainRead(key, [finish](KeychainStatus status, const QString &value, const QString &) {
        finish(status == KeychainStatus::Ok, value);
    });
}

SecretStore::Job *SecretStore::secret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind](Done done) {
        if (!alive) {
            done();
            return;
        }
        runRead(alive, serverId, kind, std::move(done));
    });
    return job;
}

SecretStore::Job *SecretStore::removeSecret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind](Done done) {
        if (!alive) {
            done();
            return;
        }
        const QString key = keychainKey(serverId, kind);
        if (resolvedBackend() == Backend::File) {
            const FileAnswer removed = fileRemove(serverId, kind);
            // A copy an earlier session stored in the keychain would come back through the
            // cross-backend read: clear it too, best effort and off the queue (the keychain
            // may not answer at all this session).
            if (crossBackendFallback())
                keychainRemove(key, [](KeychainStatus, const QString &, const QString &) {});
            answerJob(alive, removed.status != KeychainStatus::Failed, QString(), removed.error);
            done();
            return;
        }
        keychainRemove(key,
                       [this, alive, serverId, kind, done](
                           KeychainStatus status, const QString &, const QString &error) {
                           if (crossBackendFallback())
                               fileRemove(serverId, kind);
                           // Removing an absent entry is not a failure: the end state the
                           // caller wants already holds.
                           answerJob(alive, status != KeychainStatus::Failed, QString(), error);
                           done();
                       });
    });
    return job;
}

} // namespace omaseed::core
