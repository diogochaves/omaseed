// SkippedOutlook: what "Free the space…" can do, from the Files tab's reading alone (board 50
// §3, DECISIONS C30 F12–F16), and the size line (§4, F17–F18).

#include "core/SkippedOutlook.hpp"

#include <QTest>

using namespace omaseed::core;

namespace {

constexpr qint64 kMiB = 1024 * 1024;

SkippedFile file(int index, qint64 allocated, bool pieces)
{
    SkippedFile f;
    f.index = index;
    f.path = QStringLiteral("extras/%1.mkv").arg(index);
    f.localPath = QStringLiteral("/data/t/extras/%1.mkv").arg(index);
    f.size = allocated;
    f.allocated = allocated;
    f.holdsPieces = pieces;
    f.progress = pieces ? 1.0 : 0.0;
    return f;
}

SkippedReading reading(const QList<SkippedFile> &files, bool seen = true)
{
    SkippedReading r;
    r.hash = QStringLiteral("aa");
    r.seen = seen;
    r.files = files;
    for (const SkippedFile &f : files) {
        if (f.holdsSpace()) {
            r.held += f.allocated;
            ++r.holding;
        }
    }
    return r;
}

} // namespace

class TestSkippedOutlook : public QObject
{
    Q_OBJECT

private slots:
    void nothingHeldIsNone();
    void anUnseenDiskIsNone();
    void v1FreesEverything();
    void aHybridWithOnlyPiecesIsReadd();
    void aV2OnlyTorrentIsAHybridHere();
    void someOfEachIsMixed();
    void kindNames();
    void theSizeTailOnASeenDisk();
    void theSizeTailOnAnUnseenDisk();
};

void TestSkippedOutlook::nothingHeldIsNone()
{
    const SkippedOutlook o = SkippedOutlook::of(reading({file(0, 0, true)}), HashVersion::Hybrid);
    QCOMPARE(o.kind, SkippedOutlook::Kind::None);
    QCOMPARE(SkippedOutlook::of(reading({}), HashVersion::V1).kind, SkippedOutlook::Kind::None);
}

void TestSkippedOutlook::anUnseenDiskIsNone()
{
    // C17 D4: no claim about a disk omaseed can't read.
    const SkippedOutlook o =
        SkippedOutlook::of(reading({file(0, 8 * kMiB, false)}, false), HashVersion::V1);
    QCOMPARE(o.kind, SkippedOutlook::Kind::None);
    QCOMPARE(o.freeable, 0);
}

void TestSkippedOutlook::v1FreesEverything()
{
    // C17 D3: on v1-only a file with pieces goes too, with a recheck.
    const SkippedOutlook o = SkippedOutlook::of(
        reading({file(0, 8 * kMiB, true), file(1, 4 * kMiB, false)}), HashVersion::V1);
    QCOMPARE(o.kind, SkippedOutlook::Kind::Free);
    QCOMPARE(o.freeable, 12 * kMiB);
    QCOMPARE(o.readdOnly, 0);
}

void TestSkippedOutlook::aHybridWithOnlyPiecesIsReadd()
{
    // F12: every file that holds space has downloaded pieces.
    const SkippedOutlook o = SkippedOutlook::of(
        reading({file(0, 620 * kMiB, true), file(1, 410 * kMiB, true), file(2, 0, false)}),
        HashVersion::Hybrid);
    QCOMPARE(o.kind, SkippedOutlook::Kind::Readd);
    QCOMPARE(o.readdOnly, 1030 * kMiB);
    QCOMPARE(o.readdFiles, 2);
    QCOMPARE(o.freeable, 0);
    QCOMPARE(o.version, HashVersion::Hybrid);
}

void TestSkippedOutlook::aV2OnlyTorrentIsAHybridHere()
{
    // C29: v2-only is a hybrid in the free plan.
    const SkippedOutlook o =
        SkippedOutlook::of(reading({file(0, 8 * kMiB, true)}), HashVersion::V2);
    QCOMPARE(o.kind, SkippedOutlook::Kind::Readd);
    QCOMPARE(o.version, HashVersion::V2);
}

void TestSkippedOutlook::someOfEachIsMixed()
{
    // F15: a third skipped file with no piece can still go.
    const SkippedOutlook o = SkippedOutlook::of(
        reading({file(0, 620 * kMiB, true), file(1, 410 * kMiB, true), file(2, 180 * kMiB, false)}),
        HashVersion::Hybrid);
    QCOMPARE(o.kind, SkippedOutlook::Kind::Mixed);
    QCOMPARE(o.freeable, 180 * kMiB);
    QCOMPARE(o.readdOnly, 1030 * kMiB);
}

void TestSkippedOutlook::kindNames()
{
    QCOMPARE(SkippedOutlook::kindName(SkippedOutlook::Kind::None), QStringLiteral("none"));
    QCOMPARE(SkippedOutlook::kindName(SkippedOutlook::Kind::Free), QStringLiteral("free"));
    QCOMPARE(SkippedOutlook::kindName(SkippedOutlook::Kind::Mixed), QStringLiteral("mixed"));
    QCOMPARE(SkippedOutlook::kindName(SkippedOutlook::Kind::Readd), QStringLiteral("readd"));
}

void TestSkippedOutlook::theSizeTailOnASeenDisk()
{
    // F17 a: "32.0 MB · 2 skipped, 16.0 MB still on disk".
    QCOMPARE(SkippedOutlook::sizeTail(2, 16 * kMiB, true, 16 * kMiB),
             QStringLiteral("2 skipped, 16.0 MB still on disk"));
    QCOMPARE(SkippedOutlook::stillOnDisk(16 * kMiB), QStringLiteral("16.0 MB still on disk"));
    // Nothing held: no tail, whatever was downloaded.
    QCOMPARE(SkippedOutlook::sizeTail(2, 0, true, 16 * kMiB), QString());
    QCOMPARE(SkippedOutlook::stillOnDisk(0), QString());
}

void TestSkippedOutlook::theSizeTailOnAnUnseenDisk()
{
    // F18 a: from torrents/files' progress alone, no claim about the disk.
    QCOMPARE(SkippedOutlook::sizeTail(2, 0, false, 1030 * kMiB),
             QStringLiteral("1.01 GB of it in skipped files"));
    QCOMPARE(SkippedOutlook::sizeTail(2, 0, false, 0), QString());
}

QTEST_GUILESS_MAIN(TestSkippedOutlook)
#include "tst_skippedoutlook.moc"
