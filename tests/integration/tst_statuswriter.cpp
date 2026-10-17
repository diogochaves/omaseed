#include "QbtFeed.hpp"
#include "core/AltSpeed.hpp"
#include "core/GlobalState.hpp"
#include "core/SeedLedger.hpp"
#include "core/StateFile.hpp"
#include "core/SyncEngine.hpp"
#include "core/engines/qbt/QbtEngine.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"
#include "integration/BarMenu.hpp"
#include "integration/StatusWriter.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <atomic>
#include <thread>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using omaseed::core::GlobalState;
using omaseed::core::QbtEngine;
using omaseed::core::SeedLedger;
using omaseed::core::StateFile;
using omaseed::core::SyncEngine;
using omaseed::integration::StatusWriter;

namespace {

const QString kFakeKey = QStringLiteral("qbt_fake-key-6f1d0c3a9e8b7d2c");
const QString kFakePassword = QStringLiteral("hunter2-not-real");

QJsonObject torrentJson(const QString &name, double progress, const QString &state, double ratio)
{
    return QJsonObject{
        {QStringLiteral("name"), name},
        {QStringLiteral("size"), 1000},
        {QStringLiteral("progress"), progress},
        {QStringLiteral("state"), state},
        {QStringLiteral("dlspeed"), progress < 1 ? 4415221 : 0},
        {QStringLiteral("upspeed"), 0},
        {QStringLiteral("eta"), 160},
        {QStringLiteral("num_complete"), 1},
        {QStringLiteral("num_incomplete"), 2},
        {QStringLiteral("ratio"), ratio},
        {QStringLiteral("uploaded_session"), 4096},
        {QStringLiteral("category"), QStringLiteral("linux")},
        {QStringLiteral("content_path"), QStringLiteral("/srv/data/") + name},
    };
}

QJsonObject fullUpdate(qint64 upData = 1000)
{
    return QJsonObject{
        {QStringLiteral("rid"), 1},
        {QStringLiteral("full_update"), true},
        {QStringLiteral("server_state"),
         QJsonObject{
             {QStringLiteral("dl_info_speed"), 4415221},
             {QStringLiteral("up_info_speed"), 831488},
             {QStringLiteral("dl_info_data"), 500},
             {QStringLiteral("up_info_data"), upData},
             {QStringLiteral("free_space_on_disk"), qint64(1978765344768)},
             {QStringLiteral("use_alt_speed_limits"), true},
             {QStringLiteral("dl_rate_limit"), 1048576},
             {QStringLiteral("up_rate_limit"), 524288},
             {QStringLiteral("connection_status"), QStringLiteral("firewalled")},
             {QStringLiteral("alltime_ul"), qint64(9876543210)},
             {QStringLiteral("alltime_dl"), qint64(4567890123)},
             {QStringLiteral("global_ratio"), QStringLiteral("2.16")},
         }},
        {QStringLiteral("torrents"),
         QJsonObject{
             {QStringLiteral("aaaa"),
              torrentJson(QStringLiteral("arch.iso"), 0.4891, QStringLiteral("downloading"), 0.1)},
             {QStringLiteral("bbbb"),
              torrentJson(QStringLiteral("debian.iso"), 1.0, QStringLiteral("stalledUP"), 0.8)},
         }},
        {QStringLiteral("categories"),
         QJsonObject{
             {QStringLiteral("linux"),
              QJsonObject{{QStringLiteral("name"), QStringLiteral("linux")},
                          {QStringLiteral("savePath"), QStringLiteral("/srv/data/linux")}}},
         }},
    };
}

QJsonObject partial(const QJsonObject &serverState, const QJsonObject &torrents = {})
{
    return QJsonObject{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("server_state"), serverState},
        {QStringLiteral("torrents"), torrents},
    };
}

QJsonObject readStatus(const QString &path, bool *ok = nullptr)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok)
            *ok = false;
        return {};
    }
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (ok)
        *ok = error.error == QJsonParseError::NoError && doc.isObject();
    return doc.object();
}

} // namespace

class TestStatusWriter : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void writesSchemaTwo();
    void isPrivateToTheUser();
    void neverWritesASecret();
    void readerNeverSeesAPartialFile();
    void changesAreRateLimited();
    void idleWritesAHeartbeat();
    void unreachableServer();
    void sessionShareAndWeek();
    void recentSurvivesARestart();
    void milestonesFromTheSync();
    void idlePollsLeaveTheLibraryAlone();
    void topSeedersFromTheLedger();
    void todaysDownloads();
    void addCategoryFromState();
    void removedOnQuit();
    void neverStartedRemovesNothing();
    void altNamesTheSet();
    void serversAndTheSharedMenu();

private:
    void startWriter(StatusWriter &writer, SyncEngine &engine);
    QString statusPath() const { return m_runtime.filePath(QStringLiteral("omaseed/status.json")); }

    QTemporaryDir m_runtime;
    QTemporaryDir m_config;
    QByteArray m_previousConfigHome;
};

