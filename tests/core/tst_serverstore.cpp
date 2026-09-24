#include "core/ServerStore.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::AuthMode;
using omaseed::core::Server;
using omaseed::core::ServerStore;

namespace {

Server makeServer(const QString &name, const QUrl &url)
{
    Server server;
    server.id = Server::newId();
    server.name = name;
    server.url = url;
    server.authMode = AuthMode::ApiKey;
    server.addedAt = QDateTime::currentDateTimeUtc();
    return server;
}

} // namespace

class TestServerStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void startsEmptyWithNoFile();
    void addUpdateRemoveAndDefault();
    void savesAndReloads();
    void aStoreThatIsNotPersistentWritesNothing();
    void filePermissionsAreOwnerOnly();
    void corruptJsonFailsWithoutLosingTheFile();
    void futureVersionFailsWithoutLoading();
    void anAddAfterAFailedLoadKeepsTheUnreadableFile_data();
    void anAddAfterAFailedLoadKeepsTheUnreadableFile();
    void anUnmovableFileIsKeptInPlaceUntilASaveCanMoveIt();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousXdgConfigHome;
    bool m_hadPreviousXdgConfigHome = false;
};

void TestServerStore::init()
{
    QVERIFY(m_dir.isValid());
    m_hadPreviousXdgConfigHome = qEnvironmentVariableIsSet("XDG_CONFIG_HOME");
    if (m_hadPreviousXdgConfigHome)
        m_previousXdgConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestServerStore::cleanup()
{
    if (m_hadPreviousXdgConfigHome)
        qputenv("XDG_CONFIG_HOME", m_previousXdgConfigHome);
    else
        qunsetenv("XDG_CONFIG_HOME");
}

void TestServerStore::startsEmptyWithNoFile()
{
    ServerStore store;
    QVERIFY(store.load());
    QCOMPARE(store.count(), 0);
    QVERIFY(!store.defaultServer().has_value());
}

void TestServerStore::addUpdateRemoveAndDefault()
{
    ServerStore store;
    QSignalSpy addedSpy(&store, &ServerStore::added);
    QSignalSpy updatedSpy(&store, &ServerStore::updated);
    QSignalSpy removedSpy(&store, &ServerStore::removed);

    Server playground =
        makeServer(QStringLiteral("Playground"), QUrl(QStringLiteral("http://127.0.0.1:8081")));
    Server strict =
        makeServer(QStringLiteral("Strict"), QUrl(QStringLiteral("http://127.0.0.1:8082")));

    QVERIFY(store.add(playground));
    QCOMPARE(addedSpy.count(), 1);
    QCOMPARE(store.count(), 1);
    // The first server added becomes the default.
    QVERIFY(store.defaultServer().has_value());
    QCOMPARE(store.defaultServer()->id, playground.id);

    QVERIFY(store.add(strict));
    QCOMPARE(store.count(), 2);
    // Adding a second server does not steal the default.
    QCOMPARE(store.defaultId(), playground.id);

    // Adding the same id twice fails.
    QVERIFY(!store.add(playground));
    QCOMPARE(store.count(), 2);

    QVERIFY(store.setDefault(strict.id));
    QCOMPARE(store.defaultId(), strict.id);
    QVERIFY(store.byId(strict.id)->isDefault);
    QVERIFY(!store.byId(playground.id)->isDefault);

    Server renamed = playground;
    renamed.name = QStringLiteral("Playground (renamed)");
    QVERIFY(store.update(renamed));
    QCOMPARE(updatedSpy.count(), 1);
    QCOMPARE(store.byId(playground.id)->name, renamed.name);

    // Updating an unknown id fails.
    Server unknown =
        makeServer(QStringLiteral("Ghost"), QUrl(QStringLiteral("http://127.0.0.1:9999")));
    QVERIFY(!store.update(unknown));

    QVERIFY(store.remove(strict.id));
    QCOMPARE(removedSpy.count(), 1);
    QCOMPARE(store.count(), 1);
    // Removing the default falls back to whatever is left.
    QCOMPARE(store.defaultId(), playground.id);

    QVERIFY(!store.remove(strict.id)); // already gone
}

void TestServerStore::savesAndReloads()
{
    {
        ServerStore store;
        store.add(makeServer(QStringLiteral("Playground"),
                             QUrl(QStringLiteral("http://127.0.0.1:8081"))));
        store.add(
            makeServer(QStringLiteral("Strict"), QUrl(QStringLiteral("http://127.0.0.1:8082"))));
    }

    ServerStore reloaded;
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.count(), 2);
    QVERIFY(reloaded.defaultServer().has_value());
}

