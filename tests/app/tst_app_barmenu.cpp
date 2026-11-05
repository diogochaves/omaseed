// #73 — App's side of the shared menu (docs/33 §11, D10, D24, D25; DECISIONS C13): the menu
// follows the server list (the block only with two servers or more), its rows reach the app's
// own actions, the widget's Invoke and SwitchServer go through StatusService, and Settings ›
// Appearance's "In the bar" reads and writes the two old settings as one. Demo mode: no
// server is contacted.

#include "core/PauseHold.hpp"
#include "core/ServerStore.hpp"
#include "core/StateFile.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "integration/BarMenu.hpp"
#include "integration/BarWidget.hpp"
#include "integration/Tray.hpp"
#include "ui/app/App.hpp"
#include "ui/app/StatusService.hpp"

#include <QAction>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMenu>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <chrono>
#include <functional>
#include <memory>

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

/// Three torrents, as qBittorrent answers for them: `a` downloading, `b` force-started and
/// seeding, `c` kept stopped on purpose. torrents/stop and torrents/start move them; every
/// hash list they were sent is kept.
class PauseServer : public QTcpServer
{
public:
    QHash<QString, QString> state{{QStringLiteral("a"), QStringLiteral("downloading")},
                                  {QStringLiteral("b"), QStringLiteral("forcedUP")},
                                  {QStringLiteral("c"), QStringLiteral("stoppedDL")}};
    QStringList stops;
    QStringList starts;
    QStringList forced;
    /// While true, a stop is taken (the torrents stop) but its answer waits for `answerStop`,
    /// as a slow server's would.
    bool holdStopAnswer = false;
    std::function<void()> answerStop;

    static QString hash(const QString &name) { return name.repeated(40); }

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            qsizetype length = 0;
            for (const QByteArray &line : buffer->left(headerEnd).split('\n'))
                if (line.toLower().startsWith("content-length:"))
                    length = line.mid(15).trimmed().toLongLong();
            if (buffer->size() < headerEnd + 4 + length)
                return;
            const QByteArray target = buffer->left(buffer->indexOf('\n')).split(' ').value(1);
            const QUrlQuery form(QString::fromUtf8(buffer->mid(headerEnd + 4, length)));
            buffer->clear();
            const auto names = [this](const QString &hashes) {
                QStringList out;
                for (const QString &h : hashes.split(u'|'))
                    out << h.left(1);
                out.sort();
                return out;
            };