void TestStatusWriter::init()
{
    QVERIFY(m_runtime.isValid());
    QVERIFY(m_config.isValid());
    QDir(m_runtime.path()).removeRecursively();
    QDir().mkpath(m_runtime.path());
    QFile::remove(m_config.filePath(QStringLiteral("omaseed/state.json")));
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
}

void TestStatusWriter::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestStatusWriter::startWriter(StatusWriter &writer, SyncEngine &engine)
{
    writer.setPath(statusPath());
    writer.setEngine(&engine);
    writer.setServer(QStringLiteral("srv-1"),
                     QStringLiteral("home"),
                     QUrl(QStringLiteral("http://admin:%1@127.0.0.1:8081/").arg(kFakePassword)));
    writer.setWindowShown(true);
    QSignalSpy written(&writer, &StatusWriter::written);
    writer.start();
    QVERIFY(written.wait(2000));
}

void TestStatusWriter::writesSchemaTwo()
{
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    engine.globalState()->setServerVersions(QStringLiteral("v5.2.3"), QStringLiteral("2.15.1"));
    qbtfeed::demo(engine, fullUpdate());
    // Today's top seeders come from the seed ledger (#47): both gave something today.
    SeedLedger ledger;
    const qint64 at = QDateTime::currentSecsSinceEpoch();
    ledger.observe(at - 2, 0, 0, {{QStringLiteral("aaaa"), 0, 0}, {QStringLiteral("bbbb"), 0, 0}});
    ledger.observe(
        at, 400, 0, {{QStringLiteral("aaaa"), 100, 0}, {QStringLiteral("bbbb"), 300, 0}});
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    writer.setLedger(&ledger);
    startWriter(writer, engine);

    bool ok = false;
    const QJsonObject status = readStatus(statusPath(), &ok);
    QVERIFY(ok);
    QCOMPARE(status.value(QStringLiteral("v")).toInt(), 2);
    QVERIFY(status.value(QStringLiteral("t")).isDouble());
    QVERIFY(qAbs(status.value(QStringLiteral("t")).toInteger() -
                 QDateTime::currentSecsSinceEpoch()) < 5);
    QCOMPARE(status.value(QStringLiteral("pid")).toInteger(), QCoreApplication::applicationPid());
    QCOMPARE(status.value(QStringLiteral("app")).toObject().value(QStringLiteral("window")),
             QJsonValue(QStringLiteral("shown")));

    const QJsonObject server = status.value(QStringLiteral("server")).toObject();
    QCOMPARE(server.value(QStringLiteral("name")).toString(), QStringLiteral("home"));
    QCOMPARE(server.value(QStringLiteral("url")).toString(), QStringLiteral("127.0.0.1:8081"));
    QCOMPARE(server.value(QStringLiteral("version")).toString(), QStringLiteral("5.2.3"));
    QCOMPARE(server.value(QStringLiteral("reachable")).toBool(), true);
    QCOMPARE(server.value(QStringLiteral("error")).toString(), QString());
    QVERIFY(server.value(QStringLiteral("retryIn")).isDouble());
    QCOMPARE(status.value(QStringLiteral("reachable")).toString(), QStringLiteral("firewalled"));

    const QJsonObject speed = status.value(QStringLiteral("speed")).toObject();
    QCOMPARE(speed.value(QStringLiteral("dl")).toInteger(), qint64(4420000));
    QCOMPARE(speed.value(QStringLiteral("up")).toInteger(), qint64(831000));
    const QJsonObject alt = status.value(QStringLiteral("alt")).toObject();
    QCOMPARE(alt.value(QStringLiteral("on")).toBool(), true);
    QCOMPARE(alt.value(QStringLiteral("dlLimit")).toInteger(), qint64(1048576));
    QCOMPARE(alt.value(QStringLiteral("upLimit")).toInteger(), qint64(524288));
    // docs/31 D12: with no AltSpeed, today's word and no end.
    QCOMPARE(alt.value(QStringLiteral("kind")).toString(), QStringLiteral("alt"));
    QCOMPARE(alt.value(QStringLiteral("until")).toString(), QString());
    QCOMPARE(alt.value(QStringLiteral("byHand")).toBool(), false);

    const QJsonObject counts = status.value(QStringLiteral("counts")).toObject();
    for (const char *key : {"all",
                            "downloading",
                            "seeding",
                            "completed",
                            "stopped",
                            "stalled",
                            "checking",
                            "errored"})
        QVERIFY2(counts.value(QLatin1String(key)).isDouble(), key);
    QCOMPARE(counts.value(QStringLiteral("all")).toInt(), 2);
    QCOMPARE(counts.value(QStringLiteral("downloading")).toInt(), 1);
    QCOMPARE(counts.value(QStringLiteral("seeding")).toInt(), 1);
    QCOMPARE(
        status.value(QStringLiteral("disk")).toObject().value(QStringLiteral("free")).toInteger(),
        qint64(1978765344768));

    const QJsonObject share = status.value(QStringLiteral("share")).toObject();
    for (const char *key : {"sessionUp",
                            "sessionDown",
                            "sessionRatio",
                            "allTimeUp",
                            "allTimeDown",
                            "allTimeRatio",
                            "onOffer"})
        QVERIFY2(share.value(QLatin1String(key)).isDouble(), key);
    QCOMPARE(share.value(QStringLiteral("allTimeUp")).toInteger(), qint64(9876543210));
    QCOMPARE(share.value(QStringLiteral("allTimeRatio")).toDouble(), 2.16);
    QCOMPARE(share.value(QStringLiteral("sessionRatio")).toDouble(), -1.0);
    QCOMPARE(share.value(QStringLiteral("onOffer")).toInteger(), qint64(1000));

    QCOMPARE(status.value(QStringLiteral("uploadHour")).toArray().size(), 60);
    QCOMPARE(status.value(QStringLiteral("week")).toArray().size(), 7);
    const QJsonObject alive = status.value(QStringLiteral("alive")).toObject();
    QCOMPARE(alive.value(QStringLiteral("buckets")).toArray().size(), 5);
    QCOMPARE(alive.value(QStringLiteral("rows")).toArray().size(), 1);
    const QJsonArray top = status.value(QStringLiteral("topSeeders")).toArray();
    QCOMPARE(top.size(), 2);
    QCOMPARE(top.first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("debian.iso"));
    QCOMPARE(top.first().toObject().value(QStringLiteral("uploaded")).toInteger(), qint64(300));
    const QJsonArray active = status.value(QStringLiteral("active")).toArray();
    QCOMPARE(active.size(), 1);
    QCOMPARE(active.first().toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("arch.iso"));
    QVERIFY(status.value(QStringLiteral("recent")).isArray());
    const QJsonArray categories = status.value(QStringLiteral("categories")).toArray();
    QCOMPARE(categories.size(), 1);
    QCOMPARE(categories.first().toObject().value(QStringLiteral("savePath")).toString(),
             QStringLiteral("/srv/data/linux"));
    const QJsonObject add = status.value(QStringLiteral("add")).toObject();
    QVERIFY(add.value(QStringLiteral("savePath")).isString());
    QVERIFY(add.value(QStringLiteral("autoTmm")).isBool());
    QVERIFY(add.value(QStringLiteral("category")).isString());
    QVERIFY(status.value(QStringLiteral("downloads")).isArray());
}

