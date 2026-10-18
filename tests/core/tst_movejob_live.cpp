// MoveJob and QbtMoveFeed against the :8081 playground (board 42, DECISIONS C15, #96): a small
// folder with a subfolder save path is moved to another drive (/dev/shm when it is there) and
// every torrent keeps its place below the folder; then a move to a folder qBittorrent can't
// write stops after its first torrent, which qBittorrent answers 200 for and fails in its log
// (diogochaves/upstream#120). Before those, a plan made without a view of the disks asks the
// server what is already where the torrents would go (#102, C16 L13). Everything it adds is
// removed with its files at the end.

#include "core/Bencode.hpp"
#include "core/MoveJob.hpp"
#include "core/MovePlan.hpp"
#include "core/QbtClient.hpp"
#include "core/QbtMoveFeed.hpp"
#include "core/ServerFolder.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"

#include "LiveServer.hpp"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

#include <functional>

#include <unistd.h>

using omaseed::core::MoveJob;
using omaseed::core::MovePlan;
using omaseed::core::QbtClient;
using omaseed::core::QbtMoveFeed;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::ServerFolder;
using omaseed::core::SyncEngine;
using omaseed::core::Torrent;
namespace bencode = omaseed::core::bencode;

namespace {

bool answers(const QUrl &url)
{
    QTcpSocket socket;
    socket.connectToHost(url.host(), quint16(url.port(8081)));
    return socket.waitForConnected(1000);
}

template<class T>
QbtResult<T> await(QObject *context, QbtReply<T> *reply)
{
    QbtResult<T> result{omaseed::core::ApiError(omaseed::core::ApiError::Kind::Unknown,
                                                QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const QbtResult<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    return result;
}

bool waitFor(const std::function<bool()> &done, int timeoutMs)
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

/// A single-file .torrent of `path`, private so it never leaves the playground.
QByteArray torrentOf(const QString &path, QByteArray *infoHash)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray data = file.readAll();
    constexpr qint64 piece = 16384;
    QByteArray pieces;
    for (qint64 at = 0; at < data.size(); at += piece)
        pieces += QCryptographicHash::hash(data.mid(at, piece), QCryptographicHash::Sha1);
    const bencode::Dict info{{"length", bencode::Value(qint64(data.size()))},
                             {"name", bencode::Value(QFileInfo(path).fileName().toUtf8())},
                             {"piece length", bencode::Value(piece)},
                             {"pieces", bencode::Value(pieces)},
                             {"private", bencode::Value(qint64(1))}};
    const QByteArray infoBytes = bencode::encode(bencode::Value(info));
    *infoHash = QCryptographicHash::hash(infoBytes, QCryptographicHash::Sha1).toHex();
    return bencode::encode(bencode::Value(bencode::Dict{{"info", bencode::Value(info)}}));
}

} // namespace

class TestMoveJobLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void theServerSaysWhatIsAlreadyThere();
    void movesAFolderKeepingItsShape();
    void anUnwritableFolderStopsAfterTheFirst();

private:
    void add(const QString &folder, const QString &name, int kib);
    void startEngine();

    QUrl m_url;
    QbtClient m_client;
    SyncEngine m_engine;
    QString m_base;  ///< under the playground's downloads
    QString m_other; ///< the other drive
    QString m_root;  ///< the folder that moves
    QStringList m_hashes;
};

void TestMoveJobLive::initTestCase()
{
    m_url = LiveServer::playground();
    if (!answers(m_url))
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");
    m_client.setBaseUrl(m_url);
    const QbtResult<QJsonObject> prefs = await(this, m_client.appPreferences());
    QVERIFY2(!prefs.isError(), qPrintable(prefs.error().toString()));
    const QString downloads = prefs.value().value(QStringLiteral("save_path")).toString();
    QVERIFY(!downloads.isEmpty());
    const QString tag =
        QStringLiteral("omaseed-movejob-live-%1").arg(QCoreApplication::applicationPid());
    m_base = QDir(downloads).filePath(tag);
    m_root = m_base + QStringLiteral("/archive/games");
    // Another filesystem when there is one, so qBittorrent copies rather than renames.
    m_other = QFileInfo(QStringLiteral("/dev/shm")).isWritable() ? QStringLiteral("/dev/shm/") + tag
                                                                 : m_base + QStringLiteral("/fast");
    QVERIFY(QDir().mkpath(m_root + QStringLiteral("/handheld")));
    QVERIFY(QDir().mkpath(m_other));

    add(m_root, QStringLiteral("movejob-small.bin"), 20);
    add(m_root + QStringLiteral("/handheld"), QStringLiteral("movejob-mid.bin"), 40);
    add(m_root, QStringLiteral("movejob-big.bin"), 80);
    startEngine();
    QVERIFY2(waitFor(
                 [this] {
                     for (const QString &hash : std::as_const(m_hashes)) {
                         if (m_engine.torrents()->indexOfHash(hash) < 0 ||
                             m_engine.torrents()->torrent(hash).progress < 1.0)
                             return false;
                     }
                     return true;
                 },
                 30000),
             "the added torrents never finished their check");
}

