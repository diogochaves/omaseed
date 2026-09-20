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

    if (m_probeRunner) {
        m_probeRunner(
            [finish](bool keychainOk) { finish(keychainOk ? Backend::Keychain : Backend::File); });
        return;
    }

    // Real probe: write, decide on the write's result, then delete before the next operation
    // may start (so the delete can never interleave with a caller's job on the same service).
    // The jobs are deliberately NOT parented to the store: libsecret's GLib callback keeps a
    // raw pointer to a running job, so deleting one mid-flight (a store torn down while the
    // probe is out, which every QML test file does) crashes inside Job::finished. They are
    // left to qtkeychain's own auto-delete once finished; the slots have `this` as their
    // context, so a dead store simply stops listening. A job abandoned by the timeout is
    // disconnected and its late answer ignored.
    auto *write = new QKeychain::WritePasswordJob(m_service);
    write->setKey(QLatin1String(kProbeKey));
    write->setTextData(QStringLiteral("probe"));
    connect(write, &QKeychain::Job::finished, this, [this, finish](QKeychain::Job *job) {
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
    connect(timer, &QTimer::timeout, write, [this, write]() {
        QObject::disconnect(write, nullptr, this, nullptr);
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

void SecretStore::runKeychainWrite(
    Job *job, const QString &serverId, Kind kind, const QString &value, Done done)
{
    auto *write = new QKeychain::WritePasswordJob(m_service);
    write->setKey(serverId + QStringLiteral("/") + keyFor(kind));
    write->setTextData(value);
    QPointer<Job> alive(job);
    connect(write, &QKeychain::Job::finished, this, [alive, done](QKeychain::Job *finished) {
        if (alive) {
            if (finished->error() == QKeychain::NoError)
                Q_EMIT alive->finished(true, QString(), QString());
            else
                Q_EMIT alive->finished(false, QString(), finished->errorString());
        }
        done();
    });
    write->start();
}

void SecretStore::runKeychainRead(Job *job, const QString &serverId, Kind kind, Done done)
{
    auto *read = new QKeychain::ReadPasswordJob(m_service);
    read->setKey(serverId + QStringLiteral("/") + keyFor(kind));
    QPointer<Job> alive(job);
    connect(read, &QKeychain::Job::finished, this, [alive, done, read](QKeychain::Job *) {
        if (alive) {
            const QKeychain::Error error = read->error();
            if (error == QKeychain::NoError)
                Q_EMIT alive->finished(true, read->textData(), QString());
            else if (error == QKeychain::EntryNotFound)
                Q_EMIT alive->finished(false, QString(), QStringLiteral("not found"));
            else
                Q_EMIT alive->finished(false, QString(), read->errorString());
        }
        done();
    });
    read->start();
}

void SecretStore::runKeychainRemove(Job *job, const QString &serverId, Kind kind, Done done)
{
    auto *remove = new QKeychain::DeletePasswordJob(m_service);
    remove->setKey(serverId + QStringLiteral("/") + keyFor(kind));
    QPointer<Job> alive(job);
    connect(remove, &QKeychain::Job::finished, this, [alive, done](QKeychain::Job *finished) {
        if (alive) {
            const QKeychain::Error error = finished->error();
            // Removing an absent entry is not a failure: the end state the caller wants
            // already holds.
            if (error == QKeychain::NoError || error == QKeychain::EntryNotFound)
                Q_EMIT alive->finished(true, QString(), QString());
            else
                Q_EMIT alive->finished(false, QString(), finished->errorString());
        }
        done();
    });
    remove->start();
}

// --- file backend --------------------------------------------------------------------------

void SecretStore::runFileWrite(Job *job,
                               const QString &serverId,
                               Kind kind,
                               const QString &value) const
{
    const QString dir = paths::secretsDir();
    QDir().mkpath(dir);
    QFile::setPermissions(dir, ownerReadWriteExec());

    const QString path = secretFilePath(serverId);
    QJsonObject root;
    root[QStringLiteral("version")] = kSecretFileVersion;

    QFile existing(path);
    if (existing.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(existing.readAll(), &parseError);
        existing.close();
        if (parseError.error == QJsonParseError::NoError && doc.isObject())
            root = doc.object();
    }

    root[fieldFor(kind)] = value;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT job->finished(false, QString(), file.errorString());
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    file.close();
    QFile::setPermissions(path, ownerReadWrite());

    Q_EMIT job->finished(true, QString(), QString());
}

void SecretStore::runFileRead(Job *job, const QString &serverId, Kind kind) const
{
    QFile file(secretFilePath(serverId));
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT job->finished(false, QString(), QStringLiteral("not found"));
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        Q_EMIT job->finished(false, QString(), QStringLiteral("corrupt secret file"));
        return;
    }

    const QJsonValue value = doc.object().value(fieldFor(kind));
    if (!value.isString()) {
        Q_EMIT job->finished(false, QString(), QStringLiteral("not found"));
        return;
    }

    Q_EMIT job->finished(true, value.toString(), QString());
}

void SecretStore::runFileRemove(Job *job, const QString &serverId, Kind kind) const
{
    const QString path = secretFilePath(serverId);
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        Q_EMIT job->finished(true, QString(), QString()); // nothing to remove
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    file.close();
    QJsonObject root = (parseError.error == QJsonParseError::NoError && doc.isObject())
                           ? doc.object()
                           : QJsonObject();
    root.remove(fieldFor(kind));

    const bool hasApiKey = root.contains(QStringLiteral("apiKey"));
    const bool hasPassword = root.contains(QStringLiteral("password"));
    if (!hasApiKey && !hasPassword) {
        QFile::remove(path);
        Q_EMIT job->finished(true, QString(), QString());
        return;
    }

    root[QStringLiteral("version")] = kSecretFileVersion;
    QFile write(path);
    if (!write.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Q_EMIT job->finished(false, QString(), write.errorString());
        return;
    }
    write.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    write.close();
    QFile::setPermissions(path, ownerReadWrite());

    Q_EMIT job->finished(true, QString(), QString());
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

SecretStore::Job *
SecretStore::setSecret(const QString &serverId, Kind kind, const QString &value, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind, value](Done done) {
        if (!alive) {
            done();
        } else if (resolvedBackend() == Backend::Keychain) {
            runKeychainWrite(alive, serverId, kind, value, std::move(done));
        } else {
            runFileWrite(alive, serverId, kind, value);
            done();
        }
    });
    return job;
}

SecretStore::Job *SecretStore::secret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind](Done done) {
        if (!alive) {
            done();
        } else if (resolvedBackend() == Backend::Keychain) {
            runKeychainRead(alive, serverId, kind, std::move(done));
        } else {
            runFileRead(alive, serverId, kind);
            done();
        }
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
        } else if (resolvedBackend() == Backend::Keychain) {
            runKeychainRemove(alive, serverId, kind, std::move(done));
        } else {
            runFileRemove(alive, serverId, kind);
            done();
        }
    });
    return job;
}

} // namespace omaseed::core
