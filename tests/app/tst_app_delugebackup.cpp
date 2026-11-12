// A Deluge's torrents exported and imported in the app (#88; docs/57 §3–§6;
// docs/DECISIONS.md C40 X1, X3–X8): App on a fake deluge-web (FakeDeluge). The export is
// Deluge's own and reads the .torrent files from the daemon's state folder; a daemon that
// doesn't tell its folder is out of sight at once, until a folder is given. The import picks
// Deluge's importer: the sheet's data name the one label a qBittorrent's category becomes, in
// Deluge's spelling, the tags left behind, the labels made first and what Deluge can't keep;
// with the Label plugin off the sheet warns and the result says how many kept no label; and
// the links are finished from the journal later.

#include "FakeDeluge.hpp"

#include "core/ActionOutcome.hpp"
#include "core/GlobalState.hpp"
#include "core/ServerStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TarArchive.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeTorrentsImport.hpp"
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
using namespace fakedeluge;
using namespace omaseed::core;

namespace {

using Import = DelugeTorrentsImport;

const char *const kPack = "multi_file_v1_unicode.torrent";
const char *const kSingle = "single_file_v1.torrent";
const QString kRecordedPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kRecordedSingle = QStringLiteral("ed6dc254e7756302401a59491e69f6a53ac157be");
const QString kRecordedMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
const QString kLinkHash = QStringLiteral("88").repeated(20);
const QString kPluginOff = QStringLiteral("Deluge's Label plugin is off: no label will be set.");

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

/// One torrent as the fake daemon holds it once an import handed it over.
struct Held
{
    QStringList files;
    QString label;
    QJsonArray priorities;
    bool paused = true;
    bool checked = false;
    int checks = 0; ///< polls it still shows as checking
};

/// deluge-web as the app sees it: the recorded daemon for the engine's reads (the poll, its
/// settings, its labels), and a small state of its own for what an import hands it, as
/// tst_delugetorrentsimport's.
class Daemon
{
public:
    FakeServer server;
    FakeDeluge web;
    RecordedDeluge recorded;
    QJsonObject config = result("core_get_config.json").toObject();
    QHash<QString, Held> held;
    QStringList order;
    QStringList labels{QStringLiteral("music")};

    Daemon()
    {
        server.setHandler(web.handler());
        web.answer = [this](const QString &method, const QJsonArray &params) {
            return answer(method, params);
        };
    }

    QUrl url() const
    {
        QUrl url = server.url();
        url.setPath(QStringLiteral("/"));
        return url;
    }

    /// The methods that named `hash` first, reads aside.
    QStringList methodsFor(const QString &hash) const
    {
        QStringList out;
        for (const Call &call : web.calls) {
            if (call.method == QLatin1String("core.get_torrents_status") ||
                call.method == QLatin1String("web.update_ui"))
                continue;
            const QJsonValue first = call.params.at(0);
            if (first.toString() == hash || strings(first).contains(hash))
                out.append(call.method);
        }
        return out;
    }

