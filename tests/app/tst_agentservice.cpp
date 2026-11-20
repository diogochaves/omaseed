// DECISIONS C45 V3, V7, V8, V9 (#163): AgentService, the app's side of the Agent interface,
// against a small stand-in qBittorrent (tst_statusservice's kind): status read from App's
// models, the window's state followed, no credential in any answer (not the password, the user
// name, the API key, nor a tracker's passkey), and errors: the log read first, the server
// omaseed is on only, a first sync and a server down.

#include "core/AgentWords.hpp"
#include "core/GlobalState.hpp"
#include "core/ProblemLog.hpp"
#include "core/ServerListModel.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"

#include <QElapsedTimer>
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
/// torrent whose tracker carries a passkey, and a log.
class FakeQbt : public QTcpServer
{
public:
    bool refuseLogin = false;
    bool stall = false; ///< maindata never answers: the first sync never ends
    int polls = 0;
    QJsonArray log; ///< log/main's lines; each read gets those after its last_known_id
    int logReads = 0;

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
                ++logReads;
                const QByteArray key = "last_known_id=";
                const qsizetype at = target.indexOf(key);
                const int from =
                    at < 0 ? -1 : target.mid(at + key.size()).split('&').value(0).toInt();
                QJsonArray after;
                for (const QJsonValue &value : std::as_const(log))
                    if (value.toObject().value(QStringLiteral("id")).toInt() > from)
                        after << value;
                body = QJsonDocument(after).toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                ++polls;
                if (stall)
                    return; // never answered, left open

