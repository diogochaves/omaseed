// EngineRecognizer (#84; DECISIONS C25 K4–K8, K10): which engine answers at an address, asked
// in the order the address suggests, stopping at the first that answers as itself. Neither
// Transmission nor Deluge runs on this machine: FakeHttp answers as each would (Transmission's
// 409 with X-Transmission-Session-Id, its 401 with a Transmission realm, session_get;
// deluge-web's JSON-RPC auth.check_session, auth.login, web.connected, web.get_hosts,
// daemon.info), from the shapes in the engine matrix §1.1 and §4.1.

#include "FakeEngines.hpp"
#include "FakeHttp.hpp"
#include "core/engines/EngineKind.hpp"
#include "core/engines/EngineRecognizer.hpp"
#include "core/engines/deluge/DelugeFingerprint.hpp"
#include "core/engines/transmission/TransmissionFingerprint.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTest>

using namespace omaseed::core;
using fakehttp::FakeServer;
using fakehttp::httpResponse;
using fakehttp::Recorded;
using namespace fakeengines;

namespace {

Recognition recognise(const QUrl &url,
                      EngineKind only = EngineKind::Unknown,
                      const QString &username = {},
                      const QString &password = {})
{
    EngineRecognizer recognizer;
    recognizer.setTimeout(std::chrono::seconds(5));
    recognizer.setCredentials(username, password);
    QSignalSpy finished(&recognizer, &EngineRecognizer::finished);
    recognizer.start(url, only);
    if (!finished.wait(8000))
        return {};
    return finished.first().first().value<Recognition>();
}

QUrl at(const FakeServer &server, const QString &path = {})
{
    QUrl url = server.url();
    url.setPath(path);
    return url;
}

} // namespace

class TestEngineRecognizer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theAddressPicksTheFirstEngine();
    void transmissionsRpcIsFoundFromAnyOfItsAddresses();
    void delugeWebsJsonIsFoundFromItsAddress();
    void qbittorrentAnswersAsItselfAndNothingElseIsAsked();
    void qbittorrentSignedOutIsStillQbittorrent();
    void transmissionAfterQbittorrentDidNotAnswer();
    void transmissionFirstForItsPath();
    void transmissionsLoginIsSentOnlyOnceItAsks();
    void transmissionRefusesAWrongLogin();
    void anOldTransmissionHasNoRpcVersion();
    void delugeSignsInAndNamesItsDaemon();
    void delugeWithoutADaemonListsItsHosts();
    void delugeRefusesAWrongPassword();
    void delugeWantsItsPassword();
    void nothingAnsweringAsAnEngineIsUnrecognised();
    void aClosedPortFailsAtOnce();
    void aPickedEngineIsTheOnlyOneAsked();
};

void TestEngineRecognizer::theAddressPicksTheFirstEngine()
{
    using Engines = QList<EngineKind>;
    const Engines qbt{EngineKind::Qbittorrent, EngineKind::Transmission, EngineKind::Deluge};
    const Engines tr{EngineKind::Transmission, EngineKind::Qbittorrent, EngineKind::Deluge};
    const Engines de{EngineKind::Deluge, EngineKind::Qbittorrent, EngineKind::Transmission};
    QCOMPARE(EngineRecognizer::order(QUrl(QStringLiteral("http://h:8080"))), qbt);
    QCOMPARE(EngineRecognizer::order(QUrl(QStringLiteral("https://seedbox.example/qbt"))), qbt);
    QCOMPARE(EngineRecognizer::order(QUrl(QStringLiteral("http://h:9091"))), tr);
    QCOMPARE(EngineRecognizer::order(QUrl(QStringLiteral("https://box.example/transmission/web/"))),
             tr);
    QCOMPARE(EngineRecognizer::order(QUrl(QStringLiteral("http://nas:8112"))), de);
    QCOMPARE(EngineRecognizer::order(QUrl(QStringLiteral("https://nas.example/deluge/json"))), de);
}

