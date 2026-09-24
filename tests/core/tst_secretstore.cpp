#include "core/SecretStore.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <qt6keychain/keychain.h>

using omaseed::core::SecretStore;

namespace {

/// Runs `job` to completion on a nested event loop and captures its result. `job` deletes
/// itself once `finished()` fires (see `SecretStore::Job`), so nothing here outlives it.
struct JobResult
{
    bool ok = false;
    QString value;
    QString error;
};

JobResult run(SecretStore::Job *job)
{
    JobResult result;
    QEventLoop loop;
    QObject::connect(job,
                     &SecretStore::Job::finished,
                     &loop,
                     [&](bool ok, const QString &value, const QString &err) {
                         result.ok = ok;
                         result.value = value;
                         result.error = err;
                         loop.quit();
                     });
    QTimer::singleShot(5000, &loop, &QEventLoop::quit); // safety net against a hung job
    loop.exec();
    return result;
}

} // namespace

class TestSecretStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void fileBackendRoundTrips();
    void fileBackendPermissions();
    void fileBackendIsolatesServers();
    void fileBackendMissingSecretIsNotFound();
    void forcedFileBackendSkipsProbe();

    void keychainBackendRoundTrips();

    void probeResolvesAsynchronously();
    void probeTimeoutFallsBackToFile();
    void probeRunnerDecidesTheBackend();

    // INT-7: a secret stored under the other backend is still found.
    void keychainSessionFindsAndMovesAFileSecret();
    void fileSessionFindsAKeychainSecret();
    void fileSessionKeychainLookupIsBounded();
    void removeClearsBothBackends();
    void lateKeychainAnswerSwitchesToKeychain();
    // INT-8
    void aCorruptFileIsNotRewritten();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousXdgConfigHome;
    bool m_hadPreviousXdgConfigHome = false;
};

void TestSecretStore::init()
{
    QVERIFY(m_dir.isValid());
    m_hadPreviousXdgConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
    if (m_hadPreviousXdgConfigHome)
        m_previousXdgConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestSecretStore::cleanup()
{
    if (m_hadPreviousXdgConfigHome)
        qputenv("XDG_CONFIG_HOME", m_previousXdgConfigHome);
    else
        qunsetenv("XDG_CONFIG_HOME");
}

void TestSecretStore::fileBackendRoundTrips()
{
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);
    QCOMPARE(store.backendName(), QStringLiteral("file"));

    const QString serverId = QStringLiteral("server-1");

    JobResult write =
        run(store.setSecret(serverId, SecretStore::Kind::ApiKey, QStringLiteral("s3cr3t")));
    QVERIFY(write.ok);

    JobResult read = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(read.ok);
    QCOMPARE(read.value, QStringLiteral("s3cr3t"));

    // A password can live alongside the API key for the same server.
    JobResult writePassword =
        run(store.setSecret(serverId, SecretStore::Kind::Password, QStringLiteral("hunter2")));
    QVERIFY(writePassword.ok);
    JobResult readApiKeyAgain = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(readApiKeyAgain.ok);
    QCOMPARE(readApiKeyAgain.value, QStringLiteral("s3cr3t"));

    JobResult remove = run(store.removeSecret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(remove.ok);

    JobResult readAfterRemove = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(!readAfterRemove.ok);

    // The password entry is untouched by removing the API key.
    JobResult readPassword = run(store.secret(serverId, SecretStore::Kind::Password));
    QVERIFY(readPassword.ok);
    QCOMPARE(readPassword.value, QStringLiteral("hunter2"));
}

void TestSecretStore::fileBackendPermissions()
{
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);

    const QString serverId = QStringLiteral("server-perms");
    JobResult write =
        run(store.setSecret(serverId, SecretStore::Kind::ApiKey, QStringLiteral("value")));
    QVERIFY(write.ok);

    const QString secretsDir = m_dir.path() + QStringLiteral("/omaseed/secrets");
    const QString secretFile =
        secretsDir + QStringLiteral("/") + serverId + QStringLiteral(".json");

    QFileInfo dirInfo(secretsDir);
    QVERIFY(dirInfo.exists());
    QCOMPARE(dirInfo.permissions() &
                 (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                  QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther),
             QFileDevice::Permissions());

    QFileInfo fileInfo(secretFile);
    QVERIFY(fileInfo.exists());
    QCOMPARE(fileInfo.permissions() &
                 (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                  QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther),
             QFileDevice::Permissions());
}