// A --demo run keeps its servers in memory (batch-2 P5).
void TestServerStore::aStoreThatIsNotPersistentWritesNothing()
{
    const QString file = m_dir.path() + QStringLiteral("/omaseed/servers.json");
    QFile::remove(file); // the directory is shared with the cases before this one

    ServerStore store;
    QVERIFY(store.isPersistent());
    store.setPersistent(false);
    QVERIFY(store.add(
        makeServer(QStringLiteral("Playground"), QUrl(QStringLiteral("http://127.0.0.1:8081")))));
    QCOMPARE(store.count(), 1);
    QVERIFY(store.save());
    QVERIFY(!QFile::exists(file));

    ServerStore reloaded;
    QVERIFY(reloaded.load());
    QCOMPARE(reloaded.count(), 0);
}

void TestServerStore::filePermissionsAreOwnerOnly()
{
    ServerStore store;
    store.add(
        makeServer(QStringLiteral("Playground"), QUrl(QStringLiteral("http://127.0.0.1:8081"))));

    const QString configDir = m_dir.path() + QStringLiteral("/omaseed");
    const QString filePath = configDir + QStringLiteral("/servers.json");

    QFileInfo dirInfo(configDir);
    QCOMPARE(dirInfo.permissions() &
                 (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                  QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther),
             QFileDevice::Permissions());
    QVERIFY(dirInfo.permissions() & QFileDevice::ReadOwner);
    QVERIFY(dirInfo.permissions() & QFileDevice::WriteOwner);
    QVERIFY(dirInfo.permissions() & QFileDevice::ExeOwner);

    QFileInfo fileInfo(filePath);
    QVERIFY(fileInfo.exists());
    QCOMPARE(fileInfo.permissions() &
                 (QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup |
                  QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther),
             QFileDevice::Permissions());
    QVERIFY(fileInfo.permissions() & QFileDevice::ReadOwner);
    QVERIFY(fileInfo.permissions() & QFileDevice::WriteOwner);
}

void TestServerStore::corruptJsonFailsWithoutLosingTheFile()
{
    const QString configDir = m_dir.path() + QStringLiteral("/omaseed");
    QVERIFY(QDir().mkpath(configDir));
    const QString filePath = configDir + QStringLiteral("/servers.json");

    const QByteArray corrupt = "{ this is not valid json ";
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(corrupt);
    }

    ServerStore store;
    QSignalSpy failedSpy(&store, &ServerStore::loadFailed);
    QVERIFY(!store.load());
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(store.count(), 0);

    // Moved aside at once (Q9), so the first-run page can name where it went.
    QVERIFY(!QFile::exists(filePath));
    QVERIFY(store.keptPath().startsWith(filePath + QStringLiteral(".unreadable-")));
    QFile file(store.keptPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), corrupt);
}

