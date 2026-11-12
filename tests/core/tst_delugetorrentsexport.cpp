// DelugeTorrentsExport (#88, docs/DECISIONS.md C40 X1, X3): a Deluge's torrents written to a
// TorrentsBackup file, against the tests' fake deluge-web (FakeDeluge). The rows are the
// daemon's recorded ones (tests/fixtures/deluge/core_get_torrents_status.json) given the ids,
// names and files of real .torrent fixtures, whose bytes sit in a folder of the test's as the
// daemon's `state` folder would: record fields row by row, the label with the plugin on and
// off, renames with and without the .torrent, the .torrent read in the daemon's folder or
// under the one given, a link when it can't be read or isn't the torrent's, a magnet without
// metadata, a torrent gone mid-export, the batches, stop, and reach().

#include "FakeDeluge.hpp"

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTorrentsExport.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace fakehttp;
using namespace fakedeluge;
using namespace omaseed::core;

namespace {

using Export = DelugeTorrentsExport;

const QString kRecordedPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kRecordedSingle = QStringLiteral("ed6dc254e7756302401a59491e69f6a53ac157be");
const QString kRecordedError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kRecordedMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
const QString kRecordedStream = QStringLiteral("a8701869a74f56c5c3a16f4673c037a682619afb");
const QString kRecordedQueued = QStringLiteral("6f2336946faaaa1532ae3e8b3216a00bb08e55be");
const QString kForeign = QStringLiteral("/srv/deluge/state");

QByteArray torrentBytes(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

/// What the fake's core.get_magnet_uri answers.
QString daemonMagnet(const QString &hash)
{
    return QStringLiteral("magnet:?xt=urn:btih:%1&dn=x&tr=http%3a%2f%2f127.0.0.1%3a9%2fannounce")
        .arg(hash);
}

QJsonArray filesOf(const QStringList &names)
{
    QJsonArray files;
    for (qsizetype i = 0; i < names.size(); ++i)
        files.append(QJsonObject{{QStringLiteral("index"), int(i)},
                                 {QStringLiteral("path"), names.at(i)},
                                 {QStringLiteral("size"), 1000}});
    return files;
}

QJsonObject tracker(int tier, const char *url)
{
    return QJsonObject{{QStringLiteral("tier"), tier}, {QStringLiteral("url"), QLatin1String(url)}};
}

} // namespace

class TestDelugeTorrentsExport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void recordsRowByRow();
    void theManifestSaysTheDaemon();
    void limitsStateAndQueue();
    void theLabelNeedsItsPlugin();
    void whereTheFilesAreNow();
    void trackersPrioritiesAndUnits();
    void aLinkKeepsItsRenames();
    void aHybridsIdIsQbittorrents();
    void theFolderGivenIsReadByFileName();
    void anUnreadableOrForeignTorrentIsALink();
    void withoutTheDaemonsLinkOmaseedMakesOne();
    void aTorrentRemovedMeanwhileIsLeftOut();
    void twentyAtATime();
    void stopLeavesNoFile();
    void aDaemonsErrorLeavesNoFile();
    void reachSaysWhetherTheFilesAreHere();
    void theStateFolderIsBesideThePlugins();

private:
    /// A recorded torrent as the torrent of `fixtureName`: its id, name and files, its
    /// .torrent in the test's folder (unless `stored` is false). Its new id.
    QString adopt(const QString &recorded, const char *fixtureName, bool stored = true);
    QJsonObject row(const QString &hash) const;
    void set(const QString &hash, const QString &key, const QJsonValue &value);
    Export::Outcome run(const QString &torrentsFolder = {});
    TorrentsBackup::Contents read();
    TorrentRecord record(const TorrentsBackup::Contents &contents, const QString &id);
    QString target() const { return m_dir.filePath(QStringLiteral("out/t.omaseed-torrents")); }
    QStringList outFiles() const
    {
        return QDir(m_dir.filePath(QStringLiteral("out"))).entryList(QDir::Files);
    }

    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeDeluge> m_web;
    std::unique_ptr<RecordedDeluge> m_daemon;
    std::unique_ptr<DelugeEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_folder; ///< this machine's folder with the .torrent files
    QJsonObject m_config;
    QStringList m_goneAfterList; ///< ids the daemon no longer has once listed
    bool m_failBatches = false;
    bool m_noMagnets = false;
    QString m_pack, m_single, m_private, m_wrong, m_magnet;
};

