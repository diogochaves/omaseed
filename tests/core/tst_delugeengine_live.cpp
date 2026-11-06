// DelugeEngine against the real deluge-web test server (#87, E6): deluged 2.2 with deluge-web
// on :8086, started by scripts/dev/test-server.sh start --deluge (C14 E11), the Label plugin
// on. Proves that what the fake deluge-web in tst_delugeengine checks is what a real 2.2 takes:
// every call's method, params and option names are accepted, and each change shows up on the
// server and in the poll. Adds one fixture torrent of its own (multi_file_v1_unicode.torrent,
// paused, into a folder of the test's) and one label, and removes both again; the server's
// other torrents are only read. QSKIPs when nothing listens on :8086 or DELUGE_TEST_PASS isn't
// in the environment (set -a; . ~/.config/omaseed/dev.env; set +a).

#include "LiveServer.hpp"

#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
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
const QString kLabel = QStringLiteral("omaseed-live");

QString password()
{
    return QProcessEnvironment::systemEnvironment().value(QStringLiteral("DELUGE_TEST_PASS"));
}

} // namespace

class TestDelugeEngineLive : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void probesTheRealDaemon();
    void signsInAgainAndLatchesARefusal();
    void pollsAndSeesAnAdd();
    void actionsReachTheDaemon();
    void aMagnetIsLookedUpWithoutAnAdd();
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
    std::unique_ptr<DelugeEngine> makeEngine(const QString &password);

    std::unique_ptr<DelugeEngine> m_engine;
    QTemporaryDir m_dir;
    QString m_hash;
    int m_cursor = 0;
};

std::unique_ptr<DelugeEngine> TestDelugeEngineLive::makeEngine(const QString &password)
{
    auto engine = std::make_unique<DelugeEngine>();
    engine->setBaseUrl(
        LiveServer::fromEnv("OMASEED_DELUGE_URL", QStringLiteral("http://127.0.0.1:8086")));
    engine->setCredentials(QString(), password);
    engine->setTimeout(std::chrono::seconds(15));
    return engine;
}

void TestDelugeEngineLive::initTestCase()
{
    if (!listening(8086))
        QSKIP("no deluge-web on :8086: scripts/dev/test-server.sh start --deluge");
    if (password().isEmpty())
        QSKIP("no DELUGE_TEST_PASS: set -a; . ~/.config/omaseed/dev.env; set +a");
    m_engine = makeEngine(password());
    m_hash = fixtureHash(kTorrent);
    QVERIFY(m_dir.isValid());
    ok(m_engine->probe());
    // A leftover of an earlier run goes first.
    ok(m_engine->remove({m_hash}, true));
    await(this, m_engine->removeCategories({kLabel}), 20000);
}

void TestDelugeEngineLive::cleanupTestCase()
{
    if (!m_engine)
        return;
    await(this, m_engine->remove({m_hash}, true), 20000);
    await(this, m_engine->removeCategories({kLabel}), 20000);
}

TorrentPatch TestDelugeEngineLive::row()
{
    const QList<TorrentPatch> rows = ok(m_engine->torrents({m_hash}));
    return rows.isEmpty() ? TorrentPatch() : rows.first();
}

void TestDelugeEngineLive::probesTheRealDaemon()
{
    const ServerInfo info = ok(m_engine->probe());
    QVERIFY2(QVersionNumber::fromString(info.version) >= QVersionNumber(2, 1),
             qPrintable(info.version));
    QVERIFY(!m_engine->libtorrentVersion().isEmpty());
    QVERIFY(m_engine->labelPlugin()); // the test server turns it on
    QVERIFY(m_engine->daemonConnected());
    QCOMPARE(m_engine->rpc()->endpoint().path(), QStringLiteral("/json"));
    QVERIFY(m_engine->features(info).categories);

    const QList<DelugeHost> hosts = ok(m_engine->hosts());
    QVERIFY(!hosts.isEmpty());
    QVERIFY(std::any_of(hosts.cbegin(), hosts.cend(), [](const DelugeHost &host) {
        return host.status == QLatin1String("Connected") && !host.version.isEmpty();
    }));
}

void TestDelugeEngineLive::signsInAgainAndLatchesARefusal()
{
    // deluge-web forgets the session (its timeout, a restart): the next call signs in again.
    ok(m_engine->rpc()->call(QStringLiteral("auth.delete_session")));
    ok(m_engine->globalLimits());
    QVERIFY(m_engine->rpc()->signedIn());

    // A wrong password: refused once, then not sent again.
    const std::unique_ptr<DelugeEngine> wrong = makeEngine(QStringLiteral("not the password"));
    const Result<ServerInfo> refused = await(this, wrong->probe(), 20000);
    QCOMPARE(refused.error().kind(), ApiError::Kind::Unauthorized);
    QVERIFY(wrong->rpc()->loginRefused());
    QCOMPARE(await(this, wrong->globalLimits(), 20000).error().kind(),
             ApiError::Kind::Unauthorized);
}

