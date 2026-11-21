// #293 (DECISIONS C45 V14, V15, V17, V18; C50 GT3): `omaseed do move` and `do move-back`, the
// app's side, against a stand-in qBittorrent whose save paths are a temporary folder this
// machine sees (so the move sheet's own checks run on it): the Moves line off, a move asked in
// the window (where to, how much, the room) and allowed, answering at its start with the job's
// id; the job's record, the story and the strip saying "by an agent"; a plan that can't move
// refused with the sheet's words before anything is asked; move-back of an unknown id refused,
// of a landed one asked; a dry run asking nothing.

#include "core/ActionOutcome.hpp"
#include "core/GlobalState.hpp"
#include "core/MoveHistory.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/AgentAsk.hpp"
#include "ui/app/AgentDo.hpp"
#include "ui/app/AgentService.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include "FakeHttp.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <chrono>
#include <memory>

using fakehttp::FakeServer;
using fakehttp::httpResponse;
using fakehttp::Recorded;
using omaseed::app::AgentService;
using omaseed::app::App;

namespace {

const QString kLantern = QStringLiteral("3f9a1c0e") + QString(32, QLatin1Char('a'));
const QString kHarbour = QStringLiteral("3f9a1c0e") + QString(32, QLatin1Char('b'));
constexpr qint64 kFileSize = 4096;

/// Just enough qBittorrent 5 for a move: two complete torrents with a file each under `lib`,
/// their files, the free space of the folder they sit on, and set location (which moves the
/// folder on disk, as qBittorrent does, and the save path with it).
struct Qbt
{
    QString lib;
    QHash<QString, QString> savePath;

    QByteArray answer(const Recorded &request)
    {
        const QByteArray target = request.target;
        if (target.startsWith("/api/v2/app/version"))
            return httpResponse(200, "OK", "v5.2.3");
        if (target.startsWith("/api/v2/app/webapiVersion"))
            return httpResponse(200, "OK", "2.15.1");
        if (target.startsWith("/api/v2/app/preferences"))
            return httpResponse(
                200, "OK", QJsonDocument(QJsonObject{{QStringLiteral("save_path"), lib}}).toJson());
        if (target.startsWith("/api/v2/sync/maindata")) {
            QJsonObject torrents;
            for (const auto &[hash, name] :
                 {std::pair{kLantern, QStringLiteral("Lantern Festival")},
                  std::pair{kHarbour, QStringLiteral("Harbour Bells")}})
                torrents.insert(hash,
                                QJsonObject{{QStringLiteral("name"), name},
                                            {QStringLiteral("state"), QStringLiteral("stoppedUP")},
                                            {QStringLiteral("progress"), 1.0},
                                            {QStringLiteral("size"), kFileSize},
                                            {QStringLiteral("completed"), kFileSize},
                                            {QStringLiteral("downloaded"), kFileSize},
                                            {QStringLiteral("total_size"), kFileSize},
                                            {QStringLiteral("save_path"), savePath.value(hash)},
                                            {QStringLiteral("content_path"),
                                             savePath.value(hash) + QLatin1Char('/') + name},
                                            {QStringLiteral("added_on"), 1700000000}});
            const QJsonObject body{
                {QStringLiteral("rid"), 1},
                {QStringLiteral("full_update"), true},
                {QStringLiteral("server_state"),
                 QJsonObject{
                     {QStringLiteral("connection_status"), QStringLiteral("connected")},
                     {QStringLiteral("free_space_on_disk"), QStorageInfo(lib).bytesAvailable()}}},
                {QStringLiteral("torrents"), torrents}};
            return httpResponse(200, "OK", QJsonDocument(body).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/files")) {
            const QString hash =
                QUrlQuery(QUrl(QString::fromUtf8(target)).query()).queryItemValue("hash");
            const QString name = hash == kLantern ? QStringLiteral("Lantern Festival")
                                                  : QStringLiteral("Harbour Bells");
            const QJsonArray files{
                QJsonObject{{QStringLiteral("index"), 0},
                            {QStringLiteral("name"), name + QStringLiteral("/a.flac")},
                            {QStringLiteral("size"), kFileSize},
                            {QStringLiteral("completed"), kFileSize},
                            {QStringLiteral("downloaded"), kFileSize},
                            {QStringLiteral("progress"), 1.0},
                            {QStringLiteral("priority"), 1}}};
            return httpResponse(200, "OK", QJsonDocument(files).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/setLocation")) {
            const QUrlQuery form(QString::fromUtf8(request.body));
            const QString location =
                form.queryItemValue(QStringLiteral("location"), QUrl::FullyDecoded);
            for (const QString &hash :
                 form.queryItemValue(QStringLiteral("hashes")).split(QLatin1Char('|'))) {
                const QString name = hash == kLantern ? QStringLiteral("Lantern Festival")
                                                      : QStringLiteral("Harbour Bells");
                QDir().rename(savePath.value(hash) + QLatin1Char('/') + name,
                              location + QLatin1Char('/') + name);
                savePath.insert(hash, location);
            }
            return httpResponse(200, "OK", "");
        }
        if (target.startsWith("/api/v2/torrents/info"))
            return httpResponse(200, "OK", "[]");
        if (target.startsWith("/api/v2/torrents/categories"))
            return httpResponse(200, "OK", "{}");
        if (target.startsWith("/api/v2/log/") || target.startsWith("/api/v2/torrents/tags"))
            return httpResponse(200, "OK", "[]");
        return httpResponse(200, "OK", "Ok.");
    }
};

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
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(pending->replies > 0, 8000); }();
    QTest::qWait(20);
    [&]() { QCOMPARE(pending->replies, 1); }();
    return QJsonDocument::fromJson(pending->json.toUtf8()).object();
}

QJsonObject run(AgentService &service, const QString &options)
{
    return finish(begin(service, options));
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

QString moveJson(const QString &to)
{
    return QString::fromUtf8(
        QJsonDocument(
            QJsonObject{{QStringLiteral("action"), QStringLiteral("move")},
                        {QStringLiteral("torrents"), QJsonArray{QStringLiteral("3f9a1c0ea")}},
                        {QStringLiteral("to"), to}})
            .toJson(QJsonDocument::Compact));
}

} // namespace

class TestAppAgentMove : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void movesOffIsNotAllowed();
    void askedAllowedStartsAJobByAnAgent();
    void aPlanThatCantMoveIsRefused();
    void moveBack();
    void dryRunAsksNothing();
    void busUsage();

private:
    void start();
    int sent(const QByteArray &path) const;

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QTemporaryDir> m_proc;
    std::unique_ptr<FakeServer> m_server;
    Qbt m_qbt;
    std::unique_ptr<App> m_app;
    std::unique_ptr<AgentService> m_service;
};

