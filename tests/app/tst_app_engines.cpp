// A server's test recognises its engine (#84; DECISIONS C25 K4, K5, K8, K9): App's testServer
// against FakeEngines answering as qBittorrent, Transmission and Deluge would. A qBittorrent
// goes on to its signed-in probe and is counted as before, and so does a Transmission (E4) and a
// Deluge (E6, with K10's daemon step when deluge-web has none); one too old is refused for its
// version, and an answer from no engine says so and keeps "save anyway".

#include "FakeEngines.hpp"
#include "ui/app/App.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using omaseed::app::App;
using namespace fakeengines;

namespace {

struct Tested
{
    bool ok = false;
    QString version;
    QString apiVersion;
    int count = -1;
    QString error;
    QString detail;
    QVariantMap info;
};

Tested test(App &app, const QVariantMap &fields, const QString &secret = {})
{
    QSignalSpy tested(&app, &App::serverTested);
    app.testServer(fields, secret);
    Tested answer;
    if (!tested.wait(10000))
        return answer;
    const QList<QVariant> args = tested.first();
    answer.ok = args.at(1).toBool();
    answer.version = args.at(2).toString();
    answer.apiVersion = args.at(3).toString();
    answer.count = args.at(4).toInt();
    answer.error = args.at(6).toString();
    answer.detail = args.at(7).toString();
    answer.info = args.value(8).toMap();
    return answer;
}

QVariantMap fieldsFor(const QUrl &url, const QString &authMode = QStringLiteral("none"))
{
    return {{QStringLiteral("name"), QStringLiteral("box")},
            {QStringLiteral("url"), url.toString()},
            {QStringLiteral("authMode"), authMode}};
}

} // namespace

class TestAppEngines : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void aQbittorrentIsRecognisedThenProbedAsBefore();
    void aPickedQbittorrentIsNotAskedTwice();
    void aTransmissionIsRecognisedThenProbedAndCounted();
    void anOldTransmissionIsRefusedForItsVersion();
    void aDelugeIsProbedAndCounted();
    void aDelugeWithoutADaemonAsksWhichOne();
    void aWrongDelugePasswordIsTheSignIn();
    void anAnswerFromNoEngineKeepsSaveAnyway();
    void theProblemSaysTheRefusalInWords();

private:
    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppEngines::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");
}

void TestAppEngines::cleanup()
{
    m_home.reset();
}

void TestAppEngines::aQbittorrentIsRecognisedThenProbedAsBefore()
{
    FakeServer server;
    server.setHandler(qbittorrent());
    App app;
    const Tested answer = test(app, fieldsFor(server.url()));
    QVERIFY2(answer.ok, qPrintable(answer.error));
    QCOMPARE(answer.version, QStringLiteral("v5.3.0"));
    QCOMPARE(answer.apiVersion, QStringLiteral("2.16.2"));
    QCOMPARE(answer.count, 0);
    QCOMPARE(answer.info.value(QStringLiteral("engine")).toString(), QStringLiteral("qbittorrent"));
    QCOMPARE(answer.info.value(QStringLiteral("engineName")).toString(),
             QStringLiteral("qBittorrent"));
    QVERIFY(!answer.info.value(QStringLiteral("refused")).toBool());
    // One unauthenticated app/version to recognise it, then the probe and the count.
    QCOMPARE(server.requests.first().target, QByteArray("/api/v2/app/version"));
    QVERIFY(server.requests.size() >= 3);
}

void TestAppEngines::aPickedQbittorrentIsNotAskedTwice()
{
    FakeServer recognised;
    recognised.setHandler(qbittorrent());
    FakeServer picked;
    picked.setHandler(qbittorrent());
    App app;
    QVERIFY(test(app, fieldsFor(recognised.url())).ok);
    QVariantMap fields = fieldsFor(picked.url());
    fields.insert(QStringLiteral("engineChoice"), QStringLiteral("qbittorrent"));
    QVERIFY(test(app, fields).ok);
    // K4: a picked engine is the only one asked, and qBittorrent's own probe is that ask.
    QCOMPARE(picked.requests.size(), recognised.requests.size() - 1);
}

