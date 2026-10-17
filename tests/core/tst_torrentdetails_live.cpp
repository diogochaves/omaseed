// TorrentDetails against the :8081 playground (scripts/dev/test-server.sh). It adds a real
// multi-file .torrent stopped, selects it, and waits for the 2 s poll (shortened here) to
// bring back the properties, the file tree and the tracker list; then it checks that the
// peers poll only runs while the Peers tab says it is active, and deletes its fixture again.
//
// Nothing here touches the real library server: the URL comes from QBT_TEST_URL and falls
// back to the playground's fixed address. The whole case skips when :8081 is down.

#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentPeersModel.hpp"
#include "core/TorrentTrackersModel.hpp"
#include "core/engines/qbt/QbtClient.hpp"

#include "LiveServer.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcessEnvironment>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <functional>

using namespace omaseed::core;

namespace {

/// tests/fixtures/multi_file_v1_unicode.torrent: three files in two nested folders, which is
/// the smallest thing that proves the tree, and one announce URL for the tracker list.
const char *kFixtureFile = OMASEED_FIXTURES_DIR "/multi_file_v1_unicode.torrent";
const char *kFixtureHash = "6f04b6c7642d15dc5cfa6cd2478e7f128a0e556f";
const char *kCategory = "omaseed-t34-live";

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
Result<T> await(QObject *context, Reply<T> *reply, int timeoutMs = 20000)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const Result<T> &value) {
        result = value;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

} // namespace

class TestTorrentDetailsLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanup();
    void polls_properties_files_and_trackers();
    void peers_only_poll_while_the_tab_is_active();
    void piece_states_poll_while_general_is_active();

private:
    void addFixture(QbtClient &client);
    void removeFixture(QbtClient &client);

    QUrl m_url;
};

void TestTorrentDetailsLive::initTestCase()
{
    m_url = playgroundUrl();
    if (!answers(m_url))
        QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");
}

void TestTorrentDetailsLive::addFixture(QbtClient &client)
{
    QFile file(QString::fromLatin1(kFixtureFile));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.fileName()));

    QbtClient::AddOptions options;
    options.stopped = true; // the test box never downloads anything
    options.category = QString::fromLatin1(kCategory);
    const Result<QByteArray> added =
        await(this,
              client.torrentsAddFile(
                  QStringLiteral("multi_file_v1_unicode.torrent"), file.readAll(), options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));

    // The server needs a moment to parse it before torrents/files answers.
    QVERIFY(waitFor(
        [&]() {
            const Result<QJsonArray> files =
                await(this, client.torrentsFiles(QString::fromLatin1(kFixtureHash)));
            return !files.isError() && !files.value().isEmpty();
        },
        10000));
}

void TestTorrentDetailsLive::removeFixture(QbtClient &client)
{
    await(this, client.torrentsDelete({QString::fromLatin1(kFixtureHash)}, /*deleteFiles=*/true));
    // Deleting the torrent does not delete the category it was filed under: the playground
    // would otherwise collect one `omaseed-t34-live` row per run for ever (T4.8).
    await(this, client.torrentsRemoveCategories({QString::fromLatin1(kCategory)}));
}

void TestTorrentDetailsLive::cleanup()
{
    if (m_url.isEmpty() || !answers(m_url))
        return;
    QbtClient client;
    client.setBaseUrl(m_url);
    removeFixture(client);
}

