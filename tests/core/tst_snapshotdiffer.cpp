// SnapshotDiffer (#85, E4; C14 E2 a): an engine's whole objects in, maindata-shaped deltas out.
// Only the fields that moved, the torrents that went, and the labels and tracker URLs that
// came or went with them.

#include "core/engines/SnapshotDiffer.hpp"

#include <QTest>

using namespace omaseed::core;

namespace {

SnapshotDiffer::Entry entry(const QString &hash,
                            const QString &name,
                            double progress,
                            const QStringList &labels = {},
                            const QStringList &trackers = {})
{
    SnapshotDiffer::Entry e;
    e.patch = TorrentPatch(hash);
    e.patch.set(&Torrent::name, TorrentField::Name, name);
    e.patch.set(&Torrent::progress, TorrentField::Progress, progress);
    e.patch.set(&Torrent::tags, TorrentField::Tags, labels);
    e.trackers = trackers;
    return e;
}

const QString kA = QStringLiteral("aaaa");
const QString kB = QStringLiteral("bbbb");
const QString kC = QStringLiteral("cccc");
const QString kT1 = QStringLiteral("http://one/announce");
const QString kT2 = QStringLiteral("http://two/announce");

} // namespace

class TestSnapshotDiffer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void fullHandsOutEverything();
    void someGivesOnlyWhatMoved();
    void removedGoesWithItsLabelsAndTrackers();
    void everythingNotFullDiffsAndFindsTheGone();
    void labelsComeAndGoWithTheTorrents();
    void trackersMoveWithTheTorrents();
    void unknownRemovalsAndEmptyHashesAreIgnored();
    void clearStartsOver();
};

void TestSnapshotDiffer::fullHandsOutEverything()
{
    SnapshotDiffer differ;
    const SyncDelta delta = differ.everything({entry(kA, QStringLiteral("a"), 0.5, {"x"}, {kT1}),
                                               entry(kB, QStringLiteral("b"), 1.0, {"y", "x"})},
                                              true);
    QVERIFY(delta.full);
    QCOMPARE(delta.torrents.size(), 2);
    QCOMPARE(delta.torrents.at(0).hash, kA);
    QVERIFY(delta.torrents.at(0).has(TorrentField::Name));
    QCOMPARE(delta.tags, QStringList({QStringLiteral("x"), QStringLiteral("y")}));
    QCOMPARE(delta.trackers.value(kT1), QStringList({kA}));
    QVERIFY(delta.removedTorrents.isEmpty());
    QCOMPARE(differ.hashes(), QStringList({kA, kB}));
}

void TestSnapshotDiffer::someGivesOnlyWhatMoved()
{
    SnapshotDiffer differ;
    differ.everything({entry(kA, QStringLiteral("a"), 0.5), entry(kB, QStringLiteral("b"), 0.1)},
                      true);
    // A whole object again, only its progress moved: a patch with that field alone.
    const SyncDelta delta = differ.some({entry(kA, QStringLiteral("a"), 0.75)}, {});
    QVERIFY(!delta.full);
    QCOMPARE(delta.torrents.size(), 1);
    QCOMPARE(delta.torrents.at(0).hash, kA);
    QCOMPARE(delta.torrents.at(0).fields, TorrentFields({TorrentField::Progress}));
    QCOMPARE(delta.torrents.at(0).values.progress, 0.75);
    // Nothing moved: nothing handed out.
    QVERIFY(differ.some({entry(kA, QStringLiteral("a"), 0.75)}, {}).torrents.isEmpty());
    // A new torrent comes whole.
    const SyncDelta added = differ.some({entry(kC, QStringLiteral("c"), 0.0)}, {});
    QCOMPARE(added.torrents.size(), 1);
    QVERIFY(added.torrents.at(0).has(TorrentField::Name));
    QCOMPARE(differ.torrent(kC).name, QStringLiteral("c"));
}

void TestSnapshotDiffer::removedGoesWithItsLabelsAndTrackers()
{
    SnapshotDiffer differ;
    differ.everything({entry(kA, QStringLiteral("a"), 1, {"only-a"}, {kT1}),
                       entry(kB, QStringLiteral("b"), 1, {"shared"}, {kT2})},
                      true);
    const SyncDelta delta = differ.some({}, {kA});
    QCOMPARE(delta.removedTorrents, QStringList({kA}));
    QCOMPARE(delta.removedTags, QStringList({QStringLiteral("only-a")}));
    QCOMPARE(delta.removedTrackers, QStringList({kT1}));
    QVERIFY(!differ.knows(kA));
}

