// #294 (DECISIONS C50 GT2, C45 V15, V17): `omaseed do locate <hash…> --to FOLDER`, the app's
// side, against a stand-in qBittorrent whose save paths are a temporary folder this machine
// sees: every file found → set location (nothing moved) and a recheck, automatic management's
// going off said; a short find → not-found with found and files, nothing sent; a torrent whose
// files aren't missing refused; qBittorrent's incomplete folder refused (PLAN Q10); the ask
// showing the folder and the find; a path map rewriting FOLDER, and a FOLDER it doesn't cover
// refused; a dry run sending nothing.

#include "core/GlobalState.hpp"
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

namespace {

const QString kLantern = QStringLiteral("71ad02bc") + QString(32, QLatin1Char('a'));
const QString kHarbour = QStringLiteral("e50f9a13") + QString(32, QLatin1Char('b'));
constexpr qint64 kFileSize = 4096;

/// One torrent as the stand-in keeps it.
struct Item
{
    QString name;
    QString state = QStringLiteral("missingFiles");
    QString savePath; ///< as the server says it
    QString downloadPath;
    double progress = 1.0;
    bool autoTmm = false;
    QStringList files; ///< relative to the save path
};

/// Just enough qBittorrent 5 for a locate: two torrents, their files, set location (recorded,
/// moving nothing: the files are already where it points) and recheck.
struct Qbt
{
    QString savePath;      ///< the default save path, as the server says it
    QString localSavePath; ///< the same folder on this machine (its free space)
    QHash<QString, Item> items;

