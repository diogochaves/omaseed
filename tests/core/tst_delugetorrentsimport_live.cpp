// DelugeTorrentsImport against the real deluge-web test server (#88, docs/DECISIONS.md C40 X6):
// deluged 2.2 with deluge-web on :8086 (scripts/dev/test-server.sh start --deluge), the Label
// plugin on. Makes one private torrent of its own and writes its data in folders of the
// test's, under the names a record says rather than the .torrent's, then imports a file with
// that record: once with its top folder and a file renamed, a skipped file, a label and
// started (back at 100 % of what is wanted, seeding, labelled), once without its top folder
// and stopped (100 %, paused: a recheck leaves a torrent paused). The server's other torrents
// are only read (they are "already there"); the test's own and its label are removed, with
// more than a second between a removal and the next add of the same torrent. QSKIPs when
// nothing listens on :8086 or DELUGE_TEST_PASS isn't in the environment
// (set -a; . ~/.config/omaseed/dev.env; set +a).

#include "LiveServer.hpp"

#include "core/Bencode.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTorrentsImport.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

#include "FakeHttp.hpp"

using namespace omaseed::core;
using fakehttp::await;

namespace {

using Import = DelugeTorrentsImport;

const QString kTop = QStringLiteral("omaseed-deluge-import-live");
const QString kLabel = QStringLiteral("omaseed-import-live");

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

QString password()
{
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("DELUGE_TEST_PASS"));
}

QByteArray contentOf(int file, qint64 size)
{
    QByteArray out;
    QByteArray block = QByteArray::number(file) + "-omaseed-deluge-import-live";
    while (out.size() < size) {
        block = QCryptographicHash::hash(block, QCryptographicHash::Sha256);
        out += block;
    }
    return out.left(size);
}

const QList<qint64> kSizes{1 << 20, 300000, 5000};

} // namespace

class TestDelugeTorrentsImportLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void renamedWithASkippedFileComesBackSeedingAndLabelled();
    void withoutItsTopFolderComesBackPaused();

private:
    QJsonValue call(const QString &method, const QJsonArray &params = {});
    /// The test's torrent as the daemon has it; empty when it is not there.
    QJsonObject status(const QStringList &keys);
    bool waitFor(const std::function<bool()> &done, int timeoutMs = 30000);
    /// The torrent's data under `names`, in a new folder; gives the folder.
    QString dataUnder(const QString &folder, const QStringList &names);
    TorrentRecord record(const QString &savePath) const;
    TorrentsBackup::Contents write(const TorrentRecord &record, EngineKind engine);
    Import::Outcome import(const TorrentsBackup::Contents &contents, Import::Plan *plan = nullptr);
    /// Removes the test's torrent, keeping its data, and lets the daemon settle.
    void removeOwn();

    std::unique_ptr<DelugeEngine> m_engine;
    QTemporaryDir m_dir;
    QByteArray m_torrent;
    QString m_hash;
    QStringList m_names; ///< its files, as the .torrent names them
};

QJsonValue TestDelugeTorrentsImportLive::call(const QString &method, const QJsonArray &params)
{
    const Result<QJsonValue> result = await(this, m_engine->rpc()->call(method, params), 30000);
    [&]() { QVERIFY2(result.isValid(), qPrintable(method + u": " + result.error().toString())); }();
    return result.value();
}

QJsonObject TestDelugeTorrentsImportLive::status(const QStringList &keys)
{
    return call(QStringLiteral("core.get_torrents_status"),
                {QJsonObject{{QStringLiteral("id"), QJsonArray{m_hash}}},
                 QJsonArray::fromStringList(keys)})
        .toObject()
        .value(m_hash)
        .toObject();
}

bool TestDelugeTorrentsImportLive::waitFor(const std::function<bool()> &done, int timeoutMs)
{
    QElapsedTimer clock;
    clock.start();
    while (!done()) {
        if (clock.elapsed() > timeoutMs)
            return false;
        QTest::qWait(100);
    }
    return true;
}

void TestDelugeTorrentsImportLive::removeOwn()
{
    await(this, m_engine->remove({m_hash}, false), 20000);
    [&]() { QVERIFY(waitFor([this] { return status({QStringLiteral("name")}).isEmpty(); })); }();
    // A torrent removed and added again within half a second came back broken once
    // (scripts/dev/research/88-engines-backup): the daemon gets its time.
    QTest::qWait(1500);
}

