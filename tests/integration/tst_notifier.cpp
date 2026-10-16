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
    void aDaemonRestartForgetsTheLiveIds(); // INT-9
    void aServiceThatAppearsLaterIsUsed();  // INT-9

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
                          QStringLiteral("open folder"),
                          QStringLiteral("show"),
                          QStringLiteral("show"),
                          QStringLiteral("default"),
                          QStringLiteral("show")}));
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
             QStringList({QStringLiteral("show"),
                          QStringLiteral("show"),
                          QStringLiteral("default"),
                          QStringLiteral("show")}));
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
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("settings"),
                          QStringLiteral("settings"),
                          QStringLiteral("default"),
                          QStringLiteral("settings")}));
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