void TestAppEngines::aTransmissionIsRecognisedThenProbedAndCounted()
{
    FakeServer server;
    server.setHandler(transmission());
    QUrl url = server.url();
    url.setPath(QStringLiteral("/transmission/web/"));
    App app;
    const Tested answer = test(app, fieldsFor(url));
    QVERIFY2(answer.ok, qPrintable(answer.error));
    QCOMPARE(answer.version, QStringLiteral("4.1.3"));
    QCOMPARE(answer.apiVersion, QStringLiteral("6.0.0"));
    QCOMPARE(answer.count, 0);
    QCOMPARE(answer.info.value(QStringLiteral("engine")).toString(),
             QStringLiteral("transmission"));
    QCOMPARE(answer.info.value(QStringLiteral("engineName")).toString(),
             QStringLiteral("Transmission"));
    QVERIFY(!answer.info.value(QStringLiteral("refused")).toBool());
    // The probe and the count went to the RPC endpoint, as torrent_get.
    QVERIFY(std::any_of(server.requests.cbegin(), server.requests.cend(), [](const Recorded &r) {
        return methodOf(r) == QLatin1String("torrent_get");
    }));
    // Nothing went to qBittorrent's API: the path said Transmission first.
    for (const Recorded &request : std::as_const(server.requests))
        QVERIFY(!request.target.startsWith("/api/v2"));
}

void TestAppEngines::anOldTransmissionIsRefusedForItsVersion()
{
    FakeServer server;
    server.setHandler(transmission({}, {}));
    QUrl url = server.url();
    url.setPath(QStringLiteral("/transmission"));
    App app;
    const Tested answer = test(app, fieldsFor(url));
    QVERIFY(!answer.ok);
    QCOMPARE(answer.error,
             QStringLiteral("Transmission 3.00 is too old: omaseed needs 4.1 or later"));
    QVERIFY(answer.info.value(QStringLiteral("refused")).toBool());
    QVERIFY(answer.info.value(QStringLiteral("versionRefused")).toBool());
}

void TestAppEngines::aDelugeIsProbedAndCounted()
{
    FakeServer server;
    server.setHandler(deluge());
    QUrl url = server.url();
    url.setPath(QStringLiteral("/json"));
    App app;
    const Tested answer =
        test(app, fieldsFor(url, QStringLiteral("password")), QStringLiteral("deluge"));
    QVERIFY2(answer.ok, qPrintable(answer.error));
    QCOMPARE(answer.version, QStringLiteral("2.2.0"));
    QCOMPARE(answer.count, 0);
    QCOMPARE(answer.info.value(QStringLiteral("engine")).toString(), QStringLiteral("deluge"));
    // K10: one daemon, connected: no step, and its id is the one to keep.
    QVERIFY(!answer.info.value(QStringLiteral("daemonStep")).toBool());
    QCOMPARE(answer.info.value(QStringLiteral("daemon")).toString(), QStringLiteral("c0ffee"));
    QCOMPARE(answer.info.value(QStringLiteral("hosts")).toList().size(), 1);
}

void TestAppEngines::aDelugeWithoutADaemonAsksWhichOne()
{
    FakeServer server;
    server.setHandler(deluge(false));
    QUrl url = server.url();
    url.setPath(QStringLiteral("/json"));
    App app;
    QVariantMap fields = fieldsFor(url, QStringLiteral("password"));
    const Tested step = test(app, fields, QStringLiteral("deluge"));
    QVERIFY(!step.ok);
    QVERIFY(step.info.value(QStringLiteral("daemonStep")).toBool());
    const QVariantList hosts = step.info.value(QStringLiteral("hosts")).toList();
    QCOMPARE(hosts.size(), 1);
    const QVariantMap host = hosts.first().toMap();
    QCOMPARE(host.value(QStringLiteral("id")).toString(), QStringLiteral("c0ffee"));
    QCOMPARE(host.value(QStringLiteral("address")).toString(), QStringLiteral("127.0.0.1:58846"));
    QVERIFY(!host.value(QStringLiteral("connected")).toBool());
    QVERIFY(host.value(QStringLiteral("online")).toBool());
    // The step is the sheet's question, not a failure to show.
    QVERIFY(app.serverTestProblem(fields, step.error, step.detail, step.info).isEmpty());
    const auto connects = [&server]() {
        return std::count_if(
            server.requests.cbegin(), server.requests.cend(), [](const Recorded &request) {
                return request.body.contains("\"web.connect\"");
            });
    };
    QCOMPARE(connects(), 0); // the test itself never switches deluge-web

    // "use this daemon": connected, then tested again.
    fields.insert(QStringLiteral("engine"), QStringLiteral("deluge"));
    fields.insert(QStringLiteral("connectDaemon"), QStringLiteral("c0ffee"));
    const Tested used = test(app, fields, QStringLiteral("deluge"));
    QVERIFY2(used.ok, qPrintable(used.error));
    QCOMPARE(connects(), 1);
    QVERIFY(!used.info.value(QStringLiteral("daemonStep")).toBool());
    QCOMPARE(used.info.value(QStringLiteral("daemon")).toString(), QStringLiteral("c0ffee"));
    QCOMPARE(used.version, QStringLiteral("2.2.0"));
}