void TestDelugeTorrentsImportLive::initTestCase()
{
    if (!listening(8086))
        QSKIP("no deluge-web on :8086: scripts/dev/test-server.sh start --deluge");
    if (password().isEmpty())
        QSKIP("no DELUGE_TEST_PASS: set -a; . ~/.config/omaseed/dev.env; set +a");
    m_engine = std::make_unique<DelugeEngine>();
    m_engine->setBaseUrl(
        LiveServer::fromEnv("OMASEED_DELUGE_URL", QStringLiteral("http://127.0.0.1:8086")));
    m_engine->setCredentials(QString(), password());
    m_engine->setTimeout(std::chrono::seconds(15));
    QVERIFY(m_dir.isValid());
    QVERIFY(await(this, m_engine->probe(), 20000).isValid());
    QVERIFY(m_engine->labelPlugin()); // the test server turns it on

    // A top folder with a file in it and two in a folder below, 1.3 MiB in 16 KiB pieces.
    const QList<QStringList> files{{QStringLiteral("a.bin")},
                                   {QStringLiteral("sub"), QStringLiteral("b.bin")},
                                   {QStringLiteral("sub"), QStringLiteral("c.txt")}};
    QByteArray stream;
    bencode::List list;
    for (int i = 0; i < files.size(); ++i) {
        stream += contentOf(i, kSizes.at(i));
        bencode::List parts;
        for (const QString &part : files.at(i))
            parts.append(bencode::Value(part.toUtf8()));
        list.append(bencode::Value(bencode::Dict{{"length", bencode::Value(kSizes.at(i))},
                                                 {"path", bencode::Value(parts)}}));
        m_names.append(kTop + QLatin1Char('/') + files.at(i).join(QLatin1Char('/')));
    }
    constexpr qint64 piece = 16384;
    QByteArray pieces;
    for (qint64 at = 0; at < stream.size(); at += piece)
        pieces += QCryptographicHash::hash(stream.mid(at, piece), QCryptographicHash::Sha1);
    // Private, so it never leaves the playground.
    const bencode::Dict info{{"files", bencode::Value(list)},
                             {"name", bencode::Value(kTop.toUtf8())},
                             {"piece length", bencode::Value(piece)},
                             {"pieces", bencode::Value(pieces)},
                             {"private", bencode::Value(qint64(1))}};
    m_torrent = bencode::encode(bencode::Value(
        bencode::Dict{{"announce", bencode::Value(QByteArray("http://127.0.0.1:9/announce"))},
                      {"info", bencode::Value(info)}}));
    const TorrentFile parsed = TorrentFile::parse(m_torrent);
    QVERIFY(parsed.isValid());
    m_hash = parsed.infoHashV1;
    QCOMPARE(TorrentsBackup::namesUnder(parsed, QStringLiteral("Original")), m_names);
    // A leftover of an earlier run goes first (its data was that run's).
    removeOwn();
    await(this, m_engine->removeCategories({kLabel}), 20000);
}

void TestDelugeTorrentsImportLive::cleanupTestCase()
{
    if (!m_engine)
        return;
    await(this, m_engine->remove({m_hash}, false), 20000);
    waitFor([this] { return status({QStringLiteral("name")}).isEmpty(); });
    await(this, m_engine->removeCategories({kLabel}), 20000);
}

QString TestDelugeTorrentsImportLive::dataUnder(const QString &folder, const QStringList &names)
{
    const QString data = m_dir.filePath(folder);
    for (int i = 0; i < names.size(); ++i) {
        const QString path = data + QLatin1Char('/') + names.at(i);
        const QByteArray content = contentOf(i, kSizes.at(i));
        [&]() {
            QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(content), content.size());
        }();
    }
    return data;
}

TorrentRecord TestDelugeTorrentsImportLive::record(const QString &savePath) const
{
    const TorrentFile parsed = TorrentFile::parse(m_torrent);
    TorrentRecord r;
    r.id = parsed.torrentId();
    r.infohashV1 = parsed.infoHashV1;
    r.name = parsed.name;
    r.size = parsed.totalSize;
    r.progress = 1;
    r.savePath = savePath;
    r.state = QStringLiteral("started");
    return r;
}

