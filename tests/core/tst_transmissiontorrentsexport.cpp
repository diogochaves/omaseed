// TransmissionTorrentsExport (#88, docs/DECISIONS.md C40 X1, X3): a Transmission's torrents
// written to a TorrentsBackup file, against the tests' fake daemon (FakeTransmission). The rows
// are the daemon's recorded ones (tests/fixtures/transmission/torrent_get_all.json) given the
// hashes, names and files of real .torrent fixtures, whose bytes sit in a folder of the test's
// as the daemon's `torrents` folder would: record fields row by row, the .torrent read where
// the daemon says or under the folder given, a link when it can't be read or isn't the
// torrent's, a magnet without metadata, a torrent gone mid-export, the batches, stop, and
// reach().

#include "FakeTransmission.hpp"

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"
#include "core/engines/transmission/TransmissionTorrentsExport.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace fakehttp;
using namespace faketransmission;
using namespace omaseed::core;

namespace {

using Export = TransmissionTorrentsExport;

const QString kRecordedPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kRecordedSingle = QStringLiteral("2b86112ae20c6ca831cbfb3e4333c8acc6d21088");
const QString kRecordedError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kRecordedMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
const QString kForeign = QStringLiteral("/srv/transmission/torrents");

QByteArray torrentBytes(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

QString magnetOf(const QString &hash)
{
    return QStringLiteral("magnet:?xt=urn:btih:%1&dn=x&tr=http%3A%2F%2F127.0.0.1%3A9%2Fannounce")
        .arg(hash);
}

} // namespace

class TestTransmissionTorrentsExport : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void recordsRowByRow();
    void theManifestSaysTheSession();
    void limitsAndModes();
    void trackersPrioritiesAndUnits();
    void aHybridsIdIsQbittorrents();
    void theFolderGivenIsReadByFileName();
    void anUnreadableOrForeignTorrentIsALink();
    void aTorrentRemovedMeanwhileIsLeftOut();
    void twentyAtATime();
    void stopLeavesNoFile();
    void aDaemonsErrorLeavesNoFile();
    void reachSaysWhetherTheFilesAreHere();

private:
    /// A recorded row as the torrent of `fixtureName`: its hash, name and files, its .torrent
    /// in the test's folder (unless `stored` is false), `torrent_file` under `m_daemonFolder`.
    QJsonObject rowOf(const QString &recorded, const char *fixtureName, bool stored = true);
    QJsonObject &row(const QString &hash);
    Export::Outcome run(const QString &torrentsFolder = {});
    TorrentsBackup::Contents read();
    TorrentRecord record(const TorrentsBackup::Contents &contents, const QString &id);
    QString target() const { return m_dir.filePath(QStringLiteral("out/t.omaseed-torrents")); }

    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeTransmission> m_daemon;
    std::unique_ptr<TransmissionEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_folder;       ///< this machine's folder with the .torrent files
    QString m_daemonFolder; ///< what the daemon calls it
    QList<QJsonObject> m_rows;
    QJsonObject m_session;
    QStringList m_goneAfterList; ///< hashes the daemon no longer has once listed
    bool m_failBatches = false;
    QString m_pack, m_single, m_private, m_wrong, m_magnet;
};