void TestStatusWriter::isPrivateToTheUser()
{
    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);

    const QFileInfo file(statusPath());
    QCOMPARE(file.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther |
                                   QFileDevice::WriteGroup | QFileDevice::WriteOther),
             QFileDevice::Permissions());
    QVERIFY(file.permissions().testFlag(QFileDevice::ReadOwner));
    const QFileInfo dir(file.absolutePath());
    QCOMPARE(dir.permissions() & (QFileDevice::ReadGroup | QFileDevice::ReadOther |
                                  QFileDevice::ExeGroup | QFileDevice::ExeOther),
             QFileDevice::Permissions());
    QVERIFY(!QFile::exists(statusPath() + QStringLiteral(".tmp")));
}

void TestStatusWriter::neverWritesASecret()
{
    // A client signed in with a key, as the app's is; the writer never sees the client, but
    // nothing reachable from the engine may leak into the file either.
    const QString strictKey = qEnvironmentVariable("QBT_STRICT_API_KEY");
    const QString key = strictKey.isEmpty() ? kFakeKey : strictKey;
    QbtEngine client;
    client.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:9")));
    client.setApiKey(key);
    SyncEngine engine;
    engine.setClient(&client);
    engine.globalState()->setConnected(true);
    qbtfeed::demo(engine, fullUpdate());

    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);
    writer.setServerError(QStringLiteral("401 unauthorised"));

    QFile file(statusPath());
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes = file.readAll();
    QVERIFY(!bytes.isEmpty());
    QVERIFY(!bytes.contains(key.toUtf8()));
    QVERIFY(!bytes.contains(kFakePassword.toUtf8()));
    QVERIFY(!bytes.contains("admin"));
    QVERIFY(!bytes.contains("Bearer"));
}

