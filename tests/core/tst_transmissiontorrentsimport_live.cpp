// TransmissionTorrentsImport against the real Transmission test server (#88, docs/DECISIONS.md
// C40 X6): transmission-daemon 4.1 on :8085 (scripts/dev/test-server.sh start --transmission).
// Makes one private torrent of its own with its data in a folder of the test's, lets the
// daemon seed it, exports the daemon's torrents (TransmissionTorrentsExport), removes the
// torrent keeping its data and imports the file: it is back, checked at 100 %, in the state
// and with the labels, limits and file choices it was exported with, and a second export
// gives the same record. Then the same with its top folder and a file renamed on the daemon,
// stopped: the import renames before the check and leaves it stopped at 100 %. The server's
// other torrents are only read (they are "already there"); the test's own is removed.
// QSKIPs when nothing listens on :8085.

#include "LiveServer.hpp"

#include "core/Bencode.hpp"
#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"
#include "core/engines/transmission/TransmissionTorrentsExport.hpp"
#include "core/engines/transmission/TransmissionTorrentsImport.hpp"
#include "core/engines/transmission/TransmissionTranslate.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "FakeHttp.hpp"

using namespace omaseed::core;
using fakehttp::await;

namespace {

using Export = TransmissionTorrentsExport;
using Import = TransmissionTorrentsImport;

const QString kTop = QStringLiteral("omaseed-import-live");

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

QByteArray contentOf(int file, qint64 size)
{
    QByteArray out;
    QByteArray block = QByteArray::number(file) + "-omaseed-import-live";
    while (out.size() < size) {
        block = QCryptographicHash::hash(block, QCryptographicHash::Sha256);
        out += block;
    }
    return out.left(size);
}

QStringList strings(const QJsonValue &array)
{
    QStringList out;
    for (const QJsonValue &v : array.toArray())
        out.append(v.toString());
    return out;
}

} // namespace

class TestTransmissionTorrentsImportLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void aSeedingTorrentComesBackSeeding();
    void renamedOnTheDaemonComesBackRenamedAndStopped();

private:
    QJsonObject call(const QString &method, const QJsonObject &params = {});
    QJsonObject get(const QStringList &fields);
    bool waitFor(const std::function<bool()> &done, int timeoutMs = 30000);
    /// Adds the test's torrent to its data, paused, and waits for the daemon's check.
    void addOwn();
    /// Exports the daemon, and gives the file read back with the test's record.
    TorrentsBackup::Contents exportTo(const QString &name, TorrentRecord *own);
    Import::Outcome import(const TorrentsBackup::Contents &contents, Import::Plan *plan = nullptr);

    std::unique_ptr<TransmissionEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_data;
    QByteArray m_torrent;
    QString m_hash;
    QStringList m_names; ///< its files, as the .torrent names them
};

QJsonObject TestTransmissionTorrentsImportLive::call(const QString &method,
                                                     const QJsonObject &params)
{
    const Result<QJsonObject> result = await(this, m_engine->rpc()->call(method, params), 30000);
    [&]() { QVERIFY2(result.isValid(), qPrintable(method + u": " + result.error().toString())); }();
    return result.value();
}

QJsonObject TestTransmissionTorrentsImportLive::get(const QStringList &fields)
{
    const QList<QJsonObject> rows = TransmissionTranslate::rows(
        call(QStringLiteral("torrent_get"),
             {{QStringLiteral("ids"), QJsonArray{m_hash}},
              {QStringLiteral("fields"), QJsonArray::fromStringList(fields)}}));
    return rows.value(0);
}

bool TestTransmissionTorrentsImportLive::waitFor(const std::function<bool()> &done, int timeoutMs)
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

