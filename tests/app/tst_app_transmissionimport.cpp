// Importing a torrents file into a Transmission, in the app (#88; docs/57 §5, §6;
// docs/DECISIONS.md C40 X1, X4–X8): App on a fake Transmission (FakeTransmission) that holds
// what it is handed, as tst_transmissiontorrentsimport's does. The read and the run pick
// Transmission's importer; the sheet's data name the labels a qBittorrent's category and tags
// become and what Transmission can't keep; the result says it again, names the torrents it
// couldn't put where the file says, counts the links and ends with the server they are still
// on; and the links are finished from the journal later: calmly on a poll, and after a
// connect.

#include "FakeTransmission.hpp"

#include "core/ActionOutcome.hpp"
#include "core/GlobalState.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TarArchive.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/transmission/TransmissionTorrentsImport.hpp"
#include "ui/app/App.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include <memory>

using omaseed::app::App;
using namespace fakehttp;
using namespace faketransmission;
using namespace omaseed::core;

namespace {

using Import = TransmissionTorrentsImport;

const char *const kPack = "multi_file_v1_unicode.torrent";
const char *const kSingle = "single_file_v1.torrent";
const QString kLinkHash = QStringLiteral("88").repeated(20);

QByteArray torrentBytes(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

/// A record for the torrent of `fixtureName`: complete and started, in /data.
TorrentRecord recordOf(const char *fixtureName)
{
    const TorrentFile parsed = TorrentFile::parse(torrentBytes(fixtureName));
    if (!parsed.isValid())
        qFatal("fixture %s does not parse", fixtureName);
    TorrentRecord r;
    r.id = parsed.torrentId();
    r.infohashV1 = parsed.infoHashV1;
    r.name = parsed.name;
    r.size = parsed.totalSize;
    r.progress = 1;
    r.savePath = QStringLiteral("/data");
    r.state = QStringLiteral("started");
    return r;
}

TorrentRecord linkRecord()
{
    TorrentRecord r;
    r.id = kLinkHash;
    r.infohashV1 = kLinkHash;
    r.name = QStringLiteral("a link");
    r.savePath = QStringLiteral("/data");
    r.state = QStringLiteral("started");
    r.priorities = {1, 0};
    r.magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=a+link").arg(kLinkHash);
    return r;
}

QStringList strings(const QJsonValue &array)
{
    QStringList out;
    for (const QJsonValue &v : array.toArray())
        out.append(v.toString());
    return out;
}

/// One torrent as the fake daemon holds it.
struct Held
{
    QString name;
    QStringList files;
    QStringList labels;
    bool metadata = true;
    bool running = false;
    int checks = 1; ///< polls it still shows as checking: one on a paused add
};

} // namespace

class TestAppTransmissionImport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void aQbittorrentsFileGoesIntoATransmission();
    void aTransmissionsOwnFileIsNoCrossing();
    void linksAreFinishedCalmlyAndAfterAConnect();

private:
    QJsonObject answer(const QString &method, const QJsonObject &params);
    /// Writes a file "attic" exported; `torrents` are fixture names by record id.
    QString write(const QList<TorrentRecord> &records,
                  const QHash<QString, const char *> &torrents,
                  EngineKind engine);
    /// Saves the daemon as "seedbox" and starts `app` on it.
    bool startOn(App &app);
    /// Polls quickly and waits for the daemon's first answer.
    bool connected(App &app);
    /// Reads `path` for the sheet: its info, or `{error}`.
    QVariantMap read(App &app, const QString &path);
    /// "ok: …" or "failed: …", the import's result.
    QString import(App &app, const QVariantMap &choice = {});
    /// The importer's own torrent_get calls (the app's polls ask for a table).
    int looks() const;

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeTransmission> m_daemon;
    PollAnswers m_poll;
    QHash<QString, Held> m_held;
    QStringList m_order;
};

