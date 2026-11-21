// #296 (DECISIONS C50 GT4, GT5; board 67 §4, §6): `omaseed do --plan`, the app's side,
// against the stand-in qBittorrent of tst_app_agentremove (two torrents whose files are gone,
// their save path a temporary folder this machine sees): one off or refused line runs none
// and says which; a plan Settings allows runs without an ask; a mixed one asks once, grouped,
// and a box left unticked is said so; a line refused as it runs (a setting turned off during
// the ask) fails alone; a refused ask runs none; a dry run asks and sends nothing; and the
// app's own keys are refused from the bus.

#include "core/GlobalState.hpp"
#include "core/RemovedKeep.hpp"
#include "core/Server.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/AgentAsk.hpp"
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
using omaseed::core::RemovedKeep;

namespace {

// tests/fixtures/fixtures.json: single_file_v1.torrent and multi_file_v1_unicode.torrent.
const QString kSingle = QStringLiteral("857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04");
const QString kPack = QStringLiteral("6f04b6c7642d15dc5cfa6cd2478e7f128a0e556f");

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR) + QLatin1Char('/') + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// One torrent as the stand-in keeps it.
struct Item
{
    QString name;
    QString state = QStringLiteral("missingFiles");
    QString savePath;
    QString fixture; ///< its .torrent in tests/fixtures; "" fails its export
    QList<std::pair<QString, qint64>> files; ///< relative to the save path
};

/// Just enough qBittorrent 5 for a remove: the list, file lists, the export Backup reads, the
/// delete (recorded with its deleteFiles), and an add putting a torrent back.
struct Qbt
{
    QString savePath;
    QString localSavePath;
    QHash<QString, Item> items;
    QHash<QString, Item> removed; ///< what torrents/delete took, for an add to bring back
    QStringList deleteFiles;      ///< each torrents/delete's deleteFiles
    QByteArray lastAdd;

    QJsonObject info(const QString &hash, const Item &item) const
    {
        qint64 size = 0;
        for (const auto &file : item.files)
            size += file.second;
        return QJsonObject{
            {QStringLiteral("hash"), hash},
            {QStringLiteral("infohash_v1"), hash},
            {QStringLiteral("name"), item.name},
            {QStringLiteral("state"), item.state},
            {QStringLiteral("progress"), 1.0},
            {QStringLiteral("size"), size},
            {QStringLiteral("total_size"), size},
            {QStringLiteral("save_path"), item.savePath},
            {QStringLiteral("download_path"), QString()},
            {QStringLiteral("has_metadata"), true},
            {QStringLiteral("content_path"), item.savePath + QLatin1Char('/') + item.name},
            {QStringLiteral("added_on"), 1700000000}};
    }