void TestDelugeTorrentsExport::init()
{
    QVERIFY(m_dir.isValid());
    m_folder = m_dir.filePath(QStringLiteral("config/state"));
    QDir(m_folder).removeRecursively();
    QDir(m_dir.filePath(QStringLiteral("out"))).removeRecursively();
    QVERIFY(QDir().mkpath(m_folder));
    m_goneAfterList.clear();
    m_failBatches = false;
    m_noMagnets = false;
    m_daemon = std::make_unique<RecordedDeluge>();
    m_config = result("core_get_config.json").toObject();
    m_config.insert(QStringLiteral("plugins_location"),
                    m_dir.filePath(QStringLiteral("config/plugins")));

    // Five torrents: a pack (seeding, a label, file choices, "move completed" on), a single
    // file (paused, unfinished, limits of its own), a private one (queued) whose .torrent is
    // not in the folder, one whose .torrent there is another torrent's, a magnet without
    // metadata.
    m_daemon->torrents.remove(kRecordedStream);
    m_pack = adopt(kRecordedPack, "multi_file_v1_unicode.torrent");
    set(m_pack,
        QStringLiteral("trackers"),
        QJsonArray{tracker(1, "udp://127.0.0.1:9/announce"),
                   tracker(0, "http://127.0.0.1:9/announce")});
    m_single = adopt(kRecordedSingle, "single_file_v1.torrent");
    m_private = adopt(kRecordedQueued, "private_announce_list.torrent", false);
    m_wrong = adopt(kRecordedError, "open_movies_v1.torrent", false);
    {
        QFile file(QDir(m_folder).filePath(m_wrong + QStringLiteral(".torrent")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(torrentBytes("single_file_v1.torrent"));
    }
    m_magnet = kRecordedMagnet;

    m_server = std::make_unique<FakeServer>();
    m_web = std::make_unique<FakeDeluge>();
    m_web->answer = [this](const QString &method, const QJsonArray &params) -> QJsonValue {
        if (method == QLatin1String("core.get_config"))
            return m_config;
        if (method == QLatin1String("core.get_magnet_uri")) {
            const QString hash = params.at(0).toString();
            if (m_noMagnets || !m_daemon->torrents.contains(hash))
                return errorOf(4, QStringLiteral("'%1'").arg(hash));
            return daemonMagnet(hash);
        }
        if (method == QLatin1String("core.get_torrents_status") &&
            params.at(0).toObject().contains(QStringLiteral("id"))) {
            if (m_failBatches)
                return errorOf(4, QStringLiteral("the daemon fell over"));
            QJsonObject some = m_daemon->answer(method, params).toObject();
            for (const QString &gone : std::as_const(m_goneAfterList))
                some.remove(gone);
            return some;
        }
        return m_daemon->answer(method, params);
    };
    m_server->setHandler(m_web->handler());
    m_engine = std::make_unique<DelugeEngine>();
    m_engine->setBaseUrl(m_server->url());
    m_engine->setCredentials(QString(), m_web->password);
    m_engine->setTimeout(std::chrono::seconds(5));
    m_engine->rpc()->setRetryBackoff(std::chrono::milliseconds(10));
}

void TestDelugeTorrentsExport::cleanup()
{
    m_engine.reset();
    m_daemon.reset();
    m_web.reset();
    m_server.reset();
}

QString
TestDelugeTorrentsExport::adopt(const QString &recorded, const char *fixtureName, bool stored)
{
    QJsonObject json = m_daemon->torrents.take(recorded).toObject();
    const QByteArray bytes = torrentBytes(fixtureName);
    const TorrentFile parsed = TorrentFile::parse(bytes);
    if (!parsed.isValid())
        qFatal("fixture %s does not parse", fixtureName);
    const QString hash = parsed.infoHashV1;
    json.insert(QStringLiteral("hash"), hash);
    json.insert(QStringLiteral("name"), parsed.name);
    const QJsonArray files =
        filesOf(TorrentsBackup::namesUnder(parsed, QStringLiteral("Original")));
    json.insert(QStringLiteral("files"), files);
    json.insert(QStringLiteral("orig_files"), files);
    // The recorded choices, as many as the fixture has files (the rest normal).
    QJsonArray priorities = json.value(QStringLiteral("file_priorities")).toArray();
    while (priorities.size() > files.size())
        priorities.removeLast();
    while (priorities.size() < files.size())
        priorities.append(4);
    json.insert(QStringLiteral("file_priorities"), priorities);
    m_daemon->torrents.insert(hash, json);
    if (stored) {
        QFile file(QDir(m_folder).filePath(hash + QStringLiteral(".torrent")));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            qFatal("cannot store %s", fixtureName);
    }
    return hash;
}

QJsonObject TestDelugeTorrentsExport::row(const QString &hash) const
{
    if (!m_daemon->torrents.contains(hash))
        qFatal("no row %s", qPrintable(hash));
    QJsonObject json = m_daemon->torrents.value(hash).toObject();
    json.insert(QStringLiteral("hash"), hash);
    return json;
}

void TestDelugeTorrentsExport::set(const QString &hash, const QString &key, const QJsonValue &value)
{
    QJsonObject json = row(hash);
    json.insert(key, value);
    m_daemon->torrents.insert(hash, json);
}

Export::Outcome TestDelugeTorrentsExport::run(const QString &torrentsFolder)
{
    Export exporter(m_engine.get());
    exporter.setTorrentsFolder(torrentsFolder);
    QSignalSpy finished(&exporter, &Export::finished);
    exporter.start(target(),
                   QStringLiteral("box"),
                   QStringLiteral("http://box:8112"),
                   QStringLiteral("9.9.9"));
    if (finished.isEmpty() && !finished.wait(10000))
        return {};
    return finished.at(0).at(0).value<Export::Outcome>();
}

TorrentsBackup::Contents TestDelugeTorrentsExport::read()
{
    QString error;
    const auto contents = TorrentsBackup::read(target(), &error);
    if (!contents)
        qFatal("cannot read the file back: %s", qPrintable(error));
    return *contents;
}

TorrentRecord TestDelugeTorrentsExport::record(const TorrentsBackup::Contents &contents,
                                               const QString &id)
{
    for (const TorrentRecord &r : contents.records) {
        if (r.id == id)
            return r;
    }
    qFatal("no record %s", qPrintable(id));
}

void TestDelugeTorrentsExport::recordsRowByRow()
{
    // The pack: its second file renamed on the daemon (`orig_files` keeps the old name); the
    // recorded choices are high, low and not wanted.
    QJsonArray files = row(m_pack).value(QStringLiteral("files")).toArray();
    QVERIFY(files.size() >= 3);
    const QString was = files.at(1).toObject().value(QStringLiteral("path")).toString();
    const QString now = was.section(QLatin1Char('/'), 0, -2) + QStringLiteral("/Renamed here.bin");
    QJsonObject second = files.at(1).toObject();
    second.insert(QStringLiteral("path"), now);
    files[1] = second;
    set(m_pack, QStringLiteral("files"), files);

    const Export::Outcome outcome = run();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.path, target());
    QCOMPARE(outcome.torrents, 5);
    QCOMPARE(outcome.magnets, 3);
    QCOMPARE(outcome.gone, 0);
    // Private: announce URLs can carry passkeys (C9).
    QCOMPARE(QFile::permissions(target()),
             QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadUser |
                 QFileDevice::WriteUser);
    QCOMPARE(outFiles().size(), 1);

    const TorrentsBackup::Contents contents = read();
    QCOMPARE(contents.manifest.engine, EngineKind::Deluge);
    QCOMPARE(contents.manifest.count, 5);
    // Counted before the torrents are read: the .torrent that is another torrent's is found
    // out later, and is in the outcome.
    QCOMPARE(contents.manifest.magnets, 2);
    QCOMPARE(contents.records.size(), 5);
    // By id, the order of the daemon's answer.
    QStringList ids{m_pack, m_single, m_private, m_wrong, m_magnet};
    ids.sort();
    QStringList written;
    for (const TorrentRecord &r : contents.records)
        written.append(r.id);
    QCOMPARE(written, ids);

    const TorrentRecord p = record(contents, m_pack);
    QCOMPARE(p.infohashV1, m_pack);
    QCOMPARE(p.infohashV2, QString());
    QCOMPARE(p.name, row(m_pack).value(QStringLiteral("name")).toString());
    QCOMPARE(p.size, qint64(64440));
    QCOMPARE(p.progress, 1.0);
    QCOMPARE(p.savePath, QStringLiteral("/srv/deluge/fixtures-recording/data"));
    QCOMPARE(p.downloadPath, QString());
    QCOMPARE(p.category, QStringLiteral("fixtures"));
    QCOMPARE(p.tags, QStringList());
    QVERIFY(!p.autoTmm);
    QCOMPARE(p.state, QStringLiteral("started"));
    QCOMPARE(p.queuePosition, 0); // complete: none, as qBittorrent's
    QCOMPARE(p.upLimit, qint64(0));
    QCOMPARE(p.dlLimit, qint64(0));
    QCOMPARE(p.ratioLimit, -2.0); // `stop_ratio` 2.0 on the row, but not `stop_at_ratio`
    QCOMPARE(p.seedingTimeLimit, qint64(-2));
    QCOMPARE(p.inactiveSeedingTimeLimit, qint64(-2));
    QCOMPARE(p.shareLimitAction, QStringLiteral("Default"));
    QVERIFY(!p.sequential);
    QVERIFY(!p.firstLastPiecePrio);
    QCOMPARE(p.layout, QStringLiteral("Original"));
    QCOMPARE(p.renamed, QList<TorrentRecord::Rename>({{1, was, now}}));
    QList<int> priorities(files.size(), 1);
    priorities[0] = 6; // Deluge's 7
    priorities[1] = 2; // its 1: low
    priorities[2] = 0;
    QCOMPARE(p.priorities, priorities);
    QCOMPARE(p.trackers,
             QStringList({"http://127.0.0.1:9/announce", "udp://127.0.0.1:9/announce"}));
    QVERIFY(!p.isMagnet());
    // Its .torrent, byte for byte.
    QFile file(target());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(TorrentsBackup::torrentBytes(&file, contents, m_pack),
             torrentBytes("multi_file_v1_unicode.torrent"));

    // The magnet that never got its metadata: the daemon's link, nothing of its files.
    const TorrentRecord m = record(contents, m_magnet);
    QVERIFY(m.isMagnet());
    QCOMPARE(m.magnet, daemonMagnet(m_magnet));
    QCOMPARE(m.name, QStringLiteral("fixture-magnet"));
    QCOMPARE(m.size, qint64(0));
    QCOMPARE(m.state, QStringLiteral("started"));
    QVERIFY(m.priorities.isEmpty());
    QVERIFY(m.renamed.isEmpty());
    QCOMPARE(m.trackers, QStringList({"http://127.0.0.1:9/announce"}));
    QVERIFY(!contents.torrents.contains(m_magnet));
    // Only the links were asked for.
    QCOMPARE(m_web->callsOf(QStringLiteral("core.get_magnet_uri")).size(), 3);
}

void TestDelugeTorrentsExport::theManifestSaysTheDaemon()
{
    m_config.insert(QStringLiteral("move_completed"), true);
    m_config.insert(QStringLiteral("move_completed_path"), QStringLiteral("/srv/deluge/done"));
    QVERIFY(run().ok);
    const TorrentsManifest manifest = read().manifest;
    QCOMPARE(manifest.engine, EngineKind::Deluge);
    QCOMPARE(manifest.version, QStringLiteral("2.2.0"));
    QCOMPARE(manifest.apiVersion, QString());
    QCOMPARE(manifest.savePath, QStringLiteral("/srv/deluge/downloads"));
    // "Move completed" is no incomplete folder.
    QCOMPARE(manifest.downloadPath, QString());
    QCOMPARE(manifest.serverName, QStringLiteral("box"));
    QCOMPARE(manifest.serverUrl, QStringLiteral("http://box:8112"));
    QCOMPARE(manifest.appVersion, QStringLiteral("9.9.9"));
    QVERIFY(manifest.categories.isEmpty());
    QVERIFY(manifest.tags.isEmpty());
    QCOMPARE(m_web->methods().count(QStringLiteral("daemon.get_version")), 1);
    QCOMPARE(m_web->methods().count(QStringLiteral("core.get_config")), 1);
}

void TestDelugeTorrentsExport::limitsStateAndQueue()
{
    set(m_single, QStringLiteral("max_upload_speed"), 1.5);
    set(m_single, QStringLiteral("prioritize_first_last"), true);
    set(m_private, QStringLiteral("stop_at_ratio"), true);
    set(m_private, QStringLiteral("stop_ratio"), 0.5);
    QVERIFY(run().ok);
    const TorrentsBackup::Contents contents = read();

    // Recorded: paused, 250 KiB/s down, stops at 1.5 and is removed then, in order.
    const TorrentRecord s = record(contents, m_single);
    QCOMPARE(s.state, QStringLiteral("stopped"));
    QCOMPARE(s.dlLimit, qint64(256000));
    QCOMPARE(s.upLimit, qint64(1536));
    QCOMPARE(s.ratioLimit, 1.5);
    QCOMPARE(s.shareLimitAction, QStringLiteral("Remove"));
    QCOMPARE(s.seedingTimeLimit, qint64(-2));
    QCOMPARE(s.inactiveSeedingTimeLimit, qint64(-2));
    QVERIFY(s.sequential);
    QVERIFY(s.firstLastPiecePrio);
    QCOMPARE(s.progress, 0.0);

    // A link keeps its whole record. Queued is not stopped, though the daemon says `paused`.
    const TorrentRecord p = record(contents, m_private);
    QVERIFY(p.isMagnet());
    QCOMPARE(p.state, QStringLiteral("started"));
    QCOMPARE(p.ratioLimit, 0.5);
    QCOMPARE(p.shareLimitAction, QStringLiteral("Stop"));

    // An error stops it (recorded with `paused`); resumed, it runs in error.
    QCOMPARE(record(contents, m_wrong).state, QStringLiteral("stopped"));
    QJsonObject resumed = row(m_wrong);
    resumed.insert(QStringLiteral("paused"), false);
    QCOMPARE(Export::recordFrom(resumed, true, 0, {}).state, QStringLiteral("started"));

    // Deluge's queue (0, 1 and 3 here) counted from 1 among the unfinished; none for the rest.
    QCOMPARE(s.queuePosition, 1);
    QCOMPARE(record(contents, m_magnet).queuePosition, 2);
    QCOMPARE(p.queuePosition, 3);
    QCOMPARE(record(contents, m_pack).queuePosition, 0);
    QCOMPARE(record(contents, m_wrong).queuePosition, 0);
}

void TestDelugeTorrentsExport::theLabelNeedsItsPlugin()
{
    QVERIFY(run().ok);
    QCOMPARE(record(read(), m_pack).category, QStringLiteral("fixtures"));
    QCOMPARE(record(read(), m_single).category, QString());

    // Off: deluge-web may still send the label it last had. Not read.
    m_daemon->labelPlugin = false;
    QVERIFY(run().ok);
    const TorrentRecord p = record(read(), m_pack);
    QCOMPARE(p.category, QString());
    QCOMPARE(p.tags, QStringList());
}

void TestDelugeTorrentsExport::whereTheFilesAreNow()
{
    // "Move completed" on, the torrent complete, not moved (it was set afterwards): where it
    // is, never where it would go.
    QJsonObject json = row(m_pack);
    QVERIFY(json.value(QStringLiteral("move_completed")).toBool());
    QCOMPARE(json.value(QStringLiteral("move_completed_path")).toString(),
             QStringLiteral("/srv/deluge/fixtures-recording/done"));
    TorrentRecord r = Export::recordFrom(json, true, 0, {});
    QCOMPARE(r.savePath, QStringLiteral("/srv/deluge/fixtures-recording/data"));
    QCOMPARE(r.downloadPath, QString());
    // Moved: Deluge says the new folder as `download_location`.
    json.insert(QStringLiteral("download_location"),
                QStringLiteral("/srv/deluge/fixtures-recording/done"));
    json.insert(QStringLiteral("move_completed"), false);
    r = Export::recordFrom(json, true, 0, {});
    QCOMPARE(r.savePath, QStringLiteral("/srv/deluge/fixtures-recording/done"));
    // Its older name, when that is all there is.
    json.remove(QStringLiteral("download_location"));
    json.insert(QStringLiteral("save_path"), QStringLiteral("/old/name"));
    QCOMPARE(Export::recordFrom(json, true, 0, {}).savePath, QStringLiteral("/old/name"));
}

void TestDelugeTorrentsExport::trackersPrioritiesAndUnits()
{
    QJsonObject json = row(m_pack);
    json.insert(QStringLiteral("trackers"),
                QJsonArray{tracker(1, "http://b/announce"),
                           tracker(0, "http://a/announce"),
                           tracker(1, "http://c/announce"),
                           tracker(2, "http://a/announce")});
    // Deluge's 0 · 1–3 · 4 · 5–7 are not wanted · low (2 in a record, C40 X6) · normal · high.
    const qsizetype count = json.value(QStringLiteral("files")).toArray().size();
    QJsonArray own;
    const QList<int> deluge{0, 1, 2, 3, 4, 5, 6, 7};
    const QList<int> records{0, 2, 2, 2, 1, 6, 6, 6};
    QList<int> expected;
    for (qsizetype i = 0; i < count; ++i) {
        own.append(deluge.at(i % deluge.size()));
        expected.append(records.at(i % records.size()));
    }
    json.insert(QStringLiteral("file_priorities"), own);
    json.insert(QStringLiteral("max_upload_speed"), 3);
    json.insert(QStringLiteral("max_download_speed"), 0);

    const QByteArray bytes = torrentBytes("multi_file_v1_unicode.torrent");
    TorrentRecord r = Export::recordFrom(json, true, 7, bytes);
    QCOMPARE(r.trackers,
             QStringList({"http://a/announce", "http://b/announce", "http://c/announce"}));
    QCOMPARE(r.priorities, expected);
    QCOMPARE(r.upLimit, qint64(3072));
    QCOMPARE(r.dlLimit, qint64(0));
    QCOMPARE(r.queuePosition, 7);
    QVERIFY(!r.isMagnet());
    QVERIFY(r.renamed.isEmpty());

    // Every file normal: no list at all.
    QJsonArray normal;
    for (qsizetype i = 0; i < count; ++i)
        normal.append(4);
    json.insert(QStringLiteral("file_priorities"), normal);
    QVERIFY(Export::recordFrom(json, true, 0, bytes).priorities.isEmpty());
}

void TestDelugeTorrentsExport::aLinkKeepsItsRenames()
{
    // No .torrent: a link, its priorities kept, and its renames too, from `orig_files`.
    QJsonObject json = row(m_pack);
    QJsonArray files = json.value(QStringLiteral("files")).toArray();
    const QString was = files.at(0).toObject().value(QStringLiteral("path")).toString();
    QJsonObject first = files.at(0).toObject();
    first.insert(QStringLiteral("path"), QStringLiteral("Somewhere Else/first.bin"));
    files[0] = first;
    json.insert(QStringLiteral("files"), files);

    const TorrentRecord link = Export::recordFrom(json, true, 0, {}, daemonMagnet(m_pack));
    QVERIFY(link.isMagnet());
    QCOMPARE(link.magnet, daemonMagnet(m_pack));
    QCOMPARE(link.id, m_pack);
    QCOMPARE(link.layout, QStringLiteral("Original"));
    QCOMPARE(link.renamed,
             QList<TorrentRecord::Rename>({{0, was, QStringLiteral("Somewhere Else/first.bin")}}));
    QCOMPARE(link.priorities.value(0), 6);
    QCOMPARE(link.priorities.value(2), 0);

    // With the .torrent the renames are told against its own names: the same ones.
    const TorrentRecord whole =
        Export::recordFrom(json, true, 0, torrentBytes("multi_file_v1_unicode.torrent"));
    QVERIFY(!whole.isMagnet());
    QCOMPARE(whole.layout, link.layout);
    QCOMPARE(whole.renamed, link.renamed);

    // A daemon that sends no `orig_files`: a link without renames.
    json.remove(QStringLiteral("orig_files"));
    QVERIFY(Export::recordFrom(json, true, 0, {}).renamed.isEmpty());
}

void TestDelugeTorrentsExport::aHybridsIdIsQbittorrents()
{
    // Deluge's id is the v1 hash; the record's id is the one qBittorrent gives the torrent
    // (TorrentFile::torrentIdFor), with both hashes beside it.
    const QByteArray bytes = torrentBytes("v2_stub.torrent");
    const TorrentFile parsed = TorrentFile::parse(bytes);
    QVERIFY(parsed.isValid());
    QVERIFY(!parsed.infoHashV2.isEmpty());
    QJsonObject json = row(m_single);
    json.insert(QStringLiteral("hash"), parsed.infoHashV1);
    json.remove(QStringLiteral("files"));
    const TorrentRecord r = Export::recordFrom(json, true, 0, bytes);
    QCOMPARE(r.id, parsed.infoHashV2.left(40));
    QCOMPARE(r.infohashV1, parsed.infoHashV1);
    QCOMPARE(r.infohashV2, parsed.infoHashV2);
    QVERIFY(r.ids().contains(parsed.infoHashV1));
}

void TestDelugeTorrentsExport::theFolderGivenIsReadByFileName()
{
    QCOMPARE(Export::localTorrentPath(QStringLiteral("ab"), kForeign, {}),
             kForeign + QStringLiteral("/ab.torrent"));
    QCOMPARE(Export::localTorrentPath(QStringLiteral("ab"), kForeign, QStringLiteral("/mnt/s")),
             QStringLiteral("/mnt/s/ab.torrent"));
    QCOMPARE(Export::localTorrentPath(QStringLiteral("ab"), {}, {}), QString());
    QCOMPARE(Export::localTorrentPath({}, kForeign, {}), QString());

    // The daemon's folder is another machine's: nothing can be read, every torrent its link.
    m_config.insert(QStringLiteral("plugins_location"), QStringLiteral("/srv/deluge/plugins"));
    Export::Outcome outcome = run();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.torrents, 5);
    QCOMPARE(outcome.magnets, 5);
    TorrentsBackup::Contents contents = read();
    QCOMPARE(contents.manifest.magnets, 5);
    QVERIFY(contents.torrents.isEmpty());
    QVERIFY(record(contents, m_pack).isMagnet());

    // With the folder as this machine sees it.
    outcome = run(m_folder);
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.magnets, 3);
    contents = read();
    QVERIFY(!record(contents, m_pack).isMagnet());
    QVERIFY(!record(contents, m_single).isMagnet());
    QVERIFY(contents.torrents.contains(m_pack));

    // Settings that don't tell the folder at all: the same two ways.
    m_config.remove(QStringLiteral("plugins_location"));
    outcome = run();
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.magnets, 5);
    outcome = run(m_folder);
    QVERIFY(outcome.ok);
    QCOMPARE(outcome.magnets, 3);
}