void TestSecretStore::fileBackendIsolatesServers()
{
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);

    JobResult writeA = run(store.setSecret(
        QStringLiteral("server-a"), SecretStore::Kind::ApiKey, QStringLiteral("aaa")));
    JobResult writeB = run(store.setSecret(
        QStringLiteral("server-b"), SecretStore::Kind::ApiKey, QStringLiteral("bbb")));
    QVERIFY(writeA.ok);
    QVERIFY(writeB.ok);

    JobResult readA = run(store.secret(QStringLiteral("server-a"), SecretStore::Kind::ApiKey));
    JobResult readB = run(store.secret(QStringLiteral("server-b"), SecretStore::Kind::ApiKey));
    QCOMPARE(readA.value, QStringLiteral("aaa"));
    QCOMPARE(readB.value, QStringLiteral("bbb"));

    JobResult removeA =
        run(store.removeSecret(QStringLiteral("server-a"), SecretStore::Kind::ApiKey));
    QVERIFY(removeA.ok);

    JobResult readAAfterRemove =
        run(store.secret(QStringLiteral("server-a"), SecretStore::Kind::ApiKey));
    QVERIFY(!readAAfterRemove.ok);
    // server-b is unaffected by removing server-a's secret.
    JobResult readBStill = run(store.secret(QStringLiteral("server-b"), SecretStore::Kind::ApiKey));
    QVERIFY(readBStill.ok);
    QCOMPARE(readBStill.value, QStringLiteral("bbb"));
}

void TestSecretStore::fileBackendMissingSecretIsNotFound()
{
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);

    JobResult read = run(store.secret(QStringLiteral("never-existed"), SecretStore::Kind::ApiKey));
    QVERIFY(!read.ok);
    QVERIFY(!read.error.isEmpty());

    // Removing a secret that was never set is not an error (idempotent).
    JobResult remove =
        run(store.removeSecret(QStringLiteral("never-existed"), SecretStore::Kind::ApiKey));
    QVERIFY(remove.ok);
}

void TestSecretStore::forcedFileBackendSkipsProbe()
{
    // With the backend forced to File, the store must never reach for the keychain: this
    // is the "inject the probe result" case the task asks for. We cannot observe the probe
    // directly, but we can confirm the resolved backend matches the forced value and that
    // operations land on disk as the file backend, regardless of what the real keychain on
    // this machine would have reported.
    SecretStore store;
    QSignalSpy resolved(&store, &SecretStore::backendResolved);
    store.setForcedBackend(SecretStore::Backend::File);
    QCOMPARE(store.backend(), std::optional(SecretStore::Backend::File));
    QCOMPARE(store.backendName(), QStringLiteral("file"));

    JobResult write = run(store.setSecret(
        QStringLiteral("server-x"), SecretStore::Kind::ApiKey, QStringLiteral("v")));
    QVERIFY(write.ok);

    const QString secretFile = m_dir.path() + QStringLiteral("/omaseed/secrets/server-x.json");
    QVERIFY(QFileInfo::exists(secretFile));
    // A forced backend is never "detected", so nothing was announced.
    QCOMPARE(resolved.count(), 0);

    // Clearing the forced backend goes back to auto-detection.
    store.setForcedBackend(std::nullopt);
}

