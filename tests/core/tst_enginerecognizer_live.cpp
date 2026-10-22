// EngineRecognizer against the real engines' test servers (C14 E11, #84): Transmission on
// :8085 and deluge-web on :8086, started by scripts/dev/test-server.sh start --transmission /
// --deluge. Checks that the fakes in tests/FakeEngines.hpp answer as the real ones do.
// QSKIPs a server that doesn't answer, and Deluge's sign-in without DELUGE_TEST_PASS
// (~/.config/omaseed/dev.env).

#include "core/engines/EngineKind.hpp"
#include "core/engines/EngineRecognizer.hpp"

#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QVersionNumber>

using namespace omaseed::core;

namespace {

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

Recognition recognise(const QUrl &url, const QString &password = {})
{
    EngineRecognizer recognizer;
    recognizer.setTimeout(std::chrono::seconds(8));
    recognizer.setCredentials(QString(), password);
    QSignalSpy finished(&recognizer, &EngineRecognizer::finished);
    recognizer.start(url);
    if (!finished.wait(20000))
        return {};
    return finished.first().first().value<Recognition>();
}

} // namespace

class TestEngineRecognizerLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void transmissionOn8085();
    void delugeWebOn8086();
};

void TestEngineRecognizerLive::transmissionOn8085()
{
    if (!listening(8085))
        QSKIP("no Transmission on :8085: scripts/dev/test-server.sh start --transmission");
    // A plain address: qBittorrent is asked first and says nothing, then Transmission.
    const Recognition found = recognise(QUrl(QStringLiteral("http://127.0.0.1:8085")));
    QVERIFY(found.recognised());
    QCOMPARE(found.engine, EngineKind::Transmission);
    QVERIFY2(QVersionNumber::fromString(found.version) >= QVersionNumber(4, 1),
             qPrintable(found.version));
    QVERIFY2(QVersionNumber::fromString(found.apiVersion) >= QVersionNumber(6),
             qPrintable(found.apiVersion));
    QCOMPARE(found.endpoint.path(), QStringLiteral("/transmission/rpc"));
    QVERIFY(!engineSupport(found.engine, found.version, found.apiVersion).versionRefused);
}

void TestEngineRecognizerLive::delugeWebOn8086()
{
    if (!listening(8086))
        QSKIP("no deluge-web on :8086: scripts/dev/test-server.sh start --deluge");
    const Recognition bare = recognise(QUrl(QStringLiteral("http://127.0.0.1:8086")));
    QCOMPARE(bare.engine, EngineKind::Deluge);
    QVERIFY(bare.wantsSignIn);
    const QString password =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("DELUGE_TEST_PASS"));
    if (password.isEmpty())
        QSKIP("DELUGE_TEST_PASS is not set: load ~/.config/omaseed/dev.env");
    const Recognition wrong =
        recognise(QUrl(QStringLiteral("http://127.0.0.1:8086")), password + QStringLiteral("-not"));
    QVERIFY(wrong.signInRefused);
    const Recognition found = recognise(QUrl(QStringLiteral("http://127.0.0.1:8086")), password);
    QCOMPARE(found.engine, EngineKind::Deluge);
    QVERIFY(!found.signInRefused);
    QVERIFY(found.daemonConnected);
    QVERIFY2(QVersionNumber::fromString(found.version) >= QVersionNumber(2, 1),
             qPrintable(found.version));
    QCOMPARE(found.daemons.size(), 1);
}

QTEST_GUILESS_MAIN(TestEngineRecognizerLive)

#include "tst_enginerecognizer_live.moc"