void TestDelugeTorrentsExport::anUnreadableOrForeignTorrentIsALink()
{
    QVERIFY(run().ok);
    const TorrentsBackup::Contents contents = read();
    // Not in the folder.
    const TorrentRecord missing = record(contents, m_private);
    QVERIFY(missing.isMagnet());
    QCOMPARE(missing.magnet, daemonMagnet(m_private));
    QCOMPARE(missing.infohashV1, m_private);
    QCOMPARE(missing.trackers, QStringList({"http://127.0.0.1:9/announce"}));
    QVERIFY(!contents.torrents.contains(m_private));
    // There, but another torrent's.
    const TorrentRecord wrong = record(contents, m_wrong);
    QVERIFY(wrong.isMagnet());
    QVERIFY(!contents.torrents.contains(m_wrong));

    QVERIFY(
        Export::readTorrent(QDir(m_folder).filePath(m_wrong + QStringLiteral(".torrent")), m_wrong)
            .isEmpty());
    QVERIFY(!Export::readTorrent(QDir(m_folder).filePath(m_single + QStringLiteral(".torrent")),
                                 m_single.toUpper())
                 .isEmpty());
    // A v2 torrent goes by its v2 hash cut short, the id a Deluge with libtorrent 2 gives it
    // (and a hybrid): its .torrent is its own under that id too.
    const QByteArray v2 = torrentBytes("v2_stub.torrent");
    const TorrentFile parsed = TorrentFile::parse(v2);
    QVERIFY(parsed.isValid());
    QVERIFY(parsed.torrentId() != parsed.infoHashV1);
    const QString v2Path = QDir(m_folder).filePath(parsed.torrentId() + QStringLiteral(".torrent"));
    QFile stored(v2Path);
    QVERIFY(stored.open(QIODevice::WriteOnly));
    stored.write(v2);
    stored.close();
    QCOMPARE(Export::readTorrent(v2Path, parsed.torrentId()), v2);
    QVERIFY(Export::readTorrent(v2Path, m_single).isEmpty());
}