void TestEngineRecognizer::transmissionsRpcIsFoundFromAnyOfItsAddresses()
{
    const auto rpc = [](const char *address) {
        return TransmissionFingerprint::rpcUrl(QUrl(QString::fromLatin1(address))).toString();
    };
    QCOMPARE(rpc("http://h:9091"), QStringLiteral("http://h:9091/transmission/rpc"));
    QCOMPARE(rpc("http://h:9091/"), QStringLiteral("http://h:9091/transmission/rpc"));
    QCOMPARE(rpc("http://h:9091/transmission/web/"),
             QStringLiteral("http://h:9091/transmission/rpc"));
    QCOMPARE(rpc("http://h:9091/transmission/rpc"),
             QStringLiteral("http://h:9091/transmission/rpc"));
    QCOMPARE(rpc("https://box.example/tr/transmission/web/#x"),
             QStringLiteral("https://box.example/tr/transmission/rpc"));
    QCOMPARE(rpc("https://box.example/seedbox"),
             QStringLiteral("https://box.example/seedbox/transmission/rpc"));
}

void TestEngineRecognizer::delugeWebsJsonIsFoundFromItsAddress()
{
    const auto json = [](const char *address) {
        return DelugeFingerprint::jsonUrl(QUrl(QString::fromLatin1(address))).toString();
    };
    QCOMPARE(json("http://nas:8112"), QStringLiteral("http://nas:8112/json"));
    QCOMPARE(json("http://nas:8112/json"), QStringLiteral("http://nas:8112/json"));
    QCOMPARE(json("https://nas.example/deluge/"),
             QStringLiteral("https://nas.example/deluge/json"));
}

void TestEngineRecognizer::qbittorrentAnswersAsItselfAndNothingElseIsAsked()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        return request.target == "/api/v2/app/version" ? httpResponse(200, "OK", "v5.3.0")
                                                       : notFound();
    });
    const Recognition found = recognise(server.url());
    QVERIFY(found.recognised());
    QCOMPARE(found.engine, EngineKind::Qbittorrent);
    QCOMPARE(found.version, QStringLiteral("v5.3.0"));
    QVERIFY(!found.wantsSignIn);
    QCOMPARE(found.asked, QList<EngineKind>{EngineKind::Qbittorrent});
    QCOMPARE(server.requests.size(), 1);
    // Unauthenticated: no credential goes to an engine before it is known.
    QVERIFY(server.requests.first().header("Authorization").isEmpty());
}

void TestEngineRecognizer::qbittorrentSignedOutIsStillQbittorrent()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        return request.target == "/api/v2/app/version" ? httpResponse(403, "Forbidden", "Forbidden")
                                                       : notFound();
    });
    const Recognition found = recognise(
        server.url(), EngineKind::Unknown, QStringLiteral("admin"), QStringLiteral("secret"));
    QVERIFY(found.recognised());
    QCOMPARE(found.engine, EngineKind::Qbittorrent);
    QVERIFY(found.version.isEmpty());
    QVERIFY(found.wantsSignIn);
    QCOMPARE(server.requests.size(), 1);
    QVERIFY(server.requests.first().header("Authorization").isEmpty());
}

void TestEngineRecognizer::transmissionAfterQbittorrentDidNotAnswer()
{
    FakeServer server;
    server.setHandler(transmission());
    const Recognition found = recognise(server.url());
    QVERIFY(found.recognised());
    QCOMPARE(found.engine, EngineKind::Transmission);
    QCOMPARE(found.version, QStringLiteral("4.1.3"));
    QCOMPARE(found.apiVersion, QStringLiteral("6.0.0"));
    QCOMPARE(found.endpoint.path(), QStringLiteral("/transmission/rpc"));
    QCOMPARE(found.asked, (QList<EngineKind>{EngineKind::Qbittorrent, EngineKind::Transmission}));
    // qBittorrent's GET, then the 409, then session_get with the id; Deluge never asked.
    QCOMPARE(server.requests.size(), 3);
    QCOMPARE(server.requests.at(0).target, QByteArray("/api/v2/app/version"));
    QCOMPARE(server.requests.at(1).method, QByteArray("POST"));
    QCOMPARE(server.requests.at(2).header("X-Transmission-Session-Id"), QByteArray("s3ss10n"));
    QCOMPARE(methodOf(server.requests.at(2)), QStringLiteral("session_get"));
    QVERIFY(engineSupport(found.engine, found.version, found.apiVersion)
                .refusal.contains(QStringLiteral("can't drive Transmission yet")));
}

