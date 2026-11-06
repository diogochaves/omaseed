// TransmissionEngine against the real Transmission test server (#85, E4): transmission-daemon
// 4.1 on :8085, started by scripts/dev/test-server.sh start --transmission (C14 E11). Proves
// that what the fake daemon in tst_transmissionengine checks is what a real 4.1 takes: every
// request's method and field names are accepted, and each change shows up on the server and
// in the poll. Adds one fixture torrent of its own (multi_file_v1_unicode.torrent, stopped,
// into a folder of the test's) and removes it again; the server's other torrents are only
// read. QSKIPs when nothing listens on :8085.

#include "LiveServer.hpp"

#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QVersionNumber>

#include "FakeHttp.hpp"

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
using fakehttp::await;

namespace {

bool listening(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(1000);
}

QByteArray fixtureBytes(const char *name)
{
    QFile file(QStringLiteral(OMASEED_FIXTURES_DIR "/") + QString::fromLatin1(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

QString fixtureHash(const char *name)
{
    const QJsonArray all = QJsonDocument::fromJson(fixtureBytes("fixtures.json")).array();
    for (const QJsonValue &value : all) {
        if (value.toObject().value(QStringLiteral("file")).toString() == QLatin1String(name))
            return value.toObject().value(QStringLiteral("infoHashV1")).toString();
    }
    qFatal("no hash for %s", name);
}

const char *const kTorrent = "multi_file_v1_unicode.torrent";

} // namespace

class TestTransmissionEngineLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void probesTheRealDaemon();
    void pollsAndSeesAnAdd();
    void actionsReachTheDaemon();
    void removeShowsInThePoll();

private:
    template<class T>
    T ok(Reply<T> *reply)
    {
        const Result<T> result = await(this, reply, 20000);
        if (result.isError())
            qWarning("%s", qPrintable(result.error().toString()));
        [&]() { QVERIFY2(result.isValid(), qPrintable(result.error().toString())); }();
        return result.value();
    }
    TorrentPatch row();

    std::unique_ptr<TransmissionEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_hash;
    int m_cursor = 0;
};

void TestTransmissionEngineLive::initTestCase()
{
    if (!listening(8085))
        QSKIP("no Transmission on :8085: scripts/dev/test-server.sh start --transmission");
    m_engine = std::make_unique<TransmissionEngine>();
    m_engine->setBaseUrl(
        LiveServer::fromEnv("OMASEED_TRANSMISSION_URL", QStringLiteral("http://127.0.0.1:8085")));
    m_engine->setTimeout(std::chrono::seconds(15));
    m_hash = fixtureHash(kTorrent);
    QVERIFY(m_dir.isValid());
    // A leftover of an earlier run goes first.
    ok(m_engine->remove({m_hash}, true));
}

void TestTransmissionEngineLive::cleanupTestCase()
{
    if (m_engine)
        await(this, m_engine->remove({m_hash}, true), 20000);
}

TorrentPatch TestTransmissionEngineLive::row()
{
    const QList<TorrentPatch> rows = ok(m_engine->torrents({m_hash}));
    return rows.isEmpty() ? TorrentPatch() : rows.first();
}

void TestTransmissionEngineLive::probesTheRealDaemon()
{
    const ServerInfo info = ok(m_engine->probe());
    QVERIFY2(QVersionNumber::fromString(info.version) >= QVersionNumber(4, 1),
             qPrintable(info.version));
    QVERIFY2(QVersionNumber::fromString(info.apiVersion) >= QVersionNumber(6),
             qPrintable(info.apiVersion));
    QCOMPARE(m_engine->rpc()->endpoint().path(), QStringLiteral("/transmission/rpc"));
}

void TestTransmissionEngineLive::pollsAndSeesAnAdd()
{
    const SyncDelta first = ok(m_engine->sync(0));
    QVERIFY(first.full);
    QVERIFY(first.cursor.has_value());
    QVERIFY(first.serverState.has_value());
    QVERIFY(first.serverState->dlSpeed.has_value());
    m_cursor = *first.cursor;
    for (const TorrentPatch &p : first.torrents)
        QVERIFY(p.hash != m_hash);

    AddOptions options;
    options.savePath = m_dir.path();
    options.stopped = true;
    options.tags = {QStringLiteral("omaseed-live")};
    options.dlLimit = 300000;
    const AddResult added =
        ok(m_engine->addFile(QString::fromLatin1(kTorrent), fixtureBytes(kTorrent), options));
    QCOMPARE(added.addedIds, QStringList({m_hash}));

    // The next poll (recently_active) has it, whole.
    const SyncDelta next = ok(m_engine->sync(m_cursor));
    QVERIFY(!next.full);
    m_cursor = *next.cursor;
    const auto it = std::find_if(next.torrents.cbegin(),
                                 next.torrents.cend(),
                                 [this](const auto &p) { return p.hash == m_hash; });
    QVERIFY(it != next.torrents.cend());
    QCOMPARE(it->values.status.phase, TorrentPhase::Stopped);
    QCOMPARE(it->values.tags, QStringList({QStringLiteral("omaseed-live")}));
    QCOMPARE(it->values.savePath, m_dir.path());
    QCOMPARE(it->values.dlLimit, qint64(300000));
    QVERIFY(next.tags.contains(QStringLiteral("omaseed-live")));

    // Again: already there, refused.
    QVERIFY(
        ok(m_engine->addFile(QString::fromLatin1(kTorrent), fixtureBytes(kTorrent), {})).refused);
}

void TestTransmissionEngineLive::actionsReachTheDaemon()
{
    // Labels, read and written whole.
    ok(m_engine->addTags({m_hash}, {QStringLiteral("second")}));
    QCOMPARE(row().values.tags, QStringList({"omaseed-live", "second"}));
    ok(m_engine->removeTags({m_hash}, {QStringLiteral("omaseed-live")}));
    QCOMPARE(row().values.tags, QStringList({"second"}));
    QVERIFY(ok(m_engine->tags()).contains(QStringLiteral("second")));
    QCOMPARE(await(this, m_engine->addTags({m_hash}, {QStringLiteral("a,b")})).error().kind(),
             ApiError::Kind::Conflict);

    // Limits.
    ok(m_engine->setUploadLimit({m_hash}, 120000));
    ok(m_engine->setShareLimits({m_hash},
                                RatioLimit::of(1.25),
                                MinutesLimit::global(),
                                MinutesLimit::of(90),
                                std::nullopt));
    TorrentPatch p = row();
    QCOMPARE(p.values.upLimit, qint64(120000));
    QCOMPARE(p.values.ratioLimit, RatioLimit::of(1.25));
    QCOMPARE(p.values.inactiveSeedingTimeLimit, MinutesLimit::of(90));

    // Files: the second one skipped, the first low.
    const QList<ContentFile> before = ok(m_engine->files(m_hash));
    QVERIFY(before.size() >= 2);
    ok(m_engine->setFilePriority(m_hash, {1}, FilePriority::Skip));
    ok(m_engine->setFilePriority(m_hash, {0}, FilePriority::Low));
    const QList<ContentFile> after = ok(m_engine->files(m_hash));
    QCOMPARE(after.at(0).priority, std::optional<int>(int(FilePriority::Low)));
    QCOMPARE(after.at(1).priority, std::optional<int>(int(FilePriority::Skip)));

    // Sequential, flipped.
    const bool sequential = row().values.seqDl;
    ok(m_engine->toggleSequentialDownload({m_hash}));
    QCOMPARE(row().values.seqDl, !sequential);

    // Trackers, replaced whole.
    const int trackers = row().values.trackersCount;
    ok(m_engine->addTrackers(m_hash, {QStringLiteral("http://127.0.0.1:9/omaseed-live")}));
    QCOMPARE(row().values.trackersCount, trackers + 1);
    ok(m_engine->removeTrackers(m_hash, {QStringLiteral("http://127.0.0.1:9/omaseed-live")}));
    QCOMPARE(row().values.trackersCount, trackers);

    // Rename: the torrent's folder (K28), then a file inside it.
    const QString name = row().values.name;
    ok(m_engine->rename(m_hash, name + QStringLiteral(" renamed")));
    QCOMPARE(row().values.name, name + QStringLiteral(" renamed"));
    const QString first = ok(m_engine->files(m_hash)).at(0).path;
    const QString renamed =
        first.left(first.lastIndexOf(QLatin1Char('/')) + 1) + QStringLiteral("renamed.bin");
    ok(m_engine->renameFile(m_hash, first, renamed));
    QCOMPARE(ok(m_engine->files(m_hash)).at(0).path, renamed);

    // Set location (nothing on disk to move yet), then start and stop.
    QTemporaryDir elsewhere;
    ok(m_engine->setLocation({m_hash}, elsewhere.path()));
    QCOMPARE(row().values.savePath, elsewhere.path());
    QVERIFY(ok(m_engine->stoppedAmong({m_hash})).contains(m_hash));
    ok(m_engine->start({m_hash}));
    const QList<RunningTorrent> running = ok(m_engine->running());
    QVERIFY(running.contains(RunningTorrent{m_hash, false}));
    ok(m_engine->reannounce({m_hash}));
    ok(m_engine->queueDown({m_hash}));
    ok(m_engine->stop({m_hash}));
    // Transmission stops a running torrent just after it answers: seen on the next read or two.
    QTRY_VERIFY_WITH_TIMEOUT(ok(m_engine->stoppedAmong({m_hash})).contains(m_hash), 5000);

    // Details read whole.
    const TorrentProperties properties = ok(m_engine->properties(m_hash));
    QCOMPARE(properties.hash, m_hash);
    QVERIFY(properties.piecesNum > 0);
    QCOMPARE(ok(m_engine->pieceStates(m_hash)).size(), properties.piecesNum);
    ok(m_engine->trackers(m_hash));
    ok(m_engine->peers(m_hash, 0));
    ok(m_engine->webSeeds(m_hash));

    // The server's limits and turtle mode, put back as they were.
    const GlobalLimits limits = ok(m_engine->globalLimits());
    ok(m_engine->setGlobalLimits(limits));
    QCOMPARE(ok(m_engine->globalLimits()), limits);
    ok(m_engine->toggleAltSpeed());
    ok(m_engine->toggleAltSpeed());
}

void TestTransmissionEngineLive::removeShowsInThePoll()
{
    ok(m_engine->remove({m_hash}, true));
    const SyncDelta delta = ok(m_engine->sync(m_cursor));
    QVERIFY2(delta.removedTorrents.contains(m_hash), qPrintable(delta.removedTorrents.join(',')));
    QVERIFY(ok(m_engine->torrents({m_hash})).isEmpty());
}

QTEST_GUILESS_MAIN(TestTransmissionEngineLive)
#include "tst_transmissionengine_live.moc"
