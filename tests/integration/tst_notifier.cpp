#include "integration/Notifier.hpp"

#include "FakeNotificationServer.hpp"

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>

using omaseed::integration::Notifier;

class TestNotifier : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void completedBodyAndActionsMatchSpec();
    void completedWithoutFolderOmitsOpenFolderAction();
    void serverLostBodyAndActionsMatchSpec();
    void serverBackReplacesTheServerLostNotice();
    void everyNoticeCarriesTheDesktopEntryHint();
    void secondDiskNoticeForSameServerReusesTheId();
    void actionInvokedCarriesClassSubjectAndKey();
    void clickOnTheBodyActsAsTheMainButton();
    void notificationClosedForgetsTheKey();
    void aMoveIsOneNoticePerServer();
    void aCreationIsOneNoticePerServer();
    void theMainActionIsTheClickAndNoButtonOfItsOwn(); // #111
    void aDaemonRestartForgetsTheLiveIds();            // INT-9
    void aServiceThatAppearsLaterIsUsed();             // INT-9

private:
    FakeNotificationBus m_bus;
    FakeNotificationServer *m_server = nullptr;
    Notifier *m_notifier = nullptr;
};

void TestNotifier::initTestCase()
{
    QVERIFY2(m_bus.start(), "could not start a private session dbus-daemon for the test");
    m_server = m_bus.server();
    QVERIFY(m_server != nullptr);
}

void TestNotifier::cleanupTestCase()
{
    m_bus.stop();
    m_server = nullptr;
}

void TestNotifier::init()
{
    m_server->calls.clear();
    m_server->nextId = 1;
    m_notifier = new Notifier(m_bus.connection(), this);
}

void TestNotifier::cleanup()
{
    delete m_notifier;
    m_notifier = nullptr;
}

void TestNotifier::completedBodyAndActionsMatchSpec()
{
    const quint32 id =
        m_notifier->notifyCompleted(QStringLiteral("abc123"),
                                    QStringLiteral("archlinux-2026.09.01-x86_64.iso"),
                                    QStringLiteral("1.31 GB"),
                                    QStringLiteral("linux"),
                                    /*hasFolder=*/true);
    QVERIFY(id != 0);
    QCOMPARE(m_server->calls.size(), 1);

    const auto &call = m_server->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Download complete"));
    QCOMPARE(call.body, QStringLiteral("archlinux-2026.09.01-x86_64.iso · 1.31 GB · linux"));
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("open-folder"),
                          QStringLiteral("Open folder"),
                          QStringLiteral("default"),
                          QStringLiteral("Show")}));
    QCOMPARE(call.hints.value(QStringLiteral("category")).toString(),
             QStringLiteral("transfer.complete"));
}

void TestNotifier::completedWithoutFolderOmitsOpenFolderAction()
{
    m_notifier->notifyCompleted(QStringLiteral("abc123"),
                                QStringLiteral("archlinux-2026.09.01-x86_64.iso"),
                                QStringLiteral("1.31 GB"),
                                QStringLiteral("linux"),
                                /*hasFolder=*/false);
    QCOMPARE(m_server->calls.size(), 1);
    QCOMPARE(m_server->calls.first().actions,
             QStringList({QStringLiteral("default"), QStringLiteral("Show")}));
}

void TestNotifier::serverLostBodyAndActionsMatchSpec()
{
    m_notifier->notifyServerLost(
        QStringLiteral("home"),
        QStringLiteral("home didn't answer"),
        QStringLiteral("Nothing is listening at nas.local:9090. omaseed tries again in 10 s."));
    QCOMPARE(m_server->calls.size(), 1);

    const auto &call = m_server->calls.first();
    QCOMPARE(call.summary, QStringLiteral("home didn't answer"));
    QCOMPARE(
        call.body,
        QStringLiteral("Nothing is listening at nas.local:9090. omaseed tries again in 10 s."));
    QCOMPARE(call.actions, QStringList({QStringLiteral("default"), QStringLiteral("Settings")}));
    QCOMPARE(call.hints.value(QStringLiteral("category")).toString(),
             QStringLiteral("network.disconnected"));
}