void TestMoveJobLive::cleanupTestCase()
{
    if (!m_hashes.isEmpty())
        await(this, m_client.torrentsDelete(m_hashes, true));
    QTest::qWait(500);
    if (!m_other.isEmpty()) {
        QFile::setPermissions(m_other + QStringLiteral("/readonly"),
                              QFileDevice::ReadOwner | QFileDevice::WriteOwner |
                                  QFileDevice::ExeOwner);
        QDir(m_other).removeRecursively();
    }
    if (!m_base.isEmpty())
        QDir(m_base).removeRecursively();
}

void TestMoveJobLive::add(const QString &folder, const QString &name, int kib)
{
    const QString path = folder + QLatin1Char('/') + name;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    for (int i = 0; i < kib; ++i)
        file.write(QByteArray(1024, char('a' + (i % 26))));
    file.close();
    QByteArray hash;
    const QByteArray bytes = torrentOf(path, &hash);
    QVERIFY(!bytes.isEmpty());
    QbtClient::AddOptions options;
    options.savepath = folder;
    options.autoTMM = false;
    const QbtResult<QByteArray> added =
        await(this, m_client.torrentsAddFile(name + QStringLiteral(".torrent"), bytes, options));
    QVERIFY2(!added.isError(), qPrintable(added.error().toString()));
    m_hashes << QString::fromLatin1(hash);
}

void TestMoveJobLive::startEngine()
{
    m_engine.setClient(&m_client);
    m_engine.setIntervals(std::chrono::milliseconds(300),
                          std::chrono::milliseconds(1000),
                          std::chrono::milliseconds(1000),
                          std::chrono::milliseconds(1000));
    QSignalSpy first(&m_engine, &SyncEngine::firstSyncFinished);
    m_engine.start();
    QVERIFY2(waitFor([&first] { return first.count() > 0; }, 15000), "no first sync");
}

void TestMoveJobLive::theServerSaysWhatIsAlreadyThere()
{
    // A folder on the playground holds a different file named like the biggest torrent's; the
    // handheld folder isn't there at all. No probe: this machine "can't see" the disks.
    const QString target = m_base + QStringLiteral("/remote/games");
    QVERIFY(QDir().mkpath(target));
    QFile stray(target + QStringLiteral("/movejob-big.bin"));
    QVERIFY(stray.open(QIODevice::WriteOnly));
    stray.write("not the torrent's bytes");
    stray.close();

    MovePlan::Input in;
    for (int row = 0; row < m_engine.torrents()->count(); ++row)
        in.torrents << m_engine.torrents()->torrentAt(row);
    in.root = m_root;
    in.newPath = target;
    MovePlan plan = MovePlan::make(in);
    QVERIFY(!plan.seesDisks());
    QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Pending);
    QCOMPARE(plan.needListings(), QStringList({target, target + QStringLiteral("/handheld")}));

    for (const QString &folder : plan.needListings()) {
        const ServerFolder listed =
            ServerFolder::fromReply(await(this, m_client.appDirectoryContent(folder)));
        in.serverFolders.insert(folder, listed);
    }
    QCOMPARE(in.serverFolders.value(target).state, ServerFolder::State::Listed);
    QCOMPARE(in.serverFolders.value(target + QStringLiteral("/handheld")).state,
             ServerFolder::State::Missing);
    plan = MovePlan::make(in);
    QCOMPARE(plan.namesCheck(), MovePlan::NamesCheck::Server);
    QCOMPARE(plan.leftOut().size(), 1);
    QCOMPARE(plan.item(plan.leftOut().constFirst())->name, QStringLiteral("movejob-big.bin"));
    QCOMPARE(plan.toMove().size(), 2);

    // A file's path is no folder: qBittorrent says "Directory does not exist".
    const ServerFolder file = ServerFolder::fromReply(
        await(this, m_client.appDirectoryContent(target + QStringLiteral("/movejob-big.bin"))));
    QCOMPARE(file.state, ServerFolder::State::Missing);
}

