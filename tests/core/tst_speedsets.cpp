#include "core/SpeedSets.hpp"

#include <QTest>

using namespace omaseed::core;

// docs/31 §3: the word is read from the two sets, 0 as ∞.
class TestSpeedSets : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void compare_data()
    {
        QTest::addColumn<qint64>("normal");
        QTest::addColumn<qint64>("alt");
        QTest::addColumn<int>("expected");

        QTest::newRow("both unlimited") << qint64(0) << qint64(0) << 0;
        QTest::newRow("alt unlimited") << qint64(1228800) << qint64(0) << 1;
        QTest::newRow("normal unlimited") << qint64(0) << qint64(10240) << -1;
        QTest::newRow("alt higher") << qint64(1228800) << qint64(5242880) << 1;
        QTest::newRow("alt lower") << qint64(1228800) << qint64(10240) << -1;
        QTest::newRow("equal") << qint64(1228800) << qint64(1228800) << 0;
        QTest::newRow("-1 is no limit too") << qint64(-1) << qint64(0) << 0;
    }

    void compare()
    {
        QFETCH(qint64, normal);
        QFETCH(qint64, alt);
        QFETCH(int, expected);
        QCOMPARE(SpeedSets::compare(normal, alt), expected);
    }

    void kind_data()
    {
        constexpr qint64 K = 1024;
        constexpr qint64 M = 1024 * 1024;
        QTest::addColumn<qint64>("dl");
        QTest::addColumn<qint64>("up");
        QTest::addColumn<qint64>("altDl");
        QTest::addColumn<qint64>("altUp");
        QTest::addColumn<int>("kind");
        QTest::addColumn<QString>("word");
        QTest::addColumn<QString>("key");

        const auto burst = int(SpeedSets::Kind::Burst);
        const auto turtle = int(SpeedSets::Kind::Turtle);
        const auto mixed = int(SpeedSets::Kind::Mixed);
        const auto same = int(SpeedSets::Kind::Same);
        // §3's table, row by row.
        QTest::newRow("qBittorrent's defaults: turtle")
            << qint64(0) << qint64(0) << 10 * K << 10 * K << turtle << "turtle" << "turtle";
        QTest::newRow("a daytime upload cap, the full line at night")
            << qint64(0) << qint64(1200 * K) << qint64(0) << qint64(0) << burst << "burst"
            << "burst";
        QTest::newRow("both faster")
            << 4 * M << qint64(1200 * K) << qint64(0) << 5 * M << burst << "burst" << "burst";
        QTest::newRow("download the same, upload faster")
            << qint64(0) << qint64(1200 * K) << qint64(0) << 5 * M << burst << "burst" << "burst";
        QTest::newRow("only upload slower")
            << qint64(0) << qint64(0) << qint64(0) << 200 * K << turtle << "turtle" << "turtle";
        QTest::newRow("upload faster, download slower: mixed")
            << qint64(0) << qint64(1200 * K) << 2 * M << qint64(0) << mixed << "alt-speed" << "alt";
        QTest::newRow("the same: switching changes nothing")
            << qint64(0) << qint64(1200 * K) << qint64(0) << qint64(1200 * K) << same << "alt-speed"
            << "alt";
        QTest::newRow("the same, both unlimited")
            << qint64(0) << qint64(0) << qint64(0) << qint64(0) << same << "alt-speed" << "alt";
        QTest::newRow("both slower")
            << 4 * M << 2 * M << 10 * K << 10 * K << turtle << "turtle" << "turtle";
        QTest::newRow("download slower, upload unchanged")
            << 4 * M << 2 * M << 1 * M << 2 * M << turtle << "turtle" << "turtle";
    }

    void kind()
    {
        QFETCH(qint64, dl);
        QFETCH(qint64, up);
        QFETCH(qint64, altDl);
        QFETCH(qint64, altUp);
        QFETCH(int, kind);
        QFETCH(QString, word);
        QFETCH(QString, key);

        const SpeedSets::Kind got = SpeedSets::kind(dl, up, altDl, altUp);
        QCOMPARE(int(got), kind);
        QCOMPARE(SpeedSets::word(got), word);
        QCOMPARE(SpeedSets::key(got), key);
    }

    // Not read yet (before the preferences answer): today's word.
    void unknownKeepsTodaysWord()
    {
        QCOMPARE(SpeedSets::word(SpeedSets::Kind::Unknown), QStringLiteral("alt-speed"));
        QCOMPARE(SpeedSets::key(SpeedSets::Kind::Unknown), QStringLiteral("alt"));
    }
};

QTEST_GUILESS_MAIN(TestSpeedSets)
#include "tst_speedsets.moc"