    /// How often the import or the links' finisher asked for torrents' files.
    int looks() const
    {
        int n = 0;
        for (const Call &call : web.calls)
            n += call.method == QLatin1String("core.get_torrents_status") &&
                         strings(call.params.at(1)).contains(QStringLiteral("files"))
                     ? 1
                     : 0;
        return n;
    }

private:
    QJsonValue answer(const QString &method, const QJsonArray &params)
    {
        if (method == QLatin1String("core.get_config"))
            return config;
        if (method == QLatin1String("core.get_magnet_uri"))
            return QStringLiteral("magnet:?xt=urn:btih:%1&dn=x").arg(params.at(0).toString());
        if (method.startsWith(QLatin1String("label.")) && !recorded.labelPlugin)
            return errorOf(2, QStringLiteral("Unknown method"));
        if (method == QLatin1String("label.get_labels"))
            return QJsonArray::fromStringList(labels);
        if (method == QLatin1String("label.get_options"))
            return result("label_get_options_empty.json");
        if (method == QLatin1String("label.add")) {
            labels.append(params.at(0).toString());
            return QJsonValue();
        }
        if (method == QLatin1String("label.set_torrent")) {
            if (!labels.contains(params.at(1).toString()))
                return errorOf(4, QStringLiteral("Unknown Label"));
            held[params.at(0).toString()].label = params.at(1).toString();
            return QJsonValue();
        }
        const bool file = method == QLatin1String("core.add_torrent_file");
        if (file || method == QLatin1String("core.add_torrent_magnet")) {
            Held torrent;
            QString hash;
            if (file) {
                const TorrentFile parsed =
                    TorrentFile::parse(QByteArray::fromBase64(params.at(1).toString().toLatin1()));
                hash = parsed.infoHashV1;
                torrent.files = TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"));
            } else {
                hash = params.at(0).toString().section(QStringLiteral("btih:"), 1).left(40);
            }
            if (held.contains(hash))
                return errorOf(4, QStringLiteral("Torrent already in session (%1).").arg(hash));
            torrent.priorities = params.at(file ? 2 : 1)
                                     .toObject()
                                     .value(QStringLiteral("file_priorities"))
                                     .toArray();
            held.insert(hash, torrent);
            order.append(hash);
            return hash;
        }
        // The import's torrents; the recorded ones are the export's and the poll's.
        if (method == QLatin1String("core.get_torrents_status") && !held.isEmpty()) {
            const QJsonObject filter = params.at(0).toObject();
            const QStringList ids = strings(filter.value(QStringLiteral("id")));
            const QStringList keys = strings(params.at(1));
            QJsonObject out;
            for (const QString &hash : std::as_const(order)) {
                if (filter.contains(QStringLiteral("id")) && !ids.contains(hash))
                    continue;
                Held &torrent = held[hash];
                const bool checking = torrent.checks > 0;
                QJsonObject row;
                if (keys.contains(QStringLiteral("files"))) {
                    QJsonArray files;
                    for (int i = 0; i < torrent.files.size(); ++i)
                        files.append(QJsonObject{{QStringLiteral("index"), i},
                                                 {QStringLiteral("path"), torrent.files.at(i)}});
                    row.insert(QStringLiteral("files"), files);
                }
                if (keys.contains(QStringLiteral("file_priorities")))
                    row.insert(QStringLiteral("file_priorities"), torrent.priorities);
                if (keys.contains(QStringLiteral("queue")))
                    row.insert(QStringLiteral("queue"), -1);
                if (keys.contains(QStringLiteral("state"))) {
                    if (checking)
                        --torrent.checks;
                    row.insert(QStringLiteral("state"),
                               checking         ? QStringLiteral("Checking")
                               : torrent.paused ? QStringLiteral("Paused")
                                                : QStringLiteral("Seeding"));
                }
                if (keys.contains(QStringLiteral("is_finished")))
                    row.insert(QStringLiteral("is_finished"), torrent.checked && !checking);
                out.insert(hash, row);
            }
            return out;
        }
        if (method == QLatin1String("core.force_recheck") ||
            method == QLatin1String("core.resume_torrents") ||
            method == QLatin1String("core.set_torrent_options")) {
            for (const QString &hash : strings(params.at(0))) {
                if (!held.contains(hash))
                    continue;
                Held &torrent = held[hash];
                if (method == QLatin1String("core.force_recheck")) {
                    torrent.checks = 1;
                    torrent.checked = true;
                } else if (method == QLatin1String("core.resume_torrents")) {
                    torrent.paused = false;
                }
            }
            return QJsonValue();
        }
        return recorded.answer(method, params);
    }
};

} // namespace

class TestAppDelugeBackup : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void theExportIsDelugesOwn();
    void aDaemonThatTellsNoFolderIsOutOfSightAtOnce();
    void aQbittorrentsFileGoesIntoADeluge();
    void withTheLabelPluginOffItIsSaidBeforeAndAfter();
    void aDelugesOwnFileIsNoCrossing();
    void linksAreFinishedLater();