void TestSecretStore::keychainBackendRoundTrips()
{
    if (!QKeychain::isAvailable())
        QSKIP("no libsecret/keychain backend available on this machine (QKeychain::isAvailable() "
              "== false)");

    SecretStore store(QStringLiteral("io.github.diogochaves.omaseed-test"));
    store.setForcedBackend(SecretStore::Backend::Keychain);

    const QString serverId = QStringLiteral("keychain-server-1");

    JobResult write = run(
        store.setSecret(serverId, SecretStore::Kind::ApiKey, QStringLiteral("k3ychain-s3cr3t")));
    if (!write.ok)
        QSKIP(qPrintable(QStringLiteral("keychain reported no usable backend at write time: %1")
                             .arg(write.error)));

    JobResult read = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(read.ok);
    QCOMPARE(read.value, QStringLiteral("k3ychain-s3cr3t"));

    // Clean up so this test never leaves an entry behind, even in the -test service.
    JobResult remove = run(store.removeSecret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(remove.ok);

    JobResult readAfterRemove = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(!readAfterRemove.ok);
}

void TestSecretStore::probeResolvesAsynchronously()
{
    // Auto-detection against the real (test-suffixed) service. Whatever it decides, the
    // decision must arrive through the signal without anyone blocking, and a read queued
    // right behind a write must see the written value (the T4.5 regression: the probe and
    // the write are both asynchronous now, and the queue is the only thing keeping order).
    SecretStore store(QStringLiteral("io.github.diogochaves.omaseed-test"));
    QSignalSpy resolved(&store, &SecretStore::backendResolved);

    QCOMPARE(store.backend(), std::nullopt); // queues the probe, does not wait for it
    QCOMPARE(store.backendName(), QStringLiteral("detecting"));

    const QString serverId = QStringLiteral("probe-server-1");
    SecretStore::Job *write =
        store.setSecret(serverId, SecretStore::Kind::ApiKey, QStringLiteral("pr0be-s3cr3t"));
    QSignalSpy writeFinished(write, &SecretStore::Job::finished);
    SecretStore::Job *read = store.secret(serverId, SecretStore::Kind::ApiKey);
    QSignalSpy readFinished(read, &SecretStore::Job::finished);

    // Nothing has run yet: the probe, the write and the read are all still queued.
    QCOMPARE(resolved.count(), 0);
    QCOMPARE(writeFinished.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 5000);
    const auto backend = resolved.at(0).at(0).value<SecretStore::Backend>();
    QVERIFY(backend == SecretStore::Backend::Keychain || backend == SecretStore::Backend::File);
    QCOMPARE(store.backend(), std::optional(backend));
    QCOMPARE(store.backendName(),
             backend == SecretStore::Backend::Keychain ? QStringLiteral("libsecret")
                                                       : QStringLiteral("file"));

    QTRY_COMPARE_WITH_TIMEOUT(readFinished.count(), 1, 5000);
    QCOMPARE(writeFinished.count(), 1);
    QVERIFY2(writeFinished.at(0).at(0).toBool(), qPrintable(writeFinished.at(0).at(2).toString()));
    QVERIFY2(readFinished.at(0).at(0).toBool(), qPrintable(readFinished.at(0).at(2).toString()));
    QCOMPARE(readFinished.at(0).at(1).toString(), QStringLiteral("pr0be-s3cr3t"));

    // Never leave an entry behind, whichever backend took it.
    JobResult remove = run(store.removeSecret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(remove.ok);
    JobResult readAfterRemove = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(!readAfterRemove.ok);

    // The probe runs once per store; asking again is answered from the cache.
    QCOMPARE(resolved.count(), 1);
}

void TestSecretStore::probeTimeoutFallsBackToFile()
{
    // A keychain that never answers (a runner that never calls back) must not stall the
    // store: after the (shortened) timeout it settles on the file backend and the write that
    // was waiting behind the probe lands on disk.
    SecretStore store(QStringLiteral("io.github.diogochaves.omaseed-test"));
    QSignalSpy resolved(&store, &SecretStore::backendResolved);
    store.setProbeTimeout(std::chrono::milliseconds(50));
    int runnerCalls = 0;
    store.setProbeRunner([&runnerCalls](std::function<void(bool)>) { ++runnerCalls; });

    const QString serverId = QStringLiteral("timeout-server");
    SecretStore::Job *write =
        store.setSecret(serverId, SecretStore::Kind::ApiKey, QStringLiteral("late"));
    QSignalSpy writeFinished(write, &SecretStore::Job::finished);
    QCOMPARE(store.backendName(), QStringLiteral("detecting"));

    QTRY_COMPARE_WITH_TIMEOUT(resolved.count(), 1, 2000);
    QCOMPARE(resolved.at(0).at(0).value<SecretStore::Backend>(), SecretStore::Backend::File);
    QCOMPARE(runnerCalls, 1);
    QCOMPARE(store.backendName(), QStringLiteral("file"));

    QTRY_COMPARE_WITH_TIMEOUT(writeFinished.count(), 1, 2000);
    QVERIFY(writeFinished.at(0).at(0).toBool());
    QVERIFY(QFileInfo::exists(m_dir.path() + QStringLiteral("/omaseed/secrets/") + serverId +
                              QStringLiteral(".json")));

    JobResult read = run(store.secret(serverId, SecretStore::Kind::ApiKey));
    QVERIFY(read.ok);
    QCOMPARE(read.value, QStringLiteral("late"));
}

void TestSecretStore::probeRunnerDecidesTheBackend()
{
    // A runner that answers in time decides the backend, and a late timer changes nothing.
    SecretStore store(QStringLiteral("io.github.diogochaves.omaseed-test"));
    QSignalSpy resolved(&store, &SecretStore::backendResolved);
    store.setProbeTimeout(std::chrono::milliseconds(20));
    std::function<void(bool)> answer;
    store.setProbeRunner([&answer](std::function<void(bool)> done) { answer = std::move(done); });

    QCOMPARE(store.backend(), std::nullopt);
    QTRY_VERIFY(static_cast<bool>(answer)); // the probe started on the next tick
    answer(true);
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(resolved.at(0).at(0).value<SecretStore::Backend>(), SecretStore::Backend::Keychain);
    QCOMPARE(store.backendName(), QStringLiteral("libsecret"));

    QTest::qWait(60); // past the timeout: it must not flip the answer
    QCOMPARE(resolved.count(), 1);
    QCOMPARE(store.backend(), std::optional(SecretStore::Backend::Keychain));
}

namespace {

/// An in-memory keychain behind SecretStore's hooks: the cross-backend tests never touch the
/// desktop's real keyring.
struct FakeKeychain
{
    QHash<QString, QString> entries;
    bool hang = false;

    SecretStore::KeychainHooks hooks()
    {
        using Status = SecretStore::KeychainStatus;
        SecretStore::KeychainHooks h;
        h.read = [this](const QString &key, SecretStore::KeychainAnswer answer) {
            if (hang)
                return;
            if (entries.contains(key))
                answer(Status::Ok, entries.value(key), QString());
            else
                answer(Status::NotFound, QString(), QStringLiteral("not found"));
        };
        h.write = [this](const QString &key,
                         const QString &value,
                         SecretStore::KeychainAnswer answer) {
            entries.insert(key, value);
            answer(Status::Ok, QString(), QString());
        };
        h.remove = [this](const QString &key, SecretStore::KeychainAnswer answer) {
            answer(entries.remove(key) ? Status::Ok : Status::NotFound, QString(), QString());
        };
        return h;
    }
};

} // namespace

void TestSecretStore::keychainSessionFindsAndMovesAFileSecret()
{
    // A session that settled on File (locked keyring at login) stored the key in a file.
    {
        SecretStore fileSession;
        fileSession.setForcedBackend(SecretStore::Backend::File);
        QVERIFY(run(fileSession.setSecret(QStringLiteral("s"), SecretStore::Kind::ApiKey,
                                          QStringLiteral("from-file")))
                    .ok);
    }

    FakeKeychain keychain;
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::Keychain);
    store.setKeychainHooks(keychain.hooks());

    const JobResult read = run(store.secret(QStringLiteral("s"), SecretStore::Kind::ApiKey));
    QVERIFY(read.ok);
    QCOMPARE(read.value, QStringLiteral("from-file"));
    // Moved: in the keychain now, gone from the plaintext file.
    QTRY_COMPARE(keychain.entries.value(QStringLiteral("s/apiKey")), QStringLiteral("from-file"));
    QTRY_VERIFY(!QFileInfo::exists(m_dir.path() + QStringLiteral("/omaseed/secrets/s.json")));
}

void TestSecretStore::fileSessionFindsAKeychainSecret()
{
    FakeKeychain keychain;
    keychain.entries.insert(QStringLiteral("s/password"), QStringLiteral("from-keychain"));
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);
    store.setKeychainHooks(keychain.hooks());

    const JobResult read = run(store.secret(QStringLiteral("s"), SecretStore::Kind::Password));
    QVERIFY(read.ok);
    QCOMPARE(read.value, QStringLiteral("from-keychain"));
}

