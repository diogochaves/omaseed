// #292 (DECISIONS C45 V14, V15; C50 GT3, GT5, GT8): `omaseed do`'s app side, AgentDo through
// AgentService, against a stand-in qBittorrent (FakeHttp): the gate first (off, never, reads
// off, ask), the server and the torrents checked before anything runs, each fix reaching the
// server as the window sends it, the server's refusal as failed, a dry run that sends nothing,
// and pause and resume as the whole server's hold (C36 M10).

#include "core/ActionOutcome.hpp"
#include "core/AgentWords.hpp"
#include "core/GlobalState.hpp"
#include "core/PauseHold.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/AgentDo.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"

#include "FakeHttp.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <functional>
#include <memory>

using fakehttp::FakeServer;
using fakehttp::httpResponse;
using fakehttp::Recorded;
using omaseed::app::AgentService;
using omaseed::app::App;

namespace {

const QString kLantern = QStringLiteral("3f9a1c0e") + QString(32, QLatin1Char('a'));
const QString kHarbour = QStringLiteral("3f9a1c0e") + QString(32, QLatin1Char('b'));

/// Just enough qBittorrent 5: the versions, two downloading torrents, the running and stopped
/// filters, and the actions, `refuse` answering 500 on the paths it names.
struct Qbt
{
    QStringList refuse;
    bool stopped = false; ///< after a stop: the stopped filter lists both

    QByteArray answer(const Recorded &request) const
    {
        const QByteArray target = request.target;
        for (const QString &path : refuse)
            if (target.startsWith(path.toUtf8()))
                return httpResponse(500, "Internal Server Error", "boom");
        if (target.startsWith("/api/v2/app/version"))
            return httpResponse(200, "OK", "v5.2.3");
        if (target.startsWith("/api/v2/app/webapiVersion"))
            return httpResponse(200, "OK", "2.15.1");
        if (target.startsWith("/api/v2/sync/maindata")) {
            const auto torrent = [](const QString &name) {
                return QJsonObject{{QStringLiteral("name"), name},
                                   {QStringLiteral("state"), QStringLiteral("downloading")},
                                   {QStringLiteral("progress"), 0.5},
                                   {QStringLiteral("save_path"), QStringLiteral("/data/incoming")},
                                   {QStringLiteral("added_on"), 1700000000}};
            };
            const QJsonObject body{
                {QStringLiteral("rid"), 1},
                {QStringLiteral("full_update"), true},
                {QStringLiteral("server_state"),
                 QJsonObject{{QStringLiteral("connection_status"), QStringLiteral("connected")}}},
                {QStringLiteral("torrents"),
                 QJsonObject{{kLantern, torrent(QStringLiteral("Lantern Festival"))},
                             {kHarbour, torrent(QStringLiteral("Harbour Bells"))}}}};
            return httpResponse(200, "OK", QJsonDocument(body).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/info")) {
            const bool wantsStopped = target.contains("filter=stopped");
            QJsonArray list;
            if (wantsStopped == stopped)
                for (const QString &hash : {kLantern, kHarbour})
                    list.append(QJsonObject{{QStringLiteral("hash"), hash}});
            return httpResponse(200, "OK", QJsonDocument(list).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/app/preferences"))
            return httpResponse(200, "OK", "{}");
        if (target.startsWith("/api/v2/torrents/categories") || target.startsWith("/api/v2/log/"))
            return httpResponse(200, "OK", target.startsWith("/api/v2/log/") ? "[]" : "{}");
        if (target.startsWith("/api/v2/torrents/tags"))
            return httpResponse(200, "OK", "[]");
        return httpResponse(200, "OK", "Ok.");
    }
};

QJsonObject run(AgentService &service, const QString &options, QString *raw = nullptr)
{
    QString json;
    int replies = 0;
    service.doAction(options, 4242, [&](const QString &reply) {
        json = reply;
        ++replies;
    });
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(replies > 0, 5000); }();
    QTest::qWait(20);
    [&]() { QCOMPARE(replies, 1); }(); // exactly one answer
    if (raw)
        *raw = json;
    return QJsonDocument::fromJson(json.toUtf8()).object();
}

/// A do whose answer waits (on the ask): the reply lands here.
struct Pending
{
    QString json;
    int replies = 0;
};

std::shared_ptr<Pending> begin(AgentService &service, const QString &options)
{
    auto pending = std::make_shared<Pending>();
    service.doAction(options, 4242, [pending](const QString &reply) {
        pending->json = reply;
        ++pending->replies;
    });
    return pending;
}

QJsonObject finish(const std::shared_ptr<Pending> &pending)
{
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(pending->replies > 0, 5000); }();
    QTest::qWait(20);
    [&]() { QCOMPARE(pending->replies, 1); }();
    return QJsonDocument::fromJson(pending->json.toUtf8()).object();
}

QString codeOf(const QJsonObject &object)
{
    return object.value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString();
}

QString messageOf(const QJsonObject &object)
{
    return object.value(QLatin1String("error"))
        .toObject()
        .value(QLatin1String("message"))
        .toString();
}

} // namespace

class TestAppAgentDo : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void offAndReadsOffAreNotAllowed();
    void neverWhateverTheGate();
    void listSaysTheLines();
    void eachFixReachesTheServer();
    void theServerRefusingIsFailed();
    void unknownAndAmbiguousRunNothing();
    void unknownServerAndFirstSync();
    void askOpensTheWindow();
    void askRefusedOrUnanswered();
    void oneAskAtATimeAndCheckedAgain();
    void dryRunSendsNothing();
    void refusedBeforeAnythingRuns();
    void pauseAndResumeHoldTheServer();
    void notYetAndBusUsage();
    void saidByAnAgent();

private:
    /// An app on the fake server, synced with both torrents.
    void start();
    int sent(const QByteArray &path) const;
    /// A /proc for pid 4242 (the bus's caller in these tests): omaseed, started by claude.
    void writeProc();

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QTemporaryDir> m_proc;
    std::unique_ptr<FakeServer> m_server;
    Qbt m_qbt;
    std::unique_ptr<App> m_app;
    std::unique_ptr<AgentService> m_service;
};

