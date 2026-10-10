#include "integration/Theme.hpp"

#include <QTest>

using omaseed::integration::Theme;

namespace {

/// `hyprctl -j getoption` as Hyprland 0.56 prints it.
QByteArray roundingReply(int value)
{
    return QStringLiteral("{\"option\": \"decoration:rounding\", \"int\": %1, \"set\": true }")
        .arg(value)
        .toUtf8();
}

QByteArray powerReply(const char *value)
{
    return QByteArray("{\"option\": \"decoration:rounding_power\", \"float\": ") + value +
           ", \"set\": true }";
}

} // namespace

/// Under Hyprland the compositor owns the window's edge and corners: omaseed draws no edge
/// of its own, and rounds its surfaces only when Hyprland rounds (rounding_power >= 2).
class TestThemeCorners : public QObject
{
    Q_OBJECT

private slots:
    void corners_data();
    void corners();
};

void TestThemeCorners::corners_data()
{
    QTest::addColumn<QByteArray>("rounding");
    QTest::addColumn<QByteArray>("power");
    QTest::addColumn<int>("radius");
    QTest::addColumn<bool>("edge");

    // No Hyprland: hyprctl failed or printed nothing. omaseed's edge is the only one.
    QTest::newRow("no hyprland") << QByteArray() << QByteArray() << 0 << true;
    QTest::newRow("not json") << QByteArray("no such option") << QByteArray() << 0 << true;
    // Stock Omarchy: square corners, Hyprland's border.
    QTest::newRow("stock") << roundingReply(0) << powerReply("2.000000") << 0 << false;
    // The console's desk: rounding 24 at power 1 is a 12 px chamfer, not an arc.
    QTest::newRow("chamfer") << roundingReply(24) << powerReply("1.000000") << 0 << false;
    QTest::newRow("just under round") << roundingReply(8) << powerReply("1.990000") << 0 << false;
    // A round desk keeps today's radius.
    QTest::newRow("round") << roundingReply(8) << powerReply("2.000000") << 8 << false;
    QTest::newRow("squircle") << roundingReply(12) << powerReply("4.000000") << 12 << false;
    // A Hyprland without the power option only rounds.
    QTest::newRow("no power option")
        << roundingReply(8) << QByteArray("no such option") << 8 << false;
    QTest::newRow("power not asked") << roundingReply(8) << QByteArray() << 8 << false;
    QTest::newRow("negative rounding") << roundingReply(-3) << powerReply("2.000000") << 0 << false;
}

void TestThemeCorners::corners()
{
    QFETCH(QByteArray, rounding);
    QFETCH(QByteArray, power);
    QFETCH(int, radius);
    QFETCH(bool, edge);

    const Theme::WindowCorners corners = Theme::windowCorners(rounding, power);
    QCOMPARE(corners.radius, radius);
    QCOMPARE(corners.edge, edge);
}

QTEST_GUILESS_MAIN(TestThemeCorners)
#include "tst_themecorners.moc"