void TestDelugeEngineLive::pollsAndSeesAnAdd()
{
    const SyncDelta first = ok(m_engine->sync(0));
    QVERIFY(first.full);
    QVERIFY(first.cursor.has_value());
    QVERIFY(first.serverState.has_value());
    QVERIFY(first.serverState->dlSpeed.has_value());
    QVERIFY(first.serverState->dlSession.has_value());
    m_cursor = *first.cursor;
    for (const TorrentPatch &p : first.torrents)
        QVERIFY(p.hash != m_hash);

    // A label of the test's own, moving finished torrents into a folder.
    const QString done = m_dir.filePath(QStringLiteral("done"));
    ok(m_engine->createCategory(QStringLiteral("OMASEED-live"), done));

    AddOptions options;
    options.savePath = m_dir.path();
    options.stopped = true;
    options.category = kLabel;
    options.dlLimit = 300 * 1024;
    options.sequentialDownload = true;
    const AddResult added =
        ok(m_engine->addFile(QString::fromLatin1(kTorrent), fixtureBytes(kTorrent), options));
    QCOMPARE(added.addedIds, QStringList({m_hash}));

    // The next poll has it, whole, its content path from its files, and the label.
    const SyncDelta next = ok(m_engine->sync(m_cursor));
    QVERIFY(!next.full);
    m_cursor = *next.cursor;
    const auto it = std::find_if(next.torrents.cbegin(),
                                 next.torrents.cend(),
                                 [this](const auto &p) { return p.hash == m_hash; });
    QVERIFY(it != next.torrents.cend());
    QCOMPARE(it->values.status.phase, TorrentPhase::Stopped);
    QCOMPARE(it->values.category, kLabel);
    QCOMPARE(it->values.dlLimit, qint64(300 * 1024));
    QVERIFY(it->values.seqDl);
    // Unfinished, with the label's folder to go to: its save path is that folder.
    QCOMPARE(it->values.savePath, done);
    QCOMPARE(it->values.downloadPath, m_dir.path());
    QVERIFY2(it->values.contentPath.startsWith(m_dir.path() + QLatin1Char('/')),
             qPrintable(it->values.contentPath));
    const auto label = std::find_if(next.categories.cbegin(),
                                    next.categories.cend(),
                                    [](const CategoryPatch &c) { return c.name == kLabel; });
    QVERIFY(label != next.categories.cend());
    QCOMPARE(label->savePath, std::optional<QString>(done));

    // Again: already there, refused.
    QVERIFY(
        ok(m_engine->addFile(QString::fromLatin1(kTorrent), fixtureBytes(kTorrent), {})).refused);
}