    QByteArray answer(const Recorded &request)
    {
        const QByteArray target = request.target;
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
            for (auto it = items.cbegin(); it != items.cend(); ++it) {
                const Item &item = it.value();
                const qint64 size = kFileSize * item.files.size();
                torrents.insert(it.key(),
                                QJsonObject{{QStringLiteral("name"), item.name},
                                            {QStringLiteral("state"), item.state},
                                            {QStringLiteral("progress"), item.progress},
                                            {QStringLiteral("size"), size},
                                            {QStringLiteral("total_size"), size},
                                            {QStringLiteral("save_path"), item.savePath},
                                            {QStringLiteral("download_path"), item.downloadPath},
                                            {QStringLiteral("auto_tmm"), item.autoTmm},
                                            {QStringLiteral("content_path"),
                                             item.savePath + QLatin1Char('/') + item.name},
                                            {QStringLiteral("added_on"), 1700000000}});
            }
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
        if (target.startsWith("/api/v2/torrents/files")) {
            const QString hash =
                QUrlQuery(QUrl(QString::fromUtf8(target)).query()).queryItemValue("hash");
            const Item item = items.value(hash);
            QJsonArray files;
            int index = 0;
            for (const QString &path : item.files)
                files.append(QJsonObject{{QStringLiteral("index"), index++},
                                         {QStringLiteral("name"), path},
                                         {QStringLiteral("size"), kFileSize},
                                         {QStringLiteral("progress"), item.progress},
                                         {QStringLiteral("priority"), 1}});
            return httpResponse(200, "OK", QJsonDocument(files).toJson(QJsonDocument::Compact));
        }
        if (target.startsWith("/api/v2/torrents/setLocation")) {
            const QUrlQuery form(QString::fromUtf8(request.body));
            const QString location =
                form.queryItemValue(QStringLiteral("location"), QUrl::FullyDecoded);
            for (const QString &hash :
                 form.queryItemValue(QStringLiteral("hashes")).split(QLatin1Char('|'))) {
                items[hash].savePath = location;
                items[hash].autoTmm = false;
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

QString locateJson(const QString &hash, const QString &to, bool dryRun = false)
{
    QJsonObject object{{QStringLiteral("action"), QStringLiteral("locate")},
                       {QStringLiteral("torrents"), QJsonArray{hash.left(8)}},
                       {QStringLiteral("to"), to}};
    if (dryRun)
        object.insert(QStringLiteral("dryRun"), true);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

bool writeFile(const QString &path, qint64 size = kFileSize)
{
    QDir().mkpath(QFileInfo(path).path());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QByteArray(size, 'x'));
    return true;
}

} // namespace

class TestAppAgentLocate : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void fixesOffIsNotAllowed();
    void foundRepointsAndRechecks();
    void noneFoundIsNotFound();
    void partialFindIsNotFound();
    void filesNotMissingIsRefused();
    void incompleteFolderIsRefused();
    void askShowsTheFolderAndTheFind();
    void dryRunSendsNothing();
    void pathMapRewritesTheFolder();

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

void TestAppAgentLocate::initTestCase()
{
    qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=/nonexistent/omaseed-no-bus"));
}

void TestAppAgentLocate::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    for (const char *name :
         {"XDG_CONFIG_HOME", "XDG_DATA_HOME", "XDG_STATE_HOME", "XDG_CACHE_HOME"})
        qputenv(name, (m_home->path() + QLatin1Char('/') + QLatin1String(name)).toUtf8());
    for (const char *name : {"OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
    // The library the torrents say they're in (their files gone from it), and the folder the
    // agent found them in.
    m_qbt = Qbt();
    m_qbt.savePath = path(QStringLiteral("lib"));
    m_qbt.localSavePath = m_qbt.savePath;
    QVERIFY(QDir().mkpath(m_qbt.savePath));
    QVERIFY(QDir().mkpath(path(QStringLiteral("empty"))));
    Item lantern;
    lantern.name = QStringLiteral("Lantern Festival");
    lantern.savePath = m_qbt.savePath;
    lantern.autoTmm = true;
    lantern.files = {QStringLiteral("Lantern Festival/a.flac"),
                     QStringLiteral("Lantern Festival/b.flac")};
    Item harbour;
    harbour.name = QStringLiteral("Harbour Bells");
    harbour.savePath = m_qbt.savePath;
    harbour.files = {QStringLiteral("Harbour Bells/01.flac"),
                     QStringLiteral("Harbour Bells/02.flac"),
                     QStringLiteral("Harbour Bells/03.flac")};
    m_qbt.items.insert(kLantern, lantern);
    m_qbt.items.insert(kHarbour, harbour);
    for (const QString &file : lantern.files)
        QVERIFY(writeFile(path(QStringLiteral("found/") + file)));
    // Harbour Bells: two of three found.
    QVERIFY(writeFile(path(QStringLiteral("found/Harbour Bells/01.flac"))));
    QVERIFY(writeFile(path(QStringLiteral("found/Harbour Bells/03.flac"))));
    m_server = std::make_unique<FakeServer>();
    m_server->setHandler([this](const Recorded &request, int) { return m_qbt.answer(request); });
}

void TestAppAgentLocate::cleanup()
{
    m_service.reset();
    m_app.reset();
    m_proc.reset();
    m_server.reset();
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

void TestAppAgentLocate::start(bool viaEnv)
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
    // This machine sees the server's disks (the free space agrees).
    QTRY_VERIFY_WITH_TIMEOUT(m_app->freeSpaceProbe().seesServerDisks(), 5000);
}

int TestAppAgentLocate::sent(const QByteArray &prefix) const
{
    int count = 0;
    for (const Recorded &request : std::as_const(m_server->requests))
        if (request.target.startsWith(prefix))
            ++count;
    return count;
}

// C50 GT3: locate is a fix, and the Fixes line is off by default.
void TestAppAgentLocate::fixesOffIsNotAllowed()
{
    start();
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: locate")));
    const QJsonObject object = run(*m_service, locateJson(kLantern, path(QStringLiteral("found"))));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
}

// GT2: every file found → set location over them and a recheck; the answer says found, files,
// rechecking, and that qBittorrent turned automatic management off.
void TestAppAgentLocate::foundRepointsAndRechecks()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QString found = path(QStringLiteral("found"));
    QTest::ignoreMessage(QtInfoMsg,
                         QRegularExpression(QStringLiteral("^agent: locate on Lantern Festival")));
    const QJsonObject object = run(*m_service, locateJson(kLantern, found));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QCOMPARE(object.value(QLatin1String("action")).toString(), QStringLiteral("locate"));
    QCOMPARE(object.value(QLatin1String("to")).toString(), found);
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("not-asked"));
    const QJsonArray done = object.value(QLatin1String("done")).toArray();
    QCOMPARE(done.size(), 1);
    const QJsonObject line = done.first().toObject();
    QCOMPARE(line.value(QLatin1String("found")).toInt(), 2);
    QCOMPARE(line.value(QLatin1String("files")).toInt(), 2);
    QVERIFY(line.value(QLatin1String("rechecking")).toBool());
    QVERIFY(line.value(QLatin1String("managementOff")).toBool());
    const QString outcome = object.value(QLatin1String("outcome")).toString();
    QVERIFY2(outcome.startsWith(QStringLiteral("Located · Lantern Festival · rechecking")),
             qPrintable(outcome));
    QVERIFY2(outcome.contains(QStringLiteral("Automatic management went off")),
             qPrintable(outcome));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 1);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 1);
    // Nothing moved on disk.
    QVERIFY(QFile::exists(found + QStringLiteral("/Lantern Festival/a.flac")));
    QCOMPARE(m_qbt.items.value(kLantern).savePath, found);
}

// docs/67 §2: found 0 → not-found (exit 5), found and files on the line, nothing sent.
void TestAppAgentLocate::noneFoundIsNotFound()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QString empty = path(QStringLiteral("empty"));
    const QJsonObject object = run(*m_service, locateJson(kLantern, empty));
    QCOMPARE(codeOf(object), QStringLiteral("not-found"));
    QVERIFY2(messageOf(object).startsWith(
                 QStringLiteral("Not located: found 0 of 2 files in %1.").arg(empty)),
             qPrintable(messageOf(object)));
    QVERIFY2(messageOf(object).contains(QStringLiteral("“Lantern Festival/”")),
             qPrintable(messageOf(object)));
    const QJsonArray failed = object.value(QLatin1String("failed")).toArray();
    QCOMPARE(failed.size(), 1);
    QCOMPARE(failed.first().toObject().value(QLatin1String("code")).toString(),
             QStringLiteral("not-found"));
    QCOMPARE(failed.first().toObject().value(QLatin1String("found")).toInt(), 0);
    QCOMPARE(failed.first().toObject().value(QLatin1String("files")).toInt(), 2);
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

void TestAppAgentLocate::partialFindIsNotFound()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QJsonObject object = run(*m_service, locateJson(kHarbour, path(QStringLiteral("found"))));
    QCOMPARE(codeOf(object), QStringLiteral("not-found"));
    QCOMPARE(messageOf(object),
             QStringLiteral("Not located: found 2 of 3 files; 1 missing, the first "
                            "“02.flac”. A recheck there would download them again."));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
}