void TestSecretStore::fileSessionKeychainLookupIsBounded()
{
    FakeKeychain keychain;
    keychain.hang = true;
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);
    store.setKeychainHooks(keychain.hooks());
    store.setProbeTimeout(std::chrono::milliseconds(50));

    QElapsedTimer timer;
    timer.start();
    const JobResult read = run(store.secret(QStringLiteral("s"), SecretStore::Kind::ApiKey));
    QVERIFY(!read.ok);
    QVERIFY(timer.elapsed() < 2000);

    // The queue moved on: the next operation runs.
    QVERIFY(run(store.setSecret(QStringLiteral("s"), SecretStore::Kind::ApiKey,
                                QStringLiteral("x")))
                .ok);
}

void TestSecretStore::removeClearsBothBackends()
{
    {
        SecretStore fileSession;
        fileSession.setForcedBackend(SecretStore::Backend::File);
        QVERIFY(run(fileSession.setSecret(QStringLiteral("s"), SecretStore::Kind::ApiKey,
                                          QStringLiteral("stale")))
                    .ok);
    }
    FakeKeychain keychain;
    keychain.entries.insert(QStringLiteral("s/apiKey"), QStringLiteral("current"));
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::Keychain);
    store.setKeychainHooks(keychain.hooks());

    QVERIFY(run(store.removeSecret(QStringLiteral("s"), SecretStore::Kind::ApiKey)).ok);
    // Neither copy may come back through the cross-backend read.
    QVERIFY(!run(store.secret(QStringLiteral("s"), SecretStore::Kind::ApiKey)).ok);
    QVERIFY(keychain.entries.isEmpty());
}