void TestTransmissionTorrentsExport::init()
{
    QVERIFY(m_dir.isValid());
    m_folder = m_dir.filePath(QStringLiteral("config/torrents"));
    QDir(m_folder).removeRecursively();
    QDir(m_dir.filePath(QStringLiteral("out"))).removeRecursively();
    QVERIFY(QDir().mkpath(m_folder));
    m_daemonFolder = m_folder;
    m_goneAfterList.clear();
    m_failBatches = false;
    m_session = fixture("session_get.json").value(QStringLiteral("result")).toObject();
    m_session.insert(QStringLiteral("config_dir"), m_dir.filePath(QStringLiteral("config")));

    // Five torrents: a pack (seeding, two labels, a renamed file, file choices), a single
    // file (stopped, unfinished, limits of its own), a private one whose .torrent is not in
    // the folder, one whose .torrent there is another torrent's, a magnet without metadata.
    m_rows.clear();
    QJsonObject pack = rowOf(kRecordedPack, "multi_file_v1_unicode.torrent");
    m_pack = pack.value(QStringLiteral("hash_string")).toString();
    m_rows.append(pack);

    QJsonObject single = rowOf(kRecordedSingle, "single_file_v1.torrent");
    m_single = single.value(QStringLiteral("hash_string")).toString();
    single.insert(QStringLiteral("status"), 0);
    single.insert(QStringLiteral("left_until_done"), 1000);
    single.insert(QStringLiteral("percent_done"), 0.98);
    single.insert(QStringLiteral("queue_position"), 9);
    m_rows.append(single);

    QJsonObject priv = rowOf(kRecordedSingle, "private_announce_list.torrent", false);
    m_private = priv.value(QStringLiteral("hash_string")).toString();
    priv.insert(QStringLiteral("left_until_done"), 0);
    m_rows.append(priv);

    QJsonObject wrong = rowOf(kRecordedError, "open_movies_v1.torrent", false);
    m_wrong = wrong.value(QStringLiteral("hash_string")).toString();
    {
        QFile file(QDir(m_folder).filePath(m_wrong + QStringLiteral(".torrent")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(torrentBytes("single_file_v1.torrent"));
    }
    m_rows.append(wrong);

    PollAnswers recorded;
    QJsonObject magnet = recorded.byHash(kRecordedMagnet);
    m_magnet = kRecordedMagnet;
    magnet.insert(QStringLiteral("queue_position"), 4);
    magnet.insert(QStringLiteral("torrent_file"),
                  m_daemonFolder + QLatin1Char('/') + m_magnet + QStringLiteral(".torrent"));
    m_rows.append(magnet);

    m_server = std::make_unique<FakeServer>();
    m_daemon = std::make_unique<FakeTransmission>();
    m_daemon->answer = [this](const QString &method, const QJsonObject &params) -> QJsonObject {
        if (method == QLatin1String("session_get"))
            return m_session;
        if (method != QLatin1String("torrent_get"))
            return {};
        const bool some = params.contains(QStringLiteral("ids"));
        if (some && m_failBatches)
            return {{QStringLiteral("__error"),
                     fixture("error_not_absolute.json").value(QStringLiteral("error"))}};
        const QJsonArray ids = params.value(QStringLiteral("ids")).toArray();
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        QJsonArray table{fields};
        for (const QJsonObject &t : std::as_const(m_rows)) {
            const QString hash = t.value(QStringLiteral("hash_string")).toString();
            if (some && (!ids.contains(hash) || m_goneAfterList.contains(hash)))
                continue;
            QJsonArray line;
            for (const QJsonValue &field : fields)
                line.append(t.value(field.toString()));
            table.append(line);
        }
        return {{QStringLiteral("torrents"), table}};
    };
    m_server->setHandler(m_daemon->handler());
    m_engine = std::make_unique<TransmissionEngine>();
    m_engine->setBaseUrl(m_server->url());
    m_engine->setTimeout(std::chrono::seconds(5));
    m_engine->rpc()->setRetryBackoff(std::chrono::milliseconds(10));
}

void TestTransmissionTorrentsExport::cleanup()
{
    m_engine.reset();
    m_daemon.reset();
    m_server.reset();
}

QJsonObject
TestTransmissionTorrentsExport::rowOf(const QString &recorded, const char *fixtureName, bool stored)
{
    PollAnswers answers;
    QJsonObject json = answers.byHash(recorded);
    const QByteArray bytes = torrentBytes(fixtureName);
    const TorrentFile parsed = TorrentFile::parse(bytes);
    if (!parsed.isValid())
        qFatal("fixture %s does not parse", fixtureName);
    const QString hash = parsed.infoHashV1;
    json.insert(QStringLiteral("hash_string"), hash);
    json.insert(QStringLiteral("name"), parsed.name);
    QJsonArray files;
    QJsonArray stats;
    for (const QString &name : TorrentsBackup::namesUnder(parsed, QStringLiteral("Original"))) {
        files.append(QJsonObject{{QStringLiteral("name"), name}});
        stats.append(
            QJsonObject{{QStringLiteral("wanted"), true}, {QStringLiteral("priority"), 0}});
    }
    json.insert(QStringLiteral("files"), files);
    json.insert(QStringLiteral("file_stats"), stats);
    json.insert(QStringLiteral("magnet_link"), magnetOf(hash));
    json.insert(QStringLiteral("torrent_file"),
                m_daemonFolder + QLatin1Char('/') + hash + QStringLiteral(".torrent"));
    if (stored) {
        QFile file(QDir(m_folder).filePath(hash + QStringLiteral(".torrent")));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
            qFatal("cannot store %s", fixtureName);
    }
    return json;
}

QJsonObject &TestTransmissionTorrentsExport::row(const QString &hash)
{
    for (QJsonObject &t : m_rows) {
        if (t.value(QStringLiteral("hash_string")).toString() == hash)
            return t;
    }
    qFatal("no row %s", qPrintable(hash));
}

Export::Outcome TestTransmissionTorrentsExport::run(const QString &torrentsFolder)
{
    Export exporter(m_engine.get());
    exporter.setTorrentsFolder(torrentsFolder);
    QSignalSpy finished(&exporter, &Export::finished);
    exporter.start(target(),
                   QStringLiteral("box"),
                   QStringLiteral("http://box:9091"),
                   QStringLiteral("9.9.9"));
    if (finished.isEmpty() && !finished.wait(10000))
        return {};
    return finished.at(0).at(0).value<Export::Outcome>();
}

TorrentsBackup::Contents TestTransmissionTorrentsExport::read()
{
    QString error;
    const auto contents = TorrentsBackup::read(target(), &error);
    if (!contents)
        qFatal("cannot read the file back: %s", qPrintable(error));
    return *contents;
}

TorrentRecord TestTransmissionTorrentsExport::record(const TorrentsBackup::Contents &contents,
                                                     const QString &id)
{
    for (const TorrentRecord &r : contents.records) {
        if (r.id == id)
            return r;
    }
    qFatal("no record %s", qPrintable(id));
}

void TestTransmissionTorrentsExport::recordsRowByRow()
{
    // The pack: its second file renamed on the daemon, the first high, the last not wanted.
    QJsonObject &pack = row(m_pack);
    QJsonArray files = pack.value(QStringLiteral("files")).toArray();
    QJsonArray stats = pack.value(QStringLiteral("file_stats")).toArray();
    QVERIFY(files.size() >= 3);
    const QString was = files.at(1).toObject().value(QStringLiteral("name")).toString();
    const QString now = was.section(QLatin1Char('/'), 0, -2) + QStringLiteral("/Renamed here.bin");
    files[1] = QJsonObject{{QStringLiteral("name"), now}};
    stats[0] = QJsonObject{{QStringLiteral("wanted"), true}, {QStringLiteral("priority"), 1}};
    stats[files.size() - 1] =
        QJsonObject{{QStringLiteral("wanted"), false}, {QStringLiteral("priority"), 0}};
    pack.insert(QStringLiteral("files"), files);
    pack.insert(QStringLiteral("file_stats"), stats);

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
    QCOMPARE(QDir(m_dir.filePath(QStringLiteral("out"))).entryList(QDir::Files).size(), 1);

    const TorrentsBackup::Contents contents = read();
    QCOMPARE(contents.manifest.engine, EngineKind::Transmission);
    QCOMPARE(contents.manifest.count, 5);
    QCOMPARE(contents.records.size(), 5);
    // In the daemon's order.
    QCOMPARE(contents.records.at(0).id, m_pack);
    QCOMPARE(contents.records.at(4).id, m_magnet);

    const TorrentRecord p = record(contents, m_pack);
    QCOMPARE(p.infohashV1, m_pack);
    QCOMPARE(p.infohashV2, QString());
    QCOMPARE(p.name, pack.value(QStringLiteral("name")).toString());
    QCOMPARE(p.size, qint64(64440));
    QCOMPARE(p.progress, 1.0);
    QCOMPARE(p.savePath, QStringLiteral("/srv/transmission/fixtures-recording/data"));
    QCOMPARE(p.downloadPath, QString());
    QCOMPARE(p.category, QString());
    QCOMPARE(p.tags, QStringList({"fixtures", "pack"}));
    QVERIFY(!p.autoTmm);
    QCOMPARE(p.state, QStringLiteral("started"));
    QCOMPARE(p.queuePosition, 0); // complete: none, as qBittorrent's
    QCOMPARE(p.upLimit, qint64(0));
    QCOMPARE(p.dlLimit, qint64(0)); // 100 kB/s on the row, but not `download_limited`
    QCOMPARE(p.ratioLimit, -2.0);
    QCOMPARE(p.seedingTimeLimit, qint64(-2));
    QCOMPARE(p.inactiveSeedingTimeLimit, qint64(-2));
    QCOMPARE(p.shareLimitAction, QStringLiteral("Default"));
    QVERIFY(!p.sequential);
    QVERIFY(!p.firstLastPiecePrio);
    QCOMPARE(p.layout, QStringLiteral("Original"));
    QCOMPARE(p.renamed, QList<TorrentRecord::Rename>({{1, was, now}}));
    QList<int> priorities(files.size(), 1);
    priorities[0] = 6;
    priorities[files.size() - 1] = 0;
    QCOMPARE(p.priorities, priorities);
    QCOMPARE(p.trackers,
             QStringList({"http://127.0.0.1:9/announce", "udp://127.0.0.1:9/announce"}));
    QVERIFY(!p.isMagnet());
    // Its .torrent, byte for byte.
    QFile file(target());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(TorrentsBackup::torrentBytes(&file, contents, m_pack),
             torrentBytes("multi_file_v1_unicode.torrent"));

    const TorrentRecord s = record(contents, m_single);
    QCOMPARE(s.state, QStringLiteral("stopped"));
    QCOMPARE(s.progress, 0.98);
    QCOMPARE(s.tags, QStringList({"fixtures"}));
    QCOMPARE(s.renamed, QList<TorrentRecord::Rename>());
    QCOMPARE(s.priorities, QList<int>());
    QVERIFY(!s.isMagnet());
    QCOMPARE(TorrentsBackup::torrentBytes(&file, contents, m_single),
             torrentBytes("single_file_v1.torrent"));
    // The queue is the unfinished ones', from 1: the magnet (4) before the single file (9).
    QCOMPARE(record(contents, m_magnet).queuePosition, 1);
    QCOMPARE(s.queuePosition, 2);

    // A magnet without metadata: its link.
    const TorrentRecord m = record(contents, m_magnet);
    QVERIFY(m.isMagnet());
    QVERIFY(m.magnet.startsWith(QStringLiteral("magnet:?xt=urn:btih:") + m_magnet));
    QCOMPARE(m.name, QStringLiteral("fixture-magnet"));
    QCOMPARE(m.trackers, QStringList({"http://127.0.0.1:9/announce"}));
    QVERIFY(!contents.torrents.contains(m_magnet));
}

void TestTransmissionTorrentsExport::theManifestSaysTheSession()
{
    QVERIFY(run().ok);
    TorrentsManifest manifest = read().manifest;
    QCOMPARE(manifest.engine, EngineKind::Transmission);
    QCOMPARE(manifest.version, QStringLiteral("4.1.3"));
    QCOMPARE(manifest.apiVersion, QString());
    QCOMPARE(manifest.appVersion, QStringLiteral("9.9.9"));
    QCOMPARE(manifest.serverName, QStringLiteral("box"));
    QCOMPARE(manifest.serverUrl, QStringLiteral("http://box:9091"));
    QCOMPARE(manifest.savePath, QStringLiteral("/srv/transmission/downloads"));
    QCOMPARE(manifest.downloadPath, QString()); // the folder is named, but not in use
    QCOMPARE(manifest.tags, QStringList({"fixtures", "pack"}));
    QVERIFY(manifest.categories.isEmpty());
    // Known before the torrents are read: no metadata, or no file to open. The one whose
    // file is another torrent's is found when it is read (the outcome counts it).
    QCOMPARE(manifest.magnets, 2);

    m_session.insert(QStringLiteral("incomplete_dir_enabled"), true);
    QVERIFY(run().ok);
    manifest = read().manifest;
    QCOMPARE(manifest.downloadPath, QStringLiteral("/home/user/Downloads"));
}

void TestTransmissionTorrentsExport::limitsAndModes()
{
    QJsonObject &single = row(m_single);
    single.insert(QStringLiteral("download_limit"), 250);
    single.insert(QStringLiteral("download_limited"), true);
    single.insert(QStringLiteral("upload_limit"), 40);
    single.insert(QStringLiteral("upload_limited"), true);
    single.insert(QStringLiteral("seed_ratio_mode"), 1);
    single.insert(QStringLiteral("seed_ratio_limit"), 1.5);
    single.insert(QStringLiteral("seed_idle_mode"), 2);
    single.insert(QStringLiteral("sequential_download"), true);
    QJsonObject &priv = row(m_private);
    priv.insert(QStringLiteral("seed_ratio_mode"), 2);
    priv.insert(QStringLiteral("seed_idle_mode"), 1);
    priv.insert(QStringLiteral("seed_idle_limit"), 44);

    QVERIFY(run().ok);
    const TorrentsBackup::Contents contents = read();
    const TorrentRecord s = record(contents, m_single);
    QCOMPARE(s.dlLimit, qint64(250000));
    QCOMPARE(s.upLimit, qint64(40000));
    QCOMPARE(s.ratioLimit, 1.5);
    QCOMPARE(s.inactiveSeedingTimeLimit, qint64(-1));
    QCOMPARE(s.seedingTimeLimit, qint64(-2));
    QVERIFY(s.sequential);
    // A link keeps its whole record.
    const TorrentRecord p = record(contents, m_private);
    QVERIFY(p.isMagnet());
    QCOMPARE(p.ratioLimit, -1.0);
    QCOMPARE(p.inactiveSeedingTimeLimit, qint64(44));
}

void TestTransmissionTorrentsExport::trackersPrioritiesAndUnits()
{
    QJsonObject json = row(m_pack);
    const auto tracker = [](int tier, const char *url) {
        return QJsonObject{{QStringLiteral("tier"), tier},
                           {QStringLiteral("announce"), QLatin1String(url)}};
    };
    json.insert(QStringLiteral("trackers"),
                QJsonArray{tracker(1, "http://b/announce"),
                           tracker(0, "http://a/announce"),
                           tracker(1, "http://c/announce"),
                           tracker(2, "http://a/announce")});
    QJsonArray stats = json.value(QStringLiteral("file_stats")).toArray();
    // Low is 2 in a record (C40 X6): a Transmission reads it back as low, a qBittorrent,
    // which has no low, as normal.
    stats[0] = QJsonObject{{QStringLiteral("wanted"), true}, {QStringLiteral("priority"), -1}};
    json.insert(QStringLiteral("file_stats"), stats);
    json.insert(QStringLiteral("upload_limited"), true);
    json.insert(QStringLiteral("upload_limit"), 3);

    const QByteArray bytes = torrentBytes("multi_file_v1_unicode.torrent");
    TorrentRecord r = Export::recordFrom(json, 1024, 7, bytes);
    QCOMPARE(r.trackers,
             QStringList({"http://a/announce", "http://b/announce", "http://c/announce"}));
    QList<int> low(stats.size(), 1);
    low[0] = 2;
    QCOMPARE(r.priorities, low);
    QCOMPARE(r.upLimit, qint64(3072));
    QCOMPARE(r.queuePosition, 7);
    QVERIFY(!r.isMagnet());

    // No .torrent: a link, the priorities kept, no renames.
    stats[1] = QJsonObject{{QStringLiteral("wanted"), false}, {QStringLiteral("priority"), 1}};
    json.insert(QStringLiteral("file_stats"), stats);
    r = Export::recordFrom(json, 1000, 0, {});
    QVERIFY(r.isMagnet());
    QCOMPARE(r.magnet, magnetOf(m_pack));
    QCOMPARE(r.priorities.value(1), 0);
    QVERIFY(r.renamed.isEmpty());
    QCOMPARE(r.id, m_pack);
}

void TestTransmissionTorrentsExport::aHybridsIdIsQbittorrents()
{
    // Transmission keys every torrent by its v1 hash; the record's id is the one qBittorrent
    // gives it (TorrentFile::torrentIdFor), with both hashes beside it.
    const QByteArray bytes = torrentBytes("v2_stub.torrent");
    const TorrentFile parsed = TorrentFile::parse(bytes);
    QVERIFY(parsed.isValid());
    QVERIFY(!parsed.infoHashV2.isEmpty());
    QJsonObject json = row(m_single);
    json.insert(QStringLiteral("hash_string"), parsed.infoHashV1);
    json.remove(QStringLiteral("files"));
    const TorrentRecord r = Export::recordFrom(json, 1000, 0, bytes);
    QCOMPARE(r.id, parsed.infoHashV2.left(40));
    QCOMPARE(r.infohashV1, parsed.infoHashV1);
    QCOMPARE(r.infohashV2, parsed.infoHashV2);
    QVERIFY(r.ids().contains(parsed.infoHashV1));
}

void TestTransmissionTorrentsExport::theFolderGivenIsReadByFileName()
{
    // The daemon's paths are another machine's.
    for (QJsonObject &t : m_rows) {
        t.insert(QStringLiteral("torrent_file"),
                 kForeign + QLatin1Char('/') + t.value(QStringLiteral("hash_string")).toString() +
                     QStringLiteral(".torrent"));
    }
    QCOMPARE(Export::localTorrentPath(kForeign + QStringLiteral("/ab.torrent"), {}),
             kForeign + QStringLiteral("/ab.torrent"));
    QCOMPARE(Export::localTorrentPath(kForeign + QStringLiteral("/ab.torrent"),
                                      QStringLiteral("/mnt/t")),
             QStringLiteral("/mnt/t/ab.torrent"));
    QCOMPARE(Export::localTorrentPath({}, QStringLiteral("/mnt/t")), QString());

    // As given: nothing can be read, every torrent is its link.
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
}

void TestTransmissionTorrentsExport::anUnreadableOrForeignTorrentIsALink()
{
    QVERIFY(run().ok);
    const TorrentsBackup::Contents contents = read();
    // Not in the folder.
    const TorrentRecord missing = record(contents, m_private);
    QVERIFY(missing.isMagnet());
    QCOMPARE(missing.magnet, magnetOf(m_private));
    QCOMPARE(missing.infohashV1, m_private);
    QVERIFY(missing.renamed.isEmpty());
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
}

void TestTransmissionTorrentsExport::aTorrentRemovedMeanwhileIsLeftOut()
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

void TestTransmissionTorrentsExport::twentyAtATime()
{
    // 45 torrents: three reads of 20, 20 and 5, each by hash.
    const QJsonObject model = row(m_magnet);
    m_rows.clear();
    for (int i = 0; i < 45; ++i) {
        const QString hash = QStringLiteral("%1").arg(i, 40, 16, QLatin1Char('0'));
        QJsonObject t = model;
        t.insert(QStringLiteral("hash_string"), hash);
        t.insert(QStringLiteral("magnet_link"), magnetOf(hash));
        m_rows.append(t);
    }
    Export exporter(m_engine.get());
    QSignalSpy progress(&exporter, &Export::progress);
    QSignalSpy finished(&exporter, &Export::finished);
    exporter.start(target(), QStringLiteral("box"), QString(), QStringLiteral("9.9.9"));
    QVERIFY(exporter.isBusy());
    QVERIFY(finished.wait(10000));
    QVERIFY(!exporter.isBusy());
    const Export::Outcome outcome = finished.at(0).at(0).value<Export::Outcome>();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.torrents, 45);
    QCOMPARE(outcome.magnets, 45);

    QList<qsizetype> sizes;
    for (const Call &call : std::as_const(m_daemon->calls)) {
        if (call.method == QLatin1String("torrent_get") &&
            call.params.contains(QStringLiteral("ids")))
            sizes.append(call.params.value(QStringLiteral("ids")).toArray().size());
    }
    QCOMPARE(sizes, QList<qsizetype>({20, 20, 5}));
    QCOMPARE(m_daemon->methods().count(QStringLiteral("session_get")), 1);
    QList<QList<QVariant>> steps;
    for (const QList<QVariant> &step : progress)
        steps.append(step);
    QCOMPARE(steps, QList<QList<QVariant>>({{0, 45}, {20, 45}, {40, 45}, {45, 45}}));
    QCOMPARE(read().records.size(), 45);
}

void TestTransmissionTorrentsExport::stopLeavesNoFile()
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
    QVERIFY(QDir(m_dir.filePath(QStringLiteral("out"))).entryList(QDir::Files).isEmpty());
    // The answers still on their way change nothing.
    QTest::qWait(200);
    QCOMPARE(finished.size(), 1);
    QVERIFY(QDir(m_dir.filePath(QStringLiteral("out"))).entryList(QDir::Files).isEmpty());

    // No server at all.
    Export none(nullptr);
    QSignalSpy noneFinished(&none, &Export::finished);
    none.start(target(), QString(), QString(), QString());
    QCOMPARE(noneFinished.size(), 1);
    QVERIFY(!noneFinished.at(0).at(0).value<Export::Outcome>().ok);
}