void TestStatusWriter::readerNeverSeesAPartialFile()
{
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    // A large document, so a torn write would be easy to catch.
    QJsonObject data = fullUpdate();
    QJsonObject categories;
    for (int i = 0; i < 400; ++i) {
        const QString name = QStringLiteral("category-with-a-long-name-%1").arg(i);
        categories.insert(name,
                          QJsonObject{{QStringLiteral("name"), name},
                                      {QStringLiteral("savePath"),
                                       QStringLiteral("/srv/a/rather/long/path/") + name}});
    }
    data.insert(QStringLiteral("categories"), categories);
    qbtfeed::demo(engine, data);

    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);

    std::atomic<bool> stop{false};
    std::atomic<int> reads{0};
    std::atomic<int> torn{0};
    const QString path = statusPath();
    std::thread reader([&]() {
        while (!stop.load()) {
            bool ok = false;
            readStatus(path, &ok);
            if (!ok)
                ++torn;
            ++reads;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    QElapsedTimer clock;
    clock.start();
    bool shown = true;
    while (clock.elapsed() < 1500) {
        shown = !shown;
        writer.setWindowShown(shown);
        QTest::qWait(2);
    }
    stop = true;
    reader.join();

    QVERIFY2(writer.writeCount() > 50, qPrintable(QString::number(writer.writeCount())));
    QVERIFY2(reads.load() > 200, qPrintable(QString::number(reads.load())));
    QCOMPARE(torn.load(), 0);
}

void TestStatusWriter::changesAreRateLimited()
{
    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(200, 100000);
    startWriter(writer, engine);
    const int before = writer.writeCount();

    QElapsedTimer clock;
    clock.start();
    int round = 0;
    while (clock.elapsed() < 1000) {
        writer.setServerError(QStringLiteral("refused %1").arg(++round));
        QTest::qWait(10);
    }
    QTest::qWait(250);
    const int writes = writer.writeCount() - before;
    QVERIFY2(writes >= 4 && writes <= 7, qPrintable(QString::number(writes)));
    // The last change still landed.
    QCOMPARE(readStatus(statusPath())
                 .value(QStringLiteral("server"))
                 .toObject()
                 .value(QStringLiteral("error"))
                 .toString(),
             QStringLiteral("refused %1").arg(round));
}

void TestStatusWriter::idleWritesAHeartbeat()
{
    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(0, 200);
    startWriter(writer, engine);
    const qint64 firstT = readStatus(statusPath()).value(QStringLiteral("t")).toInteger();
    const int before = writer.writeCount();
    // Counted against the time that passed, not a fixed window: a starved CI runner skips
    // timer fires (Qt does not catch them up), and once wrote 3 where 1.1 s promised 4.
    QElapsedTimer clock;
    clock.start();
    QTRY_VERIFY_WITH_TIMEOUT(writer.writeCount() - before >= 4, 5000);
    const qint64 elapsed = clock.elapsed();
    const int writes = writer.writeCount() - before;
    // Never faster than one write per 200 ms heartbeat, give or take the one in flight.
    QVERIFY2(writes <= elapsed / 200 + 2,
             qPrintable(QStringLiteral("%1 writes in %2 ms").arg(writes).arg(elapsed)));
    QVERIFY(readStatus(statusPath()).value(QStringLiteral("t")).toInteger() >= firstT);

    // Without a change and with a long heartbeat, nothing is written.
    writer.setIntervals(0, 100000);
    QTest::qWait(300);
    const int quiet = writer.writeCount();
    QTest::qWait(1200);
    QCOMPARE(writer.writeCount(), quiet);
}

void TestStatusWriter::unreachableServer()
{
    SyncEngine engine;
    qbtfeed::demo(engine, fullUpdate());
    engine.globalState()->setConnected(false);
    engine.globalState()->setRetryInSeconds(7);
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);
    QSignalSpy written(&writer, &StatusWriter::written);
    writer.setServerError(QStringLiteral("connection refused"));
    QVERIFY(written.wait(2000));

    const QJsonObject status = readStatus(statusPath());
    const QJsonObject server = status.value(QStringLiteral("server")).toObject();
    QCOMPARE(server.value(QStringLiteral("reachable")).toBool(), false);
    QCOMPARE(server.value(QStringLiteral("error")).toString(),
             QStringLiteral("connection refused"));
    QCOMPARE(server.value(QStringLiteral("retryIn")).toInt(), 7);
    QCOMPARE(status.value(QStringLiteral("reachable")).toString(), QStringLiteral("disconnected"));
    QCOMPARE(
        status.value(QStringLiteral("speed")).toObject().value(QStringLiteral("dl")).toInteger(),
        qint64(0));
    // The last known lists stay.
    QCOMPARE(status.value(QStringLiteral("active")).toArray().size(), 1);
}

void TestStatusWriter::sessionShareAndWeek()
{
    const QDateTime fixed(QDate(2026, 10, 2), QTime(23, 59, 30));
    QDateTime current = fixed;
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    StatusWriter writer;
    writer.setClock([&current]() { return current; });
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);

    qbtfeed::demo(engine, fullUpdate(1000));
    qbtfeed::demo(engine,
                  partial(QJsonObject{{QStringLiteral("up_info_data"), 5000},
                                      {QStringLiteral("dl_info_data"), 2500}}));
    current = fixed.addSecs(60); // past midnight
    qbtfeed::demo(engine, partial(QJsonObject{{QStringLiteral("up_info_data"), 6000}}));

    const QJsonObject status = writer.snapshot();
    const QJsonObject share = status.value(QStringLiteral("share")).toObject();
    QCOMPARE(share.value(QStringLiteral("sessionUp")).toInteger(), qint64(5000));
    QCOMPARE(share.value(QStringLiteral("sessionDown")).toInteger(), qint64(2000));
    QCOMPARE(share.value(QStringLiteral("sessionRatio")).toDouble(), 2.5);
    const QJsonArray week = status.value(QStringLiteral("week")).toArray();
    QCOMPARE(week.at(5).toObject().value(QStringLiteral("day")).toString(),
             QStringLiteral("2026-10-02"));
    QCOMPARE(week.at(5).toObject().value(QStringLiteral("up")).toInteger(), qint64(4000));
    QCOMPARE(week.at(6).toObject().value(QStringLiteral("up")).toInteger(), qint64(1000));

    // A restart of qBittorrent: its counter starts over, nothing is lost.
    qbtfeed::demo(engine, partial(QJsonObject{{QStringLiteral("up_info_data"), 300}}));
    QCOMPARE(writer.snapshot()
                 .value(QStringLiteral("share"))
                 .toObject()
                 .value(QStringLiteral("sessionUp"))
                 .toInteger(),
             qint64(5300));

    // Another server starts the session over.
    writer.setServer(QStringLiteral("srv-2"), QStringLiteral("nas"), QUrl());
    QCOMPARE(writer.snapshot()
                 .value(QStringLiteral("share"))
                 .toObject()
                 .value(QStringLiteral("sessionUp"))
                 .toInteger(),
             qint64(0));
}

