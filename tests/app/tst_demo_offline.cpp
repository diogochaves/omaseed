#include "core/Server.hpp"
#include "core/ServerStore.hpp"

#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

using omaseed::core::Server;
using omaseed::core::ServerStore;

/// A `--demo` run must never talk to the configured default server: on a developer's machine
/// that is their real library. Each case starts the real binary with a servers.json whose
/// default points at a listener in this test, keeps it running for a while, and checks that
/// nobody connected. A real launch (no --demo) is the positive control: it does connect.
class TestDemoOffline : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void demoNeverPollsTheDefaultServer_data();
    void demoNeverPollsTheDefaultServer();
    void aRealLaunchDoesPoll();

private:
    /// Runs omaseed with `arguments` for up to `ms` and returns whether the listener saw a
    /// connection.
    bool connectsWithin(const QStringList &arguments, int ms);

    QTemporaryDir m_home;
    QTcpServer m_listener;
};

void TestDemoOffline::initTestCase()
{
    QVERIFY(m_home.isValid());
    QVERIFY(m_listener.listen(QHostAddress::LocalHost));

    qputenv("XDG_CONFIG_HOME", m_home.path().toUtf8());
    Server server;
    server.id = QStringLiteral("trap");
    server.name = QStringLiteral("trap");
    server.url = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(m_listener.serverPort()));
    ServerStore store;
    QVERIFY(store.add(server));
    QVERIFY(store.setDefault(server.id));
}

bool TestDemoOffline::connectsWithin(const QStringList &arguments, int ms)
{
    QProcess app;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), m_home.path());
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    // Never the desktop's session bus: no single-instance name, no tray, no keychain.
    env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
               QStringLiteral("unix:path=%1/no-bus").arg(m_home.path()));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), m_home.path());
    env.remove(QStringLiteral("OMASEED_SERVER_URL"));
    app.setProcessEnvironment(env);
    app.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    app.start(QStringLiteral(OMASEED_BINARY), arguments);
    if (!app.waitForStarted(5000))
        return false;

    const bool connected = m_listener.waitForNewConnection(ms);
    while (QTcpSocket *socket = m_listener.nextPendingConnection())
        delete socket;
    app.kill();
    app.waitForFinished(5000);
    return connected;
}

void TestDemoOffline::demoNeverPollsTheDefaultServer_data()
{
    QTest::addColumn<QString>("demo");
    QTest::newRow("gallery") << QStringLiteral("gallery");
    QTest::newRow("table") << QStringLiteral("table");
    QTest::newRow("unknown name") << QStringLiteral("nosuchdemo");
}

void TestDemoOffline::demoNeverPollsTheDefaultServer()
{
    QFETCH(QString, demo);
    QVERIFY2(!connectsWithin({QStringLiteral("--demo"), demo}, 4000),
             qPrintable(QStringLiteral("--demo %1 contacted the default server").arg(demo)));
}

void TestDemoOffline::aRealLaunchDoesPoll()
{
    QVERIFY2(connectsWithin({}, 10000), "a real launch never reached its default server");
}

QTEST_GUILESS_MAIN(TestDemoOffline)

#include "tst_demo_offline.moc"