void TestTransmissionTorrentsExport::aDaemonsErrorLeavesNoFile()
{
    m_failBatches = true;
    const Export::Outcome outcome = run();
    QVERIFY(!outcome.ok);
    QVERIFY(!outcome.cancelled);
    QVERIFY(!outcome.error.isEmpty());
    QVERIFY(QDir(m_dir.filePath(QStringLiteral("out"))).entryList(QDir::Files).isEmpty());
}

void TestTransmissionTorrentsExport::reachSaysWhetherTheFilesAreHere()
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
    m_rows = {row(m_pack), row(m_single), row(m_magnet)};
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(reach.value().readable);
    QCOMPARE(reach.value().tried, 2);
    QCOMPARE(reach.value().missing, 0);

    // The daemon's paths are another machine's: not from here, but under the folder given.
    for (QJsonObject &t : m_rows) {
        t.insert(QStringLiteral("torrent_file"),
                 kForeign + QLatin1Char('/') + t.value(QStringLiteral("hash_string")).toString() +
                     QStringLiteral(".torrent"));
    }
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(!reach.value().readable);
    QCOMPARE(reach.value().missing, 2);
    QCOMPARE(reach.value().daemonFolder, kForeign);
    reach = await(this, Export::reach(m_engine.get(), m_folder));
    QVERIFY(reach.value().readable);
    reach = await(this, Export::reach(m_engine.get(), m_dir.filePath(QStringLiteral("out"))));
    QVERIFY(!reach.value().readable);

    // No torrent to ask about: the session's config folder.
    m_rows = {row(m_magnet)};
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(reach.value().readable);
    QCOMPARE(reach.value().tried, 0);
    QCOMPARE(reach.value().daemonFolder, m_folder);
    m_session.insert(QStringLiteral("config_dir"), QStringLiteral("/srv/transmission"));
    reach = await(this, Export::reach(m_engine.get()));
    QVERIFY(!reach.value().readable);
    QCOMPARE(reach.value().daemonFolder, kForeign);
    reach = await(this, Export::reach(m_engine.get(), m_folder));
    QVERIFY(reach.value().readable);

    // No server.
    QVERIFY(await(this, Export::reach(nullptr, {}, this)).isError());
}

QTEST_GUILESS_MAIN(TestTransmissionTorrentsExport)
#include "tst_transmissiontorrentsexport.moc"
