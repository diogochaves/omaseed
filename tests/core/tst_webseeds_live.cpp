// #10: web seeds against the :8081 playground (scripts/dev/test-server.sh). The test makes a
// one-file .torrent of its own whose top-level `url-list` names two HTTP sources, adds it
// stopped, and checks that `torrents/webseeds` and TorrentDetails' model bring the two back;
// then it adds, edits and removes one through the client, and checks the server's refusals.
// The URLs point nowhere (a discard port, a reserved name) and the torrent never starts:
// the list is what is under test. The torrent and its category are deleted again.
//
// Nothing here touches the real library server: the URL comes from LiveServer (QBT_TEST_URL
// or the playground). The whole case skips when :8081 is down.

#include "QbtWire.hpp"
#include "core/Bencode.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentWebSeedsModel.hpp"
#include "core/engines/qbt/QbtEngine.hpp"

#include "LiveServer.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonObject>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <functional>

using namespace omaseed::core;

namespace {

const char *kCategory = "omaseed-webseeds-live";
const QString kFirst = QStringLiteral("http://127.0.0.1:9/omaseed-webseed-live/");
const QString kSecond = QStringLiteral("https://mirror.invalid/omaseed-webseed-live/");
const QString kThird = QStringLiteral("https://other.invalid/pub/");

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

/// A one-file torrent ("omaseed-webseed-live.txt", five bytes) with a BEP 19 `url-list`.
/// The info dict is this test's own, so its hash collides with nothing on the playground.
QByteArray webSeededTorrent()
{
    using namespace omaseed::core::bencode;
    const QByteArray content = QByteArrayLiteral("hello");
    const Dict info{
        {QByteArrayLiteral("length"), Value(qint64(content.size()))},
        {QByteArrayLiteral("name"), Value(QByteArrayLiteral("omaseed-webseed-live.txt"))},
        {QByteArrayLiteral("piece length"), Value(qint64(16384))},
        {QByteArrayLiteral("pieces"),
         Value(QCryptographicHash::hash(content, QCryptographicHash::Sha1))},
    };
    const Dict torrent{
        {QByteArrayLiteral("info"), Value(info)},
        {QByteArrayLiteral("url-list"),
         Value(List{Value(kFirst.toUtf8()), Value(kSecond.toUtf8())})},
    };
    return encode(Value(torrent));
}

QStringList sorted(QStringList urls)
{
    urls.sort();
    return urls;
}

QStringList urlsOf(QStringList urls)
{
    urls.sort();
    return urls;
}

} // namespace

class TestWebSeedsLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void theTorrentsUrlListComesBack();
    void addEditAndRemove();
    void theServerRefusesWhatItCannotUse();

private:
    QStringList webSeeds(QbtEngine &client);

    QUrl m_url;
    QString m_hash;
};

void TestWebSeedsLive::initTestCase()
{
    m_url = LiveServer::playground();
    if (!answers(m_url))
        QSKIP("the :8081 playground is not running (scripts/dev/test-server.sh start)");

    const QByteArray bytes = webSeededTorrent();
    QString parseError;
    const TorrentFile parsed = TorrentFile::parse(bytes, &parseError);
    QVERIFY2(parseError.isEmpty(), qPrintable(parseError));
    m_hash = parsed.infoHashV1;
    QCOMPARE(m_hash.size(), 40);

    QbtEngine client;
    client.setBaseUrl(m_url);
    AddOptions options;
    options.stopped = true; // never fetches from the made-up sources
    options.category = QString::fromLatin1(kCategory);
    const Result<AddResult> added =
        await(this, client.addFile(QStringLiteral("omaseed-webseed-live.torrent"), bytes, options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    QVERIFY2(waitFor([&]() { return webSeeds(client).size() == 2; }, 10000),
             "the torrent's web seeds never showed");
}

void TestWebSeedsLive::cleanupTestCase()
{
    if (m_hash.isEmpty() || !answers(m_url))
        return;
    QbtEngine client;
    client.setBaseUrl(m_url);
    await(this, client.remove({m_hash}, /*deleteFiles=*/true));
    await(this, client.removeCategories({QString::fromLatin1(kCategory)}));
}

QStringList TestWebSeedsLive::webSeeds(QbtEngine &client)
{
    const Result<QStringList> seeds = await(this, client.webSeeds(m_hash));
    return seeds.isError() ? QStringList() : urlsOf(seeds.value());
}

void TestWebSeedsLive::theTorrentsUrlListComesBack()
{
    QbtEngine client;
    client.setBaseUrl(m_url);
    QCOMPARE(webSeeds(client), sorted({kFirst, kSecond}));

    // The details pane's model, through the poll it runs on a selection change.
    TorrentDetails details;
    details.setPollInterval(200);
    details.setClient(&client);
    details.setHash(m_hash);
    QVERIFY2(waitFor([&]() { return details.webSeeds()->loaded(); }, 5000),
             "the web seeds never arrived");
    QCOMPARE(details.webSeeds()->count(), 2);
    QStringList shown{details.webSeeds()->urlAt(0), details.webSeeds()->urlAt(1)};
    shown.sort();
    QCOMPARE(shown, sorted({kFirst, kSecond}));
    QVERIFY(details.lastError().isEmpty());
}

void TestWebSeedsLive::addEditAndRemove()
{
    QbtEngine client;
    client.setBaseUrl(m_url);

    QVERIFY(!await(this, client.addWebSeeds(m_hash, {kThird})).isError());
    QCOMPARE(webSeeds(client), sorted({kFirst, kSecond, kThird}));

    const QString edited = QStringLiteral("https://edited.invalid/pub/");
    // The app never edits one; the endpoint is the transport's (QbtClient).
    QVERIFY(!await(this, client.client()->torrentsEditWebSeed(m_hash, kThird, edited)).isError());
    QCOMPARE(webSeeds(client), sorted({kFirst, kSecond, edited}));

    QVERIFY(!await(this, client.removeWebSeeds(m_hash, {edited})).isError());
    QCOMPARE(webSeeds(client), sorted({kFirst, kSecond}));
}

void TestWebSeedsLive::theServerRefusesWhatItCannotUse()
{
    QbtEngine client;
    client.setBaseUrl(m_url);

    // Not one of the torrent's: 409.
    const Result<QByteArray> edit =
        await(this,
              client.client()->torrentsEditWebSeed(
                  m_hash, QStringLiteral("https://none.invalid/"), kThird));
    QVERIFY(edit.isError());
    QCOMPARE(edit.error().kind(), ApiError::Kind::Conflict);

    // Not a URL: 400, and nothing added.
    const Result<Done> add = await(this, client.addWebSeeds(m_hash, {QStringLiteral("not a url")}));
    QVERIFY(add.isError());
    QCOMPARE(add.error().httpStatus(), 400);
    QCOMPARE(webSeeds(client), sorted({kFirst, kSecond}));

    // An unknown torrent: 404.
    const Result<QStringList> unknown =
        await(this, client.webSeeds(QStringLiteral("0000000000000000000000000000000000000000")));
    QVERIFY(unknown.isError());
    QCOMPARE(unknown.error().kind(), ApiError::Kind::NotFound);
}

QTEST_GUILESS_MAIN(TestWebSeedsLive)

#include "tst_webseeds_live.moc"