void TestEngineRecognizer::transmissionFirstForItsPath()
{
    FakeServer server;
    server.setHandler(transmission());
    // K6: the web UI's address, as a browser shows it.
    const Recognition found = recognise(at(server, QStringLiteral("/transmission/web/")));
    QCOMPARE(found.engine, EngineKind::Transmission);
    QCOMPARE(found.asked, QList<EngineKind>{EngineKind::Transmission});
    QCOMPARE(server.requests.first().target, QByteArray("/transmission/rpc"));
}

void TestEngineRecognizer::transmissionsLoginIsSentOnlyOnceItAsks()
{
    FakeServer server;
    server.setHandler(transmission("admin:pass"));
    const Recognition found = recognise(at(server, QStringLiteral("/transmission")),
                                        EngineKind::Unknown,
                                        QStringLiteral("admin"),
                                        QStringLiteral("pass"));
    QCOMPARE(found.engine, EngineKind::Transmission);
    QVERIFY(!found.signInRefused);
    QVERIFY(!found.wantsSignIn);
    QCOMPARE(found.version, QStringLiteral("4.1.3"));
    // The first POST went without the login: it is sent once Transmission is known.
    QVERIFY(server.requests.first().header("Authorization").isEmpty());
    QCOMPARE(server.requests.last().header("Authorization"),
             "Basic " + QByteArray("admin:pass").toBase64());

    // Without a login: it wants one, and that is all it says.
    server.requests.clear();
    const Recognition bare = recognise(at(server, QStringLiteral("/transmission")));
    QCOMPARE(bare.engine, EngineKind::Transmission);
    QVERIFY(bare.wantsSignIn);
    QCOMPARE(server.requests.size(), 1);
}

void TestEngineRecognizer::transmissionRefusesAWrongLogin()
{
    FakeServer server;
    server.setHandler(transmission("admin:pass"));
    const Recognition found = recognise(at(server, QStringLiteral("/transmission")),
                                        EngineKind::Unknown,
                                        QStringLiteral("admin"),
                                        QStringLiteral("nope"));
    QCOMPARE(found.engine, EngineKind::Transmission);
    QVERIFY(found.signInRefused);
}

void TestEngineRecognizer::anOldTransmissionHasNoRpcVersion()
{
    FakeServer server;
    server.setHandler(transmission({}, {}));
    const Recognition found = recognise(at(server, QStringLiteral("/transmission")));
    QCOMPARE(found.engine, EngineKind::Transmission);
    QCOMPARE(found.version, QStringLiteral("3.00"));
    QVERIFY(found.apiVersion.isEmpty());
    const EngineSupport support = engineSupport(found.engine, found.version, found.apiVersion);
    QVERIFY(support.versionRefused);
    QCOMPARE(support.refusal,
             QStringLiteral("Transmission 3.00 is too old: omaseed needs 4.1 or later"));
}

void TestEngineRecognizer::delugeSignsInAndNamesItsDaemon()
{
    FakeServer server;
    server.setHandler(deluge());
    const Recognition found = recognise(
        at(server, QStringLiteral("/json")), EngineKind::Unknown, {}, QStringLiteral("deluge"));
    QVERIFY(found.recognised());
    QCOMPARE(found.engine, EngineKind::Deluge);
    QCOMPARE(found.asked, QList<EngineKind>{EngineKind::Deluge});
    QVERIFY(found.daemonConnected);
    QCOMPARE(found.version, QStringLiteral("2.2.0"));
    QCOMPARE(found.daemons, QStringList{QStringLiteral("127.0.0.1:58846")});
    QStringList methods;
    for (const Recorded &request : std::as_const(server.requests))
        methods.append(methodOf(request));
    QCOMPARE(methods,
             (QStringList{QStringLiteral("auth.check_session"),
                          QStringLiteral("auth.login"),
                          QStringLiteral("web.connected"),
                          QStringLiteral("web.get_hosts"),
                          QStringLiteral("daemon.info")}));
    // deluge-web's session rides on its cookie.
    QVERIFY(server.requests.last().header("Cookie").contains("_session_id=c00k1e"));
    QVERIFY(engineSupport(found.engine, found.version, {})
                .refusal.contains(QStringLiteral("can't drive Deluge yet")));
}