                const QString tracker = QStringLiteral("https://tracker.example/%1/announce")
                                            .arg(QLatin1String(kPasskey));
                const QJsonObject torrent{
                    {QStringLiteral("name"), QStringLiteral("Lantern Festival")},
                    {QStringLiteral("state"), QStringLiteral("downloading")},
                    {QStringLiteral("progress"), 0.5},
                    {QStringLiteral("dlspeed"), 4096},
                    {QStringLiteral("tracker"), tracker},
                    {QStringLiteral("save_path"), QStringLiteral("/data/incoming")},
                    {QStringLiteral("added_on"), 1700000000}};
                body =
                    QJsonDocument(QJsonObject{{QStringLiteral("rid"), polls},
                                              // Only the first poll is a full update, as
                                              // qBittorrent's: each one resets ProblemLog.
                                              {QStringLiteral("full_update"), polls == 1},
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

    void errorsReadsTheLogFirst();
    void errorsAnotherServerIsNotCurrent();
    void unknownServerExits2();
    void firstSync();
    void serverProblemComesFirst();

    void logOnTransmissionIsNoLog();
    void logUnreachable();
    void unknownTorrent();
    void ambiguousPrefix();

private:
    /// log's answer, with `options`.
    static QJsonObject log(AgentService &service, const QString &options = QString());
    static QString codeOf(const QJsonObject &answer);
    /// errors' answer, with `options`.
    static QJsonObject errors(AgentService &service, const QString &options = QString());
    /// An app on `server`, polling fast, synced once.
    static void startOn(App &app);

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

QJsonObject TestAgentService::errors(AgentService &service, const QString &options)
{
    return answer([&](AgentService::Reply reply) { service.errors(options, reply); });
}

void TestAgentService::startOn(App &app)
{
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
}

// C45 V9: a move that failed while the window wasn't reading is in the answer, because errors
// reads the log before it answers; the torrent comes as a failure, with its cause and its line.
void TestAgentService::errorsReadsTheLogFirst()
{
    FakeQbt server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(
        app.engine()->globalState()->connected() && app.engine()->torrents()->count() == 1, 5000);
    // The first poll's read (C2) is in; the next is 15 s away.
    QTRY_VERIFY_WITH_TIMEOUT(server.logReads >= 1, 5000);
    QTest::qWait(300);
    const QJsonObject before = errors(service);
    QCOMPARE(before.value(QLatin1String("torrents")).toArray(), QJsonArray());

    server.log = {QJsonObject{
        {QStringLiteral("id"), 7},
        {QStringLiteral("timestamp"), 1792075569},
        {QStringLiteral("type"), 4},
        {QStringLiteral("message"),
         QStringLiteral("Failed to move torrent. Torrent: \"Lantern Festival\". Source: "
                        "\"/data/incoming\". Destination: \"/mnt/fast/games\". Reason: \"3f9a1c "
                        "storage move failed. file_rename (/data/incoming/Lantern Festival): "
                        "Permission denied\"")}}};
    const int reads = server.logReads;
    const QJsonObject object = errors(service);
    QVERIFY(server.logReads > reads);
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("errors"));
    const QJsonArray torrents = object.value(QLatin1String("torrents")).toArray();
    QCOMPARE(torrents.size(), 1);
    const QJsonObject t = torrents.at(0).toObject();
    QCOMPARE(t.value(QLatin1String("hash")).toString(), QLatin1String(kHash));
    QCOMPARE(t.value(QLatin1String("kind")).toString(), QStringLiteral("failure"));
    QCOMPARE(t.value(QLatin1String("cause")).toString(), QStringLiteral("noAccess"));
    QCOMPARE(t.value(QLatin1String("state")).toString(), QStringLiteral("downloading"));
    QCOMPARE(t.value(QLatin1String("savePath")).toString(), QStringLiteral("/data/incoming"));
    QVERIFY(!t.value(QLatin1String("fact")).toString().isEmpty());
    QCOMPARE(t.value(QLatin1String("since")).toString(), QStringLiteral("2026-10-15T14:46:09Z"));
    const QJsonArray log = t.value(QLatin1String("log")).toArray();
    QCOMPARE(log.size(), 1);
    QCOMPARE(log.at(0).toObject().value(QLatin1String("id")).toInt(), 7);
    QCOMPARE(object.value(QLatin1String("dismissed")).toInt(), 0);

    // Dismissed in the window: counted, not listed.
    app.engine()->problems()->dismiss(QLatin1String(kHash));
    const QJsonObject after = errors(service);
    QCOMPARE(after.value(QLatin1String("torrents")).toArray(), QJsonArray());
    QCOMPARE(after.value(QLatin1String("dismissed")).toInt(), 1);
}

// C45 V6: errors reads only the server omaseed is on.
void TestAgentService::errorsAnotherServerIsNotCurrent()
{
    FakeQbt server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(app.engine()->globalState()->connected(), 5000);
    const QString other =
        app.serverList()->add({{QStringLiteral("name"), QStringLiteral("seedbox")},
                               {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
                               {QStringLiteral("authMode"), QStringLiteral("none")}});
    QVERIFY(!other.isEmpty());
    const QJsonObject object = errors(service, QStringLiteral(R"({"server":"SEEDBOX"})"));
    const QJsonObject error = object.value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("not-current"));
    QVERIFY2(error.value(QLatin1String("message"))
                 .toString()
                 .endsWith(QLatin1String(", not seedbox. errors reads only the server omaseed is "
                                         "on.")),
             qPrintable(error.value(QLatin1String("message")).toString()));
    QCOMPARE(omaseed::core::AgentWords::exitFor(error.value(QLatin1String("code")).toString()), 4);
}

void TestAgentService::unknownServerExits2()
{
    App app;
    AgentService service(&app);
    const QJsonObject object = errors(service, QStringLiteral(R"({"server":"nowhere"})"));
    const QJsonObject error = object.value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("unknown-server"));
    QCOMPARE(error.value(QLatin1String("message")).toString(),
             QStringLiteral("No server called nowhere. omaseed status lists them."));
    QCOMPARE(omaseed::core::AgentWords::exitFor(error.value(QLatin1String("code")).toString()), 2);

    // An option errors doesn't take is usage.
    const QJsonObject usage = errors(service, QStringLiteral(R"({"lines":5})"));
    QCOMPARE(usage.value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
             QStringLiteral("usage"));
}

// Design 12: until the first full update there is nothing to say yet.
void TestAgentService::firstSync()
{
    FakeQbt server;
    server.stall = true;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(server.polls >= 1, 5000);
    const QJsonObject object = errors(service);
    const QJsonObject error = object.value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("first-sync"));
    QVERIFY(error.value(QLatin1String("message"))
                .toString()
                .endsWith(QLatin1String(" is still loading its torrents. Ask again in a moment.")));
    QCOMPARE(omaseed::core::AgentWords::exitFor(error.value(QLatin1String("code")).toString()), 4);
}

// C45 V9: a server that doesn't answer still answers errors (exit 0), its problem first, at
// once: no log read waited for.
void TestAgentService::serverProblemComesFirst()
{
    FakeQbt server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(app.engine()->globalState()->connected(), 5000);
    server.close();
    QTRY_VERIFY_WITH_TIMEOUT(!app.engine()->globalState()->connected(), 10000);

    QElapsedTimer timer;
    timer.start();
    QString raw;
    const QJsonObject object =
        answer([&](AgentService::Reply reply) { service.errors(QString(), reply); }, &raw);
    QVERIFY(timer.elapsed() < 1000);
    QVERIFY2(!object.contains(QLatin1String("error")), qPrintable(raw));
    const QJsonObject s = object.value(QLatin1String("server")).toObject();
    QCOMPARE(s.value(QLatin1String("reachable")).toBool(), false);
    QVERIFY2(!s.value(QLatin1String("problem"))
                  .toObject()
                  .value(QLatin1String("headline"))
                  .toString()
                  .isEmpty(),
             qPrintable(raw));
    QVERIFY(object.value(QLatin1String("torrents")).isArray());
}

QJsonObject TestAgentService::log(AgentService &service, const QString &options)
{
    return answer([&](AgentService::Reply reply) { service.log(options, reply); });
}