void TestSecretStore::lateKeychainAnswerSwitchesToKeychain()
{
    SecretStore store(QStringLiteral("io.github.diogochaves.omaseed-test"));
    QSignalSpy resolved(&store, &SecretStore::backendResolved);
    store.setProbeTimeout(std::chrono::milliseconds(30));
    std::function<void(bool)> answer;
    store.setProbeRunner([&answer](std::function<void(bool)> done) { answer = std::move(done); });

    QCOMPARE(store.backend(), std::nullopt);
    QTRY_COMPARE(resolved.count(), 1);
    QCOMPARE(store.backend(), std::optional(SecretStore::Backend::File));

    answer(true); // the user unlocked the keyring at the prompt
    QCOMPARE(resolved.count(), 2);
    QCOMPARE(resolved.at(1).at(0).value<SecretStore::Backend>(), SecretStore::Backend::Keychain);
    QCOMPARE(store.backend(), std::optional(SecretStore::Backend::Keychain));
}

void TestSecretStore::aCorruptFileIsNotRewritten()
{
    // Rewriting a file that does not parse would drop whatever else it held.
    const QString dir = m_dir.path() + QStringLiteral("/omaseed/secrets");
    QVERIFY(QDir().mkpath(dir));
    const QByteArray garbage = "{\"apiKey\": \"keep-me\", oops";
    {
        QFile file(dir + QStringLiteral("/c.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(garbage);
    }
    SecretStore store;
    store.setForcedBackend(SecretStore::Backend::File);
    QVERIFY(!run(store.setSecret(QStringLiteral("c"), SecretStore::Kind::Password,
                                 QStringLiteral("p")))
                 .ok);
    QFile file(dir + QStringLiteral("/c.json"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), garbage);
}

QTEST_GUILESS_MAIN(TestSecretStore)

#include "tst_secretstore.moc"