void TestNotifier::serverBackReplacesTheServerLostNotice()
{
    const quint32 lostId = m_notifier->notifyServerLost(QStringLiteral("home"),
                                                        QStringLiteral("home didn't answer"),
                                                        QStringLiteral("Nothing is listening."));
    QVERIFY(lostId != 0);

    m_notifier->notifyServerBack(QStringLiteral("home"), QStringLiteral("home"));
    QCOMPARE(m_server->calls.size(), 2);
    QCOMPARE(m_server->calls.at(1).replacesId, lostId);
    QCOMPARE(m_server->calls.at(1).summary, QStringLiteral("home is back"));

    // docs/44 §6: after a timeout the server answered again; it was never gone.
    m_notifier->notifyServerBack(QStringLiteral("home"), QStringLiteral("home"), true);
    QCOMPARE(m_server->calls.at(2).summary, QStringLiteral("home answered again"));
}

void TestNotifier::everyNoticeCarriesTheDesktopEntryHint()
{
    m_notifier->notifyCompleted(QStringLiteral("abc123"),
                                QStringLiteral("name"),
                                QStringLiteral("1 GB"),
                                QStringLiteral("linux"),
                                true);
    QCOMPARE(m_server->calls.size(), 1);
    QCOMPARE(m_server->calls.first().hints.value(QStringLiteral("desktop-entry")).toString(),
             QStringLiteral("io.github.diogochaves.omaseed"));
}

void TestNotifier::secondDiskNoticeForSameServerReusesTheId()
{
    const quint32 first =
        m_notifier->notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1.8 GB"));
    const quint32 second =
        m_notifier->notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1.2 GB"));

    QVERIFY(first != 0);
    QCOMPARE(second, first);
    QCOMPARE(m_server->calls.size(), 2);
    QCOMPARE(m_server->calls.at(1).replacesId, first);
}

void TestNotifier::actionInvokedCarriesClassSubjectAndKey()
{
    const quint32 id = m_notifier->notifyCompleted(QStringLiteral("abc123"),
                                                   QStringLiteral("name"),
                                                   QStringLiteral("1 GB"),
                                                   QStringLiteral("linux"),
                                                   true);
    QVERIFY(id != 0);

    QSignalSpy spy(m_notifier, &Notifier::actionInvoked);
    m_server->triggerActionInvoked(id, QStringLiteral("open-folder"));
    QVERIFY(spy.wait(2000));

    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("completed"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("abc123"));
    QCOMPARE(spy.first().at(2).toString(), QStringLiteral("open-folder"));
}

void TestNotifier::clickOnTheBodyActsAsTheMainButton()
{
    const quint32 id = m_notifier->notifyCompleted(QStringLiteral("abc123"),
                                                   QStringLiteral("name"),
                                                   QStringLiteral("1 GB"),
                                                   QStringLiteral("linux"),
                                                   true);
    QVERIFY(id != 0);

    QSignalSpy spy(m_notifier, &Notifier::actionInvoked);
    m_server->triggerActionInvoked(id, QStringLiteral("default"));
    QVERIFY(spy.wait(2000));

    QCOMPARE(spy.size(), 1);
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("completed"));
    QCOMPARE(spy.first().at(2).toString(), QStringLiteral("show"));
}

// Board 42 E2 (DECISIONS C15 M16): one notice for a folder's move, and a click shows it.
void TestNotifier::aMoveIsOneNoticePerServer()
{
    const quint32 id = m_notifier->notifyMove(QStringLiteral("home"),
                                              QStringLiteral("games moved"),
                                              QStringLiteral("385 moved, 2 failed"));
    QVERIFY(id != 0);
    QCOMPARE(m_server->calls.at(0).summary, QStringLiteral("games moved"));
    QCOMPARE(m_server->calls.at(0).body, QStringLiteral("385 moved, 2 failed"));
    QCOMPARE(m_server->calls.at(0).actions,
             QStringList({QStringLiteral("default"), QStringLiteral("Show")}));
    // A second for the same server takes the first one's place.
    const quint32 second = m_notifier->notifyMove(
        QStringLiteral("home"), QStringLiteral("games moved"), QStringLiteral("loose files too"));
    QCOMPARE(second, id);

    QSignalSpy spy(m_notifier, &Notifier::actionInvoked);
    m_server->triggerActionInvoked(id, QStringLiteral("default"));
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("move"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("home"));
    QCOMPARE(spy.first().at(2).toString(), QStringLiteral("show"));
}

// #146 (DECISIONS C27 CR13): a creation's notice, its own class, a click shows the window.
void TestNotifier::aCreationIsOneNoticePerServer()
{
    const quint32 id = m_notifier->notifyCreate(QStringLiteral("home"),
                                                QStringLiteral("photo-walks is made"),
                                                QStringLiteral("212 files, 1.8 GB"));
    QVERIFY(id != 0);
    QCOMPARE(m_server->calls.at(0).summary, QStringLiteral("photo-walks is made"));
    QSignalSpy spy(m_notifier, &Notifier::actionInvoked);
    m_server->triggerActionInvoked(id, QStringLiteral("default"));
    QVERIFY(spy.wait(2000));
    QCOMPARE(spy.first().at(0).toString(), QStringLiteral("create"));
    QCOMPARE(spy.first().at(1).toString(), QStringLiteral("home"));
}