QString TestAgentService::codeOf(const QJsonObject &answer)
{
    return answer.value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString();
}

// C45 V10: Transmission and Deluge keep no log omaseed reads; said before anything is tried.
void TestAgentService::logOnTransmissionIsNoLog()
{
    qputenv("OMASEED_SERVER_URL", "http://127.0.0.1:9");
    qputenv("OMASEED_SERVER_NAME", "seedbox");
    qputenv("OMASEED_SERVER_ENGINE", "transmission");
    App app;
    startOn(app);
    AgentService service(&app);
    const QJsonObject object = log(service);
    qunsetenv("OMASEED_SERVER_NAME");
    qunsetenv("OMASEED_SERVER_ENGINE");
    QCOMPARE(codeOf(object), QStringLiteral("no-log"));
    QCOMPARE(
        object.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(),
        QStringLiteral("seedbox runs Transmission, which keeps no log omaseed can read. "
                       "omaseed errors has each torrent's own error text."));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 4);
}

// C45 V10: a server down can't be read: its problem's line says why.
void TestAgentService::logUnreachable()
{
    FakeQbt server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(app.engine()->globalState()->connected(), 5000);
    server.close();
    QTRY_VERIFY_WITH_TIMEOUT(!app.engine()->globalState()->connected(), 10000);
    const QJsonObject object = log(service);
    QCOMPARE(codeOf(object), QStringLiteral("unreachable"));
    const QString message =
        object.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString();
    QVERIFY2(message.startsWith(QLatin1String("127.0.0.1 is unreachable, so its log can't be "
                                              "read: ")),
             qPrintable(message));
    QVERIFY2(message.size() > 50, qPrintable(message)); // the problem's own line follows
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 4);
}

// C45 V10: --torrent names one torrent of the library; a hash it has none of is a failure.
// A read that works: the log's lines, the torrent's own, and the next id.
void TestAgentService::unknownTorrent()
{
    FakeQbt server;
    server.log = {QJsonObject{{QStringLiteral("id"), 0},
                              {QStringLiteral("timestamp"), 1792075500},
                              {QStringLiteral("type"), 2},
                              {QStringLiteral("message"), QStringLiteral("qBittorrent started")}},
                  QJsonObject{{QStringLiteral("id"), 1},
                              {QStringLiteral("timestamp"), 1792075569},
                              {QStringLiteral("type"), 4},
                              {QStringLiteral("message"),
                               QStringLiteral("File error alert. Torrent: \"Lantern Festival\".")}},
                  QJsonObject{{QStringLiteral("id"), 2},
                              {QStringLiteral("timestamp"), 1792075600},
                              {QStringLiteral("type"), 4},
                              {QStringLiteral("message"),
                               QStringLiteral("UPnP/NAT-PMP: Port mapping failure.")}}};
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(
        app.engine()->globalState()->connected() && app.engine()->torrents()->count() == 1, 5000);

    const int reads = server.logReads;
    QJsonObject object = log(service);
    QVERIFY(server.logReads > reads);
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("log"));
    QCOMPARE(object.value(QLatin1String("matched")).toInt(), 2);
    QCOMPARE(object.value(QLatin1String("next")).toInt(), 3);
    const QJsonArray lines = object.value(QLatin1String("lines")).toArray();
    QCOMPARE(lines.size(), 2);
    QCOMPARE(lines.at(0).toObject().value(QLatin1String("torrent")).toString(),
             QLatin1String(kHash));
    QVERIFY(lines.at(1).toObject().value(QLatin1String("torrent")).isNull());

    object = log(service, QStringLiteral(R"({"torrent":"3f9a1c0e3f9a"})"));
    QCOMPARE(object.value(QLatin1String("matched")).toInt(), 1);

    object = log(service, QStringLiteral(R"({"torrent":"deadbeefcafe"})"));
    QCOMPARE(codeOf(object), QStringLiteral("unknown-torrent"));
    QCOMPARE(
        object.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(),
        QStringLiteral("No torrent deadbeefcafe on 127.0.0.1."));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 4);
}

void TestAgentService::ambiguousPrefix()
{
    FakeQbt server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    useServer(server);
    App app;
    startOn(app);
    AgentService service(&app);
    QTRY_VERIFY_WITH_TIMEOUT(
        app.engine()->globalState()->connected() && app.engine()->torrents()->count() == 1, 5000);
    const QJsonObject object = log(service, QStringLiteral(R"({"torrent":"3f9a"})"));
    QCOMPARE(codeOf(object), QStringLiteral("ambiguous"));
    QCOMPARE(
        object.value(QLatin1String("error")).toObject().value(QLatin1String("message")).toString(),
        QStringLiteral("3f9a matches 1 torrent on 127.0.0.1: give at least 8 characters of "
                       "the hash."));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 2);
}

QTEST_GUILESS_MAIN(TestAgentService)
#include "tst_agentservice.moc"
