// FreeSkippedJob when qBittorrent itself holds the skipped files (#118, #210), without a server:
// a fake engine answers for a stopped torrent of three skipped files, and a pretend /proc says
// qbittorrent-nox has them open. The job starts and stops the torrent once, the stop only once
// the torrent reads as started: a stop sent before libtorrent resumed it pauses nothing, and the
// files stay open. Then qBittorrent lets go and the files go. If it never lets go, they stay,
// named.
// tst_skipped_ntfs3_live does the same on a real disk against the playground.

#include "core/SkippedJobs.hpp"
#include "core/engines/EngineClient.hpp"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace omaseed::core;

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
const QString kHash = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

/// A reply that settles with `value` on the next turn of the event loop.
template<class T>
Reply<T> *later(QObject *parent, Result<T> value)
{
    auto *reply = new Reply<T>(parent);
    QTimer::singleShot(0, reply, [reply, value]() { reply->complete(value); });
    return reply;
}

#define OMASEED_FAKE(type, signature)                                                              \
    Reply<type> *signature override                                                                \
    {                                                                                              \
        return unsupported<type>(QStringLiteral(#signature));                                      \
    }

/// The torrent: stopped, a.bin wanted, b.bin, c.bin and d.bin skipped. `calls` records the
/// start, its taking effect ("started"), the stop and the files calls in order; `onStop` runs
/// when a stop pauses it.
class FakeEngine : public EngineClient
{
public:
    using EngineClient::EngineClient;

    QStringList calls;
    std::function<void()> onStop;

    QUrl baseUrl() const override { return {}; }
    void setBaseUrl(const QUrl &) override {}
    void setApiKey(const QString &) override {}
    bool hasApiKey() const override { return false; }
    void setCredentials(const QString &, const QString &) override {}
    void clearAuth() override {}
    void allowLoginAgain() override {}
    void setPinnedCertificate(const QString &) override {}
    void setTimeout(std::chrono::milliseconds) override {}
    int maxRetries() const override { return 0; }
    void setMaxRetries(int) override {}
    void cancelAll() override {}
    QUrl reachableUrl(const QUrl &base) const override { return base; }
    EngineFeatures features(const ServerInfo &) const override { return {}; }

    Reply<QList<TorrentPatch>> *torrents(const QStringList &) override
    {
        // libtorrent resumes it a few reads after the start is answered.
        if (m_startingIn > 0 && --m_startingIn == 0) {
            m_resumed = true;
            calls.append(QStringLiteral("started"));
        }
        TorrentStatus status;
        status.phase = m_resumed ? TorrentPhase::Downloading : TorrentPhase::Stopped;
        return later<QList<TorrentPatch>>(this,
                                          QList<TorrentPatch>{TorrentPatch(kHash).set(
                                              &Torrent::status, TorrentField::Status, status)});
    }
    Reply<QList<ContentFile>> *files(const QString &) override
    {
        calls.append(QStringLiteral("files"));
        QList<ContentFile> list;
        int index = 0;
        for (const char *name : {"a.bin", "b.bin", "c.bin", "d.bin"}) {
            ContentFile file;
            file.index = index;
            file.path = QStringLiteral("t/") + QLatin1String(name);
            file.size = 16 * kMiB;
            file.priority = index == 0 ? int(FilePriority::Normal) : int(FilePriority::Skip);
            file.pieces = std::pair{index * 16, index * 16 + 15};
            list.append(file);
            ++index;
        }
        return later<QList<ContentFile>>(this, list);
    }
    Reply<QByteArray> *pieceStates(const QString &) override
    {
        return later<QByteArray>(this, QByteArray());
    }
    Reply<Done> *start(const QStringList &) override
    {
        calls.append(QStringLiteral("start"));
        m_startingIn = 3;
        return later<Done>(this, Done{});
    }
    Reply<Done> *stop(const QStringList &) override
    {
        calls.append(QStringLiteral("stop"));
        // A stop before libtorrent resumed it pauses nothing: the files stay open.
        if (m_resumed && onStop)
            onStop();
        m_resumed = false;
        m_startingIn = 0;
        return later<Done>(this, Done{});
    }

    OMASEED_FAKE(ServerInfo, probe())
    OMASEED_FAKE(SyncDelta, sync(int))
    OMASEED_FAKE(QStringList, stoppedAmong(const QStringList &))
    OMASEED_FAKE(QList<RunningTorrent>, running())
    OMASEED_FAKE(QList<CategoryPatch>, categories())
    OMASEED_FAKE(QStringList, tags())
    OMASEED_FAKE(TorrentProperties, properties(const QString &))
    OMASEED_FAKE(QList<TrackerEntry>, trackers(const QString &))
    OMASEED_FAKE(PeersDelta, peers(const QString &, int))
    OMASEED_FAKE(QStringList, webSeeds(const QString &))
    OMASEED_FAKE(Done, startAll())
    OMASEED_FAKE(Done, stopAll())
    OMASEED_FAKE(Done, setForceStart(const QStringList &, bool))
    OMASEED_FAKE(Done, recheck(const QStringList &))
    OMASEED_FAKE(Done, reannounce(const QStringList &))
    OMASEED_FAKE(Done, rename(const QString &, const QString &))
    OMASEED_FAKE(Done, setLocation(const QStringList &, const QString &))
    OMASEED_FAKE(Done, renameFile(const QString &, const QString &, const QString &))
    OMASEED_FAKE(Done, renameFolder(const QString &, const QString &, const QString &))
    OMASEED_FAKE(Done, setFilePriority(const QString &, const QList<int> &, FilePriority))
    OMASEED_FAKE(Done, queueUp(const QStringList &))
    OMASEED_FAKE(Done, queueDown(const QStringList &))
    OMASEED_FAKE(Done, toggleSequentialDownload(const QStringList &))
    OMASEED_FAKE(Done, toggleFirstLastPiece(const QStringList &))
    OMASEED_FAKE(Done, setSuperSeeding(const QStringList &, bool))
    OMASEED_FAKE(Done, setAutoManagement(const QStringList &, bool))
    OMASEED_FAKE(Done, remove(const QStringList &, bool))
    OMASEED_FAKE(Done, addTrackers(const QString &, const QStringList &))
    OMASEED_FAKE(Done, removeTrackers(const QString &, const QStringList &))
    OMASEED_FAKE(Done, addWebSeeds(const QString &, const QStringList &))
    OMASEED_FAKE(Done, removeWebSeeds(const QString &, const QStringList &))
    OMASEED_FAKE(Done, addPeers(const QStringList &, const QStringList &))
    OMASEED_FAKE(Done, banPeers(const QStringList &))
    OMASEED_FAKE(Done, setCategory(const QStringList &, const QString &))
    OMASEED_FAKE(Done, createCategory(const QString &, const QString &))
    OMASEED_FAKE(Done, removeCategories(const QStringList &))
    OMASEED_FAKE(Done, addTags(const QStringList &, const QStringList &))
    OMASEED_FAKE(Done, removeTags(const QStringList &, const QStringList &))
    OMASEED_FAKE(Done, createTags(const QStringList &))
    OMASEED_FAKE(Done, deleteTags(const QStringList &))
    OMASEED_FAKE(Done, setDownloadLimit(const QStringList &, qint64))
    OMASEED_FAKE(Done, setUploadLimit(const QStringList &, qint64))
    OMASEED_FAKE(Done,
                 setShareLimits(const QStringList &,
                                RatioLimit,
                                MinutesLimit,
                                MinutesLimit,
                                const std::optional<QString> &))
    OMASEED_FAKE(GlobalLimits, globalLimits())
    OMASEED_FAKE(Done, setGlobalLimits(const GlobalLimits &))
    OMASEED_FAKE(Done, toggleAltSpeed())
    OMASEED_FAKE(Done, setAltSpeed(bool))
    OMASEED_FAKE(AddResult, addUrls(const QStringList &, const AddOptions &))
    OMASEED_FAKE(AddResult, addFile(const QString &, const QByteArray &, const AddOptions &))
    OMASEED_FAKE(AddResult,
                 addMany(const QList<AddFile> &, const QStringList &, const AddOptions &))

private:
    int m_startingIn = 0; ///< reads left until a start takes effect
    bool m_resumed = false;
};

} // namespace

class TestFreeSkippedJob : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void theStopWaitsForTheStartAndTheFilesGo();
    void filesQbittorrentNeverLetsGoStayNamed();

private:
    FreeSkippedJob::Input input();
    /// The pretend /proc: one process, qbittorrent-nox, with the three skipped files open.
    void holdOpen();
    void letGo();

    QTemporaryDir m_dir;
    QString m_root; ///< the save path; the torrent's content is m_root + "t"
    QString m_proc;
    QStringList m_removed;
};

