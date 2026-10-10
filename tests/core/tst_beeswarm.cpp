#include "core/Beeswarm.hpp"

#include <QElapsedTimer>
#include <QRandomGenerator>
#include <QTest>

#include <cmath>

using namespace omaseed::core;

namespace {

/// The ladder's own placement before #45, testing every dot placed so far: what the grid
/// must give, height for height.
QList<double>
everyDot(const QList<double> &xs, const QList<double> &rs, double midY, double step, int tries)
{
    QList<double> ys;
    for (qsizetype i = 0; i < xs.size(); ++i) {
        double y = midY;
        for (int k = 0; k < tries; ++k) {
            y = midY + std::ceil(k / 2.0) * step * (k % 2 ? -1 : 1);
            bool clear = true;
            for (qsizetype j = 0; j < i && clear; ++j)
                clear = std::hypot(xs.at(j) - xs.at(i), ys.at(j) - y) > rs.at(j) + rs.at(i) + 1;
            if (clear)
                break;
        }
        ys.append(y);
    }
    return ys;
}

/// A ladder's worth of dots, biggest first: radii 2.5..11 as RatioLadder makes them, x on a
/// `width`-wide axis, a share of them on its left end (ratio 0, clamped to 0.1).
void library(int n, double width, quint32 seed, QList<double> &xs, QList<double> &rs)
{
    QRandomGenerator rng(seed);
    xs.clear();
    rs.clear();
    for (int i = 0; i < n; ++i) {
        rs.append(2.5 + 8.5 * std::pow(rng.generateDouble(), 3));
        xs.append(rng.generateDouble() < 0.15 ? 8.0 : 8.0 + rng.generateDouble() * (width - 16));
    }
    std::sort(rs.begin(), rs.end(), std::greater<>());
}

} // namespace

class TestBeeswarm : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void empty() { QVERIFY(Beeswarm::place({}, {}, 70, 4).isEmpty()); }

    // tst_RatioLadder's case: radius 2.5, -4 and +4 overlap the first dot, -8 is free.
    void twoSmallEqualDots()
    {
        const QList<double> ys = Beeswarm::place({100, 100}, {2.5, 2.5}, 70, 4);
        QCOMPARE(ys, (QList<double>{70, 62}));
    }

    void stepsOutUpThenDown()
    {
        // Five dots on one spot, 1 unit across, steps of 3 (clear: 3 > 0.5 + 0.5 + 1): up first.
        const QList<double> ys =
            Beeswarm::place({10, 10, 10, 10, 10}, {0.5, 0.5, 0.5, 0.5, 0.5}, 50, 3);
        QCOMPARE(ys, (QList<double>{50, 47, 53, 44, 56}));
    }

    void outOfTriesTakesTheLastOne()
    {
        // Three tries (0, −1, +1 steps), all inside a big dot: the second keeps the last tried.
        const QList<double> ys = Beeswarm::place({0, 0}, {50, 1}, 0, 4, 3);
        QCOMPARE(ys, (QList<double>{0, 4}));
    }

    void fartherThanACellApartNeverTouch()
    {
        // Two biggest dots exactly as far apart as they reach (2r + 1) touch; a hair more does not.
        QCOMPARE(Beeswarm::place({0, 23}, {11, 11}, 0, 4).at(1), -4.0);
        QCOMPARE(Beeswarm::place({0, 23.001}, {11, 11}, 0, 4).at(1), 0.0);
        // Across a cell edge, on the negative side too.
        QCOMPARE(Beeswarm::place({-0.5, 0.5}, {2.5, 2.5}, 0, 4).at(1), -8.0);
    }

    void sameHeightsAsTestingEveryDot_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<double>("width");
        QTest::addColumn<quint32>("seed");
        QTest::newRow("sparse") << 60 << 1600.0 << 1u;
        QTest::newRow("crowded") << 1500 << 1600.0 << 2u;
        QTest::newRow("narrow") << 800 << 300.0 << 3u;
        QTest::newRow("one spot") << 400 << 16.0 << 4u;
    }

    void sameHeightsAsTestingEveryDot()
    {
        QFETCH(int, count);
        QFETCH(double, width);
        QFETCH(quint32, seed);
        QList<double> xs, rs;
        library(count, width, seed, xs, rs);
        QCOMPARE(Beeswarm::place(xs, rs, 70, 4), everyDot(xs, rs, 70, 4, 40));
    }

    // #45: 3,325 dots took the ladder 1.7 s in JavaScript, every two seconds.
    void aBigLibraryIsQuick()
    {
        QList<double> xs, rs;
        library(3500, 1600, 45, xs, rs);
        QElapsedTimer t;
        t.start();
        const QList<double> ys = Beeswarm::place(xs, rs, 70, 4);
        const qint64 ms = t.elapsed();
        QCOMPARE(ys.size(), 3500);
        qInfo("3,500 dots in %lld ms", ms);
        QVERIFY2(ms < 500, qPrintable(QStringLiteral("%1 ms").arg(ms)));
    }
};

QTEST_GUILESS_MAIN(TestBeeswarm)
#include "tst_beeswarm.moc"
