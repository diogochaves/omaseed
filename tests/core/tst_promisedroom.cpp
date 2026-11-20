// DECISIONS C47 I10: the room downloads in progress still take, summed by drive, where omaseed
// can tell the drive.

#include "core/PromisedRoom.hpp"

#include <QTest>

using omaseed::core::Torrent;
using omaseed::core::TorrentFlag;
using omaseed::core::TorrentPhase;
namespace PromisedRoom = omaseed::core::PromisedRoom;

namespace {

Torrent torrent(const QString &hash,
                TorrentPhase phase,
                qint64 left,
                const QString &savePath = QStringLiteral("/home/d/Downloads"),
                const QString &downloadPath = QString())
{
    Torrent t;
    t.hash = hash;
    t.status.phase = phase;
    t.amountLeft = left;
    t.savePath = savePath;
    t.downloadPath = downloadPath;
    return t;
}

/// /home is drive 1, /mnt/media drive 2, /mnt/away is not seen.
quint64 deviceOf(const QString &path)
{
    if (path.startsWith(QLatin1String("/home")))
        return 1;
    if (path.startsWith(QLatin1String("/mnt/media")))
        return 2;
    return 0;
}

} // namespace

class TestPromisedRoom : public QObject
{
    Q_OBJECT

private slots:
    void downloadingAndQueuedCount();
    void stoppedAndCompleteDont();
    void anIncompleteFolderIsWhereItWrites();
    void anUnseenDriveIsLeftOut();
    void twoDrivesTwoSums();
};

void TestPromisedRoom::downloadingAndQueuedCount()
{
    Torrent queued = torrent(QStringLiteral("b"), TorrentPhase::Downloading, 300);
    queued.status.flags = TorrentFlag::Queued;
    Torrent stalled = torrent(QStringLiteral("c"), TorrentPhase::Downloading, 50);
    stalled.status.flags = TorrentFlag::Stalled;
    const auto promised = PromisedRoom::byDevice(
        {torrent(QStringLiteral("a"), TorrentPhase::Downloading, 1000), queued, stalled}, deviceOf);
    QCOMPARE(promised.size(), 1);
    QCOMPARE(promised.value(1), (PromisedRoom::Promise{1350, 3}));
}

void TestPromisedRoom::stoppedAndCompleteDont()
{
    Torrent queuedSeed = torrent(QStringLiteral("d"), TorrentPhase::Downloading, 10);
    queuedSeed.status.flags = TorrentFlag::Finished;
    const auto promised =
        PromisedRoom::byDevice({torrent(QStringLiteral("a"), TorrentPhase::Stopped, 1000),
                                torrent(QStringLiteral("b"), TorrentPhase::Seeding, 0),
                                torrent(QStringLiteral("c"), TorrentPhase::Downloading, 0),
                                torrent(QStringLiteral("e"), TorrentPhase::Errored, 500),
                                queuedSeed},
                               deviceOf);
    QVERIFY(promised.isEmpty());
}

void TestPromisedRoom::anIncompleteFolderIsWhereItWrites()
{
    const Torrent t = torrent(QStringLiteral("a"),
                              TorrentPhase::Downloading,
                              700,
                              QStringLiteral("/mnt/media/films"),
                              QStringLiteral("/home/d/temp"));
    QCOMPARE(PromisedRoom::writesTo(t), QStringLiteral("/home/d/temp"));
    const auto promised = PromisedRoom::byDevice({t}, deviceOf);
    QCOMPARE(promised.value(1), (PromisedRoom::Promise{700, 1}));
    QVERIFY(!promised.contains(2));
}

void TestPromisedRoom::anUnseenDriveIsLeftOut()
{
    const auto promised = PromisedRoom::byDevice(
        {torrent(QStringLiteral("a"), TorrentPhase::Downloading, 900, QStringLiteral("/mnt/away"))},
        deviceOf);
    QVERIFY(promised.isEmpty());
}

void TestPromisedRoom::twoDrivesTwoSums()
{
    int reads = 0;
    const auto counted = [&reads](const QString &path) {
        ++reads;
        return deviceOf(path);
    };
    const auto promised = PromisedRoom::byDevice(
        {torrent(QStringLiteral("a"), TorrentPhase::Downloading, 100),
         torrent(QStringLiteral("b"), TorrentPhase::Downloading, 200),
         torrent(QStringLiteral("c"), TorrentPhase::Allocating, 40, QStringLiteral("/mnt/media")),
         torrent(QStringLiteral("d"), TorrentPhase::Downloading, 60, QStringLiteral("/mnt/media"))},
        counted);
    QCOMPARE(promised.value(1), (PromisedRoom::Promise{300, 2}));
    QCOMPARE(promised.value(2), (PromisedRoom::Promise{100, 2}));
    QCOMPARE(reads, 2); // one reading per folder
}

QTEST_APPLESS_MAIN(TestPromisedRoom)

#include "tst_promisedroom.moc"