void TestFreeSkippedJob::init()
{
    QVERIFY(m_dir.isValid());
    m_root = m_dir.filePath(QStringLiteral("save")) + QLatin1Char('/');
    m_proc = m_dir.filePath(QStringLiteral("proc"));
    QDir().mkpath(m_root + QStringLiteral("t"));
    m_removed.clear();
    holdOpen();
}

FreeSkippedJob::Input TestFreeSkippedJob::input()
{
    FreeSkippedJob::Input in;
    in.hash = kHash;
    in.name = QStringLiteral("t");
    in.running = false;
    in.torrentSize = 64 * kMiB;
    const QString root = m_root;
    in.localPathOf = [root](const QString &path) { return root + path; };
    in.contentLocal = m_root + QStringLiteral("t");
    in.procRoot = m_proc;
    in.reader = [](const QString &) { return 16 * kMiB; }; // created at full length (#116)
    in.remove = [this](const QString &path) {
        m_removed.append(path);
        return true;
    };
    in.pollMs = 10;
    in.stopTimeoutMs = 400;
    return in;
}

void TestFreeSkippedJob::holdOpen()
{
    const QString fd = m_proc + QStringLiteral("/4242/fd");
    QVERIFY(QDir().mkpath(fd));
    QFile comm(m_proc + QStringLiteral("/4242/comm"));
    QVERIFY(comm.open(QIODevice::WriteOnly));
    comm.write("qbittorrent-nox\n");
    int n = 10;
    for (const char *name : {"b.bin", "c.bin", "d.bin"}) {
        const QString file = m_root + QStringLiteral("t/") + QLatin1String(name);
        QFile touch(file);
        QVERIFY(touch.open(QIODevice::WriteOnly));
        QFile::remove(fd + QLatin1Char('/') + QString::number(n));
        QVERIFY(QFile::link(file, fd + QLatin1Char('/') + QString::number(n++)));
    }
}

