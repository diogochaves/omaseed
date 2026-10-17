// ServerLog (docs/32 §6, #56) against the :8081 playground: the whole log read from the top at
// all four levels, then only what follows. Reads only; the playground's log is never changed.

#include "core/ServerLog.hpp"
#include "core/engines/qbt/QbtClient.hpp"

#include "LiveServer.hpp"

#include <QSet>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>

using omaseed::core::QbtClient;
using omaseed::core::ServerLog;

namespace {

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

} // namespace

class TestServerLogLive : public QObject
{
    Q_OBJECT

private slots:
    void readsTheWholeLogThenWhatFollows();
};

void TestServerLogLive::readsTheWholeLogThenWhatFollows()
{
    const QUrl url = LiveServer::playground();
    if (!answers(url))
        QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");

    QbtClient client;
    client.setBaseUrl(url);
    ServerLog log;
    log.setClient(&client);
    QSignalSpy loaded(&log, &ServerLog::loadedChanged);
    log.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(log.isLoaded(), 10000);
    // Its start-up lines at least, normal and info among them, ids going up.
    QVERIFY(log.rowCount() > 0);
    QSet<int> levels;
    int previous = -1;
    for (int row = 0; row < log.rowCount(); ++row) {
        QVERIFY(log.lineAt(row).id > previous);
        previous = log.lineAt(row).id;
        levels.insert(log.lineAt(row).level);
    }
    QCOMPARE(log.lastId(), previous);
    QVERIFY(levels.contains(ServerLog::Normal) || levels.contains(ServerLog::Info));

    // A poll asks for what follows only: nothing is read twice.
    const int before = log.rowCount();
    log.onSynced(false);
    QTest::qWait(500);
    QVERIFY(log.rowCount() >= before);
    for (int row = 1; row < log.rowCount(); ++row)
        QVERIFY(log.lineAt(row).id > log.lineAt(row - 1).id);
}

QTEST_GUILESS_MAIN(TestServerLogLive)
#include "tst_serverlog_live.moc"