void TestStatusWriter::recentSurvivesARestart()
{
    {
        SyncEngine engine;
        StatusWriter writer;
        writer.setIntervals(0, 10000);
        writer.setPathMapper([](const QString &path) {
            return QString(path).replace(QStringLiteral("/srv/data"), QStringLiteral("/mnt/nas"));
        });
        startWriter(writer, engine);
        writer.addCompletion(QStringLiteral("cccc"),
                             QStringLiteral("fedora.iso"),
                             2048,
                             QStringLiteral("linux"),
                             QStringLiteral("/srv/data/fedora.iso"));
        const QJsonArray recent = writer.snapshot().value(QStringLiteral("recent")).toArray();
        QCOMPARE(recent.size(), 1);
        const QJsonObject entry = recent.first().toObject();
        QCOMPARE(entry.value(QStringLiteral("kind")).toString(), QStringLiteral("completed"));
        QCOMPARE(entry.value(QStringLiteral("path")).toString(),
                 QStringLiteral("/mnt/nas/fedora.iso"));
        QCOMPARE(entry.value(QStringLiteral("size")).toInteger(), qint64(2048));
        QCOMPARE(entry.value(QStringLiteral("category")).toString(), QStringLiteral("linux"));
        QVERIFY(entry.value(QStringLiteral("at")).isDouble());
        writer.removeFile();
    }
    const QJsonObject section = StateFile::readSection(QStringLiteral("status"));
    QCOMPARE(section.value(QStringLiteral("recent"))
                 .toObject()
                 .value(QStringLiteral("srv-1"))
                 .toArray()
                 .size(),
             1);

    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);
    QCOMPARE(writer.snapshot().value(QStringLiteral("recent")).toArray().size(), 1);
    // Another server has its own list.
    writer.setServer(QStringLiteral("srv-2"), QStringLiteral("nas"), QUrl());
    QCOMPARE(writer.snapshot().value(QStringLiteral("recent")).toArray().size(), 0);
}

void TestStatusWriter::milestonesFromTheSync()
{
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);

    // The first full update sets the starting point: debian is at 0.8.
    qbtfeed::demo(engine, fullUpdate());
    QCOMPARE(writer.snapshot().value(QStringLiteral("recent")).toArray().size(), 0);

    qbtfeed::demo(engine,
                  partial({},
                          QJsonObject{{QStringLiteral("bbbb"),
                                       QJsonObject{{QStringLiteral("ratio"), 2.05}}}}));
    const QJsonArray recent = writer.snapshot().value(QStringLiteral("recent")).toArray();
    QCOMPARE(recent.size(), 2);
    const QJsonObject newest = recent.first().toObject();
    QCOMPARE(newest.value(QStringLiteral("kind")).toString(), QStringLiteral("milestone"));
    QCOMPARE(newest.value(QStringLiteral("ratio")).toInt(), 2);
    QCOMPARE(newest.value(QStringLiteral("name")).toString(), QStringLiteral("debian.iso"));
    QCOMPARE(newest.value(QStringLiteral("path")).toString(),
             QStringLiteral("/srv/data/debian.iso"));
    QCOMPARE(recent.at(1).toObject().value(QStringLiteral("ratio")).toInt(), 1);
}