void TestTransmissionTorrentsImportLive::initTestCase()
{
    if (!listening(8085))
        QSKIP("no Transmission on :8085: scripts/dev/test-server.sh start --transmission");
    m_engine = std::make_unique<TransmissionEngine>();
    m_engine->setBaseUrl(
        LiveServer::fromEnv("OMASEED_TRANSMISSION_URL", QStringLiteral("http://127.0.0.1:8085")));
    m_engine->setTimeout(std::chrono::seconds(15));
    QVERIFY(m_dir.isValid());

    // A top folder with a file in it and two in a folder below, 1.3 MiB in 16 KiB pieces.
    m_data = m_dir.filePath(QStringLiteral("data"));
    const QList<QPair<QStringList, qint64>> files{
        {{QStringLiteral("a.bin")}, 1 << 20},
        {{QStringLiteral("sub"), QStringLiteral("b.bin")}, 300000},
        {{QStringLiteral("sub"), QStringLiteral("c.txt")}, 5000}};
    QByteArray stream;
    bencode::List list;
    for (int i = 0; i < files.size(); ++i) {
        const QString relative = files.at(i).first.join(QLatin1Char('/'));
        const QString path = m_data + QLatin1Char('/') + kTop + QLatin1Char('/') + relative;
        QVERIFY(QDir().mkpath(QFileInfo(path).absolutePath()));
        const QByteArray content = contentOf(i, files.at(i).second);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(content), content.size());
        stream += content;
        bencode::List parts;
        for (const QString &part : files.at(i).first)
            parts.append(bencode::Value(part.toUtf8()));
        list.append(bencode::Value(bencode::Dict{{"length", bencode::Value(files.at(i).second)},
                                                 {"path", bencode::Value(parts)}}));
        m_names.append(kTop + QLatin1Char('/') + relative);
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
    await(this, m_engine->remove({m_hash}, false), 20000);
}

void TestTransmissionTorrentsImportLive::cleanupTestCase()
{
    if (m_engine)
        await(this, m_engine->remove({m_hash}, false), 20000);
}

void TestTransmissionTorrentsImportLive::addOwn()
{
    const QJsonObject added =
        call(QStringLiteral("torrent_add"),
             {{QStringLiteral("metainfo"), QString::fromLatin1(m_torrent.toBase64())},
              {QStringLiteral("paused"), true},
              {QStringLiteral("download_dir"), m_data}});
    QVERIFY(added.contains(QStringLiteral("torrent_added")));
    QVERIFY(waitFor([this] {
        const QJsonObject t = get({QStringLiteral("status"), QStringLiteral("percent_done")});
        return t.value(QStringLiteral("status")).toInt() == 0 &&
               t.value(QStringLiteral("percent_done")).toDouble() >= 1;
    }));
}

TorrentsBackup::Contents TestTransmissionTorrentsImportLive::exportTo(const QString &name,
                                                                      TorrentRecord *own)
{
    const QString path = m_dir.filePath(name);
    Export exporter(m_engine.get());
    QSignalSpy finished(&exporter, &Export::finished);
    exporter.start(
        path, QStringLiteral("playground"), m_engine->baseUrl().toString(), QStringLiteral("test"));
    TorrentsBackup::Contents contents;
    [&]() {
        QVERIFY(finished.wait(60000));
        const Export::Outcome outcome = finished.at(0).at(0).value<Export::Outcome>();
        QVERIFY2(outcome.ok, qPrintable(outcome.error));
        QString error;
        const auto read = TorrentsBackup::read(path, &error);
        QVERIFY2(read.has_value(), qPrintable(error));
        contents = *read;
        const auto it = std::find_if(contents.records.cbegin(),
                                     contents.records.cend(),
                                     [this](const TorrentRecord &r) { return r.id == m_hash; });
        QVERIFY(it != contents.records.cend());
        *own = *it;
    }();
    return contents;
}

