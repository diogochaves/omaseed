// #73 — App's side of the shared menu (docs/33 §11, D10, D24, D25; DECISIONS C13): the menu
// follows the server list (the block only with two servers or more), its rows reach the app's
// own actions, the widget's Invoke and SwitchServer go through StatusService, and Settings ›
// Appearance's "In the bar" reads and writes the two old settings as one. Demo mode: no
// server is contacted.

#include "core/ServerStore.hpp"
#include "integration/BarMenu.hpp"
#include "integration/BarWidget.hpp"
#include "integration/Tray.hpp"
#include "ui/app/App.hpp"
#include "ui/app/StatusService.hpp"

#include <QAction>
#include <QJsonObject>
#include <QMenu>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::app::App;
using omaseed::app::StatusService;
using omaseed::integration::BarMenu;

namespace {

QStringList idsOf(const BarMenu *menu)
{
    QStringList ids;
    for (const BarMenu::Row &row : menu->rows())
        ids << row.id;
    return ids;
}

} // namespace

class TestAppBarMenu : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void theMenuFollowsTheServerList();
    void itsRowsReachTheApp();
    void theWidgetActsThroughStatusService();
    void theTrayShowsTheSameMenu();
    void inTheBarReadsTheTwoOldSettings();
    void inTheBarWritesTheTrayIconMode();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

void TestAppBarMenu::initTestCase()
{
    // Nothing here may reach the desktop's session bus (keychain, tray, notices).
    QVERIFY(m_dir.isValid());
    qputenv("DBUS_SESSION_BUS_ADDRESS",
            QStringLiteral("unix:path=%1/no-bus").arg(m_dir.path()).toUtf8());
}

void TestAppBarMenu::init()
{
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestAppBarMenu::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

// D19, D24: one server, no servers block; a second one turns it on, with each row's figure.
void TestAppBarMenu::theMenuFollowsTheServerList()
{
    App app;
    app.startDemo(4);
    const BarMenu *menu = app.barMenu();
    QCOMPARE(menu->servers().size(), 1);
    QVERIFY(!idsOf(menu).contains(QStringLiteral("servers")));
    const BarMenu::Server home = menu->servers().first();
    QCOMPARE(home.name, QStringLiteral("home"));
    QVERIFY(home.current);
    QCOMPARE(home.state, QStringLiteral("ok"));
    QCOMPARE(home.count, 4);
    // The demo answers: the rows that need a server are on.
    QVERIFY(menu->rows().first().enabled);

    QSignalSpy changed(menu, &BarMenu::changed);
    const QString id = app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    });
    QVERIFY(!id.isEmpty());
    QVERIFY(changed.size() > 0);
    QCOMPARE(menu->servers().size(), 2);
    const QStringList ids = idsOf(menu);
    QCOMPARE(ids.at(0), QStringLiteral("servers"));
    QCOMPARE(ids.at(2), QStringLiteral("server:") + id);
    const BarMenu::Server seedbox = menu->servers().at(1);
    QVERIFY(!seedbox.current);
    QCOMPARE(seedbox.state, QStringLiteral("unknown"));
    QCOMPARE(seedbox.count, -1);
    QCOMPARE(seedbox.seen, 0);
    QCOMPARE(menu->rows().at(1).detail, QStringLiteral("4 torrents"));
    QCOMPARE(menu->rows().at(2).detail, QString());

    QVERIFY(app.removeServer(id));
    QVERIFY(!idsOf(menu).contains(QStringLiteral("servers")));
}

void TestAppBarMenu::itsRowsReachTheApp()
{
    App app;
    app.startDemo(4);
    BarMenu *menu = app.barMenu();
    QSignalSpy add(&app, &App::addRequested);
    QSignalSpy raise(&app, &App::raiseRequested);
    QSignalSpy settings(&app, &App::openSettings);
    QSignalSpy quit(&app, &App::quitRequested);

    QVERIFY(menu->invoke(QStringLiteral("add")));
    QCOMPARE(add.size(), 1);
    QCOMPARE(raise.size(), 1); // the window comes up for the sheet, from the tray or the bar
    QVERIFY(menu->invoke(QStringLiteral("settings")));
    QCOMPARE(settings.size(), 1);
    QVERIFY(menu->invoke(QStringLiteral("alt-speed")));
    QVERIFY(menu->invoke(QStringLiteral("stop-all")));
    QVERIFY(menu->invoke(QStringLiteral("start-all")));
    QCOMPARE(quit.size(), 0);
    QVERIFY(menu->invoke(QStringLiteral("quit")));
    QCOMPARE(quit.size(), 1);

    // A server row is the one switch (App::switchServer).
    const QString id = app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    });
    QVERIFY(app.servers()->defaultId() != id);
    QVERIFY(menu->invoke(QStringLiteral("server:") + id));
    QCOMPARE(app.servers()->defaultId(), id);
}

