#include "integration/Tray.hpp"

#include <QAction>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

using omaseed::integration::Tray;

namespace {

/// The colour that covers the most non-transparent pixels of an icon pixmap. Icons are
/// simple, mostly one fill colour plus a small badge, so this is enough to prove the icon
/// tracked a given palette entry.
QColor dominantColor(const QImage &image)
{
    QHash<QRgb, int> counts;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = image.pixel(x, y);
            if (qAlpha(pixel) < 32)
                continue;
            counts[pixel]++;
        }
    }
    QRgb best = 0;
    int bestCount = -1;
    for (auto it = counts.constBegin(); it != counts.constEnd(); ++it) {
        if (it.value() > bestCount) {
            bestCount = it.value();
            best = it.key();
        }
    }
    return QColor::fromRgba(best);
}

bool closeEnough(const QColor &a, const QColor &b, int tolerance = 24)
{
    return std::abs(a.red() - b.red()) <= tolerance &&
           std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

} // namespace

class TestTray : public QObject
{
    Q_OBJECT

private slots:
    void tooltipMatchesSpec();
    void menuMatchesFig18InOrder();
    void altSpeedIsCheckableAndTogglingEmits();
    void middleClickRequestsTheOppositeAltSpeedState();
    void leftClickRequestsToggleWindow();
    void statesProduceDifferentNonNullIcons();
    void iconDominantColourTracksAFakePalette();
    void errorDiffersFromIdleByShapeWhenRedIsNotRed();
    void settingAltSpeedCheckedDoesNotEmit();
    void constructionUnderNoTrayDoesNotCrash();
    void burstFollowsTheWord();
};

void TestTray::tooltipMatchesSpec()
{
    Tray tray;
    tray.setTooltip(
        QStringLiteral("home"), QStringLiteral("4.21 MB/s"), QStringLiteral("812 KB/s"), 6, 118);
    QCOMPARE(tray.tooltip(),
             QStringLiteral("home · ↓ 4.21 MB/s ↑ 812 KB/s · 6 downloading · 118 seeding"));
}

void TestTray::menuMatchesFig18InOrder()
{
    Tray tray;
    tray.setStats(
        QStringLiteral("4.21 MB/s"), QStringLiteral("812 KB/s"), 6, 118, QStringLiteral("1.8 TB"));

    const QList<QAction *> actions = tray.menu()->actions();
    QCOMPARE(actions.size(), 12);

    QCOMPARE(actions.at(0)->text(), QStringLiteral("show omaseed"));
    QCOMPARE(actions.at(1)->text(), QStringLiteral("add torrent…"));
    QVERIFY(actions.at(2)->isSeparator());

    QCOMPARE(actions.at(3)->text(), QStringLiteral("↓ 4.21 MB/s · ↑ 812 KB/s"));
    QVERIFY(!actions.at(3)->isEnabled());
    QCOMPARE(actions.at(4)->text(), QStringLiteral("6 downloading · 118 seeding · 1.8 TB free"));
    QVERIFY(!actions.at(4)->isEnabled());

    QVERIFY(actions.at(5)->isSeparator());
    QCOMPARE(actions.at(6)->text(), QStringLiteral("alternative speed limits"));
    QVERIFY(actions.at(6)->isCheckable());
    QCOMPARE(actions.at(7)->text(), QStringLiteral("stop all"));
    QCOMPARE(actions.at(8)->text(), QStringLiteral("start all"));

    QVERIFY(actions.at(9)->isSeparator());
    QCOMPARE(actions.at(10)->text(), QStringLiteral("settings…"));
    QCOMPARE(actions.at(11)->text(), QStringLiteral("quit"));
}

void TestTray::altSpeedIsCheckableAndTogglingEmits()
{
    Tray tray;
    QAction *altSpeed = tray.menu()->actions().at(6);
    QVERIFY(altSpeed->isCheckable());
    QVERIFY(!altSpeed->isChecked());

    QSignalSpy spy(&tray, &Tray::altSpeedToggled);
    altSpeed->trigger();
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().at(0).toBool(), true);
    QVERIFY(altSpeed->isChecked());
    QVERIFY(tray.altSpeedChecked());

    altSpeed->trigger();
    QCOMPARE(spy.size(), 2);
    QCOMPARE(spy.last().at(0).toBool(), false);
}

void TestTray::middleClickRequestsTheOppositeAltSpeedState()
{
    Tray tray;
    QVERIFY(!tray.altSpeedChecked());

    QSignalSpy spy(&tray, &Tray::altSpeedToggled);
    emit tray.systemTrayIcon()->activated(QSystemTrayIcon::MiddleClick);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.last().at(0).toBool(), true);
}

void TestTray::leftClickRequestsToggleWindow()
{
    Tray tray;
    QSignalSpy spy(&tray, &Tray::toggleWindowRequested);
    emit tray.systemTrayIcon()->activated(QSystemTrayIcon::Trigger);
    QCOMPARE(spy.size(), 1);
}

void TestTray::statesProduceDifferentNonNullIcons()
{
    Tray tray;
    tray.setDownloadingCount(6);

    const QIcon disconnected = tray.iconForState(Tray::Disconnected);
    const QIcon idle = tray.iconForState(Tray::Idle);
    const QIcon downloading = tray.iconForState(Tray::Downloading);
    const QIcon altSpeed = tray.iconForState(Tray::AltSpeed);
    const QIcon error = tray.iconForState(Tray::Error);

    for (const QIcon &icon : {disconnected, idle, downloading, altSpeed, error})
        QVERIFY(!icon.isNull());

    const QImage a = disconnected.pixmap(16).toImage();
    const QImage b = idle.pixmap(16).toImage();
    const QImage c = downloading.pixmap(16).toImage();
    const QImage d = altSpeed.pixmap(16).toImage();
    const QImage e = error.pixmap(16).toImage();

    QVERIFY(a != b);
    QVERIFY(b != c);
    QVERIFY(c != d);
    QVERIFY(d != e);
    QVERIFY(a != e);
}

