// QbtClient torrent actions (T2.5) against a minimal HTTP/1.1 server of our own: asserts the
// exact form bodies the client sends (parameter names and percent-encoding) and that a
// mutating POST is never retried on a transient failure. Behaviour against a real
// qBittorrent is covered by tst_qbtclient_actions_live.

#include "core/QbtClient.hpp"

#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>

using omaseed::core::ApiError;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;

namespace {

QByteArray httpResponse(int code,
                        const QByteArray &reason,
                        const QByteArray &body,
                        const QList<QByteArray> &extraHeaders = {})
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    response += "Content-Type: text/plain; charset=UTF-8\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    for (const QByteArray &header : extraHeaders)
        response += header + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

/// One HTTP request as the fixture saw it.
struct Recorded
{
    QByteArray method;
    QByteArray target;
    QList<QByteArray> headers;
    QByteArray body;

    QByteArray header(const QByteArray &name) const
    {
        for (const QByteArray &line : headers) {
            const qsizetype colon = line.indexOf(':');
            if (colon > 0 && line.left(colon).toLower() == name.toLower())
                return line.mid(colon + 1).trimmed();
        }
        return {};
    }
};

/// Speaks just enough HTTP to answer QNetworkAccessManager.
class FakeServer : public QTcpServer
{
public:
    using Handler = std::function<QByteArray(const Recorded &, int index)>;

    explicit FakeServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }
    void setHandler(Handler handler) { m_handler = std::move(handler); }

    QList<Recorded> requests;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;

            const QList<QByteArray> lines = buffer->left(headerEnd).split('\n');
            Recorded recorded;
            const QList<QByteArray> requestLine = lines.value(0).trimmed().split(' ');
            recorded.method = requestLine.value(0);
            recorded.target = requestLine.value(1);
            for (qsizetype i = 1; i < lines.size(); ++i)
                recorded.headers.append(lines.at(i).trimmed());

            const QByteArray lengthHeader = recorded.header("Content-Length");
            const qsizetype bodyLength = lengthHeader.isEmpty() ? 0 : lengthHeader.toLongLong();
            if (buffer->size() - (headerEnd + 4) < bodyLength)
                return; // wait for the rest of the body
            recorded.body = buffer->mid(headerEnd + 4, bodyLength);
            buffer->clear();

            const int index = int(requests.size());
            requests.append(recorded);

            const QByteArray response =
                m_handler ? m_handler(recorded, index) : httpResponse(200, "OK", "ok");
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    Handler m_handler;
};

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply, int timeoutMs = 10000)
{
    QbtResult<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("test never finished"))};
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

class TestQbtClientActionsFake : public QObject
{
    Q_OBJECT

private slots:
    void hashesAreJoinedWithPipe();
    void tagsArePercentEncoded();
    void deleteFilesFlag();
    void shareLimitsSendsAllFiveFields();
    void shareLimitActionNames_data();
    void shareLimitActionNames();
    void shareLimitActionIsOmittedForAnOldApi();
    void shareLimitActionSupportFollowsTheApiVersion();
    void addUrlsSendsStoppedAndPausedAndOptions();
    void a500OnActionIsNotRetried();
    void a409OnActionMapsToConflict();
    void setPreferencesSendsOnlyTheChangedKeysAsJson();
    void removeTrackersSendsTheHashAndPipedUrls();
};

void TestQbtClientActionsFake::removeTrackersSendsTheHashAndPipedUrls()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsRemoveTrackers(QStringLiteral("h1"),
                                      QStringList{QStringLiteral("udp://a.example:1337/announce"),
                                                  QStringLiteral("https://b.example/announce")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/removeTrackers"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hash"), QUrl::FullyDecoded), QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             QStringLiteral("udp://a.example:1337/announce|https://b.example/announce"));
}

void TestQbtClientActionsFake::setPreferencesSendsOnlyTheChangedKeysAsJson()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this,
              client.appSetPreferences(QJsonObject{{QStringLiteral("dl_limit"), 51200},
                                                   {QStringLiteral("alt_up_limit"), 0}}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).method, QByteArray("POST"));
    QVERIFY(server.requests.at(0).target.endsWith("/api/v2/app/setPreferences"));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    const QJsonObject sent =
        QJsonDocument::fromJson(
            body.queryItemValue(QStringLiteral("json"), QUrl::FullyDecoded).toUtf8())
            .object();
    QCOMPARE(sent.size(), 2);
    QCOMPARE(sent.value(QStringLiteral("dl_limit")).toInteger(), 51200);
    QCOMPARE(sent.value(QStringLiteral("alt_up_limit")).toInteger(), 0);
}

