// QbtClient against a minimal HTTP/1.1 server of our own, so error mapping, retries,
// back-off, the login retry and the queue limit are all observable without a real
// qBittorrent. The live behaviour is covered by tst_qbtclient_live.

#include "core/QbtClient.hpp"

#include <QElapsedTimer>
#include <QEventLoop>
#include <QPointer>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>
#include <QUrl>

#include <algorithm>
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
    qint64 atMs = 0;

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

/// Speaks just enough HTTP to answer QNetworkAccessManager. A handler returning an empty
/// response means "never answer", which is how the timeout is tested.
class FakeServer : public QTcpServer
{
public:
    using Handler = std::function<QByteArray(const Recorded &, int index)>;

    explicit FakeServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {
        m_clock.start();
        [this]() { QVERIFY(listen(QHostAddress::LocalHost)); }();
    }

    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    void setHandler(Handler handler) { m_handler = std::move(handler); }
    void setResponseDelay(int milliseconds) { m_delayMs = milliseconds; }

    QList<Recorded> requests;
    int maxInFlight = 0;

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
            recorded.atMs = m_clock.elapsed();
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
            ++m_inFlight;
            maxInFlight = std::max(maxInFlight, m_inFlight);

            const QByteArray response =
                m_handler ? m_handler(recorded, index) : httpResponse(200, "OK", "ok");
            if (response.isEmpty())
                return; // never answer: the client must time out

            const auto answer = [this, socket, response]() {
                --m_inFlight;
                socket->write(response);
                socket->flush();
                socket->disconnectFromHost();
            };
            if (m_delayMs > 0)
                QTimer::singleShot(m_delayMs, socket, answer);
            else
                answer();
        });
    }

private:
    Handler m_handler;
    QElapsedTimer m_clock;
    int m_delayMs = 0;
    int m_inFlight = 0;
};

/// Runs the event loop until the reply finishes, then hands back its result.
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

class TestQbtClientFake : public QObject
{
    Q_OBJECT

private slots:
    void refusedConnectionIsNetwork();
    void silentServerTimesOut();
    void retriesGetWithBackoff();
    void doesNotRetryPost();
    void logsInAndRetriesOnce();
    void sendsBearerAndCsrfHeaders();
    void mapsHttpStatus_data();
    void mapsHttpStatus();
    void badJsonIsParse();
    void limitsConcurrencyToFour();
    void cancelAllFinishesEveryReply();
    void abortDuringBackoffIsCancelledAndNeverResent();
    void newBaseUrlCancelsEverythingOutstanding();
    void rejectedLoginIsNotRepeated();
    void allowLoginAgainTriesOneMoreLogin();
    void cancelledLoginLeavesTheAuthStateAlone();
    void multipartAddLogsInAgainAfterAnExpiredSession();
    void probeAbortFinishesCancelled();
    void originLeavesOutUserInfo();
    void maxRetriesIsCapped();
};

void TestQbtClientFake::refusedConnectionIsNetwork()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    } // closed again: nothing listens on that port now

    QbtClient client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
    client.setMaxRetries(0);

    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Network);
    QVERIFY(result.error().message().contains(QStringLiteral("app/version")));
}

void TestQbtClientFake::silentServerTimesOut()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return QByteArray(); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setTimeout(std::chrono::milliseconds(400));
    client.setMaxRetries(0);

    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Timeout);
    QCOMPARE(server.requests.size(), 1);
}

void TestQbtClientFake::retriesGetWithBackoff()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int index) {
        if (index < 2)
            return httpResponse(500, "Internal Server Error", "boom");
        return httpResponse(200, "OK", "v5.0.0");
    });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setRetryBackoff(std::chrono::milliseconds(60)); // 60 + 120 instead of 500 + 1000
    client.setMaxRetries(3);

    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY(!result.isError());
    QCOMPARE(result.value(), QStringLiteral("v5.0.0"));
    QCOMPARE(server.requests.size(), 3);
    // The back-off doubles, so the gaps grow: at least 60 ms then at least 120 ms.
    const qint64 firstGap = server.requests.at(1).atMs - server.requests.at(0).atMs;
    const qint64 secondGap = server.requests.at(2).atMs - server.requests.at(1).atMs;
    QVERIFY2(firstGap >= 55, qPrintable(QStringLiteral("first gap %1 ms").arg(firstGap)));
    QVERIFY2(secondGap >= 115, qPrintable(QStringLiteral("second gap %1 ms").arg(secondGap)));
    QVERIFY(secondGap > firstGap);
}

