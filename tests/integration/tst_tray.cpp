#include "integration/BarMenu.hpp"
#include "integration/Tray.hpp"

#include <QAction>
#include <QColor>
#include <QHash>
#include <QImage>
#include <QMenu>
#include <QSignalSpy>
#include <QTest>

using omaseed::integration::BarMenu;
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
    void menuIsShowTheFiguresThenTheSharedMenu();
    void altSpeedRowAsksTheModelAndFollowsIt();
    void middleClickInvokesAltSpeed();
    void serversBlockOnlyWithTwoServersOrMore();
    void leftClickRequestsToggleWindow();
    void statesProduceDifferentNonNullIcons();
    void iconDominantColourTracksAFakePalette();
    void errorDiffersFromIdleByShapeWhenRedIsNotRed();
    void theModelsAltSpeedDoesNotInvoke();
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

    // Board 42 J6 (DECISIONS C15 M20): a running move adds its line, and goes with it.
    tray.setJobLine(QStringLiteral("Moving games: 52 %, about 50 min left"));
    QCOMPARE(tray.tooltip(),
             QStringLiteral("home · ↓ 4.21 MB/s ↑ 812 KB/s · 6 downloading · 118 seeding\n"
                            "Moving games: 52 %, about 50 min left"));
    tray.setTooltip(
        QStringLiteral("home"), QStringLiteral("1 MB/s"), QStringLiteral("2 MB/s"), 0, 118);
    QVERIFY(tray.tooltip().endsWith(QStringLiteral("\nMoving games: 52 %, about 50 min left")));
    tray.setJobLine(QString());
    QCOMPARE(tray.tooltip(),
             QStringLiteral("home · ↓ 1 MB/s ↑ 2 MB/s · 0 downloading · 118 seeding"));
}

// docs/33 §11 (D10, D24; DECISIONS C13): "show omaseed" and Fig. 18's two figure rows, then
// the shared menu the bar widget's ⋯ shows too, quit last after a separator.
void TestTray::menuIsShowTheFiguresThenTheSharedMenu()
{
    Tray tray;
    tray.setStats(
        QStringLiteral("4.21 MB/s"), QStringLiteral("812 KB/s"), 6, 118, QStringLiteral("1.8 TB"));

    const QList<QAction *> actions = tray.menu()->actions();
    QCOMPARE(actions.size(), 13);

    QCOMPARE(actions.at(0)->text(), QStringLiteral("show omaseed"));
    QVERIFY(actions.at(1)->isSeparator());

    QCOMPARE(actions.at(2)->text(), QStringLiteral("↓ 4.21 MB/s · ↑ 812 KB/s"));
    QVERIFY(!actions.at(2)->isEnabled());
    QCOMPARE(actions.at(3)->text(), QStringLiteral("6 downloading · 118 seeding · 1.8 TB free"));
    QVERIFY(!actions.at(3)->isEnabled());

    QVERIFY(actions.at(4)->isSeparator());
    QCOMPARE(actions.at(5)->text(), QStringLiteral("alternative speed limits"));
    QVERIFY(actions.at(5)->isCheckable());
    QCOMPARE(actions.at(6)->text(), QStringLiteral("stop all"));
    QCOMPARE(actions.at(7)->text(), QStringLiteral("start all"));

    QVERIFY(actions.at(8)->isSeparator());
    QCOMPARE(actions.at(9)->text(), QStringLiteral("add torrent…"));
    QCOMPARE(actions.at(10)->text(), QStringLiteral("settings…"));
    QVERIFY(actions.at(11)->isSeparator());
    QCOMPARE(actions.at(12)->text(), QStringLiteral("quit"));

    // Before App gives it the shared menu, rows that need a server are off.
    QVERIFY(!actions.at(5)->isEnabled());
    QVERIFY(actions.at(9)->isEnabled());
    QVERIFY(actions.at(12)->isEnabled());
}