Import::Outcome TestTransmissionTorrentsImportLive::import(const TorrentsBackup::Contents &contents,
                                                           Import::Plan *plan)
{
    // What the daemon has now: everything but the test's torrent is already there.
    Import::Here here;
    for (const QJsonObject &row : TransmissionTranslate::rows(
             call(QStringLiteral("torrent_get"),
                  {{QStringLiteral("fields"), QJsonArray{QStringLiteral("hash_string")}}})))
        here.ids.insert(row.value(QStringLiteral("hash_string")).toString().toLower());
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

void TestTransmissionTorrentsImportLive::aSeedingTorrentComesBackSeeding()
{
    addOwn();
    // Labels, limits and file choices of its own, then seeding.
    call(QStringLiteral("torrent_set"),
         {{QStringLiteral("ids"), QJsonArray{m_hash}},
          {QStringLiteral("labels"),
           QJsonArray{QStringLiteral("omaseed-import-live"), QStringLiteral("second")}},
          {QStringLiteral("download_limit"), 300},
          {QStringLiteral("download_limited"), true},
          {QStringLiteral("seed_ratio_mode"), 1},
          {QStringLiteral("seed_ratio_limit"), 150.5},
          {QStringLiteral("seed_idle_mode"), 2},
          {QStringLiteral("priority_low"), QJsonArray{1}},
          {QStringLiteral("priority_high"), QJsonArray{2}},
          {QStringLiteral("sequential_download"), true}});
    call(QStringLiteral("torrent_start"), {{QStringLiteral("ids"), QJsonArray{m_hash}}});
    QVERIFY(waitFor([this] {
        return get({QStringLiteral("status")}).value(QStringLiteral("status")).toInt() == 6;
    }));

    TorrentRecord exported;
    const TorrentsBackup::Contents contents =
        exportTo(QStringLiteral("seeding.omaseed-torrents"), &exported);
    QVERIFY(!exported.isMagnet());
    QCOMPARE(exported.state, QStringLiteral("started"));
    QCOMPARE(exported.priorities, QList<int>({1, 2, 6}));

    // Gone from the daemon, its data kept.
    QVERIFY(await(this, m_engine->remove({m_hash}, false), 20000).isValid());
    QVERIFY(waitFor([this] { return get({QStringLiteral("status")}).isEmpty(); }));
    QVERIFY(QFile::exists(m_data + QLatin1Char('/') + m_names.at(0)));

    Import::Plan plan;
    const Import::Outcome outcome = import(contents, &plan);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(plan.add.size(), 1);
    QCOMPARE(plan.skipped.size(), contents.records.size() - 1);
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.started, 1);
    QCOMPARE(outcome.leftStopped, 0);
    QVERIFY(outcome.losses.isEmpty() && outcome.refused.isEmpty() && outcome.misplaced.isEmpty());

    // Checked at 100 %, and seeding as it was.
    const QJsonObject t = get({QStringLiteral("status"),
                               QStringLiteral("percent_done"),
                               QStringLiteral("left_until_done"),
                               QStringLiteral("have_valid"),
                               QStringLiteral("total_size"),
                               QStringLiteral("download_dir"),
                               QStringLiteral("labels")});
    QCOMPARE(t.value(QStringLiteral("percent_done")).toDouble(), 1.0);
    QCOMPARE(t.value(QStringLiteral("left_until_done")).toInteger(-1), qint64(0));
    QCOMPARE(t.value(QStringLiteral("have_valid")).toInteger(),
             t.value(QStringLiteral("total_size")).toInteger());
    QVERIFY2(t.value(QStringLiteral("status")).toInt() == 6 ||
                 t.value(QStringLiteral("status")).toInt() == 5,
             qPrintable(QString::number(t.value(QStringLiteral("status")).toInt())));
    QCOMPARE(t.value(QStringLiteral("download_dir")).toString(), m_data);
    QCOMPARE(strings(t.value(QStringLiteral("labels"))),
             QStringList({"omaseed-import-live", "second"}));

    // A Transmission → file → Transmission round trip gives the same record.
    TorrentRecord again;
    exportTo(QStringLiteral("again.omaseed-torrents"), &again);
    QCOMPARE(QJsonDocument(again.toJson()).toJson(), QJsonDocument(exported.toJson()).toJson());
    QCOMPARE(again.dlLimit, qint64(300000));
    QCOMPARE(again.ratioLimit, 150.5);
    QCOMPARE(again.inactiveSeedingTimeLimit, qint64(-1));
    QVERIFY(again.sequential);

    QVERIFY(await(this, m_engine->remove({m_hash}, false), 20000).isValid());
    QVERIFY(waitFor([this] { return get({QStringLiteral("status")}).isEmpty(); }));
}