void TestServerStore::futureVersionFailsWithoutLoading()
{
    const QString configDir = m_dir.path() + QStringLiteral("/omaseed");
    QVERIFY(QDir().mkpath(configDir));
    const QString filePath = configDir + QStringLiteral("/servers.json");

    const QByteArray futureFile = R"({"version":99,"servers":[],"defaultId":""})";
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(futureFile);
    }

    ServerStore store;
    QSignalSpy failedSpy(&store, &ServerStore::loadFailed);
    QVERIFY(!store.load());
    QCOMPARE(failedSpy.count(), 1);
    QCOMPARE(store.count(), 0);

    // Moved aside at once (Q9), so the first-run page can name where it went.
    QVERIFY(!QFile::exists(filePath));
    QVERIFY(store.keptPath().startsWith(filePath + QStringLiteral(".unreadable-")));
    QFile file(store.keptPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), futureFile);
}

void TestServerStore::anAddAfterAFailedLoadKeepsTheUnreadableFile_data()
{
    QTest::addColumn<QByteArray>("unreadable");
    QTest::newRow("corrupt") << QByteArray("{ \"version\": 1, \"servers\": [ oops");
    QTest::newRow("newer version") << QByteArray(R"({"version":2,"servers":[{"id":"x"}]})");
}

void TestServerStore::anAddAfterAFailedLoadKeepsTheUnreadableFile()
{
    QFETCH(QByteArray, unreadable);
    const QString configDir = m_dir.path() + QStringLiteral("/omaseed");
    QVERIFY(QDir().mkpath(configDir));
    const QString filePath = configDir + QStringLiteral("/servers.json");
    for (const QString &old : QDir(configDir).entryList({QStringLiteral("servers.json*")}))
        QFile::remove(configDir + QLatin1Char('/') + old);
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(unreadable);
    }

    ServerStore store;
    QVERIFY(!store.load());
    QVERIFY(!store.loadError().isEmpty());
    const QString aside = store.keptPath();
    QVERIFY(aside.startsWith(filePath + QStringLiteral(".unreadable-")));

    // First run then adds a server: the new list is saved, the old bytes survive beside it,
    // and a second save leaves the new file where it is.
    QVERIFY(store.add(makeServer(QStringLiteral("new"), QUrl(QStringLiteral("http://nas:8080")))));
    QVERIFY(store.add(makeServer(QStringLiteral("two"), QUrl(QStringLiteral("http://nas:8081")))));
    QCOMPARE(QDir(configDir).entryList({QStringLiteral("servers.json.unreadable-*")}).size(), 1);
    QCOMPARE(store.keptPath(), aside);
    QFile kept(aside);
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), unreadable);

    ServerStore reread;
    QVERIFY(reread.load());
    QCOMPARE(reread.count(), 2);
}

void TestServerStore::anUnmovableFileIsKeptInPlaceUntilASaveCanMoveIt()
{
    const QString configDir = m_dir.path() + QStringLiteral("/omaseed");
    QVERIFY(QDir().mkpath(configDir));
    const QString filePath = configDir + QStringLiteral("/servers.json");
    for (const QString &old : QDir(configDir).entryList({QStringLiteral("servers.json*")}))
        QFile::remove(configDir + QLatin1Char('/') + old);
    const QByteArray corrupt = "{ oops";
    {
        QFile file(filePath);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(corrupt);
    }

    // A directory the rename cannot write to: the file stays, and keptPath says so.
    QVERIFY(QFile::setPermissions(configDir, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    ServerStore store;
    QVERIFY(!store.load());
    QVERIFY(QFile::setPermissions(configDir,
                                  QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                      QFileDevice::ExeOwner));
    QCOMPARE(store.keptPath(), filePath);

    // The next save moves it first, then writes the new list.
    QVERIFY(store.add(makeServer(QStringLiteral("new"), QUrl(QStringLiteral("http://nas:8080")))));
    QVERIFY(store.keptPath().startsWith(filePath + QStringLiteral(".unreadable-")));
    QFile kept(store.keptPath());
    QVERIFY(kept.open(QIODevice::ReadOnly));
    QCOMPARE(kept.readAll(), corrupt);
}

QTEST_APPLESS_MAIN(TestServerStore)

#include "tst_serverstore.moc"