void TestFreeSkippedJob::letGo()
{
    QDir(m_proc + QStringLiteral("/4242/fd")).removeRecursively();
}

void TestFreeSkippedJob::theStopWaitsForTheStartAndTheFilesGo()
{
    FakeEngine engine;
    // The start takes effect three reads after its answer; only a stop after that lets go.
    engine.onStop = [this]() { letGo(); };
    FreeSkippedJob job(&engine);
    QSignalSpy finished(&job, &FreeSkippedJob::finished);
    job.start(input());
    QVERIFY(finished.wait(5000));
    const auto outcome = finished.first().first().value<FreeSkippedJob::Outcome>();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QVERIFY2(outcome.openBy.isEmpty(), qPrintable(outcome.openBy.join(QLatin1Char(','))));
    QCOMPARE(outcome.deleted, 3);
    QCOMPARE(outcome.kept, 0);
    QCOMPARE(m_removed.size(), 3);
    // One cycle, the stop once the start took effect.
    QCOMPARE(engine.calls.count(QStringLiteral("start")), 1);
    QCOMPARE(engine.calls.count(QStringLiteral("stop")), 1);
    QVERIFY(engine.calls.indexOf(QStringLiteral("start")) <
            engine.calls.indexOf(QStringLiteral("started")));
    QVERIFY(engine.calls.indexOf(QStringLiteral("started")) <
            engine.calls.indexOf(QStringLiteral("stop")));
}

void TestFreeSkippedJob::filesQbittorrentNeverLetsGoStayNamed()
{
    FakeEngine engine;
    FreeSkippedJob job(&engine);
    QSignalSpy finished(&job, &FreeSkippedJob::finished);
    job.start(input());
    QVERIFY(finished.wait(5000));
    const auto outcome = finished.first().first().value<FreeSkippedJob::Outcome>();
    QVERIFY2(outcome.ok, qPrintable(outcome.error));
    QCOMPARE(outcome.deleted, 0);
    QCOMPARE(outcome.kept, 3);
    QCOMPARE(outcome.openBy, QStringList(3, QStringLiteral("qbittorrent-nox")));
    QVERIFY(m_removed.isEmpty());
    QCOMPARE(engine.calls.count(QStringLiteral("start")), 1);
}

QTEST_GUILESS_MAIN(TestFreeSkippedJob)

#include "tst_freeskippedjob.moc"