void TestAppAgentMove::initTestCase()
{
    qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=/nonexistent/omaseed-no-bus"));
}

void TestAppAgentMove::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    for (const char *name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
        qputenv(name, (m_home->path() + QLatin1Char('/') + QLatin1String(name)).toUtf8());
    for (const char *name : {"OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
    // The library, and a folder to move to, on the temporary folder's drive.
    m_qbt = Qbt();
    m_qbt.lib = m_home->path() + QStringLiteral("/lib");
    QVERIFY(QDir().mkpath(m_home->path() + QStringLiteral("/music")));
    for (const auto &[hash, name] : {std::pair{kLantern, QStringLiteral("Lantern Festival")},
                                     std::pair{kHarbour, QStringLiteral("Harbour Bells")}}) {
        QVERIFY(QDir().mkpath(m_qbt.lib + QLatin1Char('/') + name));
        QFile file(m_qbt.lib + QLatin1Char('/') + name + QStringLiteral("/a.flac"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(kFileSize, 'x'));
        m_qbt.savePath.insert(hash, m_qbt.lib);
    }
    m_server = std::make_unique<FakeServer>();
    m_server->setHandler([this](const Recorded &request, int) { return m_qbt.answer(request); });
    qputenv("OMASEED_SERVER_URL", m_server->url().toString().toUtf8());
}

void TestAppAgentMove::cleanup()
{
    m_service.reset();
    m_app.reset();
    m_proc.reset();
    m_server.reset();
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

void TestAppAgentMove::start()
{
    using namespace std::chrono_literals;
    m_app = std::make_unique<App>();
    m_service = std::make_unique<AgentService>(m_app.get());
    m_app->engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    m_proc = std::make_unique<QTemporaryDir>();
    m_service->agentDo()->setProcRoot(m_proc->path());
    m_app->start();
    QTRY_VERIFY_WITH_TIMEOUT(m_app->engine()->globalState()->connected() &&
                                 m_app->engine()->torrents()->count() == 2,
                             5000);
    // This machine sees the server's disks (the free space agrees), so the sheet's checks run:
    // a folder other than the default one is read here.
    const QString music = m_home->path() + QStringLiteral("/music");
    QTRY_COMPARE_WITH_TIMEOUT(m_app->freeSpaceFor(music).value(QStringLiteral("source")).toString(),
                              QStringLiteral("local"),
                              5000);
}

int TestAppAgentMove::sent(const QByteArray &path) const
{
    int count = 0;
    for (const Recorded &request : std::as_const(m_server->requests))
        if (request.target.startsWith(path))
            ++count;
    return count;
}

// C50 GT3: the Moves line is off by default.
void TestAppAgentMove::movesOffIsNotAllowed()
{
    start();
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: move")));
    const QJsonObject object = run(*m_service, moveJson(m_home->path() + QStringLiteral("/music")));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QVERIFY2(messageOf(object).contains(QStringLiteral("Moves is off")),
             qPrintable(messageOf(object)));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
}

// C45 V15, V17: the ask says where, how much and the room; allowed, the job starts and the word
// answers at its start with its id. V18: its record, the story and the strip say by an agent.
void TestAppAgentMove::askedAllowedStartsAJobByAnAgent()
{
    start();
    m_app->setAgentMoves(QStringLiteral("ask"));
    // A hand-edited allow still asks (C50 GT3).
    m_app->setAgentMoves(QStringLiteral("allow"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    QStringList heard;
    m_service->setAsking([&heard](uint pid, const QString &words) {
        heard << QStringLiteral("%1 %2").arg(pid).arg(words);
    });
    const QString music = m_home->path() + QStringLiteral("/music");
    const auto pending = begin(*m_service, moveJson(music));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    // docs/62 §5: the asking CLI (its pid) is told, to say so before it waits.
    QCOMPARE(heard,
             QStringList{QStringLiteral("4242 Asking in omaseed's window… moves always ask.")});
    m_service->setAsking({});
    const QVariantMap request = ask->request();
    QCOMPARE(request.value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to move a torrent"));
    const QVariantList torrents = request.value(QStringLiteral("torrents")).toList();
    QCOMPARE(torrents.size(), 1);
    QCOMPARE(torrents.first().toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Lantern Festival"));
    QCOMPARE(torrents.first().toMap().value(QStringLiteral("line")).toString(), QString());
    const QString where = request.value(QStringLiteral("where")).toString();
    QVERIFY2(where.startsWith(QStringLiteral("to %1 · 4.0 KB · ").arg(music)) ||
                 where.startsWith(QStringLiteral("to %1 · 4 KB · ").arg(music)),
             qPrintable(where));
    QVERIFY2(where.endsWith(QStringLiteral(" free there")) ||
                 where.endsWith(QStringLiteral("on the same drive")),
             qPrintable(where));
    QVERIFY(request.value(QStringLiteral("note"))
                .toString()
                .contains(QStringLiteral("Moves always ask")));
    QCOMPARE(request.value(QStringLiteral("allowText")).toString(),
             QStringLiteral("allow the move"));
    QCOMPARE(pending->replies, 0);
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);

    QTest::ignoreMessage(QtInfoMsg,
                         QRegularExpression(QStringLiteral(
                             "^agent: move on Lantern Festival, allowed in the window")));
    ask->allow();
    const QJsonObject object = finish(pending);
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("allowed"));
    QCOMPARE(object.value(QLatin1String("asked")).toBool(), true);
    QVERIFY2(object.value(QLatin1String("job")).toString().startsWith(QStringLiteral("j-")),
             qPrintable(QJsonDocument(object).toJson()));
    QCOMPARE(object.value(QLatin1String("to")).toString(), music);
    QCOMPARE(object.value(QLatin1String("done")).toArray().size(), 1);
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Moving 1 torrent to %1: omaseed moves follows it").arg(music));
    QTRY_COMPARE_WITH_TIMEOUT(sent("/api/v2/torrents/setLocation"), 1, 5000);
    QCOMPARE(m_app->move()->job()->spec().value(QStringLiteral("by")).toString(),
             QStringLiteral("agent"));

    // It lands: the record keeps who started it, the story says so, and the strip.
    QTRY_VERIFY_WITH_TIMEOUT(!m_app->move()->history().isEmpty(), 8000);
    QCOMPARE(m_app->move()->history().newest()->by, QStringLiteral("agent"));
    bool said = false;
    for (const QVariant &line : m_app->torrentStory(kLantern)) {
        const QVariantMap event = line.toMap();
        if (event.value(QStringLiteral("title")).toString().startsWith(QStringLiteral("Moved to")))
            said = event.value(QStringLiteral("detail"))
                       .toString()
                       .endsWith(QStringLiteral("by an agent"));
    }
    QVERIFY(said);
    QTRY_COMPARE_WITH_TIMEOUT(
        m_app->outcomes()->current().value(QStringLiteral("agent")).toString(),
        QStringLiteral("by an agent"),
        5000);
}

// C15: a check that blocks the sheet's move refuses it, in the sheet's words, before any ask.
void TestAppAgentMove::aPlanThatCantMoveIsRefused()
{
    start();
    m_app->setAgentMoves(QStringLiteral("ask"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: move")));
    const QJsonObject object = run(*m_service, moveJson(m_qbt.lib));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QCOMPARE(messageOf(object), QStringLiteral("Not moved: It is already at %1.").arg(m_qbt.lib));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
}

// C18 D7, PLAN Q7: move-back takes a landed move's id (or 8+ of its first characters); one it
// doesn't keep is refused; a landed one asks, each torrent "back to" where it was.
void TestAppAgentMove::moveBack()
{
    start();
    m_app->setAgentMoves(QStringLiteral("ask"));
    // Only the refusal in the window is noted: an unknown id is the agent's mistake.
    QTest::ignoreMessage(QtInfoMsg,
                         QRegularExpression(QStringLiteral("^agent: move-back on Lantern Festival, "
                                                           "refused in the window")));
    QJsonObject object =
        run(*m_service, QStringLiteral(R"({"action":"move-back","move":"nosuchmove"})"));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY2(messageOf(object).contains(QStringLiteral("keeps no landed move nosuchmove")),
             qPrintable(messageOf(object)));

    // A move of the window's own, landed.
    QVERIFY(m_app->move()->startSelection(
        {kLantern}, m_home->path() + QStringLiteral("/music"), QStringLiteral("oneFolder")));
    QTRY_VERIFY_WITH_TIMEOUT(!m_app->move()->history().isEmpty(), 8000);
    QVERIFY(m_app->move()->history().newest()->by.isEmpty());
    const QString id = m_app->move()->history().newest()->id;

    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending =
        begin(*m_service, QStringLiteral(R"({"action":"move-back","move":"%1"})").arg(id.left(8)));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    QCOMPARE(ask->request().value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to move a torrent back"));
    QCOMPARE(ask->request()
                 .value(QStringLiteral("torrents"))
                 .toList()
                 .first()
                 .toMap()
                 .value(QStringLiteral("line"))
                 .toString(),
             QStringLiteral("back to %1").arg(m_qbt.lib));
    QVERIFY(ask->request()
                .value(QStringLiteral("where"))
                .toString()
                .startsWith(QStringLiteral("each back to where it was · ")));
    QCOMPARE(ask->request().value(QStringLiteral("allowText")).toString(),
             QStringLiteral("allow the move back"));
    ask->refuse();
    object = finish(pending);
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("refused"));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QCOMPARE(m_qbt.savePath.value(kLantern), m_home->path() + QStringLiteral("/music"));
}

// GT5: every check made, nothing asked, nothing sent.
void TestAppAgentMove::dryRunAsksNothing()
{
    start();
    m_app->setAgentMoves(QStringLiteral("ask"));
    const QString music = m_home->path() + QStringLiteral("/music");
    QJsonObject options = QJsonDocument::fromJson(moveJson(music).toUtf8()).object();
    options.insert(QStringLiteral("dryRun"), true);
    const QJsonObject object =
        run(*m_service, QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact)));
    QCOMPARE(object.value(QLatin1String("dryRun")).toBool(), true);
    QCOMPARE(object.value(QLatin1String("would")).toString(), QStringLiteral("ask"));
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("move would ask in omaseed's window · 1 torrent to %1").arg(music));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
}

// What busctl might send that the CLI never does.
void TestAppAgentMove::busUsage()
{
    start();
    m_app->setAgentMoves(QStringLiteral("ask"));
    for (const char *options :
         {R"({"action":"move","torrents":["3f9a1c0ea"]})",
          R"({"action":"move","to":"/x"})",
          R"({"action":"move-back"})",
          R"({"action":"move-back","move":"abcdefgh","torrents":["3f9a1c0ea"]})"}) {
        const QJsonObject object = run(*m_service, QString::fromUtf8(options));
        QCOMPARE(codeOf(object), QStringLiteral("usage"));
    }
}

QTEST_GUILESS_MAIN(TestAppAgentMove)
#include "tst_app_agentmove.moc"
