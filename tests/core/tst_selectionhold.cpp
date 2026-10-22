#include "core/SelectionHold.hpp"

#include <QSignalSpy>
#include <QTest>

using omaseed::core::SelectionHold;

namespace {

constexpr qint64 kMiB = 1024LL * 1024;
constexpr qint64 kGiB = 1024 * kMiB;

/// Board 44's torrent, small: one folder wanted (0, 1), the rest skipped.
QHash<int, int> priorities()
{
    return {{0, 1}, {1, 1}, {2, 0}, {3, 0}, {4, 0}};
}

QHash<int, qint64> sizes()
{
    return {{0, 4 * kGiB}, {1, 4 * kGiB}, {2, 600 * kMiB}, {3, 600 * kMiB}, {4, 10 * kMiB}};
}

} // namespace

class TestSelectionHold : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void whatWaits();
    void aTickThatDoesntFitWaits();
    void changesMergeAndTheNetIsSent();
    void aChangeThatNetsToZeroSendsNothing();
    void undoGivesBackTheServersPriorities();
    void theWindowRestartsAndRunsOut();
    void anotherTorrentStartsAgain();
};

void TestSelectionHold::whatWaits()
{
    // D6: more than 1 GiB newly wanted.
    QVERIFY(SelectionHold::mustWait(priorities(), sizes(), {2, 3}, 1));
    // 600 MiB, and files left skipped: sent at once.
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {2}, 1));
    // The whole torrent, however small.
    QVERIFY(SelectionHold::mustWait(priorities(), sizes(), {0, 1, 2, 3, 4}, 6));
    // A skip never waits (D5), nor a change among files already wanted.
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {0, 1, 2, 3, 4}, 0));
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {0, 1}, 7));
}

// DECISIONS C28 (#159): on a disk that takes every ticked file's full size at once, a widening
// that doesn't fit waits whatever its size; one that fits, or any change elsewhere, as D5 says.
void TestSelectionHold::aTickThatDoesntFitWaits()
{
    const qint64 sixHundred = sizes().value(2);
    QCOMPARE(SelectionHold::widening(priorities(), sizes(), {2}, 1), sixHundred);
    QCOMPARE(SelectionHold::widening(priorities(), sizes(), {0, 2}, 1), sixHundred);
    QCOMPARE(SelectionHold::widening(priorities(), sizes(), {2}, 0), qint64(0));
    // 600 MiB with less free: it waits.
    QVERIFY(SelectionHold::mustWait(priorities(), sizes(), {2}, 1, sixHundred - 1));
    QVERIFY(SelectionHold::mustWait(priorities(), sizes(), {2}, 1, 0));
    // Exactly enough, or more: sent at once.
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {2}, 1, sixHundred));
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {2}, 1, 10 * sixHundred));
    // A disk that allocates as it writes (-1): as before.
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {2}, 1, -1));
    // A skip, or a change among files already wanted, never takes room.
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {2}, 0, 0));
    QVERIFY(!SelectionHold::mustWait(priorities(), sizes(), {0, 1}, 7, 0));
}

void TestSelectionHold::changesMergeAndTheNetIsSent()
{
    SelectionHold hold;
    hold.add(QStringLiteral("h"), {2, 3}, 1, priorities(), sizes());
    QVERIFY(hold.waiting());
    QCOMPARE(hold.addedFiles(), 2);
    QCOMPARE(hold.addedBytes(), 1200 * kMiB);
    // While it waits, file 4 is wanted too and file 3 raised: they join it.
    QHash<int, int> now = priorities();
    now[2] = 1;
    now[3] = 1;
    hold.add(QStringLiteral("h"), {3, 4}, 7, now, sizes());
    const auto net = hold.net();
    QCOMPARE(net.value(1), QList<int>{2});
    QCOMPARE(net.value(7), (QList<int>{3, 4}));
    QCOMPARE(hold.addedFiles(), 3);
    // A skip that joins it: file 2 back to skip is no change for 2.
    now[3] = 7;
    now[4] = 7;
    hold.add(QStringLiteral("h"), {2}, 0, now, sizes());
    QVERIFY(!hold.net().contains(1));
    QCOMPARE(hold.take().value(7), (QList<int>{3, 4}));
    QVERIFY(!hold.waiting());
}

void TestSelectionHold::aChangeThatNetsToZeroSendsNothing()
{
    SelectionHold hold;
    QSignalSpy changed(&hold, &SelectionHold::changed);
    hold.add(QStringLiteral("h"), {0, 1, 2, 3, 4}, 1, priorities(), sizes());
    QVERIFY(hold.waiting());
    QHash<int, int> now{{0, 1}, {1, 1}, {2, 1}, {3, 1}, {4, 1}};
    // The misclick put right by hand: the skipped files ticked off again.
    hold.add(QStringLiteral("h"), {2, 3, 4}, 0, now, sizes());
    QVERIFY(!hold.waiting());
    QVERIFY(hold.take().isEmpty());
    QCOMPARE(changed.count(), 2);
}

void TestSelectionHold::undoGivesBackTheServersPriorities()
{
    SelectionHold hold;
    hold.add(QStringLiteral("h"), {0, 1, 2, 3, 4}, 6, priorities(), sizes());
    const auto back = hold.originals();
    QCOMPARE(back.value(1), (QList<int>{0, 1}));
    QCOMPARE(back.value(0), (QList<int>{2, 3, 4}));
    hold.clear();
    QVERIFY(!hold.waiting());
    QVERIFY(hold.net().isEmpty());
}

void TestSelectionHold::theWindowRestartsAndRunsOut()
{
    SelectionHold hold;
    hold.setWindowMs(200);
    QSignalSpy due(&hold, &SelectionHold::due);
    hold.add(QStringLiteral("h"), {2, 3}, 1, priorities(), sizes());
    const QDateTime first = hold.deadline();
    QTest::qWait(120);
    QHash<int, int> now = priorities();
    now[2] = 1;
    now[3] = 1;
    hold.add(QStringLiteral("h"), {4}, 1, now, sizes());
    QVERIFY(hold.deadline() > first);
    QTest::qWait(120);
    QCOMPARE(due.count(), 0); // restarted by the second change
    QVERIFY(due.wait(1000));
    QVERIFY(hold.waiting()); // App takes it
}

void TestSelectionHold::anotherTorrentStartsAgain()
{
    SelectionHold hold;
    hold.add(QStringLiteral("a"), {2, 3}, 1, priorities(), sizes());
    hold.add(QStringLiteral("b"), {2, 3, 4}, 1, priorities(), sizes());
    QCOMPARE(hold.hash(), QStringLiteral("b"));
    QCOMPARE(hold.net().value(1), (QList<int>{2, 3, 4}));
}

QTEST_GUILESS_MAIN(TestSelectionHold)

#include "tst_selectionhold.moc"