// #36: the parts worked out from every torrent are kept while the torrents hold still, so
// an idle poll or an upload sample never walks the library; a change, or a torrent turning
// quiet, works them out again.
void TestStatusWriter::idlePollsLeaveTheLibraryAlone()
{
    QDateTime current = QDateTime::fromSecsSinceEpoch(1'790'000'000);
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    writer.setClock([&current]() { return current; });
    startWriter(writer, engine);

    qbtfeed::demo(engine, fullUpdate());
    const QJsonObject first = writer.snapshot();
    const int builds = writer.torrentPartsBuilds();

    // Idle polls (the server's counters move, no torrent does) and a minute of writes.
    for (int i = 0; i < 20; ++i) {
        qbtfeed::demo(engine, partial(QJsonObject{{QStringLiteral("up_info_data"), 2000 + i}}));
        current = current.addSecs(3);
        writer.snapshot();
    }
    QCOMPARE(writer.torrentPartsBuilds(), builds);
    const QJsonObject idle = writer.snapshot();
    for (const char *key : {"alive", "topSeeders", "active"})
        QCOMPARE(idle.value(QLatin1String(key)), first.value(QLatin1String(key)));
    QCOMPARE(idle.value(QStringLiteral("share")).toObject().value(QStringLiteral("onOffer")),
             first.value(QStringLiteral("share")).toObject().value(QStringLiteral("onOffer")));

    // A torrent moves: worked out once more, and the file shows it.
    qbtfeed::demo(engine,
                  partial({},
                          QJsonObject{{QStringLiteral("bbbb"),
                                       QJsonObject{{QStringLiteral("dlspeed"), 1'000'000}}}}));
    writer.snapshot();
    QCOMPARE(writer.torrentPartsBuilds(), builds + 1);
    writer.snapshot();
    QCOMPARE(writer.torrentPartsBuilds(), builds + 1);

    // debian (complete, one seed) was last active 30 days ago less 2 s: still counted as
    // awake; kept until the second it turns quiet, then worked out again.
    const qint64 last = current.toSecsSinceEpoch() - 30 * 24 * 3600 + 2;
    qbtfeed::demo(engine,
                  partial({},
                          QJsonObject{{QStringLiteral("bbbb"),
                                       QJsonObject{{QStringLiteral("last_activity"), last}}}}));
    const auto quiet = [&writer]() {
        int count = 0;
        const QJsonArray buckets = writer.snapshot()
                                       .value(QStringLiteral("alive"))
                                       .toObject()
                                       .value(QStringLiteral("buckets"))
                                       .toArray();
        for (const QJsonValue &bucket : buckets)
            count += bucket.toObject().value(QStringLiteral("quiet")).toInt();
        return count;
    };
    QCOMPARE(quiet(), 0);
    const int beforeQuiet = writer.torrentPartsBuilds();
    current = current.addSecs(1);
    QCOMPARE(quiet(), 0);
    QCOMPARE(writer.torrentPartsBuilds(), beforeQuiet);
    current = current.addSecs(1);
    QCOMPARE(quiet(), 1);
    QCOMPARE(writer.torrentPartsBuilds(), beforeQuiet + 1);
}

// #47: today's top seeders rank what the seed ledger saw each torrent give today, follow the
// ledger without walking the library, and start over when the day does.
void TestStatusWriter::topSeedersFromTheLedger()
{
    QDateTime current(QDate(2026, 10, 10), QTime(23, 59));
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    SeedLedger ledger;
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    writer.setClock([&current]() { return current; });
    writer.setLedger(&ledger);
    startWriter(writer, engine);
    qbtfeed::demo(engine, fullUpdate());

    const auto top = [&writer]() {
        QStringList rows;
        for (const QJsonValue &row :
             writer.snapshot().value(QStringLiteral("topSeeders")).toArray()) {
            const QJsonObject o = row.toObject();
            rows << QStringLiteral("%1 %2")
                        .arg(o.value(QStringLiteral("name")).toString())
                        .arg(o.value(QStringLiteral("uploaded")).toInteger());
        }
        return rows;
    };
    const auto poll = [&](qint64 arch, qint64 debian) {
        ledger.observe(current.toSecsSinceEpoch(),
                       arch + debian,
                       0,
                       {{QStringLiteral("aaaa"), arch, 0}, {QStringLiteral("bbbb"), debian, 0}});
    };

    // The session's counter (4096 each in the fixture) is not today's upload: nothing yet.
    poll(1'000, 1'000);
    QCOMPARE(top(), QStringList());
    const int builds = writer.torrentPartsBuilds();

    current = current.addSecs(2);
    poll(1'500, 1'200);
    QCOMPARE(top(), QStringList({"arch.iso 500", "debian.iso 200"}));
    current = current.addSecs(2);
    poll(1'500, 2'200);
    QCOMPARE(top(), QStringList({"debian.iso 1200", "arch.iso 500"}));
    // The ledger moved, the torrents did not: the library was not walked for it.
    QCOMPARE(writer.torrentPartsBuilds(), builds);

    // Past midnight: a new day, nobody has given anything in it yet.
    current =
        QDateTime(QDate(2026, 10, 11), QTime(0, 0, 10)); // under gapSeconds since the last poll
    QCOMPARE(top(), QStringList());
    poll(1'600, 2'200);
    QCOMPARE(top(), QStringList({"arch.iso 100"}));

    // A torrent that left the library is not listed, whatever it gave.
    qbtfeed::demo(
        engine,
        QJsonObject{{QStringLiteral("rid"), 3},
                    {QStringLiteral("torrents_removed"), QJsonArray{QStringLiteral("aaaa")}}});
    QCOMPARE(top(), QStringList());

    // Without a ledger, no list.
    writer.setLedger(nullptr);
    QCOMPARE(top(), QStringList());
}

void TestStatusWriter::todaysDownloads()
{
    QTemporaryDir downloads;
    QVERIFY(downloads.isValid());
    QFile fixture(QStringLiteral(OMASEED_FIXTURES_DIR "/single_file_v1.torrent"));
    QVERIFY(fixture.open(QIODevice::ReadOnly));
    const QByteArray bytes = fixture.readAll();
    const auto put = [&](const QString &name, const QDateTime &when) {
        QFile file(downloads.filePath(name));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bytes);
        file.close();
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(when, QFileDevice::FileModificationTime));
    };
    put(QStringLiteral("today.torrent"), QDateTime::currentDateTime());
    put(QStringLiteral("old.torrent"), QDateTime::currentDateTime().addDays(-2));

    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    QStringList checked;
    writer.setLibraryCheck([&checked](const QString &v1, const QString &) {
        checked << v1;
        return true;
    });
    writer.setDownloadsFolder(downloads.path());
    startWriter(writer, engine);
    QTRY_COMPARE_WITH_TIMEOUT(
        writer.snapshot().value(QStringLiteral("downloads")).toArray().size(), 1, 3000);
    const QJsonObject file =
        writer.snapshot().value(QStringLiteral("downloads")).toArray().first().toObject();
    QCOMPARE(file.value(QStringLiteral("path")).toString(),
             downloads.filePath(QStringLiteral("today.torrent")));
    QVERIFY(!file.value(QStringLiteral("name")).toString().isEmpty());
    QVERIFY(file.value(QStringLiteral("size")).toInteger() > 0);
    QVERIFY(file.value(QStringLiteral("at")).isDouble());
    QCOMPARE(file.value(QStringLiteral("inLibrary")).toBool(), true);
    QVERIFY(!checked.isEmpty());
    QVERIFY(!file.contains(QStringLiteral("infoHashV1")));

    // A file the browser saves now shows up through the folder watch.
    put(QStringLiteral("another.torrent"), QDateTime::currentDateTime());
    QTRY_COMPARE_WITH_TIMEOUT(
        writer.snapshot().value(QStringLiteral("downloads")).toArray().size(), 2, 5000);
}

