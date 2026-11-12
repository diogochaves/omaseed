// TransmissionTorrentsExport against the real Transmission test server (#88, docs/DECISIONS.md
// C40 X3): transmission-daemon 4.1 on :8085 (scripts/dev/test-server.sh start --transmission).
// Adds one fixture torrent of its own (nested_library_v1.torrent, stopped, into a folder of the
// test's, with a label, a limit and a renamed file), exports the daemon's torrents and reads
// the file back: the daemon is on this machine, so its .torrent is read where it says; under a
// folder that doesn't hold it the torrent goes in as its link. The server's other torrents are
// only read; the test's own is removed. QSKIPs when nothing listens on :8085.

#include "LiveServer.hpp"

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionTorrentsExport.hpp"

#include <QFile>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include "FakeHttp.hpp"

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
using fakehttp::await;

namespace {

using Export = TransmissionTorrentsExport;

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

const char *const kTorrent = "nested_library_v1.torrent";

QByteArray fixtureBytes()
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kTorrent));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", kTorrent);
    return file.readAll();
}

} // namespace

class TestTransmissionTorrentsExportLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void theDaemonsTorrentFilesAreReadHere();
    void exportsAndReadsBack();
    void underAnotherFolderItIsALink();

private:
    template<class T>
    T ok(Reply<T> *reply)
    {
        const Result<T> result = await(this, reply, 20000);
        [&]() { QVERIFY2(result.isValid(), qPrintable(result.error().toString())); }();
        return result.value();
    }
    Export::Outcome run(const QString &path, const QString &torrentsFolder = {});

    std::unique_ptr<TransmissionEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_hash;
    QString m_renamedFrom;
    QString m_renamedTo;
};

void TestTransmissionTorrentsExportLive::initTestCase()
{
    if (!listening(8085))
        QSKIP("no Transmission on :8085: scripts/dev/test-server.sh start --transmission");
    m_engine = std::make_unique<TransmissionEngine>();
    m_engine->setBaseUrl(
        LiveServer::fromEnv("OMASEED_TRANSMISSION_URL", QStringLiteral("http://127.0.0.1:8085")));
    m_engine->setTimeout(std::chrono::seconds(15));
    QVERIFY(m_dir.isValid());
    const TorrentFile parsed = TorrentFile::parse(fixtureBytes());
    QVERIFY(parsed.isValid());
    m_hash = parsed.infoHashV1;
    // A leftover of an earlier run goes first.
    ok(m_engine->remove({m_hash}, true));

    AddOptions options;
    options.savePath = m_dir.filePath(QStringLiteral("data"));
    options.stopped = true;
    options.tags = {QStringLiteral("omaseed-export-live"), QStringLiteral("second")};
    options.dlLimit = 300000;
    const AddResult added =
        ok(m_engine->addFile(QString::fromLatin1(kTorrent), fixtureBytes(), options));
    QCOMPARE(added.addedIds, QStringList({m_hash}));
    const QList<ContentFile> files = ok(m_engine->files(m_hash));
    QVERIFY(!files.isEmpty());
    m_renamedFrom = files.at(0).path;
    m_renamedTo = m_renamedFrom.left(m_renamedFrom.lastIndexOf(QLatin1Char('/')) + 1) +
                  QStringLiteral("renamed for the export.bin");
    ok(m_engine->renameFile(m_hash, m_renamedFrom, m_renamedTo));
}

void TestTransmissionTorrentsExportLive::cleanupTestCase()
{
    if (m_engine)
        await(this, m_engine->remove({m_hash}, true), 20000);
}

Export::Outcome TestTransmissionTorrentsExportLive::run(const QString &path,
                                                        const QString &torrentsFolder)
{
    Export exporter(m_engine.get());
    exporter.setTorrentsFolder(torrentsFolder);
    QSignalSpy finished(&exporter, &Export::finished);
    exporter.start(
        path, QStringLiteral("playground"), m_engine->baseUrl().toString(), QStringLiteral("test"));
    if (!finished.wait(60000))
        return {};
    return finished.at(0).at(0).value<Export::Outcome>();
}