void TestEngineRecognizer::delugeWithoutADaemonListsItsHosts()
{
    FakeServer server;
    server.setHandler(deluge(false));
    const Recognition found = recognise(
        at(server, QStringLiteral("/json")), EngineKind::Unknown, {}, QStringLiteral("deluge"));
    QCOMPARE(found.engine, EngineKind::Deluge);
    QVERIFY(!found.daemonConnected);
    QVERIFY(found.version.isEmpty()); // only its daemon knows
    QCOMPARE(found.daemons, QStringList{QStringLiteral("127.0.0.1:58846")});
}

void TestEngineRecognizer::delugeRefusesAWrongPassword()
{
    FakeServer server;
    server.setHandler(deluge());
    const Recognition found = recognise(
        at(server, QStringLiteral("/json")), EngineKind::Unknown, {}, QStringLiteral("wrong"));
    QCOMPARE(found.engine, EngineKind::Deluge);
    QVERIFY(found.signInRefused);
    QCOMPARE(server.requests.size(), 2);
}

void TestEngineRecognizer::delugeWantsItsPassword()
{
    FakeServer server;
    server.setHandler(deluge());
    // A plain address: qBittorrent and Transmission are asked first, and say nothing.
    const Recognition found = recognise(server.url());
    QCOMPARE(found.engine, EngineKind::Deluge);
    QVERIFY(found.wantsSignIn);
    QCOMPARE(
        found.asked,
        (QList<EngineKind>{EngineKind::Qbittorrent, EngineKind::Transmission, EngineKind::Deluge}));
    QCOMPARE(server.requests.size(), 3);
}

void TestEngineRecognizer::nothingAnsweringAsAnEngineIsUnrecognised()
{
    FakeServer server;
    server.setHandler([](const Recorded &, int) {
        return httpResponse(200, "OK", "<html><body>a router</body></html>");
    });
    const Recognition found = recognise(server.url());
    QCOMPARE(found.outcome, Recognition::Outcome::Unrecognised);
    QCOMPARE(found.engine, EngineKind::Unknown);
    QCOMPARE(found.asked.size(), 3);
    QVERIFY(found.error.isError());
}

void TestEngineRecognizer::aClosedPortFailsAtOnce()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    }
    const Recognition found = recognise(QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port)));
    QCOMPARE(found.outcome, Recognition::Outcome::Failed);
    QCOMPARE(found.error.kind(), ApiError::Kind::Network);
    QCOMPARE(found.asked, QList<EngineKind>{EngineKind::Qbittorrent});
}

void TestEngineRecognizer::aPickedEngineIsTheOnlyOneAsked()
{
    FakeServer server;
    server.setHandler([](const Recorded &request, int) {
        return request.target == "/api/v2/app/version" ? httpResponse(200, "OK", "v5.3.0")
                                                       : notFound();
    });
    // K4: "Deluge" picked on what is a qBittorrent.
    const Recognition found = recognise(server.url(), EngineKind::Deluge);
    QCOMPARE(found.outcome, Recognition::Outcome::Unrecognised);
    QCOMPARE(found.asked, QList<EngineKind>{EngineKind::Deluge});
    QCOMPARE(server.requests.size(), 1);
    QCOMPARE(server.requests.first().target, QByteArray("/json"));
}

QTEST_GUILESS_MAIN(TestEngineRecognizer)

#include "tst_enginerecognizer.moc"