private:
    /// A recorded row as the torrent of `fixtureName`, its .torrent in `m_folder`.
    void adopt(Daemon &daemon, const QString &recorded, const char *fixtureName);
    /// Three torrents: a pack and a single file with their .torrent files in `m_folder`, and
    /// the recorded magnet without metadata.
    void fill(Daemon &daemon);
    /// Starts `app` on `daemon` through the environment, polling every 100 ms, and waits
    /// until it is connected and shows `torrents`.
    bool startOn(App &app, Daemon &daemon, int torrents);
    bool settled(App &app, const QString &reach);
    QString exportTo(App &app, const QString &path);
    /// Writes a torrents file as `engine`'s "attic" exported it; `torrents` are fixture names
    /// by record id.
    QString write(const QList<TorrentRecord> &records,
                  const QHash<QString, const char *> &torrents,
                  EngineKind engine);
    QVariantMap read(App &app, const QString &path);
    QString import(App &app);

    std::unique_ptr<QTemporaryDir> m_home;
    QString m_folder; ///< the daemon's state folder
};

void TestAppDelugeBackup::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->filePath(QStringLiteral("config")).toUtf8());
    // The import's journal is this test's own.
    qputenv("XDG_DATA_HOME", m_home->filePath(QStringLiteral("data")).toUtf8());
    m_folder = m_home->filePath(QStringLiteral("deluge/state"));
    QVERIFY(QDir().mkpath(m_folder));
    QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral("empty"))));
}

void TestAppDelugeBackup::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_ENGINE");
    qunsetenv("OMASEED_PASSWORD");
    m_home.reset();
}