void TestMoveJobLive::movesAFolderKeepingItsShape()
{
    MovePlan::Input in;
    for (int row = 0; row < m_engine.torrents()->count(); ++row)
        in.torrents << m_engine.torrents()->torrentAt(row);
    in.root = m_root;
    in.newPath = m_other + QStringLiteral("/games");
    const MovePlan plan = MovePlan::make(in);
    QVERIFY(plan.canMove());
    QCOMPARE(plan.toMove().size(), 3);

    MoveJob job;
    QbtMoveFeed feed(&m_engine, &job);
    QSignalSpy finished(&job, &MoveJob::finished);
    QVERIFY(job.start(plan, QStringLiteral("playground")));
    QVERIFY2(
        waitFor([&finished] { return finished.count() > 0; }, 60000),
        qPrintable(QStringLiteral("state %1, %2 moved").arg(int(job.state())).arg(job.moved())));
    QCOMPARE(job.state(), MoveJob::State::Done);
    QCOMPARE(job.moved(), 3);
    QCOMPARE(job.failed(), 0);
    for (const QString &hash : std::as_const(m_hashes)) {
        const Torrent &torrent = m_engine.torrents()->torrent(hash);
        const QString expected = torrent.name == QLatin1String("movejob-mid.bin")
                                     ? m_other + QStringLiteral("/games/handheld")
                                     : m_other + QStringLiteral("/games");
        QCOMPARE(torrent.savePath, expected);
        QVERIFY(QFile::exists(expected + QLatin1Char('/') + torrent.name));
    }
    job.closeSummary();
    QCOMPARE(job.state(), MoveJob::State::Idle);
}

void TestMoveJobLive::anUnwritableFolderStopsAfterTheFirst()
{
    if (::geteuid() == 0)
        QSKIP("qBittorrent running as root writes everywhere");
    const QString readonly = m_other + QStringLiteral("/readonly");
    QVERIFY(QDir().mkpath(readonly));
    QVERIFY(QFile::setPermissions(readonly, QFileDevice::ReadOwner | QFileDevice::ExeOwner));

    const auto run = [this](const QString &newPath, MoveJob &job) {
        MovePlan::Input in;
        for (int row = 0; row < m_engine.torrents()->count(); ++row)
            in.torrents << m_engine.torrents()->torrentAt(row);
        in.root = m_other + QStringLiteral("/games");
        in.newPath = newPath;
        const MovePlan plan = MovePlan::make(in);
        QVERIFY(plan.canMove());
        QSignalSpy paused(&job, &MoveJob::pausedItself);
        job.start(plan, QStringLiteral("playground"));
        QVERIFY2(waitFor([&paused] { return paused.count() > 0; }, 30000),
                 "the refusal never reached the job");
        QCOMPARE(job.pauseReason(), MoveJob::PauseReason::FirstFailed);
        QCOMPARE(job.failed(), 1);
        QCOMPARE(job.waiting(), 2); // nothing else was sent
    };

    {
        // A folder it can't create: setLocation answers 409.
        MoveJob job;
        QbtMoveFeed feed(&m_engine, &job);
        run(readonly + QStringLiteral("/games"), job);
        QCOMPARE(job.entries().constFirst().causeKey, QStringLiteral("cantCreate"));
        job.end();
    }
    {
        // A folder that is there but can't be written: 200, then the log (upstream#120).
        const QString there = readonly + QStringLiteral("/there");
        QFile::setPermissions(
            readonly, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
        QVERIFY(QDir().mkpath(there));
        QVERIFY(QFile::setPermissions(there, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        MoveJob job;
        QbtMoveFeed feed(&m_engine, &job);
        run(there, job);
        const MoveJob::Entry &first = job.entries().constFirst();
        QVERIFY2(first.causeKey == QLatin1String("noAccess") ||
                     first.causeKey == QLatin1String("readOnly"),
                 qPrintable(first.causeKey + QStringLiteral(": ") + first.reason));
        QVERIFY(first.line.startsWith(QStringLiteral("Failed to move torrent")));
        job.end();
        QFile::setPermissions(
            there, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    }
    // Every torrent is still whole at the old place.
    for (const QString &hash : std::as_const(m_hashes)) {
        const Torrent &torrent = m_engine.torrents()->torrent(hash);
        QVERIFY2(torrent.savePath.startsWith(m_other + QStringLiteral("/games")),
                 qPrintable(torrent.savePath));
        QVERIFY(QFile::exists(torrent.savePath + QLatin1Char('/') + torrent.name));
    }
}

QTEST_GUILESS_MAIN(TestMoveJobLive)

#include "tst_movejob_live.moc"