TorrentsBackup::Contents TestDelugeTorrentsImportLive::write(const TorrentRecord &record,
                                                             EngineKind engine)
{
    TorrentsManifest manifest;
    manifest.created = QDateTime::currentDateTimeUtc();
    manifest.appVersion = QStringLiteral("test");
    manifest.serverName = QStringLiteral("source");
    manifest.serverUrl = QStringLiteral("http://source:1");
    manifest.engine = engine;
    manifest.version = QStringLiteral("test");
    manifest.savePath = record.savePath;
    manifest.count = 1;
    const QString path = m_dir.filePath(QStringLiteral("t.omaseed-torrents"));
    TorrentsBackup::Contents contents;
    [&]() {
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            TarWriter writer(&file);
            QVERIFY(writer.add(
                TorrentsBackup::kManifest, QJsonDocument(manifest.toJson()).toJson(), 0));
            QVERIFY(writer.add(TorrentsBackup::recordEntry(record.id),
                               QJsonDocument(record.toJson()).toJson(),
                               0));
            QVERIFY(writer.add(TorrentsBackup::torrentEntry(record.id), m_torrent, 0));
            QVERIFY(writer.finish());
        }
        QString error;
        const auto read = TorrentsBackup::read(path, &error);
        QVERIFY2(read.has_value(), qPrintable(error));
        contents = *read;
    }();
    return contents;
}

Import::Outcome TestDelugeTorrentsImportLive::import(const TorrentsBackup::Contents &contents,
                                                     Import::Plan *plan)
{
    // What the daemon has now: its torrents and its labels.
    Import::Here here;
    const QJsonObject all = call(QStringLiteral("core.get_torrents_status"),
                                 {QJsonObject(), QJsonArray{QStringLiteral("name")}})
                                .toObject();
    for (auto it = all.constBegin(); it != all.constEnd(); ++it)
        here.ids.insert(it.key().toLower());
    for (const QJsonValue &label : call(QStringLiteral("label.get_labels")).toArray())
        here.categories.append(label.toString());
    const Import::Plan made = Import::plan(contents, here);
    if (plan != nullptr)
        *plan = made;
    Import::Options options;
    options.journalPath = m_dir.filePath(QStringLiteral("imports/live.json"));
    options.pollMs = 100;
    // The daemon is on this machine: its folders are this machine's.
    options.localPathFor = [](const QString &path) { return path; };
    Import importer(m_engine.get());
    QSignalSpy finished(&importer, &Import::finished);
    importer.start(contents, made, options);
    Import::Outcome outcome;
    [&]() {
        QVERIFY(finished.wait(60000));
        outcome = finished.at(0).at(0).value<Import::Outcome>();
        QVERIFY(!QFile::exists(options.journalPath));
    }();
    return outcome;
}