void TestAppDelugeBackup::adopt(Daemon &daemon, const QString &recorded, const char *fixtureName)
{
    QJsonObject json = daemon.recorded.torrents.take(recorded).toObject();
    const QByteArray bytes = torrentBytes(fixtureName);
    const TorrentFile parsed = TorrentFile::parse(bytes);
    QVERIFY(parsed.isValid());
    const QString hash = parsed.infoHashV1;
    json.insert(QStringLiteral("hash"), hash);
    json.insert(QStringLiteral("name"), parsed.name);
    QJsonArray files;
    QJsonArray priorities;
    int index = 0;
    for (const QString &name : TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"))) {
        files.append(
            QJsonObject{{QStringLiteral("index"), index++}, {QStringLiteral("path"), name}});
        priorities.append(4);
    }
    json.insert(QStringLiteral("files"), files);
    json.insert(QStringLiteral("orig_files"), files);
    json.insert(QStringLiteral("file_priorities"), priorities);
    daemon.recorded.torrents.insert(hash, json);
    QFile file(QDir(m_folder).filePath(hash + QStringLiteral(".torrent")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}

void TestAppDelugeBackup::fill(Daemon &daemon)
{
    for (const QString &hash : daemon.recorded.torrents.keys()) {
        if (hash != kRecordedPack && hash != kRecordedSingle && hash != kRecordedMagnet)
            daemon.recorded.torrents.remove(hash);
    }
    adopt(daemon, kRecordedPack, kPack);
    adopt(daemon, kRecordedSingle, kSingle);
    QCOMPARE(daemon.recorded.torrents.size(), 3);
}

bool TestAppDelugeBackup::startOn(App &app, Daemon &daemon, int torrents)
{
    qputenv("OMASEED_SERVER_URL", daemon.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_ENGINE", "deluge");
    qputenv("OMASEED_PASSWORD", daemon.web.password.toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor(
        [&app, torrents]() {
            return app.delugeEngine() != nullptr && app.engine()->globalState()->connected() &&
                   app.torrents()->count() == torrents;
        },
        5000);
}

bool TestAppDelugeBackup::settled(App &app, const QString &reach)
{
    return QTest::qWaitFor([&app, &reach]() { return app.torrentFilesReach() == reach; }, 5000);
}

QString TestAppDelugeBackup::exportTo(App &app, const QString &path)
{
    QSignalSpy finished(&app, &App::torrentsExportFinished);
    app.exportTorrents(path);
    if (app.torrentsBackupRunning() != QLatin1String("export"))
        return QStringLiteral("not running: ") + finished.value(0).value(0).toString();
    if (!finished.wait(5000))
        return QStringLiteral("never finished");
    return (finished.at(0).at(1).toBool() ? QStringLiteral("ok: ") : QStringLiteral("failed: ")) +
           finished.at(0).at(0).toString();
}

QString TestAppDelugeBackup::write(const QList<TorrentRecord> &records,
                                   const QHash<QString, const char *> &torrents,
                                   EngineKind engine)
{
    TorrentsManifest manifest;
    manifest.created = QDateTime(QDate(2026, 11, 12), QTime(10, 0), QTimeZone::UTC);
    manifest.appVersion = QStringLiteral("test");
    manifest.serverName = QStringLiteral("attic");
    manifest.serverUrl = QStringLiteral("http://attic.test:8080");
    manifest.engine = engine;
    manifest.version =
        engine == EngineKind::Deluge ? QStringLiteral("2.2.0") : QStringLiteral("5.3.0");
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

QVariantMap TestAppDelugeBackup::read(App &app, const QString &path)
{
    QSignalSpy spy(&app, &App::torrentsImportRead);
    const QString error = app.readTorrentsImport(path);
    if (!error.isEmpty())
        return {{QStringLiteral("error"), error}};
    if (!spy.wait(5000))
        return {{QStringLiteral("error"), QStringLiteral("never read")}};
    return spy.at(0).at(0).toMap();
}

QString TestAppDelugeBackup::import(App &app)
{
    QSignalSpy finished(&app, &App::torrentsImportFinished);
    app.applyTorrentsImport({});
    if (finished.isEmpty() && !finished.wait(20000))
        return QStringLiteral("never finished");
    return (finished.at(0).at(1).toBool() ? QStringLiteral("ok: ") : QStringLiteral("failed: ")) +
           finished.at(0).at(0).toString();
}

// C40 X1, X3: the same calls, Deluge's exporter. Its settings tell where its plugins are; the
// state folder beside them holds the .torrent files, and the row says from where.
void TestAppDelugeBackup::theExportIsDelugesOwn()
{
    Daemon daemon;
    fill(daemon);
    daemon.config.insert(QStringLiteral("plugins_location"),
                         m_home->filePath(QStringLiteral("deluge/plugins")));
    App app;
    QVERIFY(startOn(app, daemon, 3));
    QCOMPARE(app.qbtEngine(), nullptr);
    QCOMPARE(app.transmissionEngine(), nullptr);
    const QVariantMap can = app.featuresMap();
    for (const char *on : {"torrentsBackup", "torrentsExport", "torrentsImport"})
        QVERIFY2(can.value(QString::fromLatin1(on)).toBool(), on);
    QVERIFY(!can.value(QStringLiteral("qbtSettings")).toBool());
    QCOMPARE(app.torrentFilesReach(), QString());
    // One label per torrent: "label", where a Transmission's row says labels.
    QCOMPARE(app.torrentsExportSummary(),
             QStringLiteral("3 torrents with their folders, label, limits and renames, in one "
                            "private file."));

    app.refreshTorrentFilesReach();
    QCOMPARE(app.torrentFilesReach(), QStringLiteral("asking"));
    QVERIFY(settled(app, QStringLiteral("readable")));
    QCOMPARE(app.torrentFilesDaemonFolder(), m_folder);
    QVERIFY2(app.torrentFilesFolder().endsWith(QStringLiteral("/deluge/state")),
             qPrintable(app.torrentFilesFolder()));
    QCOMPARE(app.torrentFilesRemembered(), QString());
    QCOMPARE(app.torrentFilesNote(), QString());

    const QString path = m_home->filePath(QStringLiteral("out.omaseed-torrents"));
    QCOMPARE(exportTo(app, path),
             QStringLiteral("ok: Exported %1's 3 torrents to %2. 1 magnet went in as a link.")
                 .arg(app.serverName(), path));
    QVERIFY(app.torrentsBackupRunning().isEmpty());
    QString error;
    const std::optional<TorrentsBackup::Contents> contents = TorrentsBackup::read(path, &error);
    QVERIFY2(contents.has_value(), qPrintable(error));
    QCOMPARE(contents->manifest.engine, EngineKind::Deluge);
    QCOMPARE(contents->manifest.count, 3);
    QCOMPARE(contents->manifest.magnets, 1);

    // The file read back for an import: all three are here already.
    const QVariantMap info = read(app, path);
    QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
             qPrintable(info.value(QStringLiteral("error")).toString()));
    QCOMPARE(info.value(QStringLiteral("crossing")).toBool(), false);
    QCOMPARE(info.value(QStringLiteral("importCount")).toInt(), 0);
    app.engine()->stop();
}

// C40 X3: Deluge's API names no config folder. With no `plugins_location` to go by the row
// offers "their folder…" and "export links…" straight away, with no folder to show; a folder
// given is checked and then read from.
void TestAppDelugeBackup::aDaemonThatTellsNoFolderIsOutOfSightAtOnce()
{
    Daemon daemon;
    fill(daemon);
    daemon.config.remove(QStringLiteral("plugins_location"));
    App app;
    QVERIFY(startOn(app, daemon, 3));
    app.refreshTorrentFilesReach();
    QVERIFY(settled(app, QStringLiteral("unreadable")));
    QCOMPARE(app.torrentFilesDaemonFolder(), QString());
    QCOMPARE(app.torrentFilesFolder(), QString());

    // "export links…": every torrent as its link, the result saying why.
    const QString links = m_home->filePath(QStringLiteral("links.omaseed-torrents"));
    QCOMPARE(exportTo(app, links),
             QStringLiteral("ok: Exported %1's 3 torrents to %2. 3 went in as links: omaseed "
                            "couldn't read their .torrent files.")
                 .arg(app.serverName(), links));

    // A folder that isn't theirs: said, nothing kept.
    app.useTorrentFilesFolder(m_home->filePath(QStringLiteral("empty")));
    QVERIFY(QTest::qWaitFor([&app]() { return !app.torrentFilesNote().isEmpty(); }, 5000));
    QCOMPARE(app.torrentFilesReach(), QStringLiteral("unreadable"));
    QCOMPARE(app.torrentFilesNote(),
             QStringLiteral("That folder doesn't hold %1's .torrent files.").arg(app.serverName()));
    QCOMPARE(app.torrentFilesRemembered(), QString());

    // Theirs: remembered, and the files are read from it.
    app.useTorrentFilesFolder(m_folder);
    QVERIFY(settled(app, QStringLiteral("readable")));
    QCOMPARE(app.torrentFilesRemembered(), m_folder);
    QCOMPARE(app.torrentFilesNote(), QString());
    const QString path = m_home->filePath(QStringLiteral("out.omaseed-torrents"));
    QCOMPARE(exportTo(app, path),
             QStringLiteral("ok: Exported %1's 3 torrents to %2. 1 magnet went in as a link.")
                 .arg(app.serverName(), path));
    // Asked again (the pane shown again): still read from the folder given.
    app.refreshTorrentFilesReach();
    QVERIFY(settled(app, QStringLiteral("readable")));
    app.engine()->stop();
}

// C40 X1, X4, X5, X8: a qBittorrent's file. One label per torrent, the category, in Deluge's
// spelling; a tag is left behind; the label the daemon lacks is made first.
void TestAppDelugeBackup::aQbittorrentsFileGoesIntoADeluge()
{
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("music");
    single.tags = {QStringLiteral("flac")};
    single.seedingTimeLimit = 600;
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("Field Recordings");
    pack.autoTmm = true;
    const QString path =
        write({single, pack}, {{single.id, kSingle}, {pack.id, kPack}}, EngineKind::Qbittorrent);

    Daemon daemon;
    daemon.recorded.torrents = QJsonObject();
    App app;
    QVERIFY(startOn(app, daemon, 0));
    const QString name = app.serverName();
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
             QStringLiteral("Import 2 torrents into %1").arg(name));
    QVERIFY2(info.value(QStringLiteral("sub"))
                 .toString()
                 .endsWith(QStringLiteral(", attic.omaseed-torrents · qBittorrent 5.3.0")),
             qPrintable(info.value(QStringLiteral("sub")).toString()));
    QCOMPARE(info.value(QStringLiteral("crossing")).toBool(), true);
    QCOMPARE(info.value(QStringLiteral("importCount")).toInt(), 2);
    // X4: each name and what it becomes.
    QCOMPARE(info.value(QStringLiteral("namesTitle")).toString(), QStringLiteral("labels"));
    QStringList rows;
    for (const QVariant &v : info.value(QStringLiteral("names")).toList()) {
        const QVariantMap row = v.toMap();
        rows.append(QStringLiteral("%1 %2 → %3%4")
                        .arg(row.value(QStringLiteral("name")).toString(),
                             row.value(QStringLiteral("kind")).toString(),
                             row.value(QStringLiteral("to")).toString(),
                             row.value(QStringLiteral("left")).toBool() ? QStringLiteral(" (dim)")
                                                                        : QString()));
    }
    rows.sort();
    QCOMPARE(rows,
             QStringList({QStringLiteral("Field Recordings category → label field-recordings"),
                          QStringLiteral("flac tag → left behind (dim)"),
                          QStringLiteral("music category → label music")}));
    // The daemon has "music": one label is made first (label.add).
    QCOMPARE(info.value(QStringLiteral("created")).toString(),
             QStringLiteral("1 label they use is made first"));
    QCOMPARE(info.value(QStringLiteral("namesWarning")).toString(), QString());
    // X5: what Deluge can't keep, counted.
    QCOMPARE(info.value(QStringLiteral("lostTitle")).toString(),
             QStringLiteral("can't be kept on Deluge"));
    const QStringList lost = info.value(QStringLiteral("losses")).toStringList();
    QVERIFY2(lost.contains(QStringLiteral("1 has a seeding-time limit: Deluge has none")),
             qPrintable(lost.join(u" | ")));
    QVERIFY2(lost.contains(QStringLiteral("1 has tags or labels Deluge can't keep: it keeps one "
                                          "label")),
             qPrintable(lost.join(u" | ")));
    QVERIFY2(lost.contains(QStringLiteral("1 is managed automatically by its category's folder: "
                                          "it keeps the folder it is in")),
             qPrintable(lost.join(u" | ")));
    QCOMPARE(lost.size(), 3);
    QCOMPARE(info.value(QStringLiteral("kept")).toString(), QString());

    QString said;
    for (const QString &line : lost)
        said += QLatin1Char(' ') + line + QLatin1Char('.');
    QCOMPARE(import(app),
             QStringLiteral("ok: Imported into %1: 2 torrents. After the check: 2 started.%2 "
                            "They are still on attic. Unfinished ones stay stopped here until "
                            "you stop or remove them there.")
                 .arg(name, said));
    QVERIFY(app.torrentsBackupRunning().isEmpty());
    // Deluge's importer: the label made once, each torrent given its one.
    QCOMPARE(daemon.web.callsOf(QStringLiteral("label.add")).size(), 1);
    QCOMPARE(daemon.web.callsOf(QStringLiteral("label.add")).at(0).params.at(0).toString(),
             QStringLiteral("field-recordings"));
    QCOMPARE(daemon.held.value(single.infohashV1).label, QStringLiteral("music"));
    QCOMPARE(daemon.held.value(pack.infohashV1).label, QStringLiteral("field-recordings"));
    QVERIFY(!daemon.held.value(single.infohashV1).paused);
    QVERIFY(!daemon.held.value(pack.infohashV1).paused);
    app.engine()->stop();
}

// C40 X6: with the Label plugin off no label is made or set. The sheet says so before the
// import, in the labels block, and the result says how many kept none and where to turn it on.
void TestAppDelugeBackup::withTheLabelPluginOffItIsSaidBeforeAndAfter()
{
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("music");
    TorrentRecord pack = recordOf(kPack);
    pack.category = QStringLiteral("Field Recordings");
    const QString path =
        write({single, pack}, {{single.id, kSingle}, {pack.id, kPack}}, EngineKind::Qbittorrent);

    Daemon daemon;
    daemon.recorded.torrents = QJsonObject();
    daemon.recorded.labelPlugin = false;
    App app;
    QVERIFY(startOn(app, daemon, 0));
    QVERIFY(!app.delugeEngine()->labelPlugin());
    const QVariantMap info = read(app, path);
    QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
             qPrintable(info.value(QStringLiteral("error")).toString()));
    QCOMPARE(info.value(QStringLiteral("names")).toList().size(), 2);
    QCOMPARE(info.value(QStringLiteral("namesWarning")).toString(), kPluginOff);
    // Nothing is made first: there is nothing to make it with.
    QCOMPARE(info.value(QStringLiteral("created")).toString(), QString());

    QCOMPARE(import(app),
             QStringLiteral("ok: Imported into %1: 2 torrents. After the check: 2 started. 2 "
                            "kept no label: Deluge's Label plugin is off (Settings › Deluge › "
                            "Plugins).")
                 .arg(app.serverName()));
    QVERIFY(daemon.web.callsOf(QStringLiteral("label.add")).isEmpty());
    QVERIFY(daemon.web.callsOf(QStringLiteral("label.set_torrent")).isEmpty());
    app.engine()->stop();
}

// A Deluge's own file: no crossing, no names block, and so no warning without a name to set;
// one torrent with a label and the plugin off reads in the singular.
void TestAppDelugeBackup::aDelugesOwnFileIsNoCrossing()
{
    TorrentRecord single = recordOf(kSingle);
    single.category = QStringLiteral("music");
    const TorrentRecord pack = recordOf(kPack);
    const QString path =
        write({single, pack}, {{single.id, kSingle}, {pack.id, kPack}}, EngineKind::Deluge);

    Daemon daemon;
    daemon.recorded.torrents = QJsonObject();
    daemon.recorded.labelPlugin = false;
    App app;
    QVERIFY(startOn(app, daemon, 0));
    const QVariantMap info = read(app, path);
    QVERIFY2(info.value(QStringLiteral("ok")).toBool(),
             qPrintable(info.value(QStringLiteral("error")).toString()));
    QCOMPARE(info.value(QStringLiteral("crossing")).toBool(), false);
    QVERIFY2(info.value(QStringLiteral("sub"))
                 .toString()
                 .endsWith(QStringLiteral("(Deluge 2.2.0), attic.omaseed-torrents.")),
             qPrintable(info.value(QStringLiteral("sub")).toString()));
    QVERIFY(info.value(QStringLiteral("names")).toList().isEmpty());
    QCOMPARE(info.value(QStringLiteral("namesWarning")).toString(), kPluginOff);
    QCOMPARE(import(app),
             QStringLiteral("ok: Imported into %1: 2 torrents. After the check: 2 started. 1 "
                            "kept no label: Deluge's Label plugin is off (Settings › Deluge › "
                            "Plugins).")
                 .arg(app.serverName()));
    app.engine()->stop();
}

// C40 X7: a link waits in the journal; once its details are there a poll finishes it, calmly,
// and one strip says so.
void TestAppDelugeBackup::linksAreFinishedLater()
{
    const QString path = write({linkRecord()}, {}, EngineKind::Deluge);
    Daemon daemon;
    daemon.recorded.torrents = QJsonObject();
    App app;
    QVERIFY(startOn(app, daemon, 0));
    QVERIFY(read(app, path).value(QStringLiteral("ok")).toBool());
    QCOMPARE(import(app),
             QStringLiteral("ok: Imported into %1: 1 link. 1 came as a link: it is checked once "
                            "its details arrive.")
                 .arg(app.serverName()));
    QVERIFY(daemon.held.contains(kLinkHash));

    // The polls go on, and the journal is left alone for the interval (a minute).
    const int before = daemon.looks();
    QTest::qWait(450);
    QCOMPARE(daemon.looks(), before);
    // The interval over, its details not there: a look, and nothing is said.
    app.setLinksInterval(std::chrono::milliseconds(50));
    QVERIFY(QTest::qWaitFor([&daemon, before]() { return daemon.looks() > before; }, 5000));
    QTest::qWait(150);
    QVERIFY(app.outcomes()->current().isEmpty());
    QVERIFY(daemon.held.value(kLinkHash).paused);

    // They arrive: its check, and its start, since it was started at export.
    daemon.held[kLinkHash].files = QStringList{"it/a.bin", "it/b.bin"};
    daemon.held[kLinkHash].priorities = QJsonArray{4, 4};
    QVERIFY(QTest::qWaitFor([&app]() { return !app.outcomes()->current().isEmpty(); }, 10000));
    const QVariantMap strip = app.outcomes()->current();
    QCOMPARE(strip.value(QStringLiteral("headline")).toString(),
             QStringLiteral("1 imported link got its details and was checked."));
    QCOMPARE(strip.value(QStringLiteral("kind")).toString(), QStringLiteral("done"));
    QVERIFY(daemon.methodsFor(kLinkHash).contains(QStringLiteral("core.force_recheck")));
    QVERIFY(!daemon.held.value(kLinkHash).paused);
    // Nothing waits: the polls ask no more.
    QTest::qWait(200);
    const int after = daemon.looks();
    QTest::qWait(400);
    QCOMPARE(daemon.looks(), after);
    app.engine()->stop();
}

QTEST_MAIN(TestAppDelugeBackup)
#include "tst_app_delugebackup.moc"
