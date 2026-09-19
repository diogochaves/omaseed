#include "core/SecretStore.hpp"

#include "core/Paths.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>
#include <QProcessEnvironment>
#include <QTimer>

#include <qt6keychain/keychain.h>

namespace omaseed::core {

namespace {

constexpr int kSecretFileVersion = 1;

QFileDevice::Permissions ownerReadWrite()
{
    return QFileDevice::ReadOwner | QFileDevice::WriteOwner;
}

QFileDevice::Permissions ownerReadWriteExec()
{
    return ownerReadWrite() | QFileDevice::ExeOwner;
}

/// Runs a QKeychain job to completion on a nested event loop. Used only for the one-shot
/// backend probe and inside the (already asynchronous, deferred-to-the-next-tick) job
/// bodies below, so it never blocks a caller that has not yet had a chance to connect to
/// `finished()`.
QKeychain::Error runSync(QKeychain::Job &job)
{
    QEventLoop loop;
    QObject::connect(&job, &QKeychain::Job::finished, &loop, &QEventLoop::quit);
    job.start();
    loop.exec();
    return job.error();
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

SecretStore::Backend SecretStore::probeKeychain() const
{
    if (!QKeychain::isAvailable())
        return Backend::File;

    QKeychain::WritePasswordJob write(m_service);
    write.setAutoDelete(false);
    write.setKey(QStringLiteral("__omaseed_probe__"));
    write.setTextData(QStringLiteral("probe"));
    const QKeychain::Error writeError = runSync(write);

    if (writeError != QKeychain::NoError)
        return Backend::File;

    QKeychain::DeletePasswordJob remove(m_service);
    remove.setAutoDelete(false);
    remove.setKey(QStringLiteral("__omaseed_probe__"));
    runSync(remove); // best effort; the probe entry is harmless if this fails

    return Backend::Keychain;
}

SecretStore::Backend SecretStore::resolveBackend()
{
    if (m_forcedBackend)
        return *m_forcedBackend;
    if (!m_detectedBackend)
        m_detectedBackend = probeKeychain();
    return *m_detectedBackend;
}

void SecretStore::setForcedBackend(std::optional<Backend> backend)
{
    m_forcedBackend = backend;
}

SecretStore::Backend SecretStore::backend()
{
    return resolveBackend();
}

QString SecretStore::backendName()
{
    return resolveBackend() == Backend::Keychain ? QStringLiteral("libsecret")
                                                 : QStringLiteral("file");
}

QString SecretStore::secretFilePath(const QString &serverId)
{
    return paths::secretsDir() + QStringLiteral("/") + serverId + QStringLiteral(".json");
}

// --- keychain backend --------------------------------------------------------------------

void SecretStore::runKeychainWrite(Job *job,
                                   const QString &serverId,
                                   Kind kind,
                                   const QString &value) const
{
    QKeychain::WritePasswordJob write(m_service);
    write.setAutoDelete(false);
    write.setKey(serverId + QStringLiteral("/") + keyFor(kind));
    write.setTextData(value);
    const QKeychain::Error error = runSync(write);
    if (error == QKeychain::NoError)
        Q_EMIT job->finished(true, QString(), QString());
    else
        Q_EMIT job->finished(false, QString(), write.errorString());
}

void SecretStore::runKeychainRead(Job *job, const QString &serverId, Kind kind) const
{
    QKeychain::ReadPasswordJob read(m_service);
    read.setAutoDelete(false);
    read.setKey(serverId + QStringLiteral("/") + keyFor(kind));
    const QKeychain::Error error = runSync(read);
    if (error == QKeychain::NoError)
        Q_EMIT job->finished(true, read.textData(), QString());
    else if (error == QKeychain::EntryNotFound)
        Q_EMIT job->finished(false, QString(), QStringLiteral("not found"));
    else
        Q_EMIT job->finished(false, QString(), read.errorString());
}

void SecretStore::runKeychainRemove(Job *job, const QString &serverId, Kind kind) const
{
    QKeychain::DeletePasswordJob remove(m_service);
    remove.setAutoDelete(false);
    remove.setKey(serverId + QStringLiteral("/") + keyFor(kind));
    const QKeychain::Error error = runSync(remove);
    // Removing an absent entry is not a failure: the end state the caller wants already
    // holds.
    if (error == QKeychain::NoError || error == QKeychain::EntryNotFound)
        Q_EMIT job->finished(true, QString(), QString());
    else
        Q_EMIT job->finished(false, QString(), remove.errorString());
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

// The keychain backend runs each qtkeychain job in a nested event loop, which delivers the
// zero-timers of everything queued behind it — so a plain `singleShot` per operation let a
// read start *inside* the write it was queued after, and miss the entry that write had not
// finished storing yet (found in T4.5: "add a server, then use it" read back nothing). Every
// operation therefore goes through one queue, drained one at a time, re-entrancy guarded.
void SecretStore::enqueue(std::function<void()> operation)
{
    m_queue.append(std::move(operation));
    if (m_draining)
        return;
    QTimer::singleShot(0, this, [this]() { drain(); });
}

void SecretStore::drain()
{
    if (m_draining)
        return;
    m_draining = true;
    while (!m_queue.isEmpty()) {
        const std::function<void()> operation = m_queue.takeFirst();
        operation();
    }
    m_draining = false;
}

SecretStore::Job *
SecretStore::setSecret(const QString &serverId, Kind kind, const QString &value, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind, value]() {
        if (!alive)
            return;
        if (resolveBackend() == Backend::Keychain)
            runKeychainWrite(alive, serverId, kind, value);
        else
            runFileWrite(alive, serverId, kind, value);
    });
    return job;
}

SecretStore::Job *SecretStore::secret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind]() {
        if (!alive)
            return;
        if (resolveBackend() == Backend::Keychain)
            runKeychainRead(alive, serverId, kind);
        else
            runFileRead(alive, serverId, kind);
    });
    return job;
}

SecretStore::Job *SecretStore::removeSecret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QPointer<Job> alive(job);
    enqueue([this, alive, serverId, kind]() {
        if (!alive)
            return;
        if (resolveBackend() == Backend::Keychain)
            runKeychainRemove(alive, serverId, kind);
        else
            runFileRemove(alive, serverId, kind);
    });
    return job;
}

} // namespace omaseed::core
