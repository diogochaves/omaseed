// #295 (DECISIONS C50 GT1, GT6; C45 V15, V17): `omaseed do remove <hash…>`, the app's side,
// against a stand-in qBittorrent whose save path is a temporary folder this machine sees: off
// by default; a torrent whose files aren't gone by the server's word refused not-removable; one
// with a file still at its save path refused not-removable; an unseen disk goes by the server's
// word, and the ask says so; it always asks; allowed, the torrent is kept first, then removed
// without its files, the strip and the answer saying kept; a keep that fails refuses the remove;
// a dry run sends nothing; and the kept torrent added back, stopped, at the save path it had.

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

QString removeJson(const QStringList &hashes, bool dryRun = false)
{
    QJsonArray torrents;
    for (const QString &hash : hashes)
        torrents.append(hash.left(8));
    QJsonObject object{{QStringLiteral("action"), QStringLiteral("remove")},
                       {QStringLiteral("torrents"), torrents}};
    if (dryRun)
        object.insert(QStringLiteral("dryRun"), true);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
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

} // namespace

class TestAppAgentRemove : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void removeOffIsNotAllowed();
    void healthyTorrentIsNotRemovable();
    void aFileStillThereIsNotRemovable();
    void alwaysAsksThenKeepsAndRemoves();
    void unseenDiskGoesByTheServersWord();
    void keepFailureRefusesTheRemove();
    void dryRunSendsNothing();
    void addBackAddsItStoppedWhereItWas();

private:
    void start(bool viaEnv = true);
    int sent(const QByteArray &path) const;
    QString path(const QString &rel) const { return m_home->path() + QLatin1Char('/') + rel; }
    /// A remove allowed in the ask window, its answer.
    QJsonObject allowed(const QString &options);

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QTemporaryDir> m_proc;
    std::unique_ptr<FakeServer> m_server;
    Qbt m_qbt;
    std::unique_ptr<App> m_app;
    std::unique_ptr<AgentService> m_service;
};

void TestAppAgentRemove::initTestCase()
{
    qputenv("DBUS_SESSION_BUS_ADDRESS", QByteArray("unix:path=/nonexistent/omaseed-no-bus"));
}

void TestAppAgentRemove::init()
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

void TestAppAgentRemove::cleanup()
{
    m_service.reset();
    m_app.reset();
    m_proc.reset();
    m_server.reset();
    qunsetenv("OMASEED_SERVER_URL");
    m_home.reset();
}

void TestAppAgentRemove::start(bool viaEnv)
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

int TestAppAgentRemove::sent(const QByteArray &prefix) const
{
    int count = 0;
    for (const Recorded &request : std::as_const(m_server->requests))
        if (request.target.startsWith(prefix))
            ++count;
    return count;
}

QJsonObject TestAppAgentRemove::allowed(const QString &options)
{
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending = begin(*m_service, options);
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000); }();
    ask->allow();
    return finish(pending);
}