// docs/08 §1: on a theme whose "red" is the foreground colour, error must still not look like
// idle. The badge gives it a shape of its own.
void TestTray::errorDiffersFromIdleByShapeWhenRedIsNotRed()
{
    Tray tray;
    tray.setPalette({
        {QStringLiteral("accent"), QColor(QStringLiteral("#82fb9c"))},
        {QStringLiteral("foreground"), QColor(QStringLiteral("#ddf7ff"))},
        {QStringLiteral("dim"), QColor(QStringLiteral("#6a6e95"))},
        {QStringLiteral("red"), QColor(QStringLiteral("#ddf7ff"))},
        {QStringLiteral("yellow"), QColor(QStringLiteral("#50f7d4"))},
        {QStringLiteral("background"), QColor(QStringLiteral("#0b0c16"))},
    });

    for (int size : {16, 22}) {
        const QImage idle = tray.iconForState(Tray::Idle).pixmap(size).toImage();
        const QImage error = tray.iconForState(Tray::Error).pixmap(size).toImage();
        QVERIFY2(idle != error, qPrintable(QStringLiteral("identical at %1 px").arg(size)));
    }
}

void TestTray::iconDominantColourTracksAFakePalette()
{
    Tray tray;

    QHash<QString, QColor> palette{
        {QStringLiteral("accent"), QColor(QStringLiteral("#112233"))},
        {QStringLiteral("foreground"), QColor(QStringLiteral("#aabbcc"))},
        {QStringLiteral("dim"), QColor(QStringLiteral("#334455"))},
        {QStringLiteral("red"), QColor(QStringLiteral("#ee1122"))},
        {QStringLiteral("yellow"), QColor(QStringLiteral("#eecc00"))},
        {QStringLiteral("background"), QColor(QStringLiteral("#000000"))},
    };
    tray.setPalette(palette);

    const QImage idle = tray.iconForState(Tray::Idle).pixmap(16).toImage();
    QVERIFY(closeEnough(dominantColor(idle), palette.value(QStringLiteral("foreground"))));

    const QImage error = tray.iconForState(Tray::Error).pixmap(16).toImage();
    QVERIFY(closeEnough(dominantColor(error), palette.value(QStringLiteral("red"))));

    const QImage downloading = tray.iconForState(Tray::Downloading).pixmap(16).toImage();
    QVERIFY(closeEnough(dominantColor(downloading), palette.value(QStringLiteral("accent"))));
}

void TestTray::settingAltSpeedCheckedDoesNotEmit()
{
    Tray tray;
    QSignalSpy spy(&tray, &Tray::altSpeedToggled);
    tray.setAltSpeedChecked(true);
    QVERIFY(tray.altSpeedChecked());
    QCOMPARE(spy.size(), 0);

    tray.setAltSpeedChecked(true);
    QCOMPARE(spy.size(), 0);
}

void TestTray::constructionUnderNoTrayDoesNotCrash()
{
    // isSystemTrayAvailable() asks the session bus for a StatusNotifierWatcher, so it can be
    // true even under the offscreen QPA platform on a machine that has a running bar (this
    // dev box does). Either way, construction, state changes and icon building must never
    // crash, whether or not the tray ends up shown.
    Tray tray;
    QCOMPARE(tray.isAvailable(), QSystemTrayIcon::isSystemTrayAvailable());
    tray.setState(Tray::Idle);
    tray.setState(Tray::Error);
    QVERIFY(!tray.iconForState(Tray::Error).isNull());
}

// docs/31 T1 (D14): the checkbox takes the word and the end; the icon's mark is a green disc
// with ⏶ for burst, and turtle keeps today's yellow ⏷.
void TestTray::burstFollowsTheWord()
{
    Tray tray;
    tray.setPalette({
        {QStringLiteral("yellow"), QColor(QStringLiteral("#eecc00"))},
        {QStringLiteral("green"), QColor(QStringLiteral("#22dd44"))},
    });
    QAction *altSpeed = tray.menu()->actions().at(6);
    tray.setAltSpeedLabel(QStringLiteral("burst · until 07:00"));
    QCOMPARE(altSpeed->text(), QStringLiteral("burst · until 07:00"));
    QVERIFY(altSpeed->isCheckable());

    const QImage turtle = tray.iconForState(Tray::AltSpeed).pixmap(32).toImage();
    tray.setAltKind(QStringLiteral("burst"));
    QCOMPARE(tray.altKind(), QStringLiteral("burst"));
    const QImage burst = tray.iconForState(Tray::AltSpeed).pixmap(32).toImage();
    QVERIFY(turtle != burst);
    const auto has = [](const QImage &image, const QColor &colour) {
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qAlpha(image.pixel(x, y)) > 200 &&
                    closeEnough(image.pixelColor(x, y), colour, 8))
                    return true;
            }
        }
        return false;
    };
    QVERIFY(has(burst, QColor(QStringLiteral("#22dd44"))));
    QVERIFY(!has(burst, QColor(QStringLiteral("#eecc00"))));
    QVERIFY(has(turtle, QColor(QStringLiteral("#eecc00"))));
    // Idle is not touched by the word.
    const QImage idle = tray.iconForState(Tray::Idle).pixmap(32).toImage();
    tray.setAltKind(QStringLiteral("turtle"));
    QCOMPARE(tray.iconForState(Tray::Idle).pixmap(32).toImage(), idle);
}

QTEST_MAIN(TestTray)

#include "tst_tray.moc"