void TestAppTransmissionImport::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->filePath(QStringLiteral("config")).toUtf8());
    qputenv("XDG_DATA_HOME", m_home->filePath(QStringLiteral("data")).toUtf8());
    m_poll = PollAnswers();
    m_poll.torrents.clear();
    m_held.clear();
    m_order.clear();
    m_server = std::make_unique<FakeServer>();
    m_daemon = std::make_unique<FakeTransmission>();
    m_server->setHandler(m_daemon->handler());
    m_daemon->answer = [this](const QString &method, const QJsonObject &params) {
        return answer(method, params);
    };
}

void TestAppTransmissionImport::cleanup()
{
    m_daemon.reset();
    m_server.reset();
    m_home.reset();
}

QJsonObject TestAppTransmissionImport::answer(const QString &method, const QJsonObject &params)
{
    const QStringList ids = strings(params.value(QStringLiteral("ids")));
    if (method == QLatin1String("torrent_add")) {
        Held held;
        QString hash;
        if (params.contains(QStringLiteral("metainfo"))) {
            const TorrentFile parsed = TorrentFile::parse(QByteArray::fromBase64(
                params.value(QStringLiteral("metainfo")).toString().toLatin1()));
            hash = parsed.infoHashV1;
            held.name = parsed.name;
            held.files = TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"));
        } else {
            hash = params.value(QStringLiteral("filename"))
                       .toString()
                       .section(QStringLiteral("btih:"), 1)
                       .left(40);
            held.name = hash;
            held.metadata = false;
            held.checks = 0;
        }
        held.labels = strings(params.value(QStringLiteral("labels")));
        m_held.insert(hash, held);
        m_order.append(hash);
        return {{QStringLiteral("torrent_added"),
                 QJsonObject{{QStringLiteral("hash_string"), hash},
                             {QStringLiteral("id"), 1},
                             {QStringLiteral("name"), held.name}}}};
    }
    // The app's polls ask for a table; the importer for objects, about what it added.
    if (method == QLatin1String("torrent_get") && !params.contains(QStringLiteral("format"))) {
        QJsonArray rows;
        for (const QString &hash : std::as_const(m_order)) {
            if (params.contains(QStringLiteral("ids")) && !ids.contains(hash))
                continue;
            Held &held = m_held[hash];
            QJsonArray files;
            for (const QString &name : std::as_const(held.files))
                files.append(QJsonObject{{QStringLiteral("name"), name}});
            const bool checking = held.checks > 0;
            if (checking &&
                strings(params.value(QStringLiteral("fields"))).contains(QStringLiteral("status")))
                --held.checks;
            rows.append(
                QJsonObject{{QStringLiteral("hash_string"), hash},
                            {QStringLiteral("files"), files},
                            {QStringLiteral("metadata_percent_complete"), held.metadata ? 1 : 0},
                            {QStringLiteral("status"),
                             checking       ? 2
                             : held.running ? 6
                                            : 0},
                            {QStringLiteral("left_until_done"), checking ? 1000 : 0}});
        }
        return {{QStringLiteral("torrents"), rows}};
    }
    if (method == QLatin1String("torrent_verify")) {
        for (const QString &hash : ids)
            m_held[hash].checks = 1;
        return {};
    }
    if (method == QLatin1String("torrent_start")) {
        for (const QString &hash : ids)
            m_held[hash].running = true;
        return {};
    }
    return m_poll.answer(method, params);
}