void TestQbtClientFake::doesNotRetryPost()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(500, "Internal Server Error", "boom"); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setRetryBackoff(std::chrono::milliseconds(20));

    const QbtResult<QByteArray> result =
        await(this, client.post(QStringLiteral("torrents/delete")));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Server);
    QCOMPARE(server.requests.size(), 1); // a mutating POST is never sent twice
    QCOMPARE(server.requests.at(0).method, QByteArrayLiteral("POST"));
}

void TestQbtClientFake::logsInAndRetriesOnce()
{
    FakeServer server;
    server.setHandler([](const Recorded &recorded, int) -> QByteArray {
        if (recorded.target.contains("auth/login")) {
            if (!recorded.body.contains("password=secret%20pass"))
                return httpResponse(401, "Unauthorized", "Unauthorized");
            return httpResponse(
                200, "OK", "Ok.", {QByteArrayLiteral("Set-Cookie: SID=abc123; path=/")});
        }
        if (recorded.header("Cookie").isEmpty())
            return httpResponse(403, "Forbidden", "Forbidden");
        return httpResponse(200, "OK", "v5.0.0");
    });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setCredentials(QStringLiteral("admin"), QStringLiteral("secret pass"));
    QSignalSpy authStates(&client, &QbtClient::authStateChanged);

    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(result.value(), QStringLiteral("v5.0.0"));
    QCOMPARE(server.requests.size(), 3);
    QCOMPARE(server.requests.at(0).method, QByteArrayLiteral("GET"));
    QCOMPARE(server.requests.at(1).method, QByteArrayLiteral("POST"));
    QVERIFY(server.requests.at(1).target.contains("auth/login"));
    QCOMPARE(server.requests.at(2).method, QByteArrayLiteral("GET"));
    QVERIFY(server.requests.at(2).header("Cookie").contains("SID=abc123"));
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
    QCOMPARE(authStates.size(), 1);

    // A second call reuses the cookie: no further login.
    const QbtResult<QString> again = await(this, client.appVersion());
    QVERIFY(!again.isError());
    QCOMPARE(server.requests.size(), 4);
}

void TestQbtClientFake::sendsBearerAndCsrfHeaders()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", "v5.0.0"); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setApiKey(QStringLiteral("fake-key-for-tests"));

    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY(!result.isError());
    QCOMPARE(server.requests.size(), 1);
    const Recorded &recorded = server.requests.at(0);
    QCOMPARE(recorded.header("Authorization"), QByteArrayLiteral("Bearer fake-key-for-tests"));
    QCOMPARE(recorded.header("Referer"), server.url().toString().toUtf8());
    QCOMPARE(recorded.header("Origin"), server.url().toString().toUtf8());
    QVERIFY(recorded.header("User-Agent").startsWith("omaseed/"));
    QCOMPARE(recorded.target, QByteArrayLiteral("/api/v2/app/version"));
}

void TestQbtClientFake::mapsHttpStatus_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<ApiError::Kind>("kind");

    QTest::newRow("not found") << 404 << ApiError::Kind::NotFound;
    QTest::newRow("conflict") << 409 << ApiError::Kind::Conflict;
    QTest::newRow("forbidden") << 403 << ApiError::Kind::Forbidden;
    QTest::newRow("unauthorized") << 401 << ApiError::Kind::Unauthorized;
}

void TestQbtClientFake::mapsHttpStatus()
{
    QFETCH(int, status);
    QFETCH(ApiError::Kind, kind);

    FakeServer server;
    server.setHandler(
        [status](const Recorded &, int) { return httpResponse(status, "Nope", "nope"); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setMaxRetries(0);

    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), kind);
    QCOMPARE(result.error().httpStatus(), status);
    QVERIFY(result.error().message().contains(QStringLiteral("app/version")));
    // Without a credential, an auth failure is reported, never retried.
    QCOMPARE(server.requests.size(), 1);
}