void TestDelugeTorrentsExport::withoutTheDaemonsLinkOmaseedMakesOne()
{
    // core.get_magnet_uri refused: the id, the name and the trackers the status gave.
    m_noMagnets = true;
    const Export::Outcome outcome = run();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.torrents, 5);
    QCOMPARE(outcome.magnets, 3);
    const TorrentRecord m = record(read(), m_magnet);
    QCOMPARE(m.magnet,
             QStringLiteral("magnet:?xt=urn:btih:%1&dn=fixture-magnet"
                            "&tr=http%3A%2F%2F127.0.0.1%3A9%2Fannounce")
                 .arg(m_magnet));
    QCOMPARE(Export::magnetOf(row(m_magnet)), m.magnet);
    QCOMPARE(Export::magnetOf({}), QString());
}

void TestDelugeTorrentsExport::aTorrentRemovedMeanwhileIsLeftOut()
{
    m_goneAfterList = {m_single};
    const Export::Outcome outcome = run();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.torrents, 4);
    QCOMPARE(outcome.gone, 1);
    const TorrentsBackup::Contents contents = read();
    QCOMPARE(contents.records.size(), 4);
    for (const TorrentRecord &r : contents.records)
        QVERIFY(r.id != m_single);
}

void TestDelugeTorrentsExport::twentyAtATime()
{
    // 45 torrents: three reads of 20, 20 and 5, each by id.
    const QJsonObject model = m_daemon->torrents.value(m_magnet).toObject();
    m_daemon->torrents = QJsonObject();
    for (int i = 0; i < 45; ++i)
        m_daemon->torrents.insert(QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0')), model);
    Export exporter(m_engine.get());
    QSignalSpy progress(&exporter, &Export::progress);
    QSignalSpy finished(&exporter, &Export::finished);
    exporter.start(target(), QStringLiteral("box"), QString(), QStringLiteral("9.9.9"));
    QVERIFY(exporter.isBusy());
    QVERIFY(finished.wait(20000));
    QVERIFY(!exporter.isBusy());
    const Export::Outcome outcome = finished.at(0).at(0).value<Export::Outcome>();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.torrents, 45);
    QCOMPARE(outcome.magnets, 45);

    QList<qsizetype> sizes;
    for (const Call &call : m_web->callsOf(QStringLiteral("core.get_torrents_status"))) {
        const QJsonObject filter = call.params.at(0).toObject();
        if (filter.contains(QStringLiteral("id")))
            sizes.append(filter.value(QStringLiteral("id")).toArray().size());
    }
    QCOMPARE(sizes, QList<qsizetype>({20, 20, 5}));
    QCOMPARE(m_web->methods().count(QStringLiteral("core.get_torrents_status")), 4);
    QCOMPARE(m_web->methods().count(QStringLiteral("core.get_magnet_uri")), 45);
    QList<QList<QVariant>> steps;
    for (const QList<QVariant> &step : progress)
        steps.append(step);
    QCOMPARE(steps, QList<QList<QVariant>>({{0, 45}, {20, 45}, {40, 45}, {45, 45}}));
    QCOMPARE(read().records.size(), 45);
}