void TestDelugeTorrentsImportLive::renamedWithASkippedFileComesBackSeedingAndLabelled()
{
    // On disk: its top folder and a file below it under other names. The .torrent's own
    // names are nowhere.
    const QStringList renamed{QStringLiteral("Top Renamed/a.bin"),
                              QStringLiteral("Top Renamed/sub/B renamed.bin"),
                              QStringLiteral("Top Renamed/sub/c.txt")};
    const QString data = dataUnder(QStringLiteral("renamed"), renamed);
    TorrentRecord r = record(data);
    for (int i = 0; i < renamed.size(); ++i)
        r.renamed.append({i, m_names.at(i), renamed.at(i)});
    r.category = kLabel;
    r.priorities = {1, 6, 0}; // the last one not wanted
    r.dlLimit = 300 * 1024;
    r.ratioLimit = 150.5;
    r.sequential = true;
    const TorrentsBackup::Contents contents = write(r, EngineKind::Deluge);

    Import::Plan plan;
    const Import::Outcome outcome = import(contents, &plan);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(plan.add.size(), 1);
    QCOMPARE(plan.categories, QStringList({kLabel}));
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.categories, 1);
    QCOMPARE(outcome.labelsNotSet, 0);
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.leftStopped, 0);
    QVERIFY(outcome.losses.isEmpty() && outcome.refused.isEmpty() && outcome.misplaced.isEmpty());

    // Checked at 100 % of what is wanted, and seeding as it was exported.
    const QJsonObject t = status({QStringLiteral("state"),
                                  QStringLiteral("progress"),
                                  QStringLiteral("is_finished"),
                                  QStringLiteral("total_wanted"),
                                  QStringLiteral("total_wanted_done"),
                                  QStringLiteral("download_location"),
                                  QStringLiteral("label"),
                                  QStringLiteral("files"),
                                  QStringLiteral("file_priorities"),
                                  QStringLiteral("max_download_speed"),
                                  QStringLiteral("stop_at_ratio"),
                                  QStringLiteral("stop_ratio"),
                                  QStringLiteral("remove_at_ratio"),
                                  QStringLiteral("sequential_download"),
                                  QStringLiteral("seed_mode")});
    QCOMPARE(t.value(QStringLiteral("is_finished")).toBool(), true);
    QCOMPARE(t.value(QStringLiteral("progress")).toDouble(), 100.0);
    QCOMPARE(t.value(QStringLiteral("total_wanted_done")).toInteger(),
             t.value(QStringLiteral("total_wanted")).toInteger());
    QCOMPARE(t.value(QStringLiteral("total_wanted")).toInteger(), kSizes.at(0) + kSizes.at(1));
    const QString state = t.value(QStringLiteral("state")).toString();
    QVERIFY2(state == QLatin1String("Seeding") || state == QLatin1String("Queued"),
             qPrintable(state));
    QCOMPARE(t.value(QStringLiteral("download_location")).toString(), data);
    QCOMPARE(t.value(QStringLiteral("label")).toString(), kLabel);
    QStringList names;
    for (const QJsonValue &file : t.value(QStringLiteral("files")).toArray())
        names.append(file.toObject().value(QStringLiteral("path")).toString());
    QCOMPARE(names, renamed);
    QCOMPARE(t.value(QStringLiteral("file_priorities")).toArray(), (QJsonArray{4, 7, 0}));
    QCOMPARE(t.value(QStringLiteral("max_download_speed")).toDouble(), 300.0);
    QCOMPARE(t.value(QStringLiteral("stop_at_ratio")).toBool(), true);
    QCOMPARE(t.value(QStringLiteral("stop_ratio")).toDouble(), 150.5);
    QCOMPARE(t.value(QStringLiteral("remove_at_ratio")).toBool(), false);
    QCOMPARE(t.value(QStringLiteral("sequential_download")).toBool(), true);
    QCOMPARE(t.value(QStringLiteral("seed_mode")).toBool(), false);
    // Nothing was made under the old names.
    QVERIFY(!QFile::exists(data + QLatin1Char('/') + kTop));

    // The same file again: it is there already.
    Import::Plan again;
    const Import::Outcome second = import(contents, &again);
    QVERIFY(second.complete);
    QCOMPARE(again.count(), 0);
    QCOMPARE(again.skipped.size(), 1);

    // The skipped file never made the torrent unremovable (the research's trap).
    removeOwn();
    QVERIFY(status({QStringLiteral("name")}).isEmpty());
}

void TestDelugeTorrentsImportLive::withoutItsTopFolderComesBackPaused()
{
    // A qBittorrent's "NoSubfolder": its files straight in the save path.
    QStringList flat;
    for (const QString &name : std::as_const(m_names))
        flat.append(name.mid(kTop.size() + 1));
    const QString data = dataUnder(QStringLiteral("flat"), flat);
    TorrentRecord r = record(data);
    r.layout = QStringLiteral("NoSubfolder");
    r.category = QStringLiteral("Omaseed Import Live"); // in Deluge's spelling: kLabel
    r.state = QStringLiteral("stopped");
    const TorrentsBackup::Contents contents = write(r, EngineKind::Qbittorrent);

    const Import::Outcome outcome = import(contents);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.started, 0);
    QCOMPARE(outcome.leftStopped, 1);
    QVERIFY(outcome.refused.isEmpty() && outcome.misplaced.isEmpty());

    const QJsonObject t = status({QStringLiteral("state"),
                                  QStringLiteral("progress"),
                                  QStringLiteral("is_finished"),
                                  QStringLiteral("label"),
                                  QStringLiteral("files")});
    QCOMPARE(t.value(QStringLiteral("progress")).toDouble(), 100.0);
    QCOMPARE(t.value(QStringLiteral("is_finished")).toBool(), true);
    // A recheck leaves a torrent paused: stopped, as it was exported.
    QCOMPARE(t.value(QStringLiteral("state")).toString(), QStringLiteral("Paused"));
    QCOMPARE(t.value(QStringLiteral("label")).toString(), kLabel);
    QStringList names;
    for (const QJsonValue &file : t.value(QStringLiteral("files")).toArray())
        names.append(file.toObject().value(QStringLiteral("path")).toString());
    QCOMPARE(names, flat);
    QVERIFY(!QFile::exists(data + QLatin1Char('/') + kTop));
    removeOwn();
}

QTEST_GUILESS_MAIN(TestDelugeTorrentsImportLive)
#include "tst_delugetorrentsimport_live.moc"
