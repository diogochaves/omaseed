// tests/LiveServer.hpp: the live tests' URL guard. A URL on port 8080 (the real library) must
// never reach a live test, whatever the host or spelling; the playground and strict twin pass.

#include "LiveServer.hpp"

#include <QCoreApplication>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTest>

class TestLiveServer : public QObject
{
    Q_OBJECT

private slots:
    void isHomeUrl_data();
    void isHomeUrl();
    void fallsBackToTheTestServers();
    void aHomeUrlAbortsTheTest();
};

void TestLiveServer::isHomeUrl_data()
{
    QTest::addColumn<QString>("url");
    QTest::addColumn<bool>("home");

    QTest::newRow("loopback") << QStringLiteral("http://127.0.0.1:8080") << true;
    QTest::newRow("trailing slash") << QStringLiteral("http://127.0.0.1:8080/") << true;
    QTest::newRow("with a path") << QStringLiteral("http://localhost:8080/api/v2") << true;
    QTest::newRow("ipv6") << QStringLiteral("http://[::1]:8080") << true;
    QTest::newRow("lan host") << QStringLiteral("https://nas.lan:8080") << true;
    QTest::newRow("no scheme") << QStringLiteral("127.0.0.1:8080") << true;
    QTest::newRow("leading zero") << QStringLiteral("http://127.0.0.1:08080") << true;
    QTest::newRow("spaces") << QStringLiteral("  http://127.0.0.1:8080  ") << true;
    QTest::newRow("playground") << QStringLiteral("http://127.0.0.1:8081") << false;
    QTest::newRow("strict") << QStringLiteral("http://127.0.0.1:8082") << false;
    QTest::newRow("longer port") << QStringLiteral("http://127.0.0.1:18080") << false;
    QTest::newRow("prefix port") << QStringLiteral("http://127.0.0.1:80801") << false;
    QTest::newRow("default port") << QStringLiteral("http://127.0.0.1") << false;
}

void TestLiveServer::isHomeUrl()
{
    QFETCH(QString, url);
    QFETCH(bool, home);
    QCOMPARE(LiveServer::isHomeUrl(url), home);
}

void TestLiveServer::fallsBackToTheTestServers()
{
    const QByteArray savedTest = qgetenv("QBT_TEST_URL");
    const QByteArray savedStrict = qgetenv("QBT_STRICT_URL");
    qunsetenv("QBT_TEST_URL");
    qunsetenv("QBT_STRICT_URL");
    QCOMPARE(LiveServer::playground(), QUrl(QStringLiteral("http://127.0.0.1:8081")));
    QCOMPARE(LiveServer::strict(), QUrl(QStringLiteral("http://127.0.0.1:8082")));
    if (!savedTest.isNull())
        qputenv("QBT_TEST_URL", savedTest);
    if (!savedStrict.isNull())
        qputenv("QBT_STRICT_URL", savedStrict);
}

// The guard itself: this binary, run again with QBT_TEST_URL on :8080 and the child switch set,
// must exit in LiveServer::playground() instead of returning a URL.
void TestLiveServer::aHomeUrlAbortsTheTest()
{
    QProcess child;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("QBT_TEST_URL"), QStringLiteral("http://127.0.0.1:8080"));
    environment.insert(QStringLiteral("OMASEED_LIVESERVER_CHILD"), QStringLiteral("1"));
    child.setProcessEnvironment(environment);
    child.start(QCoreApplication::applicationFilePath(), {});
    QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 2);
    QVERIFY(!child.readAllStandardOutput().contains("reached"));
    QVERIFY(child.readAllStandardError().contains("refusing to run a live test"));
}

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet("OMASEED_LIVESERVER_CHILD")) {
        const QUrl url = LiveServer::playground();
        printf("reached %s\n", qPrintable(url.toString()));
        return 0;
    }
    QCoreApplication app(argc, argv);
    TestLiveServer test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_liveserver.moc"
