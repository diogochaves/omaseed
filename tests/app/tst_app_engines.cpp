// A server's test recognises its engine (#84; DECISIONS C25 K4, K5, K8, K9): App's testServer
// against FakeEngines answering as qBittorrent, Transmission and Deluge would. A qBittorrent
// goes on to its signed-in probe and is counted as before; a Transmission or a Deluge is named
// and refused (this build drives neither yet), one too old is refused for its version, and an
// answer from no engine says so and keeps "save anyway".

#include "FakeEngines.hpp"
#include "ui/app/App.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
    void aTransmissionIsNamedAndRefused();
    void anOldTransmissionIsRefusedForItsVersion();
    void aDelugeIsNamedAndRefused();
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

void TestAppEngines::aTransmissionIsNamedAndRefused()
{
    FakeServer server;
    server.setHandler(transmission());
    QUrl url = server.url();
    url.setPath(QStringLiteral("/transmission/web/"));
    App app;
    const Tested answer = test(app, fieldsFor(url));
    QVERIFY(!answer.ok);
    QCOMPARE(answer.error, QStringLiteral("omaseed can't drive Transmission yet"));
    QVERIFY(answer.detail.contains(QStringLiteral("qBittorrent")));
    QCOMPARE(answer.version, QStringLiteral("4.1.3"));
    QCOMPARE(answer.apiVersion, QStringLiteral("6.0.0"));
    QCOMPARE(answer.info.value(QStringLiteral("engine")).toString(),
             QStringLiteral("transmission"));
    QVERIFY(answer.info.value(QStringLiteral("refused")).toBool());
    QVERIFY(!answer.info.value(QStringLiteral("versionRefused")).toBool());
    // K6: the endpoint it found, for the test's line.
    QVERIFY(answer.info.value(QStringLiteral("endpoint"))
                .toString()
                .endsWith(QStringLiteral("/transmission/rpc")));
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

void TestAppEngines::aDelugeIsNamedAndRefused()
{
    FakeServer server;
    server.setHandler(deluge());
    QUrl url = server.url();
    url.setPath(QStringLiteral("/json"));
    App app;
    const Tested answer =
        test(app, fieldsFor(url, QStringLiteral("password")), QStringLiteral("deluge"));
    QVERIFY(!answer.ok);
    QCOMPARE(answer.error, QStringLiteral("omaseed can't drive Deluge yet"));
    QCOMPARE(answer.version, QStringLiteral("2.2.0"));
    QCOMPARE(answer.info.value(QStringLiteral("engine")).toString(), QStringLiteral("deluge"));
    QVERIFY(answer.info.value(QStringLiteral("daemonConnected")).toBool());
    QCOMPARE(answer.info.value(QStringLiteral("daemons")).toStringList(),
             QStringList{QStringLiteral("127.0.0.1:58846")});
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
    const QVariantMap fields = fieldsFor(QUrl(QStringLiteral("http://seedbox:9091")));
    const QVariantMap refused =
        app.serverTestProblem(fields,
                              QStringLiteral("omaseed can't drive Transmission yet"),
                              QStringLiteral("This build of omaseed drives qBittorrent."),
                              {{QStringLiteral("refused"), true}});
    QCOMPARE(refused.value(QStringLiteral("headline")).toString(),
             QStringLiteral("omaseed can't drive Transmission yet"));
    QCOMPARE(refused.value(QStringLiteral("line")).toString(),
             QStringLiteral("This build of omaseed drives qBittorrent."));
    QCOMPARE(refused.value(QStringLiteral("kind")).toString(), QStringLiteral("refused"));
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