void TestDelugeTorrentsExport::stopLeavesNoFile()
{
    Export exporter(m_engine.get());
    QSignalSpy finished(&exporter, &Export::finished);
    // Stopped once the file is open and its manifest written.
    connect(&exporter, &Export::progress, &exporter, [&exporter] { exporter.cancel(); });
    exporter.start(target(), QStringLiteral("box"), QString(), QStringLiteral("9.9.9"));
    QVERIFY(finished.wait(10000));
    const Export::Outcome outcome = finished.at(0).at(0).value<Export::Outcome>();
    QVERIFY(!outcome.ok);
    QVERIFY(outcome.cancelled);
    QCOMPARE(finished.size(), 1);
    QVERIFY(outFiles().isEmpty());
    // The answers still on their way change nothing.
    QTest::qWait(200);
    QCOMPARE(finished.size(), 1);
    QVERIFY(outFiles().isEmpty());

    // No server at all.
    Export none(nullptr);
    QSignalSpy noneFinished(&none, &Export::finished);
    none.start(target(), QString(), QString(), QString());
    QCOMPARE(noneFinished.size(), 1);
    QVERIFY(!noneFinished.at(0).at(0).value<Export::Outcome>().ok);
}

void TestDelugeTorrentsExport::aDaemonsErrorLeavesNoFile()
{
    m_failBatches = true;
    Export::Outcome outcome = run();
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.cancelled);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outFiles().isEmpty());

    // deluge-web without its daemon: the first call says so.
    m_failBatches = false;
    m_daemon->connected = false;
    m_web->answer = [](const QString &, const QJsonArray &) {
        return errorOf("unknown_method.json");
    };
    outcome = run();
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(outFiles().isEmpty());
}