void TestTray::altSpeedRowAsksTheModelAndFollowsIt()
{
    Tray tray;
    BarMenu model;
    model.setServerAnswering(true);
    tray.setMenuModel(&model);
    QCOMPARE(tray.menuModel(), &model);

    QAction *altSpeed = tray.menu()->actions().at(5);
    QVERIFY(altSpeed->isCheckable());
    QVERIFY(altSpeed->isEnabled());
    QVERIFY(!altSpeed->isChecked());

    QSignalSpy spy(&model, &BarMenu::triggered);
    altSpeed->trigger();
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("alt-speed"));
    // The check follows the server, not the click.
    QVERIFY(!altSpeed->isChecked());
    QVERIFY(!tray.altSpeedChecked());

    model.setAltSpeed(true, QStringLiteral("alternative speed limits"), QStringLiteral("A"));
    QVERIFY(tray.altSpeedChecked());

    // Each shared row goes to the model by its id.
    const QList<QAction *> actions = tray.menu()->actions();
    for (int at : {6, 7, 9, 10, 12})
        actions.at(at)->trigger();
    QCOMPARE(spy.size(), 6);
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("stop-all"));
    QCOMPARE(spy.at(2).at(0).toString(), QStringLiteral("start-all"));
    QCOMPARE(spy.at(3).at(0).toString(), QStringLiteral("add"));
    QCOMPARE(spy.at(4).at(0).toString(), QStringLiteral("settings"));
    QCOMPARE(spy.at(5).at(0).toString(), QStringLiteral("quit"));

    // A server that stops answering turns them off.
    model.setServerAnswering(false);
    QVERIFY(!altSpeed->isEnabled());
    QVERIFY(!actions.at(6)->isEnabled());
}

void TestTray::middleClickInvokesAltSpeed()
{
    Tray tray;
    BarMenu model;
    model.setServerAnswering(true);
    tray.setMenuModel(&model);

    QSignalSpy spy(&model, &BarMenu::triggered);
    emit tray.systemTrayIcon()->activated(QSystemTrayIcon::MiddleClick);
    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.last().at(0).toString(), QStringLiteral("alt-speed"));
}

// D10 (T1): with two servers or more a "servers" block of radio rows, the current one
// checked and its figure after the name (DBusMenu has no right-hand column). D19: one server,
// none.
void TestTray::serversBlockOnlyWithTwoServersOrMore()
{
    Tray tray;
    BarMenu model;
    tray.setMenuModel(&model);

    BarMenu::Server home{
        QStringLiteral("h"), QStringLiteral("home"), true, QStringLiteral("ok"), 4, 0, QString()};
    model.setServers({home});
    QCOMPARE(tray.menu()->actions().size(), 13);

    BarMenu::Server box{QStringLiteral("b"),
                        QStringLiteral("seedbox"),
                        false,
                        QStringLiteral("refused"),
                        12,
                        1789346626,
                        QStringLiteral("sign-in refused")};
    model.setServers({home, box});
    QList<QAction *> actions = tray.menu()->actions();
    QCOMPARE(actions.size(), 17);
    QCOMPARE(actions.at(5)->text(), QStringLiteral("servers"));
    QVERIFY(!actions.at(5)->isEnabled());
    QCOMPARE(actions.at(6)->text(), QStringLiteral("home · 4 torrents"));
    QVERIFY(actions.at(6)->isCheckable());
    QVERIFY(actions.at(6)->isChecked());
    QCOMPARE(actions.at(7)->text(), QStringLiteral("seedbox · sign-in refused"));
    QVERIFY(!actions.at(7)->isChecked());
    QVERIFY(actions.at(8)->isSeparator());
    QCOMPARE(actions.at(9)->text(), QStringLiteral("alternative speed limits"));
    QCOMPARE(actions.last()->text(), QStringLiteral("quit"));

    // A pick asks the app to switch; the check stays on the server the app is on until it
    // has moved.
    QSignalSpy chosen(&model, &BarMenu::serverChosen);
    actions.at(7)->trigger();
    QCOMPARE(chosen.size(), 1);
    QCOMPARE(chosen.first().at(0).toString(), QStringLiteral("b"));
    QVERIFY(actions.at(6)->isChecked());
    QVERIFY(!actions.at(7)->isChecked());

    home.current = false;
    box.current = true;
    box.state = QStringLiteral("ok");
    box.reason.clear();
    model.setServers({home, box});
    actions = tray.menu()->actions();
    QVERIFY(!actions.at(6)->isChecked());
    QVERIFY(actions.at(7)->isChecked());
    QCOMPARE(actions.at(7)->text(), QStringLiteral("seedbox · 12 torrents"));

    // Back to one: the block goes.
    model.setServers({box});
    QCOMPARE(tray.menu()->actions().size(), 13);
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

void TestTray::theModelsAltSpeedDoesNotInvoke()
{
    Tray tray;
    BarMenu model;
    model.setServerAnswering(true);
    tray.setMenuModel(&model);
    QSignalSpy spy(&model, &BarMenu::triggered);
    model.setAltSpeed(true, QStringLiteral("alternative speed limits"), QStringLiteral("A"));
    QVERIFY(tray.altSpeedChecked());
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
    BarMenu model;
    tray.setMenuModel(&model);
    QAction *altSpeed = tray.menu()->actions().at(5);
    model.setAltSpeed(false, QStringLiteral("burst · until 07:00"), QStringLiteral("Burst"));
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