QString TestAppTransmissionImport::write(const QList<TorrentRecord> &records,
                                         const QHash<QString, const char *> &torrents,
                                         EngineKind engine)
{
    TorrentsManifest manifest;
    manifest.created = QDateTime(QDate(2026, 11, 11), QTime(10, 0), QTimeZone::UTC);
    manifest.appVersion = QStringLiteral("test");
    manifest.serverName = QStringLiteral("attic");
    manifest.serverUrl = QStringLiteral("http://attic.test:8080");
    manifest.engine = engine;
    manifest.version =
        engine == EngineKind::Qbittorrent ? QStringLiteral("v5.3.0") : QStringLiteral("4.1.3");
    manifest.savePath = QStringLiteral("/data");
    manifest.count = int(records.size());
    const QString path = m_home->filePath(QStringLiteral("attic.omaseed-torrents"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qFatal("cannot write %s", qPrintable(path));
    TarWriter writer(&file);
    bool ok = writer.add(TorrentsBackup::kManifest, QJsonDocument(manifest.toJson()).toJson(), 0);
    for (const TorrentRecord &r : records) {
        ok = ok &&
             writer.add(TorrentsBackup::recordEntry(r.id), QJsonDocument(r.toJson()).toJson(), 0);
        if (!r.isMagnet())
            ok = ok && writer.add(TorrentsBackup::torrentEntry(r.id),
                                  torrentBytes(torrents.value(r.id)),
                                  0);
    }
    if (!ok || !writer.finish())
        qFatal("cannot write %s", qPrintable(path));
    return path;
}

bool TestAppTransmissionImport::connected(App &app)
{
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.engine()->globalState()->connected(); }, 5000);
}

bool TestAppTransmissionImport::startOn(App &app)
{
    QUrl url = m_server->url();
    url.setPath(QStringLiteral("/transmission/web/"));
    if (!connected(app) && app.transmissionEngine() != nullptr)
        return false;
    const QString id = app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), url.toString()},
        {QStringLiteral("authMode"), QStringLiteral("none")},
        {QStringLiteral("engine"), QStringLiteral("transmission")},
    });
    return !id.isEmpty() && QTest::qWaitFor(
                                [&app]() {
                                    return app.transmissionEngine() != nullptr &&
                                           app.engine()->globalState()->connected();
                                },
                                5000);
}

QVariantMap TestAppTransmissionImport::read(App &app, const QString &path)
{
    QSignalSpy spy(&app, &App::torrentsImportRead);
    const QString error = app.readTorrentsImport(path);
    if (!error.isEmpty())
        return {{QStringLiteral("error"), error}};
    if (!spy.wait(5000))
        return {{QStringLiteral("error"), QStringLiteral("never read")}};
    return spy.at(0).at(0).toMap();
}

QString TestAppTransmissionImport::import(App &app, const QVariantMap &choice)
{
    QSignalSpy finished(&app, &App::torrentsImportFinished);
    app.applyTorrentsImport(choice);
    if (finished.isEmpty() && !finished.wait(20000))
        return QStringLiteral("never finished");
    return (finished.at(0).at(1).toBool() ? QStringLiteral("ok: ") : QStringLiteral("failed: ")) +
           finished.at(0).at(0).toString();
}

int TestAppTransmissionImport::looks() const
{
    int n = 0;
    for (const Call &call : m_daemon->calls)
        n += call.method == QLatin1String("torrent_get") &&
                     !call.params.contains(QStringLiteral("format"))
                 ? 1
                 : 0;
    return n;
}

