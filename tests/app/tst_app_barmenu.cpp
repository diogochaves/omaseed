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
#include <QJsonArray>
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
    void theWindowShowsTheSameMenu();
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

// C24 H9: the window's mark menu is C13's third place, the same rows in the same order, in the
// tray's words; a pick goes through the same invoke.
void TestAppBarMenu::theWindowShowsTheSameMenu()
{
    App app;
    app.startDemo(4);
    const BarMenu *menu = app.barMenu();
    const auto windowIds = [&app]() {
        QStringList ids;
        for (const QVariant &row : app.appMenuRows())
            ids << row.toMap().value(QStringLiteral("id")).toString();
        return ids;
    };
    QCOMPARE(windowIds(), idsOf(menu));
    QCOMPARE(windowIds(),
             (QStringList{QStringLiteral("alt-speed"),
                          QStringLiteral("stop-all"),
                          QStringLiteral("start-all"),
                          QStringLiteral("separator:actions"),
                          QStringLiteral("add"),
                          QStringLiteral("settings"),
                          QStringLiteral("separator:quit"),
                          QStringLiteral("quit")}));

    QSignalSpy changed(&app, &App::appMenuRowsChanged);
    const QString id = app.addServer(QVariantMap{
        {QStringLiteral("name"), QStringLiteral("seedbox")},
        {QStringLiteral("url"), QStringLiteral("http://127.0.0.1:9")},
        {QStringLiteral("authMode"), QStringLiteral("none")},
    });
    QVERIFY(changed.size() > 0);
    QCOMPARE(windowIds(), idsOf(menu));
    // The status file's menu, the widget's, lists the same ids.
    QStringList widgetIds;
    for (const QJsonValue &row : menu->menuJson())
        widgetIds << row.toObject().value(QStringLiteral("id")).toString();
    QCOMPARE(windowIds(), widgetIds);

    const QVariantList rows = app.appMenuRows();
    const QList<BarMenu::Row> trayRows = menu->rows();
    QCOMPARE(rows.size(), trayRows.size());
    for (qsizetype i = 0; i < rows.size(); ++i) {
        const QVariantMap row = rows.at(i).toMap();
        const BarMenu::Row &tray = trayRows.at(i);
        QCOMPARE(row.value(QStringLiteral("label")).toString(), tray.label);
        QCOMPARE(row.value(QStringLiteral("kind")).toString(), BarMenu::kindName(tray.kind));
        QCOMPARE(row.value(QStringLiteral("enabled")).toBool(), tray.enabled);
        QCOMPARE(row.value(QStringLiteral("checked")).toBool(), tray.checked);
        QCOMPARE(row.value(QStringLiteral("detail")).toString(), tray.detail);
    }
    const QVariantMap home = rows.at(1).toMap();
    QCOMPARE(home.value(QStringLiteral("label")).toString(), QStringLiteral("home"));
    QCOMPARE(home.value(QStringLiteral("state")).toString(), QStringLiteral("ok"));
    QVERIFY(home.value(QStringLiteral("checked")).toBool());
    QCOMPARE(rows.at(2).toMap().value(QStringLiteral("state")).toString(),
             QStringLiteral("unknown"));
    QCOMPARE(rows.last().toMap().value(QStringLiteral("state")).toString(), QString());

    QSignalSpy settings(&app, &App::openSettings);
    QVERIFY(app.invokeAppMenu(QStringLiteral("settings")));
    QCOMPARE(settings.size(), 1);
    QVERIFY(!app.invokeAppMenu(QStringLiteral("servers")));
    QVERIFY(!app.invokeAppMenu(QStringLiteral("separator:quit")));
    QVERIFY(!app.invokeAppMenu(QStringLiteral("nowhere")));
    QVERIFY(app.invokeAppMenu(QStringLiteral("server:") + id));
    QCOMPARE(app.servers()->defaultId(), id);
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