// C50 GT3: the Remove line is off by default.
void TestAppAgentRemove::removeOffIsNotAllowed()
{
    start();
    m_app->setAgentFixes(QStringLiteral("allow"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: remove")));
    const QJsonObject object = run(*m_service, removeJson({kSingle}));
    QCOMPARE(codeOf(object), QStringLiteral("not-allowed"));
    QVERIFY2(messageOf(object).contains(QStringLiteral("Remove is off")),
             qPrintable(messageOf(object)));
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// GT1: a torrent the server doesn't say lost its files is never removed through do.
void TestAppAgentRemove::healthyTorrentIsNotRemovable()
{
    m_qbt.items[kSingle].state = QStringLiteral("stoppedUP");
    start();
    m_app->setAgentRemove(QStringLiteral("ask"));
    const QJsonObject object = run(*m_service, removeJson({kSingle}));
    QCOMPARE(codeOf(object), QStringLiteral("not-removable"));
    QVERIFY2(messageOf(object).startsWith(QStringLiteral("Not removed: ")) &&
                 messageOf(object).endsWith(QStringLiteral(
                     "omaseed do removes only a torrent whose files are gone. Try recheck.")),
             qPrintable(messageOf(object)));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// GT1: where omaseed sees the disk, any file at the save path refuses it (docs/67 §2's words).
void TestAppAgentRemove::aFileStillThereIsNotRemovable()
{
    QVERIFY(writeFile(m_qbt.savePath + QStringLiteral("/Unicode Pack/b.txt"), 10));
    start();
    m_app->setAgentRemove(QStringLiteral("ask"));
    const QJsonObject object = run(*m_service, removeJson({kSingle, kPack}));
    QCOMPARE(codeOf(object), QStringLiteral("not-removable"));
    QVERIFY2(messageOf(object).startsWith(
                 QStringLiteral("Not removed: Unicode Pack has its files (1 of 2 at %1).\n")
                     .arg(m_qbt.savePath)),
             qPrintable(messageOf(object)));
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
    QVERIFY(RemovedKeep::list().isEmpty());
}

// GT1: it asks, whatever the line says (a hand-edited allow reads as ask); allowed, each kept
// (GT6), then removed without its files.
void TestAppAgentRemove::alwaysAsksThenKeepsAndRemoves()
{
    start();
    m_app->setAgentRemove(QStringLiteral("allow")); // read as ask
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending = begin(*m_service, removeJson({kSingle, kPack}));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    const QVariantMap request = ask->request();
    QCOMPARE(request.value(QStringLiteral("head")).toString(),
             QStringLiteral("An agent asks to remove 2 torrents"));
    QCOMPARE(request.value(QStringLiteral("where")).toString(),
             QStringLiteral("none of their files is on disk (checked) · each kept 30 days to add "
                            "back"));
    QCOMPARE(request.value(QStringLiteral("allowText")).toString(),
             QStringLiteral("allow the remove"));
    const QString line =
        request.value(QStringLiteral("torrents")).toList().first().toMap().value("line").toString();
    QVERIFY2(line.endsWith(QStringLiteral("files gone (checked)")), qPrintable(line));
    QCOMPARE(sent("/api/v2/torrents/export"), 0); // nothing kept before the answer
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^removed: .* kept for 30")));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^removed: .* kept for 30")));
    QTest::ignoreMessage(
        QtInfoMsg,
        QRegularExpression(QStringLiteral("^agent: remove on 2 torrents, allowed in the window")));
    ask->allow();
    const QJsonObject object = finish(pending);
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("allowed"));
    const QJsonArray done = object.value(QLatin1String("done")).toArray();
    QCOMPARE(done.size(), 2);
    const QString day = QDate::currentDate().addDays(RemovedKeep::kDays).toString(Qt::ISODate);
    QCOMPARE(done.first().toObject().value(QLatin1String("kept")).toString(), day);
    QVERIFY2(object.value(QLatin1String("outcome"))
                 .toString()
                 .startsWith(QStringLiteral("Removed 2 torrents from ")),
             qPrintable(object.value(QLatin1String("outcome")).toString()));
    QCOMPARE(m_qbt.deleteFiles, QStringList{QStringLiteral("false")});
    QCOMPARE(sent("/api/v2/torrents/export"), 2);
    // Each kept on its own, with its record, and the strip says so.
    const QList<RemovedKeep::Entry> kept = RemovedKeep::list();
    QCOMPARE(kept.size(), 2);
    QStringList keptHashes{kept.at(0).hash, kept.at(1).hash};
    keptHashes.sort();
    QStringList both{kSingle, kPack};
    both.sort();
    QCOMPARE(keptHashes, both);
    const QVariantMap strip = m_app->outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(),
             QStringLiteral("2 removed · kept 30 days"));
    QCOMPARE(strip.value(QStringLiteral("agent")).toString(), QStringLiteral("by an agent"));
}