            QByteArray body = "Ok.";
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.3";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = "2.15.1";
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                QJsonObject torrents;
                for (auto it = state.cbegin(); it != state.cend(); ++it)
                    torrents.insert(hash(it.key()),
                                    QJsonObject{{QStringLiteral("name"), it.key()},
                                                {QStringLiteral("state"), it.value()}});
                body = QJsonDocument(QJsonObject{{QStringLiteral("rid"), 1},
                                                 {QStringLiteral("full_update"), true},
                                                 {QStringLiteral("torrents"), torrents}})
                           .toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/torrents/info")) {
                const bool stopped = target.contains("filter=stopped");
                QJsonArray list;
                for (auto it = state.cbegin(); it != state.cend(); ++it) {
                    if (it.value().startsWith(QLatin1String("stopped")) != stopped)
                        continue;
                    list.append(QJsonObject{
                        {QStringLiteral("hash"), hash(it.key())},
                        {QStringLiteral("state"), it.value()},
                        {QStringLiteral("force_start"), it.value() == QLatin1String("forcedUP")}});
                }
                body = QJsonDocument(list).toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/torrents/stop")) {
                const QStringList picked = names(form.queryItemValue(QStringLiteral("hashes")));
                stops << picked.join(u',');
                for (const QString &name : picked)
                    state.insert(name, QStringLiteral("stoppedUP"));
                if (holdStopAnswer) {
                    answerStop = [socket]() {
                        socket->write("HTTP/1.1 200 OK\r\nContent-Length: 3\r\n"
                                      "Connection: close\r\n\r\nOk.");
                        socket->disconnectFromHost();
                    };
                    return;
                }
            } else if (target.startsWith("/api/v2/torrents/start")) {
                const QStringList picked = names(form.queryItemValue(QStringLiteral("hashes")));
                starts << picked.join(u',');
                for (const QString &name : picked)
                    state.insert(name, QStringLiteral("uploading"));
            } else if (target.startsWith("/api/v2/torrents/setForceStart")) {
                const QStringList picked = names(form.queryItemValue(QStringLiteral("hashes")));
                forced << picked.join(u',');
                for (const QString &name : picked)
                    state.insert(name, QStringLiteral("forcedUP"));
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

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
    void pauseHoldsWhatRanAndResumeStartsOnlyThat();
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
    // C36 M10: the demo stops nothing, but the row is there; resume needs a held pause.
    QVERIFY(menu->invoke(QStringLiteral("pause")));
    QVERIFY(menu->invoke(QStringLiteral("stop-all")));
    QVERIFY(!menu->invoke(QStringLiteral("resume")));
    QVERIFY(!app.pauseHold()->held());
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

// DECISIONS C36 M10: Pause Torrents stops what runs and holds it, in state.json; the row turns
// into Resume Torrents, which starts exactly those again (the force-started one forced) and
// leaves the one kept stopped on purpose as it was.
void TestAppBarMenu::pauseHoldsWhatRanAndResumeStartsOnlyThat()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    PauseServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    struct Unset
    {
        ~Unset() { qunsetenv("OMASEED_SERVER_URL"); }
    } unset;

    App app;
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    BarMenu *menu = app.barMenu();
    const auto rowId = [menu]() { return menu->rows().at(1).id; };
    QTRY_VERIFY_WITH_TIMEOUT(app.torrents()->count() == 3 && menu->rows().at(1).enabled, 5000);
    QCOMPARE(rowId(), QStringLiteral("pause"));
    QVERIFY(!app.pauseHold()->server().isEmpty());

    server.holdStopAnswer = true;
    QVERIFY(app.invokeAppMenu(QStringLiteral("pause")));
    // On its way: the row is off, and a second pick is refused rather than sent.
    QVERIFY(app.pauseHold()->busy());
    QVERIFY(!menu->rows().at(1).enabled);
    QVERIFY(!app.invokeAppMenu(QStringLiteral("pause")));
    QTRY_COMPARE_WITH_TIMEOUT(server.stops, QStringList{QStringLiteral("a,b")}, 5000);
    // The server has the stop before omaseed has its answer: the row reads Resume from the
    // hold, but stays off, and a resume is not taken, until the answer comes.
    QCOMPARE(rowId(), QStringLiteral("resume"));
    QVERIFY(app.pauseHold()->busy());
    QVERIFY(!menu->rows().at(1).enabled);
    QVERIFY(!menu->invoke(QStringLiteral("resume")));
    QVERIFY(server.answerStop);
    server.answerStop();
    QTRY_VERIFY_WITH_TIMEOUT(!app.pauseHold()->busy(), 5000);
    QVERIFY(menu->rows().at(1).enabled);
    QCOMPARE(menu->rows().at(1).label, QStringLiteral("resume torrents"));
    // Held in state.json, under the server it was paused on.
    const QJsonObject held = omaseed::core::StateFile::readSection(QStringLiteral("pauseHold"))
                                 .value(app.pauseHold()->server())
                                 .toObject();
    QCOMPARE(held.value(QStringLiteral("hashes")).toArray().size(), 2);
    QCOMPARE(held.value(QStringLiteral("forced")).toArray(),
             QJsonArray{PauseServer::hash(QStringLiteral("b"))});

    // C13's old id still reaches it; the resume's start on its way turns the row off too.
    QVERIFY(menu->invoke(QStringLiteral("start-all")));
    QVERIFY(!menu->rows().at(1).enabled);
    QTRY_COMPARE_WITH_TIMEOUT(rowId(), QStringLiteral("pause"), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(menu->rows().at(1).enabled, 5000);
    QCOMPARE(server.starts, QStringList{QStringLiteral("a")});
    QCOMPARE(server.forced, QStringList{QStringLiteral("b")});
    QCOMPARE(server.state.value(QStringLiteral("c")), QStringLiteral("stoppedDL"));
    QVERIFY(omaseed::core::StateFile::readSection(QStringLiteral("pauseHold")).isEmpty());
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
                          QStringLiteral("pause"),
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