void TestQbtClientActionsFake::hashesAreJoinedWithPipe()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsStart(QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/start"));
    QCOMPARE(server.requests.at(0).header("Content-Type"),
             QByteArrayLiteral("application/x-www-form-urlencoded"));
    QCOMPARE(server.requests.at(0).body, QByteArrayLiteral("hashes=a%7Cb"));
}

void TestQbtClientActionsFake::tagsArePercentEncoded()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this,
              client.torrentsAddTags(QStringList{QStringLiteral("h1")},
                                     QStringList{QStringLiteral("a"), QStringLiteral("b")}));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/addTags"));
    // qBittorrent expects a comma-separated tag list; the comma is percent-encoded in an
    // x-www-form-urlencoded body.
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("tags"), QUrl::FullyDecoded),
             QStringLiteral("a,b"));
    QVERIFY(server.requests.at(0).body.contains("tags=a%2Cb"));
}

void TestQbtClientActionsFake::deleteFilesFlag()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsDelete(QStringList{QStringLiteral("h1")}, true));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/delete"));
    QCOMPARE(server.requests.at(0).body, QByteArrayLiteral("hashes=h1&deleteFiles=true"));
}

void TestQbtClientActionsFake::shareLimitsSendsAllFiveFields()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, -2, -2, -2));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("ratioLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("seedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));
    // 5.2.3 answers 400 without this field, so the default argument is Default, not "omit".
    QCOMPARE(body.queryItemValue(QStringLiteral("shareLimitAction"), QUrl::FullyDecoded),
             QStringLiteral("Default"));
}

void TestQbtClientActionsFake::shareLimitActionNames_data()
{
    QTest::addColumn<QbtClient::ShareLimitAction>("action");
    QTest::addColumn<QString>("name");

    // The qBittorrent 5.2 meta-enum names (T5.5 Q14), which torrents/info echoes back as
    // `share_limit_action`.
    QTest::newRow("default") << QbtClient::ShareLimitAction::Default << QStringLiteral("Default");
    QTest::newRow("stop") << QbtClient::ShareLimitAction::Stop << QStringLiteral("Stop");
    QTest::newRow("remove") << QbtClient::ShareLimitAction::Remove << QStringLiteral("Remove");
    QTest::newRow("remove with content")
        << QbtClient::ShareLimitAction::RemoveWithContent << QStringLiteral("RemoveWithContent");
    QTest::newRow("super seeding")
        << QbtClient::ShareLimitAction::EnableSuperSeeding << QStringLiteral("EnableSuperSeeding");
}

void TestQbtClientActionsFake::shareLimitActionNames()
{
    QFETCH(QbtClient::ShareLimitAction, action);
    QFETCH(QString, name);

    QCOMPARE(QbtClient::shareLimitActionName(action), name);
    QCOMPARE(QbtClient::shareLimitActionFromName(name), action);

    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });
    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result = await(
        this, client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, 2, 60, -1, action));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("shareLimitAction"), QUrl::FullyDecoded), name);
    QCOMPARE(body.queryItemValue(QStringLiteral("ratioLimit"), QUrl::FullyDecoded),
             QStringLiteral("2"));
    QCOMPARE(body.queryItemValue(QStringLiteral("seedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("60"));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-1"));
}

void TestQbtClientActionsFake::shareLimitActionIsOmittedForAnOldApi()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", ""); });
    QbtClient client;
    client.setBaseUrl(server.url());

    // A caller talking to a pre-2.15.1 server passes nullopt: the field is left off
    // entirely (not sent empty), the four limits still travel.
    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsSetShareLimits(QStringList{QStringLiteral("h1")}, -2, -2, -2, std::nullopt));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QVERIFY(!body.hasQueryItem(QStringLiteral("shareLimitAction")));
    QCOMPARE(body.queryItemValue(QStringLiteral("hashes"), QUrl::FullyDecoded),
             QStringLiteral("h1"));
    QCOMPARE(body.queryItemValue(QStringLiteral("inactiveSeedingTimeLimit"), QUrl::FullyDecoded),
             QStringLiteral("-2"));

    // Unknown or empty names read as Default rather than failing.
    QCOMPARE(QbtClient::shareLimitActionFromName(QString()), QbtClient::ShareLimitAction::Default);
    QCOMPARE(QbtClient::shareLimitActionFromName(QStringLiteral("DeleteFiles")),
             QbtClient::ShareLimitAction::Default);
}