void TestQbtClientFake::badJsonIsParse()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(200, "OK", "{not json at all"); });

    QbtClient client;
    client.setBaseUrl(server.url());

    const QbtResult<QJsonObject> result = await(this, client.syncMainData(0));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Parse);
    QVERIFY(result.error().message().contains(QStringLiteral("sync/maindata")));
}

void TestQbtClientFake::limitsConcurrencyToFour()
{
    FakeServer server;
    server.setResponseDelay(120);
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", "v5.0.0"); });

    QbtClient client;
    client.setBaseUrl(server.url());

    constexpr int total = 10;
    int finished = 0;
    QEventLoop loop;
    for (int i = 0; i < total; ++i) {
        client.appVersion()->then(this, [&](const QbtResult<QString> &result) {
            QVERIFY(!result.isError());
            if (++finished == total)
                loop.quit();
        });
    }
    QTimer::singleShot(10000, &loop, [&loop]() { loop.quit(); });
    loop.exec();

    QCOMPARE(finished, total);
    QCOMPARE(server.requests.size(), total);
    QCOMPARE(server.maxInFlight, client.maxConcurrentRequests());
}

void TestQbtClientFake::cancelAllFinishesEveryReply()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return QByteArray(); }); // never answers

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setTimeout(std::chrono::milliseconds(5000));

    QList<ApiError> errors;
    for (int i = 0; i < 6; ++i) {
        client.appVersion()->then(
            this, [&errors](const QbtResult<QString> &result) { errors.append(result.error()); });
    }
    QTest::qWait(100);
    client.cancelAll();
    QTRY_COMPARE(errors.size(), 6);
    for (const ApiError &error : errors)
        QCOMPARE(error.kind(), ApiError::Kind::Cancelled);
}

void TestQbtClientFake::abortDuringBackoffIsCancelledAndNeverResent()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(500, "Internal Server Error", "boom"); });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setRetryBackoff(std::chrono::milliseconds(300));
    client.setMaxRetries(3);

    QList<ApiError> errors;
    auto *reply = client.appVersion();
    QPointer<QbtReply<QString>> guard(reply);
    reply->then(this,
                [&errors](const QbtResult<QString> &result) { errors.append(result.error()); });
    QTRY_COMPARE(server.requests.size(), 1);
    QTest::qWait(50); // the 500 is in; the job now waits out its back-off
    QVERIFY(errors.isEmpty());
    QVERIFY(!guard.isNull());

    guard->abort();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().kind(), ApiError::Kind::Cancelled);
    QTest::qWait(500); // well past the back-off
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(errors.size(), 1);
}

// APP-1 / NET-3: a server switch reuses one client. Nothing queued, in flight or backing off
// for the old server may finish normally or reach the new one.
void TestQbtClientFake::newBaseUrlCancelsEverythingOutstanding()
{
    FakeServer oldServer;
    oldServer.setHandler([](const Recorded &recorded, int) -> QByteArray {
        if (recorded.method == "GET")
            return httpResponse(500, "Internal Server Error", "boom"); // into back-off
        return QByteArray();                                           // the POST hangs
    });
    FakeServer newServer;

    QbtClient client;
    client.setBaseUrl(oldServer.url());
    client.setRetryBackoff(std::chrono::milliseconds(200));
    client.setMaxRetries(3);
    client.setMaxConcurrentRequests(1);

    QList<ApiError> errors;
    const auto record = [&errors](const auto &result) { errors.append(result.error()); };
    client.appVersion()->then(this, record);                            // backs off
    client.post(QStringLiteral("torrents/delete"))->then(this, record); // in flight
    client.get(QStringLiteral("torrents/files"))->then(this, record);   // queued
    QTRY_COMPARE(oldServer.requests.size(), 2);
    QTest::qWait(30);
    QVERIFY(errors.isEmpty());

    client.setBaseUrl(newServer.url());
    QTRY_COMPARE(errors.size(), 3);
    for (const ApiError &error : std::as_const(errors))
        QCOMPARE(error.kind(), ApiError::Kind::Cancelled);

    QTest::qWait(500);
    QCOMPARE(oldServer.requests.size(), 2);
    QCOMPARE(newServer.requests.size(), 0);
    QCOMPARE(errors.size(), 3);
}