void TestAppEngines::aWrongDelugePasswordIsTheSignIn()
{
    FakeServer server;
    server.setHandler(deluge());
    QUrl url = server.url();
    url.setPath(QStringLiteral("/json"));
    App app;
    const Tested answer =
        test(app, fieldsFor(url, QStringLiteral("password")), QStringLiteral("nope"));
    QVERIFY(!answer.ok);
    QCOMPARE(answer.error, QStringLiteral("wrong password"));
    QVERIFY(!answer.info.value(QStringLiteral("refused")).toBool());
    QCOMPARE(answer.info.value(QStringLiteral("engine")).toString(), QStringLiteral("deluge"));
}

void TestAppEngines::anAnswerFromNoEngineKeepsSaveAnyway()
{
    FakeServer server;
    server.setHandler(
        [](const Recorded &, int) { return httpResponse(404, "Not Found", "nothing here"); });
    App app;
    const Tested answer = test(app, fieldsFor(server.url()));
    QVERIFY(!answer.ok);
    QVERIFY2(answer.error.endsWith(
                 QStringLiteral("answered, but not as qBittorrent, Transmission or Deluge")),
             qPrintable(answer.error));
    QVERIFY(answer.info.value(QStringLiteral("unrecognised")).toBool());
    QVERIFY(!answer.info.value(QStringLiteral("refused")).toBool());
    QVERIFY(answer.detail.contains(QStringLiteral("404")));

    // Deluge picked on it: only Deluge asked, and the words say which.
    QVariantMap fields = fieldsFor(server.url());
    fields.insert(QStringLiteral("engineChoice"), QStringLiteral("deluge"));
    const Tested picked = test(app, fields);
    QVERIFY2(picked.error.endsWith(QStringLiteral("didn't answer as Deluge")),
             qPrintable(picked.error));
}

void TestAppEngines::theProblemSaysTheRefusalInWords()
{
    App app;
    const QVariantMap fields = fieldsFor(QUrl(QStringLiteral("http://nas:8112")));
    const QVariantMap refused = app.serverTestProblem(
        fields,
        QStringLiteral("omaseed can't drive Deluge yet"),
        QStringLiteral("This build of omaseed drives qBittorrent and Transmission."),
        {{QStringLiteral("refused"), true}});
    QCOMPARE(refused.value(QStringLiteral("headline")).toString(),
             QStringLiteral("omaseed can't drive Deluge yet"));
    QCOMPARE(refused.value(QStringLiteral("line")).toString(),
             QStringLiteral("This build of omaseed drives qBittorrent and Transmission."));
    QCOMPARE(refused.value(QStringLiteral("kind")).toString(), QStringLiteral("refused"));
    // K6: where Deluge answered, when it isn't the address typed.
    const QVariantMap found = app.serverTestProblem(
        fields,
        QStringLiteral("omaseed can't drive Deluge yet"),
        QStringLiteral("Not yet."),
        {{QStringLiteral("refused"), true},
         {QStringLiteral("endpoint"), QStringLiteral("http://nas:8112/json")}});
    QCOMPARE(found.value(QStringLiteral("line")).toString(),
             QStringLiteral("Not yet. It answered at http://nas:8112/json."));
    const QVariantMap unknown = app.serverTestProblem(fields,
                                                      QStringLiteral("x answered, but not as …"),
                                                      QStringLiteral("not found: GET app/version"),
                                                      {{QStringLiteral("unrecognised"), true}});
    QCOMPARE(unknown.value(QStringLiteral("kind")).toString(), QStringLiteral("unrecognised"));
    QCOMPARE(unknown.value(QStringLiteral("details")).toString(),
             QStringLiteral("not found: GET app/version"));
}

QTEST_MAIN(TestAppEngines)

#include "tst_app_engines.moc"