void TestDelugeEngineLive::actionsReachTheDaemon()
{
    // The label, off and on again (lower case either way).
    ok(m_engine->setCategory({m_hash}, QString()));
    QCOMPARE(row().values.category, QString());
    ok(m_engine->setCategory({m_hash}, QStringLiteral("Omaseed-Live")));
    QCOMPARE(row().values.category, kLabel);

    // Limits.
    ok(m_engine->setUploadLimit({m_hash}, 120 * 1024));
    ok(m_engine->setShareLimits({m_hash},
                                RatioLimit::of(1.25),
                                MinutesLimit::global(),
                                MinutesLimit::global(),
                                QStringLiteral("Remove")));
    TorrentPatch p = row();
    QCOMPARE(p.values.upLimit, qint64(120 * 1024));
    QCOMPARE(p.values.ratioLimit, RatioLimit::of(1.25));
    QCOMPARE(p.values.shareLimitAction, QStringLiteral("Remove"));
    ok(m_engine->setShareLimits({m_hash}, RatioLimit::global(), {}, {}, std::nullopt));

    // Files: the second one skipped, the first low.
    const QList<ContentFile> before = ok(m_engine->files(m_hash));
    QVERIFY(before.size() >= 2);
    ok(m_engine->setFilePriority(m_hash, {1}, FilePriority::Skip));
    ok(m_engine->setFilePriority(m_hash, {0}, FilePriority::Low));
    const QList<ContentFile> after = ok(m_engine->files(m_hash));
    QCOMPARE(after.at(0).priority, std::optional<int>(int(FilePriority::Low)));
    QCOMPARE(after.at(1).priority, std::optional<int>(int(FilePriority::Skip)));

    // Sequential and first-and-last, flipped.
    const bool sequential = row().values.seqDl;
    ok(m_engine->toggleSequentialDownload({m_hash}));
    QCOMPARE(row().values.seqDl, !sequential);
    const bool firstLast = row().values.firstLastPiecePrio;
    ok(m_engine->toggleFirstLastPiece({m_hash}));
    QCOMPARE(row().values.firstLastPiecePrio, !firstLast);
    ok(m_engine->setSuperSeeding({m_hash}, false));

    // Trackers, replaced whole.
    const qsizetype trackers = ok(m_engine->trackers(m_hash)).size();
    ok(m_engine->addTrackers(m_hash, {QStringLiteral("http://127.0.0.1:9/omaseed-live")}));
    QCOMPARE(ok(m_engine->trackers(m_hash)).size(), trackers + 1);
    ok(m_engine->removeTrackers(m_hash, {QStringLiteral("http://127.0.0.1:9/omaseed-live")}));
    QCOMPARE(ok(m_engine->trackers(m_hash)).size(), trackers);

    // Rename: the name shown (K28), then a file inside it (Deluge renames in the background).
    const QString name = row().values.name;
    ok(m_engine->rename(m_hash, name + QStringLiteral(" renamed")));
    QCOMPARE(row().values.name, name + QStringLiteral(" renamed"));
    const QString first = ok(m_engine->files(m_hash)).at(0).path;
    const QString renamed =
        first.left(first.lastIndexOf(QLatin1Char('/')) + 1) + QStringLiteral("renamed.bin");
    ok(m_engine->renameFile(m_hash, first, renamed));
    QTRY_COMPARE_WITH_TIMEOUT(ok(m_engine->files(m_hash)).at(0).path, renamed, 5000);

    // Set location (nothing on disk to move yet), then start, force and stop.
    QTemporaryDir elsewhere;
    ok(m_engine->setLocation({m_hash}, elsewhere.path()));
    // A move turns the label's move-when-done off: that folder is its folder now.
    QTRY_COMPARE_WITH_TIMEOUT(row().values.savePath, elsewhere.path(), 5000);
    QCOMPARE(row().values.downloadPath, QString());
    QVERIFY(ok(m_engine->stoppedAmong({m_hash})).contains(m_hash));
    ok(m_engine->start({m_hash}));
    QTRY_VERIFY_WITH_TIMEOUT(!ok(m_engine->stoppedAmong({m_hash})).contains(m_hash), 5000);
    ok(m_engine->setForceStart({m_hash}, true));
    QTRY_VERIFY_WITH_TIMEOUT(ok(m_engine->running()).contains(RunningTorrent{m_hash, true}), 5000);
    ok(m_engine->setForceStart({m_hash}, false));
    ok(m_engine->reannounce({m_hash}));
    ok(m_engine->queueDown({m_hash}));
    ok(m_engine->queueUp({m_hash}));
    ok(m_engine->stop({m_hash}));
    QTRY_VERIFY_WITH_TIMEOUT(ok(m_engine->stoppedAmong({m_hash})).contains(m_hash), 5000);
    ok(m_engine->recheck({m_hash}));

    // Details read whole.
    const TorrentProperties properties = ok(m_engine->properties(m_hash));
    QCOMPARE(properties.hash, m_hash);
    QVERIFY(properties.piecesNum > 0);
    QCOMPARE(ok(m_engine->pieceStates(m_hash)).size(), properties.piecesNum);
    ok(m_engine->peers(m_hash, 0));
    QCOMPARE(await(this, m_engine->webSeeds(m_hash)).error().kind(), ApiError::Kind::NotFound);

    // Room and names on the server's disks.
    const FreeSpaceAt room = ok(m_engine->freeSpace(m_dir.filePath(QStringLiteral("not/yet"))));
    QCOMPARE(room.path, m_dir.path());
    QVERIFY(room.bytesFree > 0);
    QCOMPARE(ok(m_engine->folderContent(m_dir.filePath(QStringLiteral("nope")))).state,
             ServerFolder::State::Missing);
    QCOMPARE(ok(m_engine->folderContent(m_dir.path())).state, ServerFolder::State::Listed);

    // The server's limits and settings, put back as they were.
    const GlobalLimits limits = ok(m_engine->globalLimits());
    ok(m_engine->setGlobalLimits(limits));
    QCOMPARE(ok(m_engine->globalLimits()), limits);
    const QVariantMap prefs = ok(m_engine->sessionPreferences());
    QVERIFY(prefs.contains(QStringLiteral("listen_port")));
    ok(m_engine->setSessionPreferences(
        {{QStringLiteral("dht"), prefs.value(QStringLiteral("dht"))}}));
}

void TestDelugeEngineLive::aMagnetIsLookedUpWithoutAnAdd()
{
    // Nobody has this one: still looking at once, and Deluge's own call waits on the server.
    const FetchedMetadata fetched = ok(m_engine->fetchMetadata(
        QStringLiteral("magnet:?xt=urn:btih:%1&dn=nobody").arg(QString(40, QLatin1Char('c')))));
    QVERIFY(!fetched.ready);
    QCOMPARE(await(this, m_engine->fetchMetadata(QStringLiteral("https://example.org/x.torrent")))
                 .error()
                 .kind(),
             ApiError::Kind::NotFound);
}

void TestDelugeEngineLive::removeShowsInThePoll()
{
    ok(m_engine->remove({m_hash}, true));
    const SyncDelta delta = ok(m_engine->sync(m_cursor));
    QVERIFY2(delta.removedTorrents.contains(m_hash), qPrintable(delta.removedTorrents.join(',')));
    QVERIFY(ok(m_engine->torrents({m_hash})).isEmpty());
    ok(m_engine->removeCategories({kLabel}));
    const QList<CategoryPatch> labels = ok(m_engine->categories());
    QVERIFY(std::none_of(
        labels.cbegin(), labels.cend(), [](const CategoryPatch &c) { return c.name == kLabel; }));
}

QTEST_GUILESS_MAIN(TestDelugeEngineLive)
#include "tst_delugeengine_live.moc"