void TestStatusWriter::addCategoryFromState()
{
    StateFile::writeSection(QStringLiteral("widgetAdd"),
                            QJsonObject{{QStringLiteral("category"), QStringLiteral("films")}});
    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);
    QCOMPARE(writer.snapshot()
                 .value(QStringLiteral("add"))
                 .toObject()
                 .value(QStringLiteral("category"))
                 .toString(),
             QStringLiteral("films"));
    StateFile::writeSection(QStringLiteral("widgetAdd"),
                            QJsonObject{{QStringLiteral("category"), QStringLiteral("linux")}});
    writer.reloadAddCategory();
    QCOMPARE(writer.snapshot()
                 .value(QStringLiteral("add"))
                 .toObject()
                 .value(QStringLiteral("category"))
                 .toString(),
             QStringLiteral("linux"));
}

void TestStatusWriter::removedOnQuit()
{
    SyncEngine engine;
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);
    QVERIFY(QFile::exists(statusPath()));
    writer.removeFile();
    QVERIFY(!QFile::exists(statusPath()));
    // Nothing writes it back afterwards.
    writer.markDirty();
    QTest::qWait(100);
    QVERIFY(!QFile::exists(statusPath()));
}

void TestStatusWriter::neverStartedRemovesNothing()
{
    // Another omaseed's file (a demo quitting beside a real app) is left alone.
    QDir().mkpath(QFileInfo(statusPath()).absolutePath());
    QFile other(statusPath());
    QVERIFY(other.open(QIODevice::WriteOnly));
    other.write("{\"v\":2}");
    other.close();
    StatusWriter writer;
    writer.setPath(statusPath());
    writer.removeFile();
    QVERIFY(QFile::exists(statusPath()));
}

// docs/31 D12: the widget never asks qBittorrent, so the app writes the word, the end and
// whether a hand toggle set it: alt.kind, alt.until, alt.byHand.
void TestStatusWriter::altNamesTheSet()
{
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    qbtfeed::demo(engine, fullUpdate()); // alternative limits on
    omaseed::core::ServerPreferences preferences(nullptr);
    omaseed::core::AltSpeed alt;
    const QDate today = QDate::currentDate();
    alt.setClock([today]() { return QDateTime(today, QTime(15, 0)); });
    alt.setGlobalState(engine.globalState());
    alt.setPreferences(&preferences);
    preferences.setOffline({
        {QStringLiteral("dl_limit"), 0},
        {QStringLiteral("up_limit"), 1228800},
        {QStringLiteral("alt_dl_limit"), 0},
        {QStringLiteral("alt_up_limit"), 0},
        {QStringLiteral("scheduler_enabled"), true},
        {QStringLiteral("schedule_from_hour"), 23},
        {QStringLiteral("schedule_from_min"), 0},
        {QStringLiteral("schedule_to_hour"), 7},
        {QStringLiteral("schedule_to_min"), 0},
        {QStringLiteral("scheduler_days"), 0},
    });
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    writer.setAltSpeed(&alt);
    startWriter(writer, engine);

    bool ok = false;
    QJsonObject status = readStatus(statusPath(), &ok);
    QVERIFY(ok);
    QCOMPARE(status.value(QStringLiteral("v")).toInt(), 2); // additions, not a new schema
    QJsonObject altJson = status.value(QStringLiteral("alt")).toObject();
    QCOMPARE(altJson.value(QStringLiteral("on")).toBool(), true);
    QCOMPARE(altJson.value(QStringLiteral("kind")).toString(), QStringLiteral("burst"));
    // On at 15:00 against a 23:00–07:00 schedule: a hand toggle, ended tomorrow at 07:00.
    QCOMPARE(altJson.value(QStringLiteral("until")).toString(), QStringLiteral("07:00"));
    QCOMPARE(altJson.value(QStringLiteral("byHand")).toBool(), true);

    // The set changes: the file follows.
    QSignalSpy written(&writer, &StatusWriter::written);
    preferences.apply({{QStringLiteral("alt_up_limit"), 10240}});
    QVERIFY(written.wait(2000));
    status = readStatus(statusPath(), &ok);
    altJson = status.value(QStringLiteral("alt")).toObject();
    QCOMPARE(altJson.value(QStringLiteral("kind")).toString(), QStringLiteral("turtle"));
}