// docs/33 §11 change 6: SwitchServer and Invoke, through the handler StatusBus calls.
void TestAppBarMenu::theWidgetActsThroughStatusService()
{
    App app;
    app.startDemo(4);
    StatusService service(&app);
    QSignalSpy settings(&app, &App::openSettings);
    QVERIFY(service.invoke(QStringLiteral("settings")));
    QCOMPARE(settings.size(), 1);
    QVERIFY(!service.invoke(QStringLiteral("pause-all")));

    const QString id = app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    });
    QVERIFY(service.switchServer(id));
    QCOMPARE(app.servers()->defaultId(), id);
    QVERIFY(!service.switchServer(QStringLiteral("nowhere")));
}

// D10: the tray's rows under its own are the app's menu.
void TestAppBarMenu::theTrayShowsTheSameMenu()
{
    App app;
    app.startDemo(4);
    omaseed::integration::Tray tray(nullptr);
    app.attachTray(&tray);
    QCOMPARE(tray.menuModel(), app.barMenu());
    app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    });
    QStringList texts;
    for (const QAction *action : tray.menu()->actions())
        texts << action->text();
    QVERIFY2(texts.contains(QStringLiteral("servers")), qPrintable(texts.join(u'|')));
    QVERIFY(texts.contains(QStringLiteral("home · 4 torrents")));
    QVERIFY(texts.contains(QStringLiteral("seedbox")));
    QCOMPARE(texts.last(), QStringLiteral("quit"));
}

// D25 (DECISIONS C13): widget = in the bar + auto (or never); both = in the bar + always;
// tray icon = not in the bar + auto or always; none = not in the bar + never.
void TestAppBarMenu::inTheBarReadsTheTwoOldSettings()
{
    QCOMPARE(App::inTheBarFor(true, QStringLiteral("auto")), QStringLiteral("widget"));
    QCOMPARE(App::inTheBarFor(true, QStringLiteral("never")), QStringLiteral("widget"));
    QCOMPARE(App::inTheBarFor(true, QStringLiteral("always")), QStringLiteral("both"));
    QCOMPARE(App::inTheBarFor(false, QStringLiteral("auto")), QStringLiteral("tray"));
    QCOMPARE(App::inTheBarFor(false, QStringLiteral("always")), QStringLiteral("tray"));
    QCOMPARE(App::inTheBarFor(false, QStringLiteral("never")), QStringLiteral("none"));
}

void TestAppBarMenu::inTheBarWritesTheTrayIconMode()
{
    App app;
    app.barWidget()->setPrograms(QStringLiteral("omaseed-tests-have-no-omarchy"),
                                 QStringLiteral("omaseed-tests-have-no-omarchy"));
    QVERIFY(!app.barWidget()->available());
    QVERIFY(!app.barWidget()->inBar());
    app.setTrayIconMode(QStringLiteral("auto"));
    QCOMPARE(app.inTheBar(), QStringLiteral("tray"));

    QSignalSpy changed(&app, &App::inTheBarChanged);
    app.setInTheBar(QStringLiteral("none"));
    QCOMPARE(app.trayIconMode(), QStringLiteral("never"));
    QCOMPARE(app.inTheBar(), QStringLiteral("none"));
    QVERIFY(changed.size() > 0);

    app.setInTheBar(QStringLiteral("tray"));
    QCOMPARE(app.trayIconMode(), QStringLiteral("auto"));
    QCOMPARE(app.inTheBar(), QStringLiteral("tray"));

    // Without the shell the widget cannot be had: nothing changes.
    app.setInTheBar(QStringLiteral("both"));
    QCOMPARE(app.trayIconMode(), QStringLiteral("auto"));
    QCOMPARE(app.inTheBar(), QStringLiteral("tray"));
    app.setInTheBar(QStringLiteral("bogus"));
    QCOMPARE(app.inTheBar(), QStringLiteral("tray"));
}

QTEST_MAIN(TestAppBarMenu)
#include "tst_app_barmenu.moc"
