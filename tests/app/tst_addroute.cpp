// #218 — where an add from outside the window opens (docs/DECISIONS.md C35 W1, W2, W6):
// AddRoute's whole table, then App::requestAdd acting on it (the signals, the raise, the
// main window's focus over time on an injected clock). No window, no server, no sleep.

#include "ui/app/AddRoute.hpp"
#include "ui/app/App.hpp"
#include "ui/app/StatusService.hpp"

#include <QSignalSpy>
#include <QTest>

using omaseed::app::AddRoute;
using omaseed::app::AddSource;
using omaseed::app::App;
using omaseed::app::kAddFocusGrace;
using omaseed::app::routeAdd;
using omaseed::app::StatusService;

Q_DECLARE_METATYPE(omaseed::app::AddSource)
Q_DECLARE_METATYPE(omaseed::app::AddRoute)

namespace {

/// The main window's focus before an add: now, 3 s ago, 30 s ago, never.
enum class Focus { Now, ThreeSecondsAgo, ThirtySecondsAgo, Never };

qint64 msSince(Focus focus)
{
    switch (focus) {
    case Focus::Now:
        return 0;
    case Focus::ThreeSecondsAgo:
        return 3'000;
    case Focus::ThirtySecondsAgo:
        return 30'000;
    case Focus::Never:
        return -1;
    }
    return -1;
}

const char *nameOf(AddSource source)
{
    switch (source) {
    case AddSource::Handoff:
        return "handoff";
    case AddSource::Startup:
        return "startup";
    case AddSource::Menu:
        return "menu";
    case AddSource::Bus:
        return "bus";
    }
    return "?";
}

const char *nameOf(Focus focus)
{
    switch (focus) {
    case Focus::Now:
        return "active now";
    case Focus::ThreeSecondsAgo:
        return "active 3 s ago";
    case Focus::ThirtySecondsAgo:
        return "active 30 s ago";
    case Focus::Never:
        return "never active";
    }
    return "?";
}

/// The rules as the plan states them, one line each, so the table below reads against them.
void expected(AddSource source, Focus focus, bool firstRun, AddRoute *route, bool *raise)
{
    const bool popup = source == AddSource::Menu || source == AddSource::Bus;
    if (firstRun) { // W6
        *route = AddRoute::Main;
        *raise = true;
    } else if (focus == Focus::Now) { // W2
        *route = AddRoute::Main;
        *raise = false;
    } else if (source == AddSource::Startup) { // settled at #218
        *route = AddRoute::Main;
        *raise = true;
    } else if (popup && focus == Focus::ThreeSecondsAgo) { // W2, within kAddFocusGrace
        *route = AddRoute::Main;
        *raise = false;
    } else { // W1
        *route = AddRoute::Window;
        *raise = false;
    }
}

void addRows()
{
    QTest::addColumn<AddSource>("source");
    QTest::addColumn<int>("focus");
    QTest::addColumn<bool>("firstRun");
    QTest::addColumn<AddRoute>("route");
    QTest::addColumn<bool>("raise");
    for (AddSource source :
         {AddSource::Handoff, AddSource::Startup, AddSource::Menu, AddSource::Bus})
        for (Focus focus :
             {Focus::Now, Focus::ThreeSecondsAgo, Focus::ThirtySecondsAgo, Focus::Never})
            for (bool firstRun : {false, true}) {
                AddRoute route;
                bool raise;
                expected(source, focus, firstRun, &route, &raise);
                QTest::addRow(
                    "%s, %s%s", nameOf(source), nameOf(focus), firstRun ? ", first run" : "")
                    << source << int(focus) << firstRun << route << raise;
            }
}

} // namespace

class TestAddRoute : public QObject
{
    Q_OBJECT

private slots:
    void theDecision_data() { addRows(); }
    void theDecision();
    void theGraceIsFiveSeconds();
    void requestAddActsOnIt_data() { addRows(); }
    void requestAddActsOnIt();
    void theMenusAddGoesThroughIt();
    void theBusGoesThroughIt();
};

void TestAddRoute::theDecision()
{
    QFETCH(AddSource, source);
    QFETCH(int, focus);
    QFETCH(bool, firstRun);
    QFETCH(AddRoute, route);
    QFETCH(bool, raise);
    const auto f = Focus(focus);
    const auto routing = routeAdd(source, firstRun, f == Focus::Now, msSince(f));
    QCOMPARE(routing.route, route);
    QCOMPARE(routing.raise, raise);
}