// NET-5: qBittorrent bans the IP after a few failed logins, so a rejected password must not
// be tried again on every later request.
void TestQbtClientFake::rejectedLoginIsNotRepeated()
{
    FakeServer server;
    server.setHandler([](const Recorded &recorded, int) -> QByteArray {
        if (recorded.target.contains("auth/login")) {
            if (recorded.body.contains("password=right"))
                return httpResponse(
                    200, "OK", "Ok.", {QByteArrayLiteral("Set-Cookie: SID=ok1; path=/")});
            return httpResponse(401, "Unauthorized", "Unauthorized");
        }
        if (recorded.header("Cookie").isEmpty())
            return httpResponse(403, "Forbidden", "Forbidden");
        return httpResponse(200, "OK", "v5.0.0");
    });
    const auto logins = [&server]() {
        return std::count_if(server.requests.cbegin(),
                             server.requests.cend(),
                             [](const Recorded &r) { return r.target.contains("auth/login"); });
    };

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setMaxRetries(0);
    client.setCredentials(QStringLiteral("admin"), QStringLiteral("wrong"));

    for (int i = 0; i < 4; ++i) {
        const QbtResult<QString> result = await(this, client.appVersion());
        QVERIFY(result.isError());
        // VER-1: the 403s after the refused login report the login's verdict, not Forbidden.
        QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    }
    QCOMPARE(logins(), 1);
    QCOMPARE(client.authState(), QbtClient::AuthState::NeedsCredential);

    // New credentials lift the latch: one fresh login, which works.
    client.setCredentials(QStringLiteral("admin"), QStringLiteral("right"));
    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(logins(), 2);
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
}

// REG-2: a user's retry lifts the latch for one login; a refusal latches it again.
void TestQbtClientFake::allowLoginAgainTriesOneMoreLogin()
{
    bool banned = true;
    FakeServer server;
    server.setHandler([&banned](const Recorded &recorded, int) -> QByteArray {
        if (recorded.target.contains("auth/login")) {
            if (!banned)
                return httpResponse(
                    200, "OK", "Ok.", {QByteArrayLiteral("Set-Cookie: SID=ok1; path=/")});
            return httpResponse(403, "Forbidden", "Forbidden");
        }
        if (recorded.header("Cookie").isEmpty())
            return httpResponse(403, "Forbidden", "Forbidden");
        return httpResponse(200, "OK", "v5.0.0");
    });
    const auto logins = [&server]() {
        return std::count_if(server.requests.cbegin(),
                             server.requests.cend(),
                             [](const Recorded &r) { return r.target.contains("auth/login"); });
    };

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setMaxRetries(0);
    client.setCredentials(QStringLiteral("admin"), QStringLiteral("secret"));

    QVERIFY(await(this, client.appVersion()).isError());
    const QbtResult<QString> latched = await(this, client.appVersion());
    QVERIFY(latched.isError());
    // A banned login (403) stays Forbidden while latched.
    QCOMPARE(latched.error().kind(), ApiError::Kind::Forbidden);
    QCOMPARE(client.authState(), QbtClient::AuthState::Forbidden);
    QCOMPARE(logins(), 1);

    // Still banned: the retry logs in once, is refused, and latches again.
    client.allowLoginAgain();
    QVERIFY(await(this, client.appVersion()).isError());
    QVERIFY(await(this, client.appVersion()).isError());
    QCOMPARE(logins(), 2);

    // The ban has expired: the next retry gets in.
    banned = false;
    client.allowLoginAgain();
    const QbtResult<QString> result = await(this, client.appVersion());
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(logins(), 3);
    QCOMPARE(client.authState(), QbtClient::AuthState::Ok);
}