void TestTransmissionTorrentsExportLive::theDaemonsTorrentFilesAreReadHere()
{
    const Export::Reach reach = ok(Export::reach(m_engine.get()));
    QVERIFY2(reach.readable, qPrintable(reach.daemonFolder));
    QVERIFY(reach.tried >= 1);
    QVERIFY(reach.daemonFolder.endsWith(QStringLiteral("/torrents")));
    // The same folder, named: still there. A folder without them: not.
    QVERIFY(ok(Export::reach(m_engine.get(), reach.daemonFolder)).readable);
    QVERIFY(!ok(Export::reach(m_engine.get(), m_dir.path())).readable);
}

void TestTransmissionTorrentsExportLive::exportsAndReadsBack()
{
    const QString path = m_dir.filePath(QStringLiteral("playground.omaseed-torrents"));
    const Export::Outcome outcome = run(path);
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY(outcome.torrents >= 1);

    QString error;
    const auto contents = TorrentsBackup::read(path, &error);
    QVERIFY2(contents.has_value(), qPrintable(error));
    QCOMPARE(contents->manifest.engine, EngineKind::Transmission);
    QVERIFY2(contents->manifest.version.startsWith(QStringLiteral("4.")),
             qPrintable(contents->manifest.version));
    QVERIFY(!contents->manifest.version.contains(QLatin1Char(' ')));
    QVERIFY(!contents->manifest.savePath.isEmpty());
    QVERIFY(contents->manifest.tags.contains(QStringLiteral("omaseed-export-live")));
    QCOMPARE(contents->records.size(), outcome.torrents);

    const auto it = std::find_if(contents->records.cbegin(),
                                 contents->records.cend(),
                                 [this](const TorrentRecord &r) { return r.id == m_hash; });
    QVERIFY(it != contents->records.cend());
    QVERIFY(!it->isMagnet());
    QCOMPARE(it->infohashV1, m_hash);
    QCOMPARE(it->savePath, m_dir.filePath(QStringLiteral("data")));
    QCOMPARE(it->category, QString());
    QCOMPARE(it->tags, QStringList({"omaseed-export-live", "second"}));
    QCOMPARE(it->state, QStringLiteral("stopped"));
    QVERIFY(it->queuePosition >= 1); // nothing of it is here
    QCOMPARE(it->dlLimit, qint64(300000));
    QCOMPARE(it->upLimit, qint64(0));
    QCOMPARE(it->layout, QStringLiteral("Original"));
    QCOMPARE(it->renamed, QList<TorrentRecord::Rename>({{0, m_renamedFrom, m_renamedTo}}));
    const TorrentFile fixture = TorrentFile::parse(fixtureBytes());
    QCOMPARE(it->trackers, fixture.trackers());

    // Its .torrent is the torrent's, with the names it came with.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const TorrentFile stored =
        TorrentFile::parse(TorrentsBackup::torrentBytes(&file, *contents, m_hash));
    QVERIFY(stored.isValid());
    QCOMPARE(stored.infoHashV1, m_hash);
    QCOMPARE(TorrentsBackup::namesUnder(stored, QStringLiteral("Original")).at(0), m_renamedFrom);
}

void TestTransmissionTorrentsExportLive::underAnotherFolderItIsALink()
{
    const QString path = m_dir.filePath(QStringLiteral("links.omaseed-torrents"));
    const Export::Outcome outcome = run(path, m_dir.path());
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.magnets, outcome.torrents);
    const auto contents = TorrentsBackup::read(path);
    QVERIFY(contents.has_value());
    QVERIFY(contents->torrents.isEmpty());
    const auto it = std::find_if(contents->records.cbegin(),
                                 contents->records.cend(),
                                 [this](const TorrentRecord &r) { return r.id == m_hash; });
    QVERIFY(it != contents->records.cend());
    QVERIFY(it->isMagnet());
    QVERIFY2(it->magnet.startsWith(QStringLiteral("magnet:?xt=urn:btih:") + m_hash),
             qPrintable(it->magnet));
    QCOMPARE(it->tags, QStringList({"omaseed-export-live", "second"}));
    QVERIFY(it->renamed.isEmpty());
}

QTEST_GUILESS_MAIN(TestTransmissionTorrentsExportLive)
#include "tst_transmissiontorrentsexport_live.moc"
