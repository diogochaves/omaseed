#include "core/SecretStore.hpp"

#include "core/Paths.hpp"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileDevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
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
    return QStringLiteral("io.github.diogochaves.omaseed");
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

SecretStore::Job *
SecretStore::setSecret(const QString &serverId, Kind kind, const QString &value, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QTimer::singleShot(0, job, [this, job, serverId, kind, value]() {
        if (resolveBackend() == Backend::Keychain)
            runKeychainWrite(job, serverId, kind, value);
        else
            runFileWrite(job, serverId, kind, value);
    });
    return job;
}

SecretStore::Job *SecretStore::secret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QTimer::singleShot(0, job, [this, job, serverId, kind]() {
        if (resolveBackend() == Backend::Keychain)
            runKeychainRead(job, serverId, kind);
        else
            runFileRead(job, serverId, kind);
    });
    return job;
}

SecretStore::Job *SecretStore::removeSecret(const QString &serverId, Kind kind, QObject *parent)
{
    auto *job = new Job(parent ? parent : this);
    QTimer::singleShot(0, job, [this, job, serverId, kind]() {
        if (resolveBackend() == Backend::Keychain)
            runKeychainRemove(job, serverId, kind);
        else
            runFileRemove(job, serverId, kind);
    });
    return job;
}

} // namespace omaseed::core