// #111: Omarchy 4.5 draws a notice's actions as buttons, "default" aside (the click). The main
// action goes only as "default", so it is no second button doing what the click does, and its
// label is sentence case like the shell's own. 4.0.x draws no buttons and invokes "default".
void TestNotifier::theMainActionIsTheClickAndNoButtonOfItsOwn()
{
    m_notifier->notifyCompleted(QStringLiteral("a"),
                                QStringLiteral("name"),
                                QStringLiteral("1 GB"),
                                QStringLiteral("linux"),
                                /*hasFolder=*/true);
    m_notifier->notifyErrored(
        QStringLiteral("b"), QStringLiteral("name"), QStringLiteral("headline"), QString());
    m_notifier->notifyServerLost(
        QStringLiteral("home"), QStringLiteral("home didn't answer"), QStringLiteral("line"));
    m_notifier->notifyDiskAlmostFull(QStringLiteral("disk"), QStringLiteral("1 GB"));
    m_notifier->notifyWeeklyRecap(
        QStringLiteral("recap"), QStringLiteral("Your week"), QStringLiteral("line"));
    m_notifier->notifyMove(
        QStringLiteral("move"), QStringLiteral("games moved"), QStringLiteral("line"));
    QCOMPARE(m_server->calls.size(), 6);

    const QStringList mainKeys{QStringLiteral("show"),
                               QStringLiteral("show"),
                               QStringLiteral("settings"),
                               QStringLiteral("settings"),
                               QStringLiteral("console"),
                               QStringLiteral("show")};
    for (qsizetype n = 0; n < m_server->calls.size(); ++n) {
        const QStringList actions = m_server->calls.at(n).actions;
        QVERIFY2(actions.size() % 2 == 0, qPrintable(actions.join(u',')));
        QCOMPARE(actions.count(QStringLiteral("default")), 1);
        QVERIFY2(!actions.contains(mainKeys.at(n)), qPrintable(actions.join(u',')));
        for (qsizetype i = 1; i < actions.size(); i += 2)
            QVERIFY2(actions.at(i).at(0).isUpper(), qPrintable(actions.at(i)));
    }
    // The one other button: the completed notice's "Open folder", before the click.
    QCOMPARE(m_server->calls.first().actions.first(), QStringLiteral("open-folder"));
}

void TestNotifier::notificationClosedForgetsTheKey()
{
    const quint32 id =
        m_notifier->notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1.8 GB"));
    QVERIFY(id != 0);

    QSignalSpy closedSpy(m_notifier, &Notifier::noticeClosed);
    m_server->triggerNotificationClosed(id, 1);
    QVERIFY(closedSpy.wait(2000));

    m_notifier->notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1.2 GB"));
    QCOMPARE(m_server->calls.size(), 2);
    QCOMPARE(m_server->calls.at(1).replacesId, 0u);
}

void TestNotifier::aDaemonRestartForgetsTheLiveIds()
{
    const quint32 id =
        m_notifier->notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1.8 GB"));
    QVERIFY(id != 0);

    // The shell restarts: the name goes away and comes back (a new daemon, ids from 1).
    const QString name = QStringLiteral("org.freedesktop.Notifications");
    QVERIFY(m_bus.connection().unregisterService(name));
    QVERIFY(m_bus.connection().registerService(name));
    QTest::qWait(200); // the owner-change signals

    m_notifier->notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1.2 GB"));
    QCOMPARE(m_server->calls.size(), 2);
    // A stale replaces_id could replace some other app's notice in the new daemon.
    QCOMPARE(m_server->calls.at(1).replacesId, 0u);
}

void TestNotifier::aServiceThatAppearsLaterIsUsed()
{
    const QString name = QStringLiteral("org.freedesktop.Notifications");
    QVERIFY(m_bus.connection().unregisterService(name));
    Notifier early(m_bus.connection());
    QCOMPARE(early.notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1 GB")), 0u);

    QVERIFY(m_bus.connection().registerService(name));
    QTest::qWait(200);
    QVERIFY(early.notifyDiskAlmostFull(QStringLiteral("home"), QStringLiteral("1 GB")) != 0u);
}

QTEST_GUILESS_MAIN(TestNotifier)

#include "tst_notifier.moc"