void TestQbtClientActionsFake::shareLimitActionSupportFollowsTheApiVersion()
{
    QVERIFY(QbtClient::supportsShareLimitAction(QStringLiteral("2.15.1")));
    QVERIFY(QbtClient::supportsShareLimitAction(QStringLiteral("2.16.0")));
    QVERIFY(QbtClient::supportsShareLimitAction(QStringLiteral("3.0")));
    QVERIFY(!QbtClient::supportsShareLimitAction(QStringLiteral("2.15.0")));
    QVERIFY(!QbtClient::supportsShareLimitAction(QStringLiteral("2.11.4")));
    QVERIFY(!QbtClient::supportsShareLimitAction(QString()));
    QVERIFY(!QbtClient::supportsShareLimitAction(QStringLiteral("unknown")));
}

void TestQbtClientActionsFake::addUrlsSendsStoppedAndPausedAndOptions()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(200, "OK", R"({"added_torrent_ids":[]})");
    });

    QbtClient client;
    client.setBaseUrl(server.url());

    QbtClient::AddOptions options;
    options.category = QStringLiteral("t25");
    options.tags = {QStringLiteral("a"), QStringLiteral("b")};
    options.savepath = QStringLiteral("/tmp/t25");
    options.stopped = true;
    options.sequentialDownload = true;
    options.rename = QStringLiteral("renamed");
    options.contentLayout = QbtClient::ContentLayout::Subfolder;

    const QbtResult<QByteArray> result = await(
        this,
        client.torrentsAddUrls(QStringList{QStringLiteral("magnet:?xt=urn:btih:abc")}, options));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.at(0).target, QByteArrayLiteral("/api/v2/torrents/add"));

    const QUrlQuery body(QString::fromUtf8(server.requests.at(0).body));
    QCOMPARE(body.queryItemValue(QStringLiteral("urls"), QUrl::FullyDecoded),
             QStringLiteral("magnet:?xt=urn:btih:abc"));
    QCOMPARE(body.queryItemValue(QStringLiteral("category"), QUrl::FullyDecoded),
             QStringLiteral("t25"));
    QCOMPARE(body.queryItemValue(QStringLiteral("tags"), QUrl::FullyDecoded),
             QStringLiteral("a,b"));
    QCOMPARE(body.queryItemValue(QStringLiteral("savepath"), QUrl::FullyDecoded),
             QStringLiteral("/tmp/t25"));
    QCOMPARE(body.queryItemValue(QStringLiteral("stopped"), QUrl::FullyDecoded),
             QStringLiteral("true"));
    // Sent alongside `stopped` so a pre-5.0 server (which only knows `paused`) also works.
    QCOMPARE(body.queryItemValue(QStringLiteral("paused"), QUrl::FullyDecoded),
             QStringLiteral("true"));
    QCOMPARE(body.queryItemValue(QStringLiteral("sequentialDownload"), QUrl::FullyDecoded),
             QStringLiteral("true"));
    QCOMPARE(body.queryItemValue(QStringLiteral("rename"), QUrl::FullyDecoded),
             QStringLiteral("renamed"));
    QCOMPARE(body.queryItemValue(QStringLiteral("contentLayout"), QUrl::FullyDecoded),
             QStringLiteral("Subfolder"));
}

void TestQbtClientActionsFake::a500OnActionIsNotRetried()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(500, "Internal Server Error", "boom"); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setRetryBackoff(std::chrono::milliseconds(20));

    const QbtResult<QByteArray> result =
        await(this, client.torrentsStop(QStringList{QStringLiteral("h1")}));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Server);
    QCOMPARE(server.requests.size(), 1); // a mutating POST is never retried
}

void TestQbtClientActionsFake::a409OnActionMapsToConflict()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(409, "Conflict", "Conflict"); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QByteArray> result =
        await(this, client.torrentsRecheck(QStringList{QStringLiteral("h1")}));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(result.error().httpStatus(), 409);
    QCOMPARE(server.requests.size(), 1);
}

QTEST_MAIN(TestQbtClientActionsFake)

#include "tst_qbtclient_actions_fake.moc"