void TestTransmissionTorrentsImportLive::renamedOnTheDaemonComesBackRenamedAndStopped()
{
    addOwn();
    // The top folder and a file below it renamed on the daemon (which renames them on disk).
    call(QStringLiteral("torrent_rename_path"),
         {{QStringLiteral("ids"), QJsonArray{m_hash}},
          {QStringLiteral("path"), kTop},
          {QStringLiteral("name"), QStringLiteral("Top Renamed")}});
    call(QStringLiteral("torrent_rename_path"),
         {{QStringLiteral("ids"), QJsonArray{m_hash}},
          {QStringLiteral("path"), QStringLiteral("Top Renamed/sub/b.bin")},
          {QStringLiteral("name"), QStringLiteral("B renamed.bin")}});
    const QStringList renamed{QStringLiteral("Top Renamed/a.bin"),
                              QStringLiteral("Top Renamed/sub/B renamed.bin"),
                              QStringLiteral("Top Renamed/sub/c.txt")};
    QVERIFY(waitFor([&] { return QFile::exists(m_data + QLatin1Char('/') + renamed.at(1)); }));

    TorrentRecord exported;
    const TorrentsBackup::Contents contents =
        exportTo(QStringLiteral("renamed.omaseed-torrents"), &exported);
    QCOMPARE(exported.state, QStringLiteral("stopped"));
    QCOMPARE(exported.layout, QStringLiteral("Original"));
    QCOMPARE(exported.renamed.size(), 3); // the top folder is in every file's name

    QVERIFY(await(this, m_engine->remove({m_hash}, false), 20000).isValid());
    QVERIFY(waitFor([this] { return get({QStringLiteral("status")}).isEmpty(); }));

    const Import::Outcome outcome = import(contents);
    QVERIFY2(outcome.complete && outcome.errors.isEmpty(), qPrintable(outcome.errors.join(u"; ")));
    QCOMPARE(outcome.added, 1);
    QCOMPARE(outcome.started, 0);
    QCOMPARE(outcome.leftStopped, 1);
    // Only the new names are on disk: the expected case, nothing refused.
    QVERIFY(outcome.refused.isEmpty() && outcome.misplaced.isEmpty());

    const QJsonObject t = get({QStringLiteral("status"),
                               QStringLiteral("percent_done"),
                               QStringLiteral("left_until_done"),
                               QStringLiteral("name"),
                               QStringLiteral("files")});
    QCOMPARE(t.value(QStringLiteral("percent_done")).toDouble(), 1.0);
    QCOMPARE(t.value(QStringLiteral("left_until_done")).toInteger(-1), qint64(0));
    QCOMPARE(t.value(QStringLiteral("status")).toInt(), 0); // stopped, as it was exported
    QCOMPARE(t.value(QStringLiteral("name")).toString(), QStringLiteral("Top Renamed"));
    QStringList names;
    for (const QJsonValue &file : t.value(QStringLiteral("files")).toArray())
        names.append(file.toObject().value(QStringLiteral("name")).toString());
    QCOMPARE(names, renamed);
    // Nothing was made under the old names.
    QVERIFY(!QFile::exists(m_data + QLatin1Char('/') + kTop));

    TorrentRecord again;
    exportTo(QStringLiteral("renamed-again.omaseed-torrents"), &again);
    QCOMPARE(QJsonDocument(again.toJson()).toJson(), QJsonDocument(exported.toJson()).toJson());
}

QTEST_GUILESS_MAIN(TestTransmissionTorrentsImportLive)
#include "tst_transmissiontorrentsimport_live.moc"