void TestQbtClientFake::cancelledLoginLeavesTheAuthStateAlone()
{
    FakeServer server;
    server.setHandler([](const Recorded &recorded, int) -> QByteArray {
        if (recorded.target.contains("auth/login"))
            return QByteArray(); // the login never answers
        return httpResponse(403, "Forbidden", "Forbidden");
    });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setCredentials(QStringLiteral("admin"), QStringLiteral("pw"));
    QSignalSpy authStates(&client, &QbtClient::authStateChanged);

    QList<ApiError> errors;
    client.appVersion()->then(
        this, [&errors](const QbtResult<QString> &result) { errors.append(result.error()); });
    QTRY_COMPARE(server.requests.size(), 2); // the GET, then the login
    client.cancelAll();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().kind(), ApiError::Kind::Cancelled);
    QCOMPARE(authStates.size(), 0);
    QCOMPARE(client.authState(), QbtClient::AuthState::Unknown);
}

// NET-12 / NET-13: an expired SID on "add .torrent" logs in and resends the multipart body,
// and the file name in the part header cannot break it.
void TestQbtClientFake::multipartAddLogsInAgainAfterAnExpiredSession()
{
    FakeServer server;
    server.setHandler([](const Recorded &recorded, int) -> QByteArray {
        if (recorded.target.contains("auth/login"))
            return httpResponse(
                200, "OK", "Ok.", {QByteArrayLiteral("Set-Cookie: SID=fresh; path=/")});
        if (recorded.header("Cookie").isEmpty())
            return httpResponse(403, "Forbidden", "Forbidden");
        return httpResponse(200, "OK", "Ok.");
    });

    QbtClient client;
    client.setBaseUrl(server.url());
    client.setCredentials(QStringLiteral("admin"), QStringLiteral("pw"));

    const QByteArray bytes = QByteArrayLiteral("d4:infod4:name4:demoee");
    QbtClient::AddOptions options;
    options.category = QStringLiteral("movies");
    const QbtResult<QByteArray> result = await(
        this, client.torrentsAddFile(QStringLiteral("we\"ird\r\nname é.torrent"), bytes, options));
    QVERIFY2(!result.isError(), qPrintable(result.error().toString()));
    QCOMPARE(server.requests.size(), 3);
    QVERIFY(server.requests.at(1).target.contains("auth/login"));
    const Recorded &resent = server.requests.at(2);
    QVERIFY(resent.target.contains("torrents/add"));
    QVERIFY(resent.body.contains(bytes));
    QVERIFY(resent.body.contains("movies"));
    QVERIFY(resent.body.contains("filename=\"we_ird__name _.torrent\""));
    // The first attempt carried the same body; the retry rebuilt it rather than reusing it.
    QVERIFY(server.requests.at(0).body.contains(bytes));
}

void TestQbtClientFake::probeAbortFinishesCancelled()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return QByteArray(); }); // never answers

    QbtClient client;
    client.setBaseUrl(server.url());

    QList<ApiError> errors;
    auto *reply = client.probe();
    reply->then(this, [&errors](const auto &result) { errors.append(result.error()); });
    QTRY_COMPARE(server.requests.size(), 2);
    reply->abort();
    QTRY_COMPARE(errors.size(), 1);
    QCOMPARE(errors.first().kind(), ApiError::Kind::Cancelled);
}

void TestQbtClientFake::originLeavesOutUserInfo()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) { return httpResponse(200, "OK", "v5.0.0"); });

    QUrl withUser = server.url();
    withUser.setUserInfo(QStringLiteral("admin:secret"));
    QbtClient client;
    client.setBaseUrl(withUser);

    QVERIFY(!await(this, client.appVersion()).isError());
    const Recorded &recorded = server.requests.at(0);
    QCOMPARE(recorded.header("Origin"), server.url().toString().toUtf8());
    QCOMPARE(recorded.header("Referer"), server.url().toString().toUtf8());
    QVERIFY(!recorded.header("Origin").contains("secret"));
}

void TestQbtClientFake::maxRetriesIsCapped()
{
    QbtClient client;
    client.setMaxRetries(1000); // 1 << 999 would be undefined
    QCOMPARE(client.maxRetries(), 10);
    client.setMaxRetries(-3);
    QCOMPARE(client.maxRetries(), 0);
}

QTEST_MAIN(TestQbtClientFake)

#include "tst_qbtclient_fake.moc"