// C40 X1, X4, X5, X8: a qBittorrent's file. The sheet's data and the result are made the same
// way as on a qBittorrent, by Transmission's importer.
void TestAppTransmissionImport::aQbittorrentsFileGoesIntoATransmission()
{
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("music");
    single.tags = {QStringLiteral("flac")};
    single.seedingTimeLimit = 600;
    // Without its top folder on qBittorrent: Transmission has no such layout.
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("music");
    pack.layout = QStringLiteral("NoSubfolder");
    pack.autoTmm = true;
    const QString path = write({single, pack, linkRecord()},
                               {{single.id, kSingle}, {pack.id, kPack}},
                               EngineKind::Qbittorrent);

    App app;
    QVERIFY(startOn(app));
    QCOMPARE(app.qbtEngine(), nullptr);
    // The server the file came from, saved: the result's last line names it (X8).
    Server attic;
    attic.id = Server::newId();
    attic.name = QStringLiteral("attic");
    attic.url = QUrl(QStringLiteral("http://attic.test:8080"));
    QVERIFY(app.servers()->add(attic));

    const QVariantMap info = read(app, path);
    QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
             qPrintable(info.value(QStringLiteral("error")).toString()));
    QCOMPARE(info.value(QStringLiteral("title")).toString(),
             QStringLiteral("Import 3 torrents into seedbox"));
    QVERIFY2(info.value(QStringLiteral("sub"))
                 .toString()
                 .endsWith(QStringLiteral(", attic.omaseed-torrents · qBittorrent 5.3.0")),
             qPrintable(info.value(QStringLiteral("sub")).toString()));
    QCOMPARE(info.value(QStringLiteral("crossing")).toBool(), true);
    QCOMPARE(info.value(QStringLiteral("importCount")).toInt(), 3);
    // X4: the category, then the tags, each a label; nothing is created first.
    QCOMPARE(info.value(QStringLiteral("namesTitle")).toString(), QStringLiteral("labels"));
    const QVariantList names = info.value(QStringLiteral("names")).toList();
    QCOMPARE(names.size(), 2);
    QCOMPARE(names.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("music"));
    QCOMPARE(names.at(0).toMap().value(QStringLiteral("kind")).toString(),
             QStringLiteral("category"));
    QCOMPARE(names.at(0).toMap().value(QStringLiteral("to")).toString(),
             QStringLiteral("label music"));
    QCOMPARE(names.at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("tag"));
    QCOMPARE(names.at(1).toMap().value(QStringLiteral("to")).toString(),
             QStringLiteral("label flac"));
    QCOMPARE(info.value(QStringLiteral("created")).toString(), QString());
    // X5: what Transmission can't keep, counted.
    QCOMPARE(info.value(QStringLiteral("lostTitle")).toString(),
             QStringLiteral("can't be kept on Transmission"));
    const QStringList lost{
        QStringLiteral("1 has a seeding-time limit: Transmission has none"),
        QStringLiteral("1 is managed automatically by its category's folder: it keeps the folder "
                       "it is in")};
    QCOMPARE(info.value(QStringLiteral("losses")).toStringList(), lost);
    QCOMPARE(info.value(QStringLiteral("kept")).toString(), QString());

    QCOMPARE(import(app),
             QStringLiteral("failed: Imported into seedbox: 2 torrents, 1 link. After the check: "
                            "1 started, 1 left stopped. 1 is not where the file says and stays "
                            "stopped: %1. 1 came as a link: it is checked once its details "
                            "arrive. %2. %3. They are still on attic. Unfinished ones stay "
                            "stopped here until you stop or remove them there.")
                 .arg(pack.name, lost.at(0), lost.at(1)));
    QVERIFY(app.torrentsBackupRunning().isEmpty());
    QCOMPARE(m_held.value(single.infohashV1).labels, QStringList({"music", "flac"}));
    QVERIFY(m_held.value(single.infohashV1).running);
    QVERIFY(!m_held.value(pack.infohashV1).running);
    QVERIFY(!m_held.value(kLinkHash).running);
    app.engine()->stop();
}

