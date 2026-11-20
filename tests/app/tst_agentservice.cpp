// DECISIONS C45 V3, V7, V8 (#163): AgentService, the app's side of the Agent interface, against
// a small stand-in qBittorrent (tst_statusservice's kind): status read from App's models, the
// window's state followed, and no credential in any answer: not the password, the user name,
// the API key, nor a tracker's passkey.

#include "core/GlobalState.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <functional>
#include <memory>

using omaseed::app::AgentService;
using omaseed::app::App;

namespace {

const char *kUser = "attic-owner";
const char *kPassword = "hunter2-correct-horse";
const char *kApiKey = "qbt_0123456789abcdefSECRETKEY";
const char *kPasskey = "f00dfacecafebeef1234";
const char *kHash = "3f9a1c0e3f9a1c0e3f9a1c0e3f9a1c0e3f9a1c0e";

/// Just enough qBittorrent: a sign-in (or a refused one), the versions, one downloading
/// torrent whose tracker carries a passkey.
class FakeQbt : public QTcpServer
{
public:
    bool refuseLogin = false;
    int polls = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            qsizetype length = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            buffer->clear();

            QByteArray status = "200 OK";
            QByteArray headers;
            QByteArray body = "Ok.";
            if (target.startsWith("/api/v2/auth/login")) {
                if (refuseLogin)
                    body = "Fails.";
                else
                    headers = "Set-Cookie: SID=abc; path=/\r\n";
            } else if (refuseLogin) {
                // Never signed in: every other call is forbidden, as qBittorrent answers.
                status = "403 Forbidden";
                body = "Forbidden";
            } else if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.3";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = "2.15.1";
            } else if (target.startsWith("/api/v2/log/")) {
                body = "[]";
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                ++polls;
                const QString tracker = QStringLiteral("https://tracker.example/%1/announce")
                                            .arg(QLatin1String(kPasskey));
                const QJsonObject torrent{
                    {QStringLiteral("name"), QStringLiteral("Lantern Festival")},
                    {QStringLiteral("state"), QStringLiteral("downloading")},
                    {QStringLiteral("progress"), 0.5},
                    {QStringLiteral("dlspeed"), 4096},
                    {QStringLiteral("tracker"), tracker},
                    {QStringLiteral("added_on"), 1700000000}};
                body =
                    QJsonDocument(QJsonObject{{QStringLiteral("rid"), polls},
                                              {QStringLiteral("full_update"), true},
                                              {QStringLiteral("server_state"),
                                               QJsonObject{{QStringLiteral("dl_info_speed"), 4096},
                                                           {QStringLiteral("up_info_speed"), 512},
                                                           {QStringLiteral("connection_status"),
                                                            QStringLiteral("firewalled")}}},
                                              {QStringLiteral("torrents"),
                                               QJsonObject{{QLatin1String(kHash), torrent}}}})
                        .toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/app/preferences")) {
                body = "{}";
            } else if (target.startsWith("/api/v2/torrents/trackers")) {
                body = QStringLiteral(R"([{"url":"https://tracker.example/%1/announce"}])")
                           .arg(QLatin1String(kPasskey))
                           .toUtf8();
            }
            socket->write("HTTP/1.1 " + status +
                          "\r\nContent-Length: " + QByteArray::number(body.size()) + "\r\n" +
                          headers + "Connection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

/// Runs one read and returns its JSON object.
QJsonObject answer(const std::function<void(AgentService::Reply)> &start, QString *raw = nullptr)
{
    QString json;
    bool done = false;
    start([&](const QString &reply) {
        json = reply;
        done = true;
    });
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(done, 5000); }();
    if (raw)
        *raw = json;
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

} // namespace

class TestAgentService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void statusFromTheModels();
    void windowShownFollows();
    void optionsStatusDoesNotTake();
    void noCredentialInAnyAnswer_data();
    void noCredentialInAnyAnswer();

private:
    /// Points the app at `server` from the environment, with `credentials` set.
    void useServer(const FakeQbt &server);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAgentService::initTestCase()
{
    // Nothing here may reach the desktop's session bus (keychain, notices).
    QTemporaryDir dir;
    qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=/nonexistent/omaseed-no-bus"));
}

void TestAgentService::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    for (const char *name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
        qputenv(name, (m_home->path() + QLatin1Char('/') + QLatin1String(name)).toUtf8());
    for (const char *name :
         {"OMASEED_SERVER_URL", "OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
}

void TestAgentService::cleanup()
{
    for (const char *name :
         {"OMASEED_SERVER_URL", "OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
    m_home.reset();
}

void TestAgentService::useServer(const FakeQbt &server)
{
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
}

void TestAgentService::statusFromTheModels()
{
    FakeQbt server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(
        app.engine()->globalState()->connected() && app.engine()->torrents()->count() == 1, 5000);

    QString raw;
    const QJsonObject object = answer(
        [&](AgentService::Reply reply) { service.status(QStringLiteral("{}"), reply); }, &raw);
    QVERIFY2(raw.startsWith(QLatin1String("{\"v\":1,\"command\":\"status\",\"at\":\"")),
             qPrintable(raw));
    QVERIFY(object.value(QLatin1String("at")).toString().endsWith(QLatin1Char('Z')));
    QVERIFY(!object.value(QLatin1String("omaseed")).toString().isEmpty());
    const QJsonObject s = object.value(QLatin1String("server")).toObject();
    QCOMPARE(s.value(QLatin1String("engine")).toString(), QStringLiteral("qbittorrent"));
    QCOMPARE(s.value(QLatin1String("version")).toString(), QStringLiteral("5.2.3"));
    QCOMPARE(s.value(QLatin1String("address")).toString(),
             QStringLiteral("127.0.0.1:%1").arg(server.serverPort()));
    QCOMPARE(s.value(QLatin1String("reachable")).toBool(), true);
    QCOMPARE(s.value(QLatin1String("connection")).toString(), QStringLiteral("firewalled"));
    QVERIFY(s.value(QLatin1String("problem")).isNull());
    const QJsonObject speed = object.value(QLatin1String("speed")).toObject();
    QCOMPARE(speed.value(QLatin1String("down")).toInteger(), 4096);
    QCOMPARE(speed.value(QLatin1String("up")).toInteger(), 512);
    QCOMPARE(object.value(QLatin1String("alt")).toObject().value(QLatin1String("pick")).toString(),
             QStringLiteral("off"));
    const QJsonObject questions = object.value(QLatin1String("questions")).toObject();
    QCOMPARE(questions.value(QLatin1String("all")).toInt(), 1);
    QCOMPARE(questions.value(QLatin1String("arriving")).toInt(), 1);
    QCOMPARE(questions.value(QLatin1String("needsYou")).toInt(), 0);
    const QJsonObject moves = object.value(QLatin1String("moves")).toObject();
    QCOMPARE(moves.value(QLatin1String("running")).toInt(), 0);
    QVERIFY(moves.value(QLatin1String("now")).isNull());
    QCOMPARE(object.value(QLatin1String("paused")).toInt(), 0);
    QVERIFY(object.value(QLatin1String("servers")).isArray());

    // The server gone: still an answer (exit 0), unreachable with the window's words.
    server.close();
    QTRY_VERIFY_WITH_TIMEOUT(!app.engine()->globalState()->connected(), 10000);
    const QJsonObject down =
        answer([&](AgentService::Reply reply) { service.status(QString(), reply); });
    const QJsonObject downServer = down.value(QLatin1String("server")).toObject();
    QCOMPARE(downServer.value(QLatin1String("reachable")).toBool(), false);
    QCOMPARE(downServer.value(QLatin1String("connection")).toString(),
             QStringLiteral("disconnected"));
    const QJsonObject problem = downServer.value(QLatin1String("problem")).toObject();
    QVERIFY2(!problem.value(QLatin1String("headline")).toString().isEmpty(),
             qPrintable(QJsonDocument(down).toJson()));
    QVERIFY(!problem.value(QLatin1String("line")).toString().isEmpty());
    QVERIFY(!down.contains(QLatin1String("error")));
}

void TestAgentService::windowShownFollows()
{
    App app; // no server: status still answers
    AgentService service(&app);
    const auto window = [&]() {
        return answer([&](AgentService::Reply reply) { service.status(QString(), reply); })
            .value(QLatin1String("window"))
            .toString();
    };
    QCOMPARE(window(), QStringLiteral("hidden"));
    service.setWindowShown(true);
    QCOMPARE(window(), QStringLiteral("shown"));
    service.setWindowShown(false);
    QCOMPARE(window(), QStringLiteral("hidden"));
}

void TestAgentService::optionsStatusDoesNotTake()
{
    App app;
    AgentService service(&app);
    const QJsonObject object = answer([&](AgentService::Reply reply) {
        service.status(QStringLiteral(R"({"server":"attic"})"), reply);
    });
    QVERIFY(!object.value(QLatin1String("omaseed")).toString().isEmpty()); // the app made it
    const QJsonObject error = object.value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("usage"));
    QVERIFY(error.value(QLatin1String("message"))
                .toString()
                .startsWith(QLatin1String("omaseed: status doesn't take ")));
}

void TestAgentService::noCredentialInAnyAnswer_data()
{
    QTest::addColumn<QString>("auth");
    QTest::newRow("cookie") << QStringLiteral("cookie");
    QTest::newRow("api key") << QStringLiteral("apiKey");
    QTest::newRow("refused") << QStringLiteral("refused");
}

void TestAgentService::noCredentialInAnyAnswer()
{
    QFETCH(QString, auth);
    FakeQbt server;
    server.refuseLogin = auth == QLatin1String("refused");
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    if (auth == QLatin1String("apiKey")) {
        qputenv("OMASEED_API_KEY", kApiKey);
    } else {
        qputenv("OMASEED_USERNAME", kUser);
        qputenv("OMASEED_PASSWORD", kPassword);
    }
    App app;
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    AgentService service(&app);
    if (server.refuseLogin)
        QTRY_VERIFY_WITH_TIMEOUT(app.serverRefused(), 5000);
    else
        QTRY_VERIFY_WITH_TIMEOUT(app.engine()->torrents()->count() == 1, 5000);

    QString raw;
    answer([&](AgentService::Reply reply) { service.status(QStringLiteral("{}"), reply); }, &raw);
    QVERIFY(raw.startsWith(QLatin1String("{\"v\":1")));
    if (server.refuseLogin) // the refusal's words are there, and still no credential in them
        QVERIFY2(raw.contains(QLatin1String("\"problem\":{")), qPrintable(raw));
    for (const char *secret : {kUser, kPassword, kApiKey, kPasskey})
        QVERIFY2(!raw.contains(QLatin1String(secret)), qPrintable(raw));
}

QTEST_GUILESS_MAIN(TestAgentService)
#include "tst_agentservice.moc"
