// DelugeTorrentsExport against the real deluge-web test server (#88, docs/DECISIONS.md C40
// X3): deluged 2.2 with deluge-web on :8086 (scripts/dev/test-server.sh start --deluge), the
// Label plugin on. Adds one fixture torrent of its own (nested_library_v1.torrent, paused, into
// a folder of the test's, with a label, a limit and a renamed file), exports the daemon's
// torrents and reads the file back: the daemon is on this machine, so its .torrent is read in
// the state folder its settings point at; under a folder that doesn't hold it the torrent goes
// in as its link, renames included. The server's other torrents are only read; the test's own
// and its label are removed. QSKIPs when nothing listens on :8086 or DELUGE_TEST_PASS isn't in
// the environment (set -a; . ~/.config/omaseed/dev.env; set +a).

#include "LiveServer.hpp"

#include "core/TorrentFile.hpp"
#include "core/TorrentsBackup.hpp"
#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeTorrentsExport.hpp"

#include <QFile>
#include <QProcessEnvironment>
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

using Export = DelugeTorrentsExport;

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

const char *const kTorrent = "nested_library_v1.torrent";
const QString kLabel = QStringLiteral("omaseed-export-live");

QByteArray fixtureBytes()
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(kTorrent));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", kTorrent);
    return file.readAll();
}

QString password()
{
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("DELUGE_TEST_PASS"));
}

} // namespace

class TestDelugeTorrentsExportLive : public QObject
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

    std::unique_ptr<DelugeEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_hash;
    QString m_renamedFrom;
    QString m_renamedTo;
};

void TestDelugeTorrentsExportLive::initTestCase()
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
    const TorrentFile parsed = TorrentFile::parse(fixtureBytes());
    QVERIFY(parsed.isValid());
    m_hash = parsed.infoHashV1;
    ok(m_engine->probe());
    QVERIFY(m_engine->labelPlugin());
    // A leftover of an earlier run goes first; Deluge then needs a moment before the same
    // torrent is added again (scripts/dev/research/88-engines-backup).
    if (!ok(m_engine->torrents({m_hash})).isEmpty()) {
        ok(m_engine->remove({m_hash}, true));
        QTest::qWait(2000);
    }
    await(this, m_engine->removeCategories({kLabel}), 20000);
    ok(m_engine->createCategory(kLabel));

    AddOptions options;
    options.savePath = m_dir.filePath(QStringLiteral("data"));
    options.stopped = true;
    options.category = kLabel;
    options.dlLimit = 300 * 1024;
    const AddResult added =
        ok(m_engine->addFile(QString::fromLatin1(kTorrent), fixtureBytes(), options));
    QCOMPARE(added.addedIds, QStringList({m_hash}));
    const QList<ContentFile> files = ok(m_engine->files(m_hash));
    QVERIFY(!files.isEmpty());
    m_renamedFrom = files.at(0).path;
    m_renamedTo = m_renamedFrom.left(m_renamedFrom.lastIndexOf(QLatin1Char('/')) + 1) +
                  QStringLiteral("renamed for the export.bin");
    ok(m_engine->renameFile(m_hash, m_renamedFrom, m_renamedTo));
    QTRY_COMPARE_WITH_TIMEOUT(ok(m_engine->files(m_hash)).at(0).path, m_renamedTo, 5000);
}

void TestDelugeTorrentsExportLive::cleanupTestCase()
{
    if (!m_engine || m_hash.isEmpty())
        return;
    await(this, m_engine->remove({m_hash}, true), 20000);
    await(this, m_engine->removeCategories({kLabel}), 20000);
}

Export::Outcome TestDelugeTorrentsExportLive::run(const QString &path,
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

void TestDelugeTorrentsExportLive::theDaemonsTorrentFilesAreReadHere()
{
    const Export::Reach reach = ok(Export::reach(m_engine.get()));
    QVERIFY2(reach.readable, qPrintable(reach.daemonFolder));
    QVERIFY(reach.tried >= 1);
    QVERIFY(reach.daemonFolder.endsWith(QStringLiteral("/state")));
    QVERIFY(
        QFile::exists(reach.daemonFolder + QLatin1Char('/') + m_hash + QStringLiteral(".torrent")));
    // The same folder, named: still there. A folder without them: not.
    QVERIFY(ok(Export::reach(m_engine.get(), reach.daemonFolder)).readable);
    QVERIFY(!ok(Export::reach(m_engine.get(), m_dir.path())).readable);
}

void TestDelugeTorrentsExportLive::exportsAndReadsBack()
{
    const QString path = m_dir.filePath(QStringLiteral("playground.omaseed-torrents"));
    const Export::Outcome outcome = run(path);
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY(outcome.torrents >= 1);

    QString error;
    const auto contents = TorrentsBackup::read(path, &error);
    QVERIFY2(contents.has_value(), qPrintable(error));
    QCOMPARE(contents->manifest.engine, EngineKind::Deluge);
    QVERIFY2(contents->manifest.version.startsWith(QStringLiteral("2.")),
             qPrintable(contents->manifest.version));
    QVERIFY(!contents->manifest.savePath.isEmpty());
    QCOMPARE(contents->manifest.downloadPath, QString());
    QVERIFY(contents->manifest.tags.isEmpty());
    QCOMPARE(contents->records.size(), outcome.torrents);

    const auto it = std::find_if(contents->records.cbegin(),
                                 contents->records.cend(),
                                 [this](const TorrentRecord &r) { return r.id == m_hash; });
    QVERIFY(it != contents->records.cend());
    QVERIFY(!it->isMagnet());
    QCOMPARE(it->infohashV1, m_hash);
    QCOMPARE(it->savePath, m_dir.filePath(QStringLiteral("data")));
    QCOMPARE(it->category, kLabel);
    QCOMPARE(it->tags, QStringList());
    QCOMPARE(it->state, QStringLiteral("stopped"));
    QVERIFY(it->queuePosition >= 1); // nothing of it is here
    QCOMPARE(it->dlLimit, qint64(300 * 1024));
    QCOMPARE(it->upLimit, qint64(0));
    QCOMPARE(it->layout, QStringLiteral("Original"));
    QCOMPARE(it->renamed, QList<TorrentRecord::Rename>({{0, m_renamedFrom, m_renamedTo}}));
    const TorrentFile fixture = TorrentFile::parse(fixtureBytes());
    QCOMPARE(it->trackers, fixture.trackers());

    // Its .torrent is the torrent's, as it was added.
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(TorrentsBackup::torrentBytes(&file, *contents, m_hash), fixtureBytes());
}

void TestDelugeTorrentsExportLive::underAnotherFolderItIsALink()
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
    QCOMPARE(it->category, kLabel);
    // Deluge keeps the .torrent's own names beside the current ones: a link's renames too.
    QCOMPARE(it->renamed, QList<TorrentRecord::Rename>({{0, m_renamedFrom, m_renamedTo}}));
}

QTEST_GUILESS_MAIN(TestDelugeTorrentsExportLive)
#include "tst_delugetorrentsexport_live.moc"