void TestAppAgentDo::initTestCase()
{
    qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=/nonexistent/omaseed-no-bus"));
}

void TestAppAgentDo::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    for (const char *name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
        qputenv(name, (m_home->path() + QLatin1Char('/') + QLatin1String(name)).toUtf8());
    for (const char *name : {"OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
    m_qbt = Qbt();
    m_server = std::make_unique<FakeServer>();
    m_server->setHandler([this](const Recorded &request, int) { return m_qbt.answer(request); });
    qputenv("OMASEED_SERVER_URL", m_server->url().toString().toUtf8());
}

void TestAppAgentDo::cleanup()
{
    m_service.reset();
    m_app.reset();
    m_proc.reset();
    m_server.reset();
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

void TestAppAgentDo::start()
{
    using namespace std::chrono_literals;
    m_app = std::make_unique<App>();
    m_service = std::make_unique<AgentService>(m_app.get());
    m_app->engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    // An empty /proc: the journal says the bus's pid alone, whatever runs as 4242 here.
    m_proc = std::make_unique<QTemporaryDir>();
    m_service->agentDo()->setProcRoot(m_proc->path());
    m_app->start();
    QTRY_VERIFY_WITH_TIMEOUT(m_app->engine()->globalState()->connected() &&
                                 m_app->engine()->torrents()->count() == 2,
                             5000);
}

void TestAppAgentDo::writeProc()
{
    const auto write = [this](uint pid, const QByteArray &line) {
        QVERIFY(QDir(m_proc->path()).mkpath(QString::number(pid)));
        QFile file(QStringLiteral("%1/%2/stat").arg(m_proc->path()).arg(pid));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(line);
    };
    write(4242, "4242 (omaseed) S 48213 4242 4100 0 -1");
    write(48213, "48213 (claude) S 4100 48213 4100 0 -1");
}

int TestAppAgentDo::sent(const QByteArray &path) const
{
    int count = 0;
    for (const Recorded &request : std::as_const(m_server->requests))
        if (request.target.startsWith(path))
            ++count;
    return count;
}

// C50 GT3: everything off by default; GT8: reads off says so, before the line.
void TestAppAgentDo::offAndReadsOffAreNotAllowed()
{
    start();
    QJsonObject object =
        run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("do"));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QCOMPARE(messageOf(object),
             QStringLiteral("omaseed doesn't let agents recheck: Fixes is off in Settings › "
                            "General › Agents. Only the person can change that."));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 5);

    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentReads(QStringLiteral("off"));
    object = run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QVERIFY(messageOf(object).contains(QStringLiteral("Reads is off")));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

void TestAppAgentDo::neverWhateverTheGate()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    for (const char *action : {"delete", "rename", "tags", "quit"}) {
        const QJsonObject object = run(*m_service,
                                       QStringLiteral(R"({"action":"%1","torrents":["3f9a1c0ea"]})")
                                           .arg(QLatin1String(action)));
        QCOMPARE(codeOf(object), QStringLiteral("never"));
        QCOMPARE(messageOf(object),
                 QStringLiteral("omaseed do doesn't delete, rename or change settings: those "
                                "stay with the person, in the window."));
    }
}

void TestAppAgentDo::listSaysTheLines()
{
    App app;
    AgentService service(&app);
    app.setAgentFixes(QStringLiteral("allow"));
    app.setAgentMoves(QStringLiteral("ask"));
    app.setAgentReads(QStringLiteral("off")); // --list answers with reads off (assumed, S3)
    QString raw;
    const QJsonObject object = run(service, QStringLiteral(R"({"list":true})"), &raw);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(raw));
    QVERIFY(raw.startsWith(QStringLiteral(R"({"v":1,"command":"do","at":")")));
    QVERIFY(object.value(QLatin1String("list")).toBool());
    const QJsonArray lines = object.value(QLatin1String("lines")).toArray();
    QCOMPARE(lines.size(), 4);
    QCOMPARE(lines.at(0).toObject().value(QLatin1String("level")).toString(),
             QStringLiteral("off"));
    QCOMPARE(lines.at(1).toObject().value(QLatin1String("kind")).toString(),
             QStringLiteral("fixes"));
    QCOMPARE(lines.at(1).toObject().value(QLatin1String("level")).toString(),
             QStringLiteral("allow"));
    QCOMPARE(lines.at(2).toObject().value(QLatin1String("level")).toString(),
             QStringLiteral("ask"));
    QVERIFY(object.value(QLatin1String("never")).toArray().contains(QStringLiteral("rename")));
}

// Allowed: each fix goes out as the window sends it, and answers once the server has.
void TestAppAgentDo::eachFixReachesTheServer()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QList<std::pair<QString, QByteArray>> fixes{
        {QStringLiteral("recheck"), "/api/v2/torrents/recheck"},
        {QStringLiteral("reannounce"), "/api/v2/torrents/reannounce"},
        {QStringLiteral("retry"), "/api/v2/torrents/start"},
    };
    for (const auto &[action, path] : fixes) {
        const int before = sent(path);
        QString raw;
        const QJsonObject object =
            run(*m_service,
                QStringLiteral(R"({"action":"%1","torrents":["3F9A1C0EA"]})").arg(action),
                &raw);
        QVERIFY2(codeOf(object).isEmpty(), qPrintable(raw));
        QCOMPARE(sent(path), before + 1);
        QVERIFY(raw.startsWith(QStringLiteral(R"({"v":1,"command":"do","at":")")));
        QCOMPARE(object.value(QLatin1String("action")).toString(), action);
        QCOMPARE(object.value(QLatin1String("asked")).toBool(true), false);
        QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("not-asked"));
        const QJsonArray done = object.value(QLatin1String("done")).toArray();
        QCOMPARE(done.size(), 1);
        QCOMPARE(done.at(0).toObject().value(QLatin1String("hash")).toString(), kLantern);
        QCOMPARE(done.at(0).toObject().value(QLatin1String("name")).toString(),
                 QStringLiteral("Lantern Festival"));
        QVERIFY(object.value(QLatin1String("failed")).toArray().isEmpty());
        QVERIFY2(object.value(QLatin1String("outcome"))
                     .toString()
                     .startsWith(QStringLiteral("Lantern Festival ")),
                 qPrintable(raw));
    }
    // Both torrents, one request; the same torrent twice is once.
    const QJsonObject both =
        run(*m_service,
            QStringLiteral(
                R"({"action":"recheck","torrents":["3f9a1c0ea","3f9a1c0eb","3f9a1c0eaa"]})"));
    QCOMPARE(both.value(QLatin1String("done")).toArray().size(), 2);
    QCOMPARE(both.value(QLatin1String("outcome")).toString(), QStringLiteral("2 sent to recheck"));
    const Recorded last = [this]() {
        for (qsizetype i = m_server->requests.size() - 1; i >= 0; --i)
            if (m_server->requests.at(i).target.startsWith("/api/v2/torrents/recheck"))
                return m_server->requests.at(i);
        return Recorded();
    }();
    QVERIFY(last.body.contains(kLantern.toUtf8()));
    QVERIFY(last.body.contains(kHarbour.toUtf8()));
}

// C45 V13: the server refusing is failed (exit 6), in the strip's words.
void TestAppAgentDo::theServerRefusingIsFailed()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_qbt.refuse = {QStringLiteral("/api/v2/torrents/recheck")};
    QString raw;
    const QJsonObject object =
        run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"), &raw);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(raw));
    QVERIFY(object.value(QLatin1String("done")).toArray().isEmpty());
    const QJsonArray failed = object.value(QLatin1String("failed")).toArray();
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.at(0).toObject().value(QLatin1String("code")).toString(),
             QStringLiteral("failed"));
    QVERIFY(!failed.at(0).toObject().value(QLatin1String("message")).toString().isEmpty());
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Lantern Festival didn't recheck"));
    QCOMPARE(omaseed::core::AgentWords::exitFor(
                 failed.at(0).toObject().value(QLatin1String("code")).toString()),
             6);
}