// GT2: a torrent whose files are all where it is isn't missing them: that's a move.
void TestAppAgentLocate::filesNotMissingIsRefused()
{
    m_qbt.items[kLantern].state = QStringLiteral("stoppedUP");
    for (const QString &file : m_qbt.items.value(kLantern).files)
        QVERIFY(writeFile(m_qbt.savePath + QLatin1Char('/') + file));
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QJsonObject object = run(*m_service, locateJson(kLantern, path(QStringLiteral("found"))));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY2(messageOf(object).contains(QStringLiteral("aren't missing")) &&
                 messageOf(object).contains(QStringLiteral("omaseed do move 71ad02bc")),
             qPrintable(messageOf(object)));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
}

// PLAN Q10: a torrent not complete with its own incomplete folder is the window's.
void TestAppAgentLocate::incompleteFolderIsRefused()
{
    m_qbt.items[kLantern].downloadPath = path(QStringLiteral("incomplete"));
    m_qbt.items[kLantern].progress = 0.5;
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    const QJsonObject object = run(*m_service, locateJson(kLantern, path(QStringLiteral("found"))));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY2(messageOf(object).contains(QStringLiteral("incomplete folder")),
             qPrintable(messageOf(object)));
}

// C45 V17: the ask shows the folder, found N of N and automatic management going off.
void TestAppAgentLocate::askShowsTheFolderAndTheFind()
{
    start();
    m_app->setAgentFixes(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const QString found = path(QStringLiteral("found"));
    const auto pending = begin(*m_service, locateJson(kLantern, found));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    const QVariantMap request = ask->request();
    QCOMPARE(request.value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to locate a torrent"));
    const QVariantList torrents = request.value(QStringLiteral("torrents")).toList();
    QCOMPARE(torrents.size(), 1);
    const QString line = torrents.first().toMap().value(QStringLiteral("line")).toString();
    QVERIFY2(line.startsWith(QStringLiteral("found 2 of 2 files · ")) &&
                 line.endsWith(QStringLiteral("was in %1").arg(m_qbt.savePath)),
             qPrintable(line));
    QCOMPARE(request.value(QStringLiteral("where")).toString(),
             QStringLiteral("in %1 · nothing moves, then a recheck").arg(found));
    QVERIFY(request.value(QStringLiteral("checks"))
                .toStringList()
                .join(QLatin1Char(' '))
                .contains(QStringLiteral("Automatic management goes off for it")));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
    QTest::ignoreMessage(
        QtInfoMsg,
        QRegularExpression(QStringLiteral("^agent: locate on Lantern Festival, allowed in the "
                                          "window")));
    ask->allow();
    const QJsonObject object = finish(pending);
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("allowed"));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 1);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 1);
}

