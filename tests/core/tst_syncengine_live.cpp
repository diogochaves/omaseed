// SyncEngine against the :8081 playground (scripts/dev/test-server.sh). It adds a stopped
// magnet, watches it turn up in TorrentModel through a normal poll, then deletes it and
// watches it leave. Nothing here touches the real library server; the URL comes from
// QBT_TEST_URL and falls back to the playground's fixed address.

#include "core/CategoryModel.hpp"
#include "core/FilterCounts.hpp"
#include "core/GlobalState.hpp"
#include "core/QbtClient.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include "LiveServer.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

using namespace omaseed::core;

namespace {

/// A magnet nobody seeds, added stopped: the test box never downloads anything.
const char *kTestHash = "0102030405060708090a0b0c0d0e0f1011121314";
const char *kTestName = "omaseed-syncengine-live-fixture";

QUrl playgroundUrl()
{
    return LiveServer::playground();
}

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(80)));
    return socket.waitForConnected(2000);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 20000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

} // namespace

class TestSyncEngineLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanup();
    void seesAnAddedTorrentAndThenItsRemoval();

private:
    void deleteFixtureTorrent();

    QUrl m_url;
};

void TestSyncEngineLive::initTestCase()
{
    m_url = playgroundUrl();
    if (!m_url.isValid() || !answers(m_url)) {
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");
    }
}

void TestSyncEngineLive::cleanup()
{
    if (m_url.isValid() && answers(m_url))
        deleteFixtureTorrent();
}

void TestSyncEngineLive::deleteFixtureTorrent()
{
    QbtClient client;
    client.setBaseUrl(m_url);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("hashes"), QString::fromLatin1(kTestHash));
    form.addQueryItem(QStringLiteral("deleteFiles"), QStringLiteral("false"));
    await(this, client.post(QStringLiteral("torrents/delete"), form));
}

void TestSyncEngineLive::seesAnAddedTorrentAndThenItsRemoval()
{
    deleteFixtureTorrent(); // a leftover from an interrupted run must not confuse us

    QbtClient client;
    client.setBaseUrl(m_url);

    SyncEngine engine;
    engine.setClient(&client);
    engine.setIntervals(std::chrono::milliseconds(300),
                        std::chrono::milliseconds(1000),
                        std::chrono::milliseconds(1000),
                        std::chrono::milliseconds(1000));

    QSignalSpy firstSync(&engine, &SyncEngine::firstSyncFinished);
    engine.start();
    QVERIFY2(waitFor([&] { return firstSync.count() == 1; }, 15000),
             "the first full update never arrived");

    QCOMPARE(engine.globalState()->connected(), true);
    QVERIFY(engine.globalState()->serverVersion().startsWith(QLatin1Char('v')));
    QVERIFY(!engine.globalState()->apiVersion().isEmpty());
    QCOMPARE(engine.counts()->total(), engine.torrents()->count());
    const int before = engine.torrents()->count();

    // Add it the way the app will (T2.5 wraps this endpoint), stopped so nothing downloads.
    QbtClient adder;
    adder.setBaseUrl(m_url);
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("urls"),
                      QStringLiteral("magnet:?xt=urn:btih:%1&dn=%2")
                          .arg(QString::fromLatin1(kTestHash), QString::fromLatin1(kTestName)));
    form.addQueryItem(QStringLiteral("stopped"), QStringLiteral("true"));
    form.addQueryItem(QStringLiteral("category"), QStringLiteral("fixtures"));
    const QbtResult<QByteArray> added =
        await(this, adder.post(QStringLiteral("torrents/add"), form));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));

    QVERIFY2(
        waitFor([&] { return engine.torrents()->indexOfHash(QString::fromLatin1(kTestHash)) >= 0; },
                5000),
        "the added torrent did not show up within 5 s");

    const Torrent &t = engine.torrents()->torrent(QString::fromLatin1(kTestHash));
    QCOMPARE(t.name, QString::fromLatin1(kTestName));
    QCOMPARE(t.category, QStringLiteral("fixtures"));
    QVERIFY(t.inGroup(StatusGroup::Stopped));
    QCOMPARE(engine.torrents()->count(), before + 1);
    QCOMPARE(engine.counts()->total(), before + 1);
    QCOMPARE(engine.counts()->categoryCount(QStringLiteral("fixtures")) >= 1, true);
    QVERIFY(engine.rid() > 0);

    deleteFixtureTorrent();

    QVERIFY2(
        waitFor([&] { return engine.torrents()->indexOfHash(QString::fromLatin1(kTestHash)) < 0; },
                5000),
        "the deleted torrent did not leave the model within 5 s");
    QCOMPARE(engine.torrents()->count(), before);
    QCOMPARE(engine.counts()->total(), before);

    engine.stop();
    QCOMPARE(engine.isRunning(), false);
}

QTEST_MAIN(TestSyncEngineLive)
#include "tst_syncengine_live.moc"