// A Transmission's own file: no crossing, the head names its engine, and its links may be
// torrents whose .torrent files the export couldn't read (C40 X3).
void TestAppTransmissionImport::aTransmissionsOwnFileIsNoCrossing()
{
    const TorrentRecord single = recordOf(kSingle);
    const QString path =
        write({single, linkRecord()}, {{single.id, kSingle}}, EngineKind::Transmission);
    App app;
    QVERIFY(startOn(app));
    const QVariantMap info = read(app, path);
    QVERIFY(info.value(QStringLiteral("ok")).toBool());
    QCOMPARE(info.value(QStringLiteral("crossing")).toBool(), false);
    QVERIFY2(info.value(QStringLiteral("sub"))
                 .toString()
                 .endsWith(QStringLiteral("(Transmission 4.1.3), attic.omaseed-torrents.")),
             qPrintable(info.value(QStringLiteral("sub")).toString()));
    QVERIFY(info.value(QStringLiteral("names")).toList().isEmpty());
    const QVariantMap links = info.value(QStringLiteral("plan")).toList().at(2).toMap();
    QCOMPARE(links.value(QStringLiteral("label")).toString(),
             QStringLiteral("links, without their .torrent files"));
    QCOMPARE(links.value(QStringLiteral("count")).toInt(), 1);
    // No saved server it came from, nothing lost, nothing misplaced: the plain words.
    QCOMPARE(import(app),
             QStringLiteral("ok: Imported into seedbox: 1 torrent, 1 link. After the check: 1 "
                            "started. 1 came as a link: it is checked once its details arrive."));
    app.engine()->stop();
}

// C40 X7: the links wait in the journal. A poll looks at them no more often than the
// interval, a look that finds no details says nothing, and a later start finishes them once
// the daemon answers: one strip.
void TestAppTransmissionImport::linksAreFinishedCalmlyAndAfterAConnect()
{
    const QString path = write({linkRecord()}, {}, EngineKind::Transmission);
    QString journal;
    {
        App app;
        QVERIFY(startOn(app));
        journal = Import::Journal::pathFor(app.servers()->servers().constFirst().id);
        QVERIFY(read(app, path).value(QStringLiteral("ok")).toBool());
        QCOMPARE(import(app),
                 QStringLiteral("ok: Imported into seedbox: 1 link. 1 came as a link: it is "
                                "checked once its details arrive."));
        QCOMPARE(Import::linksWaiting(journal), 1);

        // The polls go on, and the journal is left alone for the interval (a minute).
        const int before = looks();
        QTest::qWait(450);
        QCOMPARE(looks(), before);
        // The interval over: one look. Its details are not there, so nothing is said.
        app.setLinksInterval(std::chrono::milliseconds(50));
        QVERIFY(QTest::qWaitFor([this, before]() { return looks() > before; }, 5000));
        QTest::qWait(150);
        QVERIFY(app.outcomes()->current().isEmpty());
        QCOMPARE(Import::linksWaiting(journal), 1);
        QVERIFY(!m_held.value(kLinkHash).running);
        app.engine()->stop();
    }

    // Its details arrive while omaseed is closed.
    m_held[kLinkHash].metadata = true;
    m_held[kLinkHash].files = QStringList{"it/a.bin", "it/b.bin"};
    m_daemon->calls.clear();
    App app;
    QVERIFY(connected(app));
    QVERIFY(QTest::qWaitFor([&app]() { return !app.outcomes()->current().isEmpty(); }, 5000));
    const QVariantMap strip = app.outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(),
             QStringLiteral("1 imported link got its details and was checked."));
    QCOMPARE(strip.value(QStringLiteral("kind")).toString(), QStringLiteral("done"));
    // Its file choices, its check, and its start: it was started at export.
    QStringList sent;
    for (const Call &call : m_daemon->calls) {
        if (call.method != QLatin1String("torrent_get") &&
            strings(call.params.value(QStringLiteral("ids"))).contains(kLinkHash))
            sent.append(call.method);
    }
    QCOMPARE(sent, QStringList({"torrent_set", "torrent_verify", "torrent_start"}));
    QVERIFY(m_held.value(kLinkHash).running);
    QCOMPARE(Import::linksWaiting(journal), 0);
    // Nothing waits: the polls ask no more.
    const int after = looks();
    app.setLinksInterval(std::chrono::milliseconds(50));
    QTest::qWait(400);
    QCOMPARE(looks(), after);
    app.engine()->stop();
}

QTEST_MAIN(TestAppTransmissionImport)
#include "tst_app_transmissionimport.moc"
