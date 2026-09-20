#include "integration/Theme.hpp"

#include <QColor>
#include <QTest>

using omaseed::integration::Theme;

namespace {

constexpr qreal kFloor = 3.0;

/// The smallest lift that reaches the floor: at it, but not by more than a single 1/256
/// step's worth of contrast.
constexpr qreal kStepEpsilon = 0.06;

} // namespace

/// docs/QUESTIONS.md Q19: `dimForeground` reads at 3:1 against the window background in
/// every theme. The ratio is WCAG 2's, and the lift is the least one that gets there.
class TestThemeContrast : public QObject
{
    Q_OBJECT

private slots:
    void contrastRatioIsWcags();
    void contrastRatioIsSymmetric();
    void liftIsTheSmallestThatReachesTheFloor_data();
    void liftIsTheSmallestThatReachesTheFloor();
    void themesAlreadyAtTheFloorAreUntouched_data();
    void themesAlreadyAtTheFloorAreUntouched();
    void degeneratePaletteGetsTheForeground();
};

void TestThemeContrast::contrastRatioIsWcags()
{
    QCOMPARE(Theme::contrastRatio(Qt::white, Qt::black), 21.0);
    QCOMPARE(Theme::contrastRatio(Qt::white, Qt::white), 1.0);
    // WCAG's own worked example: #767676 on white passes AA at 4.54.
    QVERIFY(qAbs(Theme::contrastRatio(QColor(QStringLiteral("#767676")), Qt::white) - 4.54) < 0.01);
    // The review's table (docs/REVIEW-v0.1.0.md §2) came from the same formula.
    QVERIFY(qAbs(Theme::contrastRatio(QColor(QStringLiteral("#4f585e")),
                                      QColor(QStringLiteral("#2d353b"))) -
                 1.72) < 0.01);
}

void TestThemeContrast::contrastRatioIsSymmetric()
{
    const QColor a(QStringLiteral("#565f89"));
    const QColor b(QStringLiteral("#1a1b26"));
    QCOMPARE(Theme::contrastRatio(a, b), Theme::contrastRatio(b, a));
}

void TestThemeContrast::liftIsTheSmallestThatReachesTheFloor_data()
{
    QTest::addColumn<QColor>("background");
    QTest::addColumn<QColor>("foreground");
    QTest::addColumn<QColor>("dim");

    // The worst and the reference theme of the review's table, values from their colors.toml.
    QTest::newRow("everforest 1.72")
        << QColor(QStringLiteral("#2d353b")) << QColor(QStringLiteral("#d3c6aa"))
        << QColor(QStringLiteral("#4f585e"));
    QTest::newRow("tokyo-night 2.76")
        << QColor(QStringLiteral("#1a1b26")) << QColor(QStringLiteral("#a9b1d6"))
        << QColor(QStringLiteral("#565f89"));
    // A light theme lifts the other way, towards a darker foreground.
    QTest::newRow("white 1.82") << QColor(QStringLiteral("#ffffff"))
                                << QColor(QStringLiteral("#000000"))
                                << QColor(QStringLiteral("#c0c0c0"));
}

void TestThemeContrast::liftIsTheSmallestThatReachesTheFloor()
{
    QFETCH(QColor, background);
    QFETCH(QColor, foreground);
    QFETCH(QColor, dim);
    QVERIFY(Theme::contrastRatio(dim, background) < kFloor);

    const QColor lifted = Theme::liftToContrast(dim, foreground, background, kFloor);
    const qreal ratio = Theme::contrastRatio(lifted, background);
    QVERIFY2(ratio >= kFloor,
             qPrintable(QStringLiteral("%1 reads at %2").arg(lifted.name()).arg(ratio)));
    QVERIFY2(ratio < kFloor + kStepEpsilon,
             qPrintable(QStringLiteral("%1 overshoots at %2").arg(lifted.name()).arg(ratio)));
    QVERIFY(lifted != dim);
    QVERIFY(lifted != foreground);
    QCOMPARE(lifted.alpha(), 255);
    // Deterministic: the same inputs give the same byte values every time.
    QCOMPARE(Theme::liftToContrast(dim, foreground, background, kFloor), lifted);
}

void TestThemeContrast::themesAlreadyAtTheFloorAreUntouched_data()
{
    QTest::addColumn<QColor>("background");
    QTest::addColumn<QColor>("foreground");
    QTest::addColumn<QColor>("dim");

    // gruvbox sits just over the line at 3.03 (values from its colors.toml).
    QTest::newRow("gruvbox 3.03") << QColor(QStringLiteral("#282828"))
                                  << QColor(QStringLiteral("#d4be98"))
                                  << QColor(QStringLiteral("#7c6f64"));
    QTest::newRow("far above") << QColor(Qt::black) << QColor(Qt::white)
                               << QColor(QStringLiteral("#a0a0a0"));
}

void TestThemeContrast::themesAlreadyAtTheFloorAreUntouched()
{
    QFETCH(QColor, background);
    QFETCH(QColor, foreground);
    QFETCH(QColor, dim);
    QVERIFY(Theme::contrastRatio(dim, background) >= kFloor);
    QCOMPARE(Theme::liftToContrast(dim, foreground, background, kFloor).name(), dim.name());
}

void TestThemeContrast::degeneratePaletteGetsTheForeground()
{
    // Nothing on the line from dim to foreground reaches 3:1: the foreground is the best
    // the palette has, so that is what the role becomes.
    const QColor background(QStringLiteral("#202020"));
    const QColor foreground(QStringLiteral("#404040"));
    const QColor dim(QStringLiteral("#282828"));
    QVERIFY(Theme::contrastRatio(foreground, background) < kFloor);
    QCOMPARE(Theme::liftToContrast(dim, foreground, background, kFloor).name(), foreground.name());
}

QTEST_MAIN(TestThemeContrast)

#include "tst_themecontrast.moc"