void TestTorrentDetailsLive::polls_properties_files_and_trackers()
{
    QbtClient client;
    client.setBaseUrl(m_url);
    addFixture(client);

    TorrentDetails details;
    details.setClient(&client);
    details.setPollInterval(200); // the real cadence is 2 s; a test should not wait for it
    details.setHash(QString::fromLatin1(kFixtureHash));

    QVERIFY2(waitFor([&]() { return details.files()->count() > 0; }, 5000),
             "the file tree never arrived");
    QVERIFY2(waitFor([&]() { return details.trackers()->count() > 0; }, 5000),
             "the tracker list never arrived");
    QVERIFY2(waitFor([&]() { return !details.properties().isEmpty(); }, 5000),
             "the properties never arrived");

    // The fixture is "Unicode Pack" with three files under two nested folders.
    QCOMPARE(details.files()->fileCount(), 3);
    QCOMPARE(details.files()->count(), 7);
    QCOMPARE(
        details.files()->data(details.files()->index(0), TorrentFilesModel::NameRole).toString(),
        QStringLiteral("Unicode Pack/"));
    QVERIFY(details.files()->isFolder(0));
    QCOMPARE(details.properties().value(QStringLiteral("name")).toString(),
             QStringLiteral("Unicode Pack"));
    QCOMPARE(details.properties().value(QStringLiteral("hashV1")).toString(),
             QString::fromLatin1(kFixtureHash));

    // DHT/PeX/LSD plus the torrent's own announce URL, whatever the playground's settings.
    QVERIFY(details.trackers()->count() >= 1);
    QVERIFY(!details.hasTrackerError());
    QVERIFY(details.lastError().isEmpty());

    // The pane going away stops the polls; the fold state and the rows survive it.
    details.setVisible(false);
    QTest::qWait(500);
    QCOMPARE(details.files()->count(), 7);

    details.setHash(QString());
    QCOMPARE(details.files()->count(), 0);
}

void TestTorrentDetailsLive::peers_only_poll_while_the_tab_is_active()
{
    QbtClient client;
    client.setBaseUrl(m_url);
    addFixture(client);

    TorrentDetails details;
    details.setClient(&client);
    details.setPollInterval(200);
    details.setHash(QString::fromLatin1(kFixtureHash));

    // The Peers tab is not on show, so sync/torrentPeers is never asked for and the rid
    // stays at 0 (the value that means "send me a full update").
    QVERIFY(waitFor([&]() { return details.files()->count() > 0; }, 5000));
    QTest::qWait(500);
    QCOMPARE(details.peers()->rid(), 0);

    // Opening the tab starts it. A stopped torrent nobody seeds has no peers, so the rid
    // moving off zero is the whole assertion.
    details.setPeersActive(true);
    QVERIFY2(waitFor([&]() { return details.peers()->rid() > 0; }, 5000),
             "sync/torrentPeers never answered");
    QCOMPARE(details.peers()->count(), 0);

    // Leaving the tab forgets the rid, so the next visit gets a full update.
    details.setPeersActive(false);
    QCOMPARE(details.peers()->rid(), 0);
}

void TestTorrentDetailsLive::piece_states_poll_while_general_is_active()
{
    // docs/12 G3: torrents/pieceStates rides with the properties while General is on show.
    QbtClient client;
    client.setBaseUrl(m_url);
    addFixture(client);

    TorrentDetails details;
    details.setClient(&client);
    details.setPollInterval(200);
    details.setHash(QString::fromLatin1(kFixtureHash));

    // The fixture is one 16 KiB piece, added stopped, so nothing of it is had.
    QVERIFY2(waitFor([&]() { return details.pieceCount() > 0; }, 5000),
             "torrents/pieceStates never answered");
    QCOMPARE(details.pieceCount(), 1);
    QCOMPARE(details.piecesHaveCount(), 0);
    QCOMPARE(details.pieceBuckets(300), (QList<int>{0}));
    QVERIFY(details.lastError().isEmpty());

    // Another tab: the states are dropped and no longer asked for.
    details.setGeneralActive(false);
    QCOMPARE(details.pieceCount(), 0);
    QTest::qWait(500);
    QCOMPARE(details.pieceCount(), 0);

    // Back on General, they come back straight away.
    details.setGeneralActive(true);
    QVERIFY(waitFor([&]() { return details.pieceCount() == 1; }, 5000));
}

QTEST_MAIN(TestTorrentDetailsLive)
#include "tst_torrentdetails_live.moc"