void TestDelugeTorrentsExport::reachSaysWhetherTheFilesAreHere()
{
    using Reach = Export::Reach;
    // Four torrents have their metadata; two of their files aren't theirs to read.
    Result<Reach> reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(reach.isValid());
    QVERIFY(!reach.value().readable);
    QCOMPARE(reach.value().tried, 4);
    QCOMPARE(reach.value().missing, 2);
    QCOMPARE(reach.value().daemonFolder, m_folder);

    // Only the two that are there.
    m_daemon->torrents.remove(m_private);
    m_daemon->torrents.remove(m_wrong);
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(reach.value().readable);
    QCOMPARE(reach.value().tried, 2);
    QCOMPARE(reach.value().missing, 0);

    // The daemon's folder is another machine's: not from here, but under the folder given.
    m_config.insert(QStringLiteral("plugins_location"), QStringLiteral("/srv/deluge/plugins"));
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(!reach.value().readable);
    QCOMPARE(reach.value().missing, 2);
    QCOMPARE(reach.value().daemonFolder, kForeign);
    reach = await(this, Export::reach(m_engine.get(), m_folder));
    QVERIFY(reach.value().readable);
    reach = await(this, Export::reach(m_engine.get(), m_dir.filePath(QStringLiteral("out"))));
    QVERIFY(!reach.value().readable);

    // Settings that don't tell where: an unknown folder, readable only when one is given.
    m_config.remove(QStringLiteral("plugins_location"));
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(reach.isValid());
    QVERIFY(!reach.value().readable);
    QCOMPARE(reach.value().daemonFolder, QString());
    QCOMPARE(reach.value().missing, 2);
    reach = await(this, Export::reach(m_engine.get(), m_folder));
    QVERIFY(reach.value().readable);

    // No torrent to try: the folder itself.
    m_daemon->torrents.remove(m_pack);
    m_daemon->torrents.remove(m_single);
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(!reach.value().readable); // still unknown
    QCOMPARE(reach.value().tried, 0);
    m_config.insert(QStringLiteral("plugins_location"),
                    m_dir.filePath(QStringLiteral("config/plugins")));
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(reach.value().readable);
    QCOMPARE(reach.value().tried, 0);
    QCOMPARE(reach.value().daemonFolder, m_folder);
    reach = await(this, Export::reach(m_engine.get(), QStringLiteral("/nowhere/at/all")));
    QVERIFY(!reach.value().readable);

    // The daemon refuses: an error. No server: one too.
    m_web->answer = [](const QString &, const QJsonArray &) {
        return errorOf("unknown_method.json");
    };
    QVERIFY(await(this, Export::reach(m_engine.get())).isError());
    QVERIFY(await(this, Export::reach(nullptr, {}, this)).isError());
}

void TestDelugeTorrentsExport::theStateFolderIsBesideThePlugins()
{
    const auto folder = [](const QString &plugins) {
        return Export::stateFolder({{QStringLiteral("plugins_location"), plugins}});
    };
    QCOMPARE(folder(QStringLiteral("/home/u/.config/deluge/plugins")),
             QStringLiteral("/home/u/.config/deluge/state"));
    QCOMPARE(folder(QStringLiteral("/config/plugins/")), QStringLiteral("/config/state"));
    QCOMPARE(folder(QStringLiteral("/plugins")), QStringLiteral("/state"));
    QCOMPARE(folder(QStringLiteral("plugins")), QString());
    QCOMPARE(folder(QString()), QString());
    QCOMPARE(Export::stateFolder({}), QString());
}

QTEST_GUILESS_MAIN(TestDelugeTorrentsExport)
#include "tst_delugetorrentsexport.moc"