void TestAddRoute::theGraceIsFiveSeconds()
{
    // The edge: still "just before" at the grace, not one millisecond after.
    const qint64 grace = kAddFocusGrace.count();
    QCOMPARE(grace, 5'000);
    QCOMPARE(routeAdd(AddSource::Menu, false, false, grace).route, AddRoute::Main);
    QCOMPARE(routeAdd(AddSource::Bus, false, false, grace + 1).route, AddRoute::Window);
    // A hand-off counts only focus now, however recent.
    QCOMPARE(routeAdd(AddSource::Handoff, false, false, 0).route, AddRoute::Window);
}

void TestAddRoute::requestAddActsOnIt()
{
    QFETCH(AddSource, source);
    QFETCH(int, focus);
    QFETCH(bool, firstRun);
    QFETCH(AddRoute, route);
    QFETCH(bool, raise);

    App app;
    if (!firstRun)
        app.startDemo(4); // a library to add to, and never the network
    qint64 now = 1'000'000;
    app.setAddClock([&now]() { return now; });
    switch (Focus(focus)) {
    case Focus::Now:
        app.setMainWindowActive(true);
        break;
    case Focus::ThreeSecondsAgo:
    case Focus::ThirtySecondsAgo:
        app.setMainWindowActive(true);
        app.setMainWindowActive(false);
        now += msSince(Focus(focus));
        break;
    case Focus::Never:
        break;
    }

    QSignalSpy raised(&app, &App::raiseRequested);
    QSignalSpy blank(&app, &App::addRequested);
    QSignalSpy listed(&app, &App::addUrisRequested);
    QSignalSpy window(&app, &App::addWindowRequested);
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071");

    // With sources: file:// URIs reach either window as paths.
    app.requestAdd({magnet, QStringLiteral("file:///tmp/a%20b.torrent")}, source);
    const QStringList readable{magnet, QStringLiteral("/tmp/a b.torrent")};
    QCOMPARE(raised.count(), raise ? 1 : 0);
    QCOMPARE(blank.count(), 0);
    if (route == AddRoute::Main) {
        QCOMPARE(listed.count(), 1);
        QCOMPARE(listed.first().first().toStringList(), readable);
        QCOMPARE(window.count(), 0);
    } else {
        QCOMPARE(window.count(), 1);
        QCOMPARE(window.first().first().toStringList(), readable);
        QCOMPARE(listed.count(), 0);
    }

    // With none (the menu's add, the widget's add with nothing ticked): the blank sheet.
    app.requestAdd({}, source);
    QCOMPARE(raised.count(), raise ? 2 : 0);
    if (route == AddRoute::Main) {
        QCOMPARE(blank.count(), 1);
        QCOMPARE(window.count(), 0);
    } else {
        QCOMPARE(blank.count(), 0);
        QCOMPARE(window.count(), 2);
        QVERIFY(window.last().first().toStringList().isEmpty());
    }
}

void TestAddRoute::theMenusAddGoesThroughIt()
{
    App app;
    app.startDemo(4);
    qint64 now = 0;
    app.setAddClock([&now]() { return now; });
    QSignalSpy raised(&app, &App::raiseRequested);
    QSignalSpy blank(&app, &App::addRequested);
    QSignalSpy window(&app, &App::addWindowRequested);

    // Never focused: the add window, blank, and the main window stays where it is.
    QVERIFY(app.barMenu()->invoke(QStringLiteral("add")));
    QCOMPARE(window.count(), 1);
    QCOMPARE(blank.count(), 0);
    QCOMPARE(raised.count(), 0);

    // The window's mark (or the tray a moment after the window had focus): its own sheet.
    app.setMainWindowActive(true);
    app.setMainWindowActive(false);
    now += 3'000;
    QVERIFY(app.barMenu()->invoke(QStringLiteral("add")));
    QCOMPARE(blank.count(), 1);
    QCOMPARE(window.count(), 1);
    QCOMPARE(raised.count(), 0);
}

void TestAddRoute::theBusGoesThroughIt()
{
    // First run (no server, no demo): the main window comes up and holds the add (W6). The
    // service itself no longer raises.
    App app;
    StatusService service(&app);
    QSignalSpy raised(&app, &App::raiseRequested);
    QSignalSpy serviceRaised(&service, &StatusService::raiseRequested);
    QSignalSpy listed(&app, &App::addUrisRequested);
    QSignalSpy window(&app, &App::addWindowRequested);
    service.openAddSheet({QStringLiteral("file:///tmp/a%20b.torrent")});
    QCOMPARE(raised.count(), 1);
    QCOMPARE(serviceRaised.count(), 0);
    QCOMPARE(listed.count(), 1);
    QCOMPARE(window.count(), 0);

    // With a library and the main window elsewhere for a while: the add window.
    App demo;
    demo.startDemo(4);
    StatusService demoService(&demo);
    QSignalSpy demoWindow(&demo, &App::addWindowRequested);
    QSignalSpy demoRaised(&demo, &App::raiseRequested);
    demoService.openAddSheet({});
    QCOMPARE(demoWindow.count(), 1);
    QVERIFY(demoWindow.first().first().toStringList().isEmpty());
    QCOMPARE(demoRaised.count(), 0);
}

QTEST_MAIN(TestAddRoute)
#include "tst_addroute.moc"