    QByteArray answer(const Recorded &request)
    {
        const QByteArray target = request.target;
        const QUrlQuery query(QUrl(QString::fromUtf8(target)).query());
        if (target.startsWith("/api/v2/app/version"))
            return httpResponse(200, "OK", "v5.2.3");
        if (target.startsWith("/api/v2/app/webapiVersion"))
            return httpResponse(200, "OK", "2.15.1");
        if (target.startsWith("/api/v2/app/preferences"))
            return httpResponse(
                200,
                "OK",
                QJsonDocument(QJsonObject{{QStringLiteral("save_path"), savePath}}).toJson());
        if (target.startsWith("/api/v2/sync/maindata")) {
            QJsonObject torrents;
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                torrents.insert(it.key(), info(it.key(), it.value()));
            const QJsonObject body{
                {QStringLiteral("rid"), 1},
                {QStringLiteral("full_update"), true},
                {QStringLiteral("server_state"),
                 QJsonObject{{QStringLiteral("connection_status"), QStringLiteral("connected")},
                             {QStringLiteral("free_space_on_disk"),
                              QStorageInfo(localSavePath).bytesAvailable()}}},
                {QStringLiteral("torrents"), torrents}};
            return httpResponse(200, "OK", QJsonDocument(body).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/info")) {
            const QStringList only = query.queryItemValue(QStringLiteral("hashes"))
                                         .split(QLatin1Char('|'), Qt::SkipEmptyParts);
            QJsonArray out;
            for (auto it = items.cbegin(); it != items.cend(); ++it)
                if (only.isEmpty() || only.contains(it.key()))
                    out.append(info(it.key(), it.value()));
            return httpResponse(200, "OK", QJsonDocument(out).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/files")) {
            const Item item = items.value(query.queryItemValue(QStringLiteral("hash")));
            QJsonArray files;
            int index = 0;
            for (const auto &file : item.files)
                files.append(QJsonObject{{QStringLiteral("index"), index++},
                                         {QStringLiteral("name"), file.first},
                                         {QStringLiteral("size"), file.second},
                                         {QStringLiteral("progress"), 1.0},
                                         {QStringLiteral("priority"), 1}});
            return httpResponse(200, "OK", QJsonDocument(files).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/export")) {
            const Item item = items.value(query.queryItemValue(QStringLiteral("hash")));
            if (item.fixture.isEmpty())
                return httpResponse(500, "Internal Server Error", "disk read error");
            return httpResponse(200, "OK", fixture(item.fixture));
        }
        if (target.startsWith("/api/v2/torrents/delete")) {
            const QUrlQuery form(QString::fromUtf8(request.body));
            deleteFiles << form.queryItemValue(QStringLiteral("deleteFiles"));
            for (const QString &hash :
                 form.queryItemValue(QStringLiteral("hashes")).split(QLatin1Char('|')))
                if (items.contains(hash))
                    removed.insert(hash, items.take(hash));
            return httpResponse(200, "OK", "");
        }
        if (target.startsWith("/api/v2/torrents/add")) {
            lastAdd = request.body;
            for (auto it = removed.begin(); it != removed.end(); ++it) {
                Item item = it.value();
                item.state = QStringLiteral("stoppedDL");
                items.insert(it.key(), item);
            }
            removed.clear();
            return httpResponse(200, "OK", "Ok.");
        }
        if (target.startsWith("/api/v2/torrents/categories"))
            return httpResponse(200, "OK", "{}");
        if (target.startsWith("/api/v2/log/") || target.startsWith("/api/v2/torrents/tags") ||
            target.startsWith("/api/v2/torrents/trackers"))
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

bool writeFile(const QString &path, qint64 size)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QByteArray(size, 'x'));
    return true;
}

/// A plan's options as the CLI sends them: the file's text.
QString planJson(const QString &plan, bool dryRun = false)
{
    QJsonObject object{{QStringLiteral("plan"), plan}};
    if (dryRun)
        object.insert(QStringLiteral("dryRun"), true);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

/// recheck the single file torrent, remove the pack (or both, `both`).
QString mixedPlan(bool both = false)
{
    return QStringLiteral(R"({"v":1,"lines":[{"action":"recheck","torrents":["%1"]},)"
                          R"({"action":"remove","torrents":[%2]}]})")
        .arg(kSingle.left(8),
             both ? QStringLiteral("\"%1\",\"%2\"").arg(kPack.left(8), kSingle.left(8))
                  : QStringLiteral("\"%1\"").arg(kPack.left(8)));
}

QJsonObject lineOf(const QJsonObject &answer, int i)
{
    return answer.value(QLatin1String("lines")).toArray().at(i).toObject();
}

} // namespace

class TestAppAgentPlan : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void oneOffLineRunsNone();
    void oneRefusedLineRunsNone();
    void allowedPlanRunsWithoutAnAsk();
    void mixedAsksOnceAndSaysUnticked();
    void lineRefusedAfterTheAskFailsAlone();
    void refusedAskRunsNone();
    void dryRunAsksAndSendsNothing();
    void usageFromTheBus();

private:
    void start(bool viaEnv = true);
    int sent(const QByteArray &path) const;
    QString path(const QString &rel) const { return m_home->path() + QLatin1Char('/') + rel; }

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QTemporaryDir> m_proc;
    std::unique_ptr<FakeServer> m_server;
    Qbt m_qbt;
    std::unique_ptr<App> m_app;
    std::unique_ptr<AgentService> m_service;
};

void TestAppAgentPlan::initTestCase()
{
    qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=/nonexistent/omaseed-no-bus"));
}

void TestAppAgentPlan::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    for (const char *name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
        qputenv(name, (m_home->path() + QLatin1Char('/') + QLatin1String(name)).toUtf8());
    for (const char *name : {"OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
    m_qbt = Qbt();
    m_qbt.savePath = path(QStringLiteral("lib"));
    m_qbt.localSavePath = m_qbt.savePath;
    QVERIFY(QDir().mkpath(m_qbt.savePath));
    Item single;
    single.name = QStringLiteral("Single.bin");
    single.savePath = m_qbt.savePath;
    single.fixture = QStringLiteral("single_file_v1.torrent");
    single.files = {{QStringLiteral("Single.bin"), 50000}};
    Item pack;
    pack.name = QStringLiteral("Unicode Pack");
    pack.savePath = m_qbt.savePath;
    pack.fixture = QStringLiteral("multi_file_v1_unicode.torrent");
    pack.files = {{QStringLiteral("Unicode Pack/a.txt"), 2000},
                  {QStringLiteral("Unicode Pack/b.txt"), 2226}};
    m_qbt.items.insert(kSingle, single);
    m_qbt.items.insert(kPack, pack);
    m_server = std::make_unique<FakeServer>();
    m_server->setHandler([this](const Recorded &request, int) { return m_qbt.answer(request); });
}

void TestAppAgentPlan::cleanup()
{
    m_service.reset();
    m_app.reset();
    m_proc.reset();
    m_server.reset();
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

void TestAppAgentPlan::start(bool viaEnv)
{
    using namespace std::chrono_literals;
    if (viaEnv)
        qputenv("OMASEED_SERVER_URL", m_server->url().toString().toUtf8());
    m_app = std::make_unique<App>();
    m_service = std::make_unique<AgentService>(m_app.get());
    m_app->engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    m_proc = std::make_unique<QTemporaryDir>();
    m_service->agentDo()->setProcRoot(m_proc->path());
    m_app->start();
    QTRY_VERIFY_WITH_TIMEOUT(m_app->engine()->globalState()->connected() &&
                                 m_app->engine()->torrents()->count() == 2,
                             5000);
    if (viaEnv)
        QTRY_VERIFY_WITH_TIMEOUT(m_app->freeSpaceProbe().seesServerDisks(), 5000);
}

int TestAppAgentPlan::sent(const QByteArray &prefix) const
{
    int count = 0;
    for (const Recorded &request : std::as_const(m_server->requests))
        if (request.target.startsWith(prefix))
            ++count;
    return count;
}

// GT4: one line off (Remove is off by default) and nothing runs, the recheck included.
void TestAppAgentPlan::oneOffLineRunsNone()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QJsonObject object = run(*m_service, planJson(mixedPlan()));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QVERIFY2(messageOf(object).startsWith(
                 QStringLiteral("1 line off: nothing ran. Fix the plan and try again. Line 2 "
                                "(remove): omaseed doesn't let agents remove")),
             qPrintable(messageOf(object)));
    QVERIFY(object.value(QLatin1String("plan")).toBool());
    QCOMPARE(lineOf(object, 0).value(QLatin1String("would")).toString(), QStringLiteral("run"));
    QCOMPARE(lineOf(object, 1).value(QLatin1String("would")).toString(), QStringLiteral("off"));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// GT4 with GT1's guard: a remove whose files are there after all refuses the plan.
void TestAppAgentPlan::oneRefusedLineRunsNone()
{
    QVERIFY(writeFile(m_qbt.savePath + QStringLiteral("/Unicode Pack/a.txt"), 10));
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentRemove(QStringLiteral("ask"));
    const QJsonObject object = run(*m_service, planJson(mixedPlan()));
    QCOMPARE(codeOf(object), QStringLiteral("not-removable"));
    QCOMPARE(lineOf(object, 1).value(QLatin1String("would")).toString(), QStringLiteral("refused"));
    QVERIFY(lineOf(object, 1)
                .value(QLatin1String("error"))
                .toObject()
                .value(QLatin1String("message"))
                .toString()
                .contains(QStringLiteral("Unicode Pack has its files")));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// Every line allowed in Settings: no window, each line run and answered.
void TestAppAgentPlan::allowedPlanRunsWithoutAnAsk()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QString plan = QStringLiteral(R"({"lines":[{"action":"recheck","torrents":["%1"]},)"
                                        R"({"action":"reannounce","torrents":["%2","%1"]}]})")
                             .arg(kSingle.left(8), kPack.left(8));
    const QJsonObject object = run(*m_service, planJson(plan));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QVERIFY(!object.value(QLatin1String("asked")).toBool());
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("not-asked"));
    QCOMPARE(lineOf(object, 0).value(QLatin1String("done")).toArray().size(), 1);
    QCOMPARE(lineOf(object, 1).value(QLatin1String("done")).toArray().size(), 2);
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Done: 3 of 3 · by an agent"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 1);
    QCOMPARE(sent("/api/v2/torrents/reannounce"), 1);
}

// GT4: one window for the lot, grouped; the allowed line dim, a box per torrent that asks;
// a box left unticked runs nothing and is said.
void TestAppAgentPlan::mixedAsksOnceAndSaysUnticked()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentRemove(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending = begin(*m_service, planJson(mixedPlan(true)));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    const QVariantMap request = ask->request();
    QVERIFY(request.value(QStringLiteral("plan")).toBool());
    QCOMPARE(request.value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to run a plan"));
    QCOMPARE(request.value(QStringLiteral("allowText")).toString(), QStringLiteral("allow these"));
    QCOMPARE(request.value(QStringLiteral("word")).toString(), QStringLiteral("omaseed do --plan"));
    const QVariantList groups = request.value(QStringLiteral("groups")).toList();
    QCOMPARE(groups.size(), 2);
    const QVariantMap allowed = groups.at(0).toMap();
    QVERIFY(!allowed.value(QStringLiteral("asks")).toBool());
    QCOMPARE(allowed.value(QStringLiteral("head")).toString(), QStringLiteral("Recheck 1"));
    QVERIFY(allowed.value(QStringLiteral("rows")).toList().isEmpty());
    const QVariantMap removes = groups.at(1).toMap();
    QVERIFY(removes.value(QStringLiteral("asks")).toBool());
    QVERIFY2(removes.value(QStringLiteral("head"))
                 .toString()
                 .startsWith(QStringLiteral("Remove 2 from ")),
             qPrintable(removes.value(QStringLiteral("head")).toString()));
    QCOMPARE(removes.value(QStringLiteral("more")).toString(),
             QStringLiteral("none of their files is on disk (checked) · each kept 30 days to add "
                            "back"));
    const QVariantList rows = removes.value(QStringLiteral("rows")).toList();
    QCOMPARE(rows.size(), 2);
    QCOMPARE(rows.at(0).toMap().value(QStringLiteral("key")).toString(),
             QStringLiteral("1:") + kPack);
    QVERIFY(rows.at(0).toMap().value(QStringLiteral("line")).toString().startsWith(m_qbt.savePath));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0); // nothing before the answer

    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^removed: .* kept for 30")));
    ask->allow({QStringLiteral("1:") + kPack});
    const QJsonObject object = finish(pending);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QVERIFY(object.value(QLatin1String("asked")).toBool());
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("allowed"));
    QCOMPARE(lineOf(object, 0).value(QLatin1String("done")).toArray().size(), 1);
    QVERIFY(!lineOf(object, 0).contains(QLatin1String("unticked")));
    const QJsonObject removed = lineOf(object, 1);
    QCOMPARE(removed.value(QLatin1String("done")).toArray().size(), 1);
    QCOMPARE(
        removed.value(QLatin1String("done")).toArray().first().toObject().value("hash").toString(),
        kPack);
    const QJsonArray unticked = removed.value(QLatin1String("unticked")).toArray();
    QCOMPARE(unticked.size(), 1);
    QCOMPARE(unticked.first().toObject().value(QLatin1String("hash")).toString(), kSingle);
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Done: 2 of 3 · by an agent"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 1);
    QCOMPARE(sent("/api/v2/torrents/delete"), 1);
    QVERIFY(m_qbt.items.contains(kSingle));
    QVERIFY(!m_qbt.items.contains(kPack));
    QVERIFY(!ask->isOpen());
}

// GT4: each line checked again as it runs; Fixes turned off during the ask fails the recheck
// alone, and the remove the person allowed still runs.
void TestAppAgentPlan::lineRefusedAfterTheAskFailsAlone()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentRemove(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending = begin(*m_service, planJson(mixedPlan()));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    m_app->setAgentFixes(QStringLiteral("off"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^removed: .* kept for 30")));
    ask->allow({QStringLiteral("1:") + kPack});
    const QJsonObject object = finish(pending);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    const QJsonArray failed = lineOf(object, 0).value(QLatin1String("failed")).toArray();
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().toObject().value(QLatin1String("code")).toString(),
             QStringLiteral("not-allowed"));
    QCOMPARE(failed.first().toObject().value(QLatin1String("hash")).toString(), kSingle);
    QCOMPARE(lineOf(object, 1).value(QLatin1String("done")).toArray().size(), 1);
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Done: 1 of 2 · by an agent"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
    QCOMPARE(sent("/api/v2/torrents/delete"), 1);
}

// V17: refuse (esc, close) runs none of the plan, the allowed lines included.
void TestAppAgentPlan::refusedAskRunsNone()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentRemove(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending = begin(*m_service, planJson(mixedPlan()));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: plan on")));
    ask->refuse();
    const QJsonObject object = finish(pending);
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY(object.value(QLatin1String("plan")).toBool());
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("refused"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// GT5: every check, line by line; nothing asked or sent. 0 would run or ask, 5 one off.
void TestAppAgentPlan::dryRunAsksAndSendsNothing()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    m_app->setAgentRemove(QStringLiteral("ask"));
    QJsonObject object = run(*m_service, planJson(mixedPlan(), true));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QVERIFY(object.value(QLatin1String("dryRun")).toBool());
    QCOMPARE(lineOf(object, 0).value(QLatin1String("would")).toString(), QStringLiteral("run"));
    QCOMPARE(lineOf(object, 1).value(QLatin1String("would")).toString(), QStringLiteral("ask"));
    QCOMPARE(lineOf(object, 1).value(QLatin1String("torrents")).toArray().size(), 1);
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Would ask once, for 1 remove."));
    QCOMPARE(object.value(QLatin1String("torrents")).toInt(), 2);
    QVERIFY(!m_app->agentAsk()->isOpen());

    m_app->setAgentRemove(QStringLiteral("off"));
    object = run(*m_service, planJson(mixedPlan(), true));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QVERIFY(messageOf(object).startsWith(QStringLiteral("1 line off: nothing would run.")));
    QCOMPARE(lineOf(object, 1).value(QLatin1String("would")).toString(), QStringLiteral("off"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
    QCOMPARE(sent("/api/v2/torrents/export"), 0);
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// A plan that doesn't read is usage; the app's own keys never come from the bus.
void TestAppAgentPlan::usageFromTheBus()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    QJsonObject object = run(*m_service, planJson(QStringLiteral(R"({"lines":[]})")));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(*m_service,
                 QStringLiteral(R"({"action":"recheck","torrents":["%1"],"probe":true})")
                     .arg(kSingle.left(8)));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(*m_service,
                 QStringLiteral(R"({"action":"recheck","torrents":["%1"],"inPlan":true})")
                     .arg(kSingle.left(8)));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    object = run(
        *m_service,
        QStringLiteral(R"({"plan":"{\"lines\":[{\"action\":\"pause\"}]}","action":"recheck"})"));
    QCOMPARE(codeOf(object), QStringLiteral("usage"));
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

QTEST_GUILESS_MAIN(TestAppAgentPlan)
#include "tst_app_agentplan.moc"