// docs/33 §11 change 5 (D24, DECISIONS C13): `servers` and `menu`, added within v2. Without a
// menu both lists are present and empty (lists are always present); with one they follow it.
void TestStatusWriter::serversAndTheSharedMenu()
{
    SyncEngine engine;
    engine.globalState()->setConnected(true);
    qbtfeed::demo(engine, fullUpdate());
    StatusWriter writer;
    writer.setIntervals(0, 10000);
    startWriter(writer, engine);

    bool ok = false;
    QJsonObject status = readStatus(statusPath(), &ok);
    QVERIFY(ok);
    QCOMPARE(status.value(QStringLiteral("v")).toInt(), 2);
    QVERIFY(status.value(QStringLiteral("servers")).isArray());
    QVERIFY(status.value(QStringLiteral("menu")).isArray());
    QVERIFY(status.value(QStringLiteral("menu")).toArray().isEmpty());

    omaseed::integration::BarMenu menu;
    menu.setServerAnswering(true);
    menu.setServers({{QStringLiteral("a"),
                      QStringLiteral("public"),
                      false,
                      QStringLiteral("ok"),
                      0,
                      1789346000,
                      QString()},
                     {QStringLiteral("b"),
                      QStringLiteral("private"),
                      true,
                      QStringLiteral("ok"),
                      4,
                      1789346600,
                      QString()}});
    QSignalSpy written(&writer, &StatusWriter::written);
    writer.setMenu(&menu);
    QVERIFY(written.wait(2000));
    status = readStatus(statusPath(), &ok);
    const QJsonArray servers = status.value(QStringLiteral("servers")).toArray();
    QCOMPARE(servers.size(), 2);
    const QJsonObject priv = servers.at(1).toObject();
    QCOMPARE(priv.value(QStringLiteral("id")).toString(), QStringLiteral("b"));
    QCOMPARE(priv.value(QStringLiteral("name")).toString(), QStringLiteral("private"));
    QCOMPARE(priv.value(QStringLiteral("current")).toBool(), true);
    QCOMPARE(priv.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
    QCOMPARE(priv.value(QStringLiteral("count")).toInt(), 4);
    QCOMPARE(priv.value(QStringLiteral("seen")).toInteger(), 1789346600);
    QCOMPARE(priv.value(QStringLiteral("reason")).toString(), QString());

    const QJsonArray rows = status.value(QStringLiteral("menu")).toArray();
    QStringList ids;
    for (const QJsonValue &row : rows)
        ids << row.toObject().value(QStringLiteral("id")).toString();
    QCOMPARE(ids,
             QStringList({QStringLiteral("servers"),
                          QStringLiteral("server:a"),
                          QStringLiteral("server:b"),
                          QStringLiteral("separator:servers"),
                          QStringLiteral("alt-speed"),
                          QStringLiteral("stop-all"),
                          QStringLiteral("start-all"),
                          QStringLiteral("separator:actions"),
                          QStringLiteral("add"),
                          QStringLiteral("settings"),
                          QStringLiteral("separator:quit"),
                          QStringLiteral("quit")}));
    const QJsonObject current = rows.at(2).toObject();
    QCOMPARE(current.value(QStringLiteral("kind")).toString(), QStringLiteral("radio"));
    QCOMPARE(current.value(QStringLiteral("checked")).toBool(), true);
    QCOMPARE(current.value(QStringLiteral("detail")).toString(), QStringLiteral("4 torrents"));
    // The widget's words: the shell's card style (docs/20 N2).
    QCOMPARE(rows.last().toObject().value(QStringLiteral("label")).toString(),
             QStringLiteral("Quit omaseed"));

    // A change in the menu is a change in the file.
    menu.setAltSpeed(true, QStringLiteral("alternative speed limits"), QStringLiteral("Alt"));
    QVERIFY(written.wait(2000));
    status = readStatus(statusPath(), &ok);
    const QJsonObject alt = status.value(QStringLiteral("menu")).toArray().at(4).toObject();
    QCOMPARE(alt.value(QStringLiteral("kind")).toString(), QStringLiteral("check"));
    QCOMPARE(alt.value(QStringLiteral("checked")).toBool(), true);
}

QTEST_GUILESS_MAIN(TestStatusWriter)
#include "tst_statuswriter.moc"