void TestSnapshotDiffer::everythingNotFullDiffsAndFindsTheGone()
{
    SnapshotDiffer differ;
    differ.everything({entry(kA, QStringLiteral("a"), 0.5),
                       entry(kB, QStringLiteral("b"), 0.5),
                       entry(kC, QStringLiteral("c"), 0.5)},
                      true);
    // After a gap: every torrent again, but as a partial delta.
    const SyncDelta delta = differ.everything(
        {entry(kA, QStringLiteral("a"), 0.5), entry(kC, QStringLiteral("c renamed"), 0.5)}, false);
    QVERIFY(!delta.full);
    QCOMPARE(delta.removedTorrents, QStringList({kB}));
    QCOMPARE(delta.torrents.size(), 1);
    QCOMPARE(delta.torrents.at(0).hash, kC);
    QCOMPARE(delta.torrents.at(0).fields, TorrentFields({TorrentField::Name}));
}

void TestSnapshotDiffer::labelsComeAndGoWithTheTorrents()
{
    SnapshotDiffer differ;
    differ.everything(
        {entry(kA, QStringLiteral("a"), 0, {"x"}), entry(kB, QStringLiteral("b"), 0, {"x"})}, true);
    // kA trades x for y: y comes; x stays (kB has it).
    SyncDelta delta = differ.some({entry(kA, QStringLiteral("a"), 0, {"y"})}, {});
    QCOMPARE(delta.tags, QStringList({QStringLiteral("y")}));
    QVERIFY(delta.removedTags.isEmpty());
    QCOMPARE(delta.torrents.at(0).fields, TorrentFields({TorrentField::Tags}));
    // kB drops x: no torrent has it, so it goes (Transmission keeps no empty label).
    delta = differ.some({entry(kB, QStringLiteral("b"), 0, {})}, {});
    QVERIFY(delta.tags.isEmpty());
    QCOMPARE(delta.removedTags, QStringList({QStringLiteral("x")}));
}

void TestSnapshotDiffer::trackersMoveWithTheTorrents()
{
    SnapshotDiffer differ;
    differ.everything({entry(kA, QStringLiteral("a"), 0, {}, {kT1}),
                       entry(kB, QStringLiteral("b"), 0, {}, {kT1})},
                      true);
    // kA moves from one to two: both URLs' lists moved.
    const SyncDelta delta = differ.some({entry(kA, QStringLiteral("a"), 0, {}, {kT2})}, {});
    QCOMPARE(delta.trackers.value(kT1), QStringList({kB}));
    QCOMPARE(delta.trackers.value(kT2), QStringList({kA}));
    QVERIFY(delta.removedTrackers.isEmpty());
    QVERIFY(delta.torrents.isEmpty()); // no field of kA's moved
    const SyncDelta gone = differ.some({entry(kB, QStringLiteral("b"), 0, {}, {})}, {});
    QCOMPARE(gone.removedTrackers, QStringList({kT1}));
}

void TestSnapshotDiffer::unknownRemovalsAndEmptyHashesAreIgnored()
{
    SnapshotDiffer differ;
    differ.everything({entry(kA, QStringLiteral("a"), 0)}, true);
    SnapshotDiffer::Entry nameless = entry(QString(), QStringLiteral("?"), 0);
    const SyncDelta delta = differ.some({nameless}, {kB});
    QVERIFY(delta.removedTorrents.isEmpty());
    QVERIFY(delta.torrents.isEmpty());
    QCOMPARE(differ.hashes(), QStringList({kA}));
}

void TestSnapshotDiffer::clearStartsOver()
{
    SnapshotDiffer differ;
    differ.everything({entry(kA, QStringLiteral("a"), 0, {"x"}, {kT1})}, true);
    differ.clear();
    QVERIFY(differ.isEmpty());
    const SyncDelta delta = differ.some({entry(kA, QStringLiteral("a"), 0, {"x"})}, {});
    QCOMPARE(delta.torrents.size(), 1);
    QVERIFY(delta.torrents.at(0).has(TorrentField::Name)); // new again: whole
    QCOMPARE(delta.tags, QStringList({QStringLiteral("x")}));
}

QTEST_GUILESS_MAIN(TestSnapshotDiffer)
#include "tst_snapshotdiffer.moc"