// GT5: the find runs, nothing is sent.
void TestAppAgentLocate::dryRunSendsNothing()
{
    start();
    m_app->setAgentFixes(QStringLiteral("ask"));
    const QJsonObject object =
        run(*m_service, locateJson(kLantern, path(QStringLiteral("found")), true));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QVERIFY(object.value(QLatin1String("dryRun")).toBool());
    QCOMPARE(object.value(QLatin1String("would")).toString(), QStringLiteral("ask"));
    const QJsonObject line = object.value(QLatin1String("torrents")).toArray().first().toObject();
    QCOMPARE(line.value(QLatin1String("found")).toInt(), 2);
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);
    QCOMPARE(sent("/api/v2/torrents/recheck"), 0);
}

// C14 E11: FOLDER is the server's path; the path map gives this machine's. A FOLDER no
// mapping covers can't be checked: refused.
void TestAppAgentLocate::pathMapRewritesTheFolder()
{
    m_qbt.savePath = QStringLiteral("/srv/media/lib");
    for (Item &item : m_qbt.items)
        item.savePath = m_qbt.savePath;
    {
        omaseed::core::Server server;
        server.id = QStringLiteral("nas");
        server.name = QStringLiteral("nas");
        server.url = m_server->url();
        server.pathMap.append({QStringLiteral("/srv/media"), m_home->path()});
        omaseed::core::ServerStore store;
        QVERIFY(store.add(server));
    }
    start(false);
    m_app->setAgentFixes(QStringLiteral("allow"));

    QJsonObject object = run(*m_service, locateJson(kLantern, QStringLiteral("/elsewhere/found")));
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QVERIFY2(messageOf(object).contains(QStringLiteral("can't see /elsewhere/found")),
             qPrintable(messageOf(object)));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 0);

    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: locate")));
    object = run(*m_service, locateJson(kLantern, QStringLiteral("/srv/media/found")));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QCOMPARE(sent("/api/v2/torrents/setLocation"), 1);
    QCOMPARE(m_qbt.items.value(kLantern).savePath, QStringLiteral("/srv/media/found"));
}

QTEST_GUILESS_MAIN(TestAppAgentLocate)
#include "tst_app_agentlocate.moc"