// GT1: a server whose disks omaseed can't see: the server's word alone, and the ask says so.
void TestAppAgentRemove::unseenDiskGoesByTheServersWord()
{
    m_qbt.savePath = QStringLiteral("/srv/attic/lib");
    for (Item &item : m_qbt.items)
        item.savePath = m_qbt.savePath;
    {
        omaseed::core::Server server;
        server.id = QStringLiteral("attic");
        server.name = QStringLiteral("attic");
        server.url = m_server->url();
        omaseed::core::ServerStore store;
        QVERIFY(store.add(server));
    }
    start(false);
    QVERIFY(m_app->freeSpaceProbe().localPathFor(m_qbt.savePath).isEmpty());
    m_app->setAgentRemove(QStringLiteral("ask"));
    omaseed::app::AgentAsk *ask = m_app->agentAsk();
    const auto pending = begin(*m_service, removeJson({kSingle}));
    QTRY_VERIFY_WITH_TIMEOUT(ask->isOpen(), 5000);
    QCOMPARE(ask->request().value(QStringLiteral("where")).toString(),
             QStringLiteral("omaseed can't see attic's disks: the server's word alone · kept 30 "
                            "days to add back"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: remove")));
    ask->refuse();
    const QJsonObject object = finish(pending);
    QCOMPARE(codeOf(object), QStringLiteral("refused"));
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
    QCOMPARE(sent("/api/v2/torrents/export"), 0);
}

// GT6, Q11: no way back (its export failed), no remove; what was kept for the others goes.
void TestAppAgentRemove::keepFailureRefusesTheRemove()
{
    m_qbt.items[kPack].fixture.clear();
    start();
    m_app->setAgentRemove(QStringLiteral("ask"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^removed: .* kept for 30")));
    const QJsonObject object = allowed(removeJson({kSingle, kPack}));
    QCOMPARE(codeOf(object), QStringLiteral("not-removable"));
    QVERIFY2(messageOf(object).startsWith(QStringLiteral("Not removed: ")) &&
                 messageOf(object).endsWith(
                     QStringLiteral("omaseed keeps a way back before every remove.")),
             qPrintable(messageOf(object)));
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
    QVERIFY(RemovedKeep::list().isEmpty());
}

// GT5: every check, the disk's too; nothing asked, kept or removed.
void TestAppAgentRemove::dryRunSendsNothing()
{
    start();
    m_app->setAgentRemove(QStringLiteral("ask"));
    const QJsonObject object = run(*m_service, removeJson({kSingle}, true));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QVERIFY(object.value(QLatin1String("dryRun")).toBool());
    QCOMPARE(object.value(QLatin1String("would")).toString(), QStringLiteral("ask"));
    QVERIFY(object.value(QLatin1String("torrents"))
                .toArray()
                .first()
                .toObject()
                .value(QLatin1String("checked"))
                .toBool());
    QVERIFY(!m_app->agentAsk()->isOpen());
    QCOMPARE(sent("/api/v2/torrents/export"), 0);
    QCOMPARE(sent("/api/v2/torrents/delete"), 0);
}

// GT6: the kept torrent goes back stopped, at the save path it had, and is forgotten.
void TestAppAgentRemove::addBackAddsItStoppedWhereItWas()
{
    start();
    m_app->setAgentRemove(QStringLiteral("ask"));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^removed: .* kept for 30")));
    QTest::ignoreMessage(QtInfoMsg, QRegularExpression(QStringLiteral("^agent: remove")));
    const QJsonObject object = allowed(removeJson({kSingle}));
    QVERIFY2(codeOf(object).isEmpty(), qPrintable(messageOf(object)));
    QTRY_COMPARE_WITH_TIMEOUT(m_app->engine()->torrents()->count(), 1, 5000);
    const QList<RemovedKeep::Entry> kept = RemovedKeep::list();
    QCOMPARE(kept.size(), 1);
    QCOMPARE(kept.first().savePath, m_qbt.savePath);

    QTest::ignoreMessage(QtInfoMsg,
                         QRegularExpression(QStringLiteral("^removed: Single.bin added back")));
    QString back = QStringLiteral("never");
    m_app->addBackRemoved(kept.first().id, [&back](const QString &why) { back = why; });
    QTRY_VERIFY_WITH_TIMEOUT(back != QLatin1String("never"), 8000);
    QVERIFY2(back.isEmpty(), qPrintable(back));
    QCOMPARE(sent("/api/v2/torrents/add"), 1);
    QVERIFY(m_qbt.lastAdd.contains(m_qbt.savePath.toUtf8()));
    QVERIFY(m_qbt.lastAdd.contains("stopped"));
    QVERIFY(RemovedKeep::list().isEmpty());
}

QTEST_GUILESS_MAIN(TestAppAgentRemove)
#include "tst_app_agentremove.moc"
