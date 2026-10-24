#include "core/ActionOutcome.hpp"

#include <QSignalSpy>
#include <QTest>

using omaseed::core::ActionOutcome;
using omaseed::core::ActionOutcomes;

namespace {

ActionOutcome outcome(const QString &action,
                      const QStringList &hashes,
                      const QStringList &failed = {},
                      const QString &target = {})
{
    ActionOutcome o;
    o.action = action;
    o.hashes = hashes;
    o.failedHashes = failed;
    o.target = target;
    if (hashes.size() == 1)
        o.name = QStringLiteral("Arcade Set 01");
    if (!failed.isEmpty())
        o.failedName = QStringLiteral("Short Film 02");
    o.cause = failed.isEmpty() ? QString() : QStringLiteral("no room on /tmp");
    return o;
}

} // namespace

// Board 45 §2 (DECISIONS C18 D1–D3): the strip's words and the rows' marks.
class TestActionOutcome : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    // O1: one torrent, by name; the row's mark names the folder's last part.
    void aQuickMoveLanded()
    {
        const QVariantMap w = outcome(QStringLiteral("move"),
                                      {QStringLiteral("a")},
                                      {},
                                      QStringLiteral("/srv/library/archive"))
                                  .words();
        QCOMPARE(w.value(QStringLiteral("kind")).toString(), QStringLiteral("done"));
        QCOMPARE(w.value(QStringLiteral("mark")).toString(), QStringLiteral("✓"));
        QCOMPARE(w.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Arcade Set 01 moved to /srv/library/archive"));
        QCOMPARE(w.value(QStringLiteral("line")).toString(), QString());
        QCOMPARE(w.value(QStringLiteral("doneRow")).toString(), QStringLiteral("moved · archive"));
    }

    // O2: a failure names the torrent, and the line says why.
    void aMoveThatFailed()
    {
        ActionOutcome o = outcome(QStringLiteral("move"),
                                  {QStringLiteral("a")},
                                  {QStringLiteral("a")},
                                  QStringLiteral("/tmp/locked/dest"));
        o.failedName = QStringLiteral("Short Film 07");
        o.cause = QStringLiteral("qBittorrent can't write to /tmp/locked/dest");
        const QVariantMap w = o.words();
        QCOMPARE(w.value(QStringLiteral("kind")).toString(), QStringLiteral("failed"));
        QCOMPARE(w.value(QStringLiteral("mark")).toString(), QStringLiteral("✕"));
        QCOMPARE(w.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Short Film 07 didn't move"));
        QCOMPARE(w.value(QStringLiteral("line")).toString(),
                 QStringLiteral("qBittorrent can't write to /tmp/locked/dest"));
        QCOMPARE(w.value(QStringLiteral("failedRow")).toString(), QStringLiteral("didn't move"));
    }

    // O3: counts, never a list; the line names the first that failed.
    void severalAsCounts()
    {
        const QVariantMap w =
            outcome(QStringLiteral("move"),
                    {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                    {QStringLiteral("c")},
                    QStringLiteral("/tmp/fast/keep"))
                .words();
        QCOMPARE(w.value(QStringLiteral("kind")).toString(), QStringLiteral("mixed"));
        QCOMPARE(w.value(QStringLiteral("mark")).toString(), QStringLiteral("!"));
        QCOMPARE(w.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("2 moved to /tmp/fast/keep, 1 didn't"));
        QCOMPARE(w.value(QStringLiteral("line")).toString(),
                 QStringLiteral("Short Film 02: no room on /tmp"));
        QCOMPARE(w.value(QStringLiteral("doneHashes")).toStringList(),
                 QStringList({QStringLiteral("a"), QStringLiteral("b")}));
    }

    void allFailedAsACount()
    {
        const QVariantMap w =
            outcome(QStringLiteral("stop"),
                    {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")},
                    {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")})
                .words();
        QCOMPARE(w.value(QStringLiteral("kind")).toString(), QStringLiteral("failed"));
        QCOMPARE(w.value(QStringLiteral("headline")).toString(), QStringLiteral("3 didn't stop"));
        QCOMPARE(w.value(QStringLiteral("line")).toString(), QStringLiteral("no room on /tmp"));
    }

    // D2: every action on torrents from the table has its words.
    void everyTableAction_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<QString>("target");
        QTest::addColumn<QString>("one");
        QTest::addColumn<QString>("many");
        QTest::addColumn<QString>("row");

        QTest::newRow("start") << QStringLiteral("start") << QStringLiteral("")
                               << QStringLiteral("Arcade Set 01 started")
                               << QStringLiteral("3 started") << QStringLiteral("started");
        QTest::newRow("stop") << QStringLiteral("stop") << QStringLiteral("")
                              << QStringLiteral("Arcade Set 01 stopped")
                              << QStringLiteral("3 stopped") << QStringLiteral("stopped");
        QTest::newRow("recheck") << QStringLiteral("recheck") << QStringLiteral("")
                                 << QStringLiteral("Arcade Set 01 sent to recheck")
                                 << QStringLiteral("3 sent to recheck")
                                 << QStringLiteral("rechecking");
        QTest::newRow("reannounce")
            << QStringLiteral("reannounce") << QStringLiteral("")
            << QStringLiteral("Arcade Set 01 reannounced") << QStringLiteral("3 reannounced")
            << QStringLiteral("reannounced");
        QTest::newRow("category") << QStringLiteral("category") << QStringLiteral("games")
                                  << QStringLiteral("Arcade Set 01 put in games")
                                  << QStringLiteral("3 put in games")
                                  << QStringLiteral("category · games");
        QTest::newRow("no category")
            << QStringLiteral("category") << QStringLiteral("")
            << QStringLiteral("Arcade Set 01 taken out of its category")
            << QStringLiteral("3 taken out of its category") << QStringLiteral("no category");
        QTest::newRow("tags") << QStringLiteral("addTags") << QStringLiteral("keep")
                              << QStringLiteral("Arcade Set 01 tagged keep")
                              << QStringLiteral("3 tagged keep") << QStringLiteral("tagged · keep");
        QTest::newRow("rename") << QStringLiteral("rename") << QStringLiteral("Arcade 1")
                                << QStringLiteral("Arcade Set 01 renamed to Arcade 1")
                                << QStringLiteral("3 renamed to Arcade 1")
                                << QStringLiteral("renamed");
        QTest::newRow("back") << QStringLiteral("moveBack") << QStringLiteral("")
                              << QStringLiteral("Arcade Set 01 moved back")
                              << QStringLiteral("3 moved back") << QStringLiteral("moved back");
        QTest::newRow("delete") << QStringLiteral("delete") << QStringLiteral("")
                                << QStringLiteral("Arcade Set 01 deleted")
                                << QStringLiteral("3 deleted") << QStringLiteral("");
        QTest::newRow("sequential")
            << QStringLiteral("sequential") << QStringLiteral("on")
            << QStringLiteral("Arcade Set 01 set to download in order")
            << QStringLiteral("3 set to download in order") << QStringLiteral("in order");
    }

    void everyTableAction()
    {
        QFETCH(QString, action);
        QFETCH(QString, target);
        QFETCH(QString, one);
        QFETCH(QString, many);
        QFETCH(QString, row);
        QCOMPARE(outcome(action, {QStringLiteral("a")}, {}, target)
                     .words()
                     .value(QStringLiteral("headline"))
                     .toString(),
                 one);
        const QVariantMap w =
            outcome(
                action, {QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")}, {}, target)
                .words();
        QCOMPARE(w.value(QStringLiteral("headline")).toString(), many);
        QCOMPARE(w.value(QStringLiteral("doneRow")).toString(), row);
    }

    // D1, D3: the strip, and each row's mark while it lasts.
    void marksTheRows()
    {
        ActionOutcomes outcomes;
        QSignalSpy current(&outcomes, &ActionOutcomes::currentChanged);
        outcomes.report(outcome(QStringLiteral("move"),
                                {QStringLiteral("a"), QStringLiteral("b")},
                                {QStringLiteral("b")},
                                QStringLiteral("/srv/keep")));
        QCOMPARE(current.count(), 1);
        QCOMPARE(outcomes.current().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("mixed"));
        QCOMPARE(outcomes.rowMark(QStringLiteral("a")).value(QStringLiteral("text")).toString(),
                 QStringLiteral("moved · keep"));
        QCOMPARE(outcomes.rowMark(QStringLiteral("a")).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("done"));
        QCOMPARE(outcomes.rowMark(QStringLiteral("b")).value(QStringLiteral("text")).toString(),
                 QStringLiteral("didn't move"));
        QCOMPARE(outcomes.rowMark(QStringLiteral("b")).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("failed"));
        QVERIFY(outcomes.rowMark(QStringLiteral("z")).isEmpty());
    }

    // D3: done for 6 s on the row; a failure until × / esc.
    void aDoneMarkGoesAfterItsTime()
    {
        ActionOutcomes outcomes;
        outcomes.setDoneDuration(std::chrono::milliseconds(50));
        QSignalSpy revision(&outcomes, &ActionOutcomes::revisionChanged);
        outcomes.report(outcome(QStringLiteral("stop"),
                                {QStringLiteral("a"), QStringLiteral("b")},
                                {QStringLiteral("b")}));
        QTRY_VERIFY(outcomes.rowMark(QStringLiteral("a")).isEmpty());
        QCOMPARE(outcomes.rowMark(QStringLiteral("b")).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("failed"));
        QVERIFY(revision.count() >= 2);
        outcomes.dismiss();
        QVERIFY(outcomes.current().isEmpty());
        QVERIFY(outcomes.rowMark(QStringLiteral("b")).isEmpty());
    }

    // D3: the next action takes the strip, and what the last one left on the rows.
    void theNextActionTakesOver()
    {
        ActionOutcomes outcomes;
        outcomes.report(outcome(QStringLiteral("move"),
                                {QStringLiteral("a")},
                                {QStringLiteral("a")},
                                QStringLiteral("/x")));
        const int first = outcomes.current().value(QStringLiteral("serial")).toInt();
        outcomes.report(outcome(QStringLiteral("stop"), {QStringLiteral("c")}));
        QVERIFY(outcomes.rowMark(QStringLiteral("a")).isEmpty());
        QCOMPARE(outcomes.rowMark(QStringLiteral("c")).value(QStringLiteral("text")).toString(),
                 QStringLiteral("stopped"));
        // The first strip's time running out no longer takes the second away.
        outcomes.expire(first);
        QCOMPARE(outcomes.current().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Arcade Set 01 stopped"));
        outcomes.expire(outcomes.current().value(QStringLiteral("serial")).toInt());
        QVERIFY(outcomes.current().isEmpty());
        // A done row keeps its own 6 s after the strip went.
        QCOMPARE(outcomes.rowMark(QStringLiteral("c")).value(QStringLiteral("kind")).toString(),
                 QStringLiteral("done"));
    }

    void clearTakesEverything()
    {
        ActionOutcomes outcomes;
        outcomes.report(outcome(QStringLiteral("stop"), {QStringLiteral("a")}));
        outcomes.clear();
        QVERIFY(outcomes.current().isEmpty());
        QVERIFY(outcomes.rowMark(QStringLiteral("a")).isEmpty());
    }

    void fromAMap()
    {
        ActionOutcomes outcomes;
        outcomes.reportMap({{QStringLiteral("action"), QStringLiteral("start")},
                            {QStringLiteral("hashes"), QStringList({QStringLiteral("a")})},
                            {QStringLiteral("name"), QStringLiteral("Film")}});
        QCOMPARE(outcomes.current().value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Film started"));
    }

    // #146: a creation's strip comes with its words made, and marks no row.
    void wordsAsGiven()
    {
        ActionOutcomes outcomes;
        outcomes.report(outcome(QStringLiteral("stop"), {QStringLiteral("a")}));
        outcomes.reportWords({{QStringLiteral("kind"), QStringLiteral("dim")},
                              {QStringLiteral("headline"), QStringLiteral("x wasn't made")}});
        QCOMPARE(outcomes.current().value(QStringLiteral("kind")).toString(),
                 QStringLiteral("dim"));
        QVERIFY(outcomes.current().value(QStringLiteral("serial")).toInt() > 0);
        QVERIFY(outcomes.rowMark(QStringLiteral("a")).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestActionOutcome)
#include "tst_actionoutcome.moc"