void TestAppAgentDo::unknownAndAmbiguousRunNothing()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    QJsonObject object =
        run(*m_service,
            QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea","deadbeef00"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("unknown-torrent"));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 4);
    object = run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0e"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("ambiguous"));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 2);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

void TestAppAgentDo::unknownServerAndFirstSync()
{
    // Before the first sync there is nothing to act on.
    {
        App app;
        AgentService service(&app);
        app.setAgentFixes(QStringLiteral("allow"));
        const QJsonObject object =
            run(service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
        QCOMPARE(codeOf(object), QStringLiteral("first-sync"));
    }
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QJsonObject object =
        run(*m_service,
            QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"],"server":"nowhere"})"));
    QCOMPARE(codeOf(object), QStringLiteral("unknown-server"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

// Until the ask window (S5): an action that would ask is no-answer, and nothing runs.
void TestAppAgentDo::askOpensTheWindow()
{
    start();
    writeProc();
    m_app->setAgentFixes(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending =
        begin(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QTRY_VERIFY(ask->isOpen());
    const QVariantMap request = ask->request();
    QCOMPARE(request.value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to recheck a torrent"));
    const QVariantList torrents = request.value(QStringLiteral("torrents")).toList();
    QCOMPARE(torrents.size(), 1);
    QCOMPARE(torrents.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Lantern Festival"));
    QVERIFY(torrents.first()
                .toMap()
                .value(QStringLiteral("line"))
                .toString()
                .startsWith(QStringLiteral("in /data/incoming · ")));
    QCOMPARE(request.value(QStringLiteral("word")).toString(), QStringLiteral("omaseed do"));
    QCOMPARE(request.value(QStringLiteral("parent")).toString(), QStringLiteral("claude"));
    QCOMPARE(request.value(QStringLiteral("parentPid")).toUInt(), 48213u);
    QCOMPARE(pending->replies, 0); // nothing answered, nothing sent while it asks
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);

    QTest::ignoreMessage(QtInfoMsg,
                         "agent: recheck on Lantern Festival, allowed in the window (claude, pid "
                         "48213)");
    ask->allow();
    QVERIFY(!ask->isOpen());
    const QJsonObject object = finish(pending);
    QCOMPARE(object.value(QLatin1String("asked")).toBool(), true);
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("allowed"));
    QCOMPARE(object.value(QLatin1String("done")).toArray().size(), 1);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 1);
    QCOMPARE(m_app->outcomes()->current().value(QStringLiteral("agent")).toString(),
             QStringLiteral("by an agent"));

    // Many torrents: the count in the head.
    const auto two = begin(*m_service,
                           QStringLiteral(R"({"action":"reannounce","torrents":["3f9a1c0ea",)"
                                          R"("3f9a1c0eb"]})"));
    QTRY_VERIFY(ask->isOpen());
    QCOMPARE(ask->request().value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to reannounce 2 torrents"));
    QCOMPARE(ask->request().value(QStringLiteral("more")).toString(), QString());
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: reannounce")));
    ask->refuse();
    finish(two);
}

void TestAppAgentDo::askRefusedOrUnanswered()
{
    start();
    writeProc();
    m_app->setAgentFixes(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();

    auto pending =
        begin(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QTRY_VERIFY(ask->isOpen());
    QTest::ignoreMessage(QtInfoMsg,
                         "agent: recheck on Lantern Festival, refused in the window (claude, pid "
                         "48213)");
    ask->refuse();
    QJsonObject object = finish(pending);
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QCOMPARE(messageOf(object), QStringLiteral("Refused in omaseed's window: not done."));
    QCOMPARE(object.value(QLatin1String("asked")).toBool(), true);
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("refused"));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 5);
    QCOMPARE(m_app->agentLast().value(QStringLiteral("what")).toString(),
             QStringLiteral("recheck · Lantern Festival · refused in the window"));

    // Two minutes, here a few ms: no answer.
    ask->setTimeoutMs(50);
    pending = begin(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QTest::ignoreMessage(QtInfoMsg,
                         "agent: recheck on Lantern Festival, no answer in the window (claude, "
                         "pid 48213)");
    object = finish(pending);
    QVERIFY(!ask->isOpen());
    QCOMPARE(codeOf(object), QStringLiteral("no-answer"));
    QCOMPARE(messageOf(object),
             QStringLiteral("Nobody answered in omaseed's window for 2 minutes: not done."));
    QCOMPARE(omaseed::core::AgentWords::exitFor(codeOf(object)), 5);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

// PLAN Q5: one ask at a time; a setting turned off while asking wins over the allow.
void TestAppAgentDo::oneAskAtATimeAndCheckedAgain()
{
    start();
    writeProc();
    m_app->setAgentFixes(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto first =
        begin(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QTRY_VERIFY(ask->isOpen());

    QTest::ignoreMessage(QtInfoMsg,
                         "agent: reannounce on Harbour Bells, not asked: another ask was open "
                         "(claude, pid 48213)");
    const QJsonObject second =
        run(*m_service, QStringLiteral(R"({"action":"reannounce","torrents":["3f9a1c0eb"]})"));
    QCOMPARE(codeOf(second), QStringLiteral("no-answer"));
    QCOMPARE(second.value(QLatin1String("asked")).toBool(), false);
    QVERIFY(ask->isOpen());
    QCOMPARE(ask->request().value(QStringLiteral("action")).toString(), QStringLiteral("recheck"));

    // The person turns Fixes off, then clicks allow on the ask still up.
    m_app->setAgentFixes(QStringLiteral("off"));
    QTest::ignoreMessage(QtInfoMsg,
                         "agent: recheck, not allowed: Fixes is off (claude, pid 48213)");
    ask->allow();
    const QJsonObject object = finish(first);
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);

    // Allowed while the line says allow now: still asked, said so.
    m_app->setAgentFixes(QStringLiteral("ask"));
    const auto third =
        begin(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QTRY_VERIFY(ask->isOpen());
    m_app->setAgentFixes(QStringLiteral("allow"));
    QTest::ignoreMessage(QtInfoMsg,
                         "agent: recheck on Lantern Festival, allowed in the window (claude, pid "
                         "48213)");
    ask->allow();
    QCOMPARE(finish(third).value(QLatin1String("answer")).toString(), QStringLiteral("allowed"));
}

// C50 GT5: every check, nothing asked, nothing done.
void TestAppAgentDo::dryRunSendsNothing()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    QString raw;
    QJsonObject object =
        run(*m_service,
            QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"],"dryRun":true})"),
            &raw);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(raw));
    QVERIFY(object.value(QLatin1String("dryRun")).toBool());
    QCOMPARE(object.value(QLatin1String("would")).toString(), QStringLiteral("run"));
    QCOMPARE(object.value(QLatin1String("torrents")).toArray().size(), 1);
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("recheck would run · Lantern Festival"));

    m_app->setAgentFixes(QStringLiteral("ask"));
    object = run(*m_service,
                 QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"],"dryRun":true})"));
    QCOMPARE(object.value(QLatin1String("would")).toString(), QStringLiteral("ask"));

    // A check that fails fails the dry run the same way.
    object = run(*m_service,
                 QStringLiteral(R"({"action":"recheck","torrents":["deadbeef00"],"dryRun":true})"));
    QCOMPARE(codeOf(object), QStringLiteral("unknown-torrent"));
    m_app->setAgentFixes(QStringLiteral("off"));
    object = run(*m_service,
                 QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"],"dryRun":true})"));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

void TestAppAgentDo::refusedBeforeAnythingRuns()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    QJsonObject object =
        run(*m_service, QStringLiteral(R"({"action":"start-here","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY(messageOf(object).contains(QStringLiteral("wasn't stopped by a failed move")));
    object = run(*m_service, QStringLiteral(R"({"action":"dismiss","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QCOMPARE(messageOf(object),
             QStringLiteral("Not dismissed: Lantern Festival shows no failure, so there is "
                            "nothing to dismiss."));
    object = run(*m_service, QStringLiteral(R"({"action":"resume"})"));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY(messageOf(object).contains(QStringLiteral("nothing to resume")));
    QCOMPARE(sent("/api/v2/torrents/start"), 0);
}

// C36 M10: pause stops what runs and holds it; resume starts exactly that.
void TestAppAgentDo::pauseAndResumeHoldTheServer()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    QString raw;
    QJsonObject object = run(*m_service, QStringLiteral(R"({"action":"pause"})"), &raw);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(raw));
    QCOMPARE(sent("/api/v2/torrents/stop"), 1);
    QCOMPARE(object.value(QLatin1String("count")).toInt(), 2);
    QVERIFY2(object.value(QLatin1String("outcome"))
                 .toString()
                 .startsWith(QStringLiteral("Paused 2 torrents on ")),
             qPrintable(raw));
    QVERIFY(m_app->pauseHold()->held());

    m_qbt.stopped = true;
    object = run(*m_service, QStringLiteral(R"({"action":"resume"})"), &raw);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(raw));
    QCOMPARE(sent("/api/v2/torrents/start"), 1);
    QCOMPARE(object.value(QLatin1String("count")).toInt(), 2);
    QVERIFY(!m_app->pauseHold()->held());
}

void TestAppAgentDo::notYetAndBusUsage()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentMoves(QStringLiteral("ask"));
    m_app->setAgentRemove(QStringLiteral("ask"));
    QJsonObject object =
        run(*m_service, QStringLiteral(R"({"action":"remove","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    QVERIFY(messageOf(object).contains(QStringLiteral("do remove isn't in this omaseed yet")));
    // #294: locate takes --to.
    object = run(*m_service, QStringLiteral(R"({"action":"locate","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    // What busctl might send that the CLI never does.
    object = run(*m_service, QStringLiteral(R"({"action":"pause","torrents":["3f9a1c0ea"]})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(*m_service, QStringLiteral(R"({"action":"recheck"})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":"3f9a1c0ea"})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(*m_service, QStringLiteral(R"({"bogus":1})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(*m_service, QStringLiteral("not json"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
}

// C45 V18 (board 62 §5): an agent's act says so in the strip, the journal and the Agents row's
// last line; a refusal by the gate is noted too; the window's own act says nothing more.
void TestAppAgentDo::saidByAnAgent()
{
    start();
    QTest::ignoreMessage(QtInfoMsg, "agent: recheck, not allowed: Fixes is off (pid 4242)");
    run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QVERIFY2(m_app->agentLast()
                 .value(QStringLiteral("text"))
                 .toString()
                 .startsWith(QStringLiteral("last: recheck · not allowed: Fixes is off · today ")),
             qPrintable(m_app->agentLast().value(QStringLiteral("text")).toString()));
    QVERIFY(m_app->outcomes()->current().isEmpty());

    m_app->setAgentFixes(QStringLiteral("allow"));
    QTest::ignoreMessage(QtInfoMsg,
                         "agent: recheck on Lantern Festival, allowed in Settings (pid 4242)");
    run(*m_service, QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"]})"));
    QVariantMap strip = m_app->outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(),
             QStringLiteral("Lantern Festival sent to recheck"));
    QCOMPARE(strip.value(QStringLiteral("agent")).toString(), QStringLiteral("by an agent"));
    QCOMPARE(m_app->agentLast().value(QStringLiteral("what")).toString(),
             QStringLiteral("recheck · Lantern Festival"));

    // The server refusing two: the journal says why, the line says they didn't.
    m_qbt.refuse = {QStringLiteral("/api/v2/torrents/reannounce")};
    QTest::ignoreMessage(QtInfoMsg,
                         QRegularExpression(QStringLiteral(
                             "^agent: reannounce on 2 torrents, allowed in Settings, didn't: .+ "
                             "\\(pid 4242\\)$")));
    run(*m_service,
        QStringLiteral(R"({"action":"reannounce","torrents":["3f9a1c0ea","3f9a1c0eb"]})"));
    QCOMPARE(m_app->outcomes()->current().value(QStringLiteral("agent")).toString(),
             QStringLiteral("by an agent"));
    QCOMPARE(m_app->agentLast().value(QStringLiteral("what")).toString(),
             QStringLiteral("reannounce · 2 torrents · didn't"));
    m_qbt.refuse.clear();

    // The window's own recheck: no suffix, the last line kept.
    m_app->recheck({kHarbour});
    QTRY_COMPARE(m_app->outcomes()->current().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Harbour Bells sent to recheck"));
    QCOMPARE(m_app->outcomes()->current().value(QStringLiteral("agent")).toString(), QString());
    QCOMPARE(m_app->agentLast().value(QStringLiteral("what")).toString(),
             QStringLiteral("reannounce · 2 torrents · didn't"));

    // The hold has no strip of its own: an agent's pause puts one up.
    run(*m_service, QStringLiteral(R"({"action":"pause"})"));
    strip = m_app->outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(), QStringLiteral("2 paused"));
    QCOMPARE(strip.value(QStringLiteral("agent")).toString(), QStringLiteral("by an agent"));
    QVERIFY(m_app->agentLast()
                .value(QStringLiteral("what"))
                .toString()
                .startsWith(QStringLiteral("pause · ")));

    // A dry run tried nothing: the last line stays.
    m_app->setAgentFixes(QStringLiteral("off"));
    const QString before = m_app->agentLast().value(QStringLiteral("what")).toString();
    run(*m_service,
        QStringLiteral(R"({"action":"recheck","torrents":["3f9a1c0ea"],"dryRun":true})"));
    QCOMPARE(m_app->agentLast().value(QStringLiteral("what")).toString(), before);
}

QTEST_GUILESS_MAIN(TestAppAgentDo)
#include "tst_app_agentdo.moc"
