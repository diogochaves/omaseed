#include "integration/Notifier.hpp"

#include <QDBusConnection>
#include <QList>
#include <QProcess>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariant>
#include <QVariantMap>

using omaseed::integration::Notifier;

namespace {

/// Stands in for the shell's org.freedesktop.Notifications service on a private session bus,
/// so the test never touches whatever notification daemon runs on the real desktop.
class FakeNotificationServer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    struct Call
    {
        QString appName;
        uint replacesId = 0;
        QString appIcon;
        QString summary;
        QString body;
        QStringList actions;
        QVariantMap hints;
        int expireTimeout = 0;
    };

    explicit FakeNotificationServer(QObject *parent = nullptr)
        : QObject(parent)
    {}

    QList<Call> calls;
    uint nextId = 1;

public slots:
    uint Notify(const QString &appName,
                uint replacesId,
                const QString &appIcon,
                const QString &summary,
                const QString &body,
                const QStringList &actions,
                const QVariantMap &hints,
                int expireTimeout)
    {
        calls.append(
            Call{appName, replacesId, appIcon, summary, body, actions, hints, expireTimeout});
        return replacesId != 0 ? replacesId : nextId++;
    }

    QString GetServerInformation(QString &vendor, QString &version, QString &specVersion)
    {
        vendor = QStringLiteral("omaseed-test");
        version = QStringLiteral("1.0");
        specVersion = QStringLiteral("1.2");
        return QStringLiteral("fake-notify");
    }

    QStringList GetCapabilities() { return {QStringLiteral("actions"), QStringLiteral("body")}; }

    /// Test-only helpers: emitting a signal from outside the class it is declared on is not
    /// otherwise possible, so the test drives the shell's side of the protocol through these.
    void triggerActionInvoked(uint id, const QString &actionKey)
    {
        emit ActionInvoked(id, actionKey);
    }
    void triggerNotificationClosed(uint id, uint reason) { emit NotificationClosed(id, reason); }

signals:
    void ActionInvoked(uint id, const QString &actionKey);
    void NotificationClosed(uint id, uint reason);
};

} // namespace

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
    void trackerErrorBodyAndActionsMatchSpec();
    void serverLostBodyAndActionsMatchSpec();
    void serverBackReplacesTheServerLostNotice();
    void everyNoticeCarriesTheDesktopEntryHint();
    void secondTrackerErrorForSameHostReusesTheId();
    void actionInvokedCarriesClassSubjectAndKey();
    void notificationClosedForgetsTheKey();

private:
    bool startPrivateBus();

    QProcess m_daemon;
    QString m_address;
    QDBusConnection m_connection{QStringLiteral("uninitialised")};
    FakeNotificationServer *m_server = nullptr;
    Notifier *m_notifier = nullptr;
};

bool TestNotifier::startPrivateBus()
{
    m_daemon.setProgram(QStringLiteral("dbus-daemon"));
    m_daemon.setArguments({QStringLiteral("--session"),
                           QStringLiteral("--print-address"),
                           QStringLiteral("--nofork")});
    m_daemon.start();
    if (!m_daemon.waitForStarted(5000))
        return false;

    QByteArray line;
    for (int i = 0; i < 50 && !line.contains('\n'); ++i) {
        if (m_daemon.waitForReadyRead(200))
            line += m_daemon.readAllStandardOutput();
    }
    if (!line.contains('\n'))
        return false;

    m_address = QString::fromUtf8(line.split('\n').constFirst()).trimmed();
    return !m_address.isEmpty();
}

void TestNotifier::initTestCase()
{
    QVERIFY2(startPrivateBus(), "could not start a private session dbus-daemon for the test");

    m_connection = QDBusConnection::connectToBus(m_address, QStringLiteral("omaseedNotifierTest"));
    QVERIFY(m_connection.isConnected());

    m_server = new FakeNotificationServer(this);
    QVERIFY(m_connection.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                                        QStringLiteral("org.freedesktop.Notifications"),
                                        m_server,
                                        QDBusConnection::ExportAllSlots |
                                            QDBusConnection::ExportAllSignals));
    QVERIFY(m_connection.registerService(QStringLiteral("org.freedesktop.Notifications")));
}

void TestNotifier::cleanupTestCase()
{
    if (m_connection.isConnected()) {
        m_connection.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
        m_connection.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("omaseedNotifierTest"));

    m_daemon.terminate();
    if (!m_daemon.waitForFinished(2000))
        m_daemon.kill();
}

void TestNotifier::init()
{
    m_server->calls.clear();
    m_server->nextId = 1;
    m_notifier = new Notifier(m_connection, this);
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
             QStringList({QStringLiteral("show"), QStringLiteral("show")}));
}

void TestNotifier::trackerErrorBodyAndActionsMatchSpec()
{
    m_notifier->notifyTrackerError(QStringLiteral("tracker.opentrackr.org"), 3);
    QCOMPARE(m_server->calls.size(), 1);

    const auto &call = m_server->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Tracker error"));
    QCOMPARE(call.body, QStringLiteral("tracker.opentrackr.org · timed out for 3 torrents"));
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("show-trackers"),
                          QStringLiteral("show trackers"),
                          QStringLiteral("reannounce"),
                          QStringLiteral("reannounce")}));
    QCOMPARE(call.hints.value(QStringLiteral("category")).toString(),
             QStringLiteral("network.error"));
}

void TestNotifier::serverLostBodyAndActionsMatchSpec()
{
    m_notifier->notifyServerLost(QStringLiteral("home"),
                                 QStringLiteral("home"),
                                 QStringLiteral("nas.local:9090"),
                                 QStringLiteral("connection refused"),
                                 10);
    QCOMPARE(m_server->calls.size(), 1);

    const auto &call = m_server->calls.first();
    QCOMPARE(call.summary, QStringLiteral("home is unreachable"));
    QCOMPARE(call.body,
             QStringLiteral("nas.local:9090 · connection refused · retrying every 10 s"));
    QCOMPARE(call.actions, QStringList({QStringLiteral("settings"), QStringLiteral("settings")}));
    QCOMPARE(call.hints.value(QStringLiteral("category")).toString(),
             QStringLiteral("network.disconnected"));
}

void TestNotifier::serverBackReplacesTheServerLostNotice()
{
    const quint32 lostId = m_notifier->notifyServerLost(QStringLiteral("home"),
                                                        QStringLiteral("home"),
                                                        QStringLiteral("nas.local:9090"),
                                                        QStringLiteral("connection refused"),
                                                        10);
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

void TestNotifier::secondTrackerErrorForSameHostReusesTheId()
{
    const quint32 first =
        m_notifier->notifyTrackerError(QStringLiteral("tracker.opentrackr.org"), 3);
    const quint32 second =
        m_notifier->notifyTrackerError(QStringLiteral("tracker.opentrackr.org"), 5);

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

void TestNotifier::notificationClosedForgetsTheKey()
{
    const quint32 id = m_notifier->notifyTrackerError(QStringLiteral("tracker.opentrackr.org"), 3);
    QVERIFY(id != 0);

    QSignalSpy closedSpy(m_notifier, &Notifier::noticeClosed);
    m_server->triggerNotificationClosed(id, 1);
    QVERIFY(closedSpy.wait(2000));

    m_notifier->notifyTrackerError(QStringLiteral("tracker.opentrackr.org"), 1);
    QCOMPARE(m_server->calls.size(), 2);
    QCOMPARE(m_server->calls.at(1).replacesId, 0u);
}

QTEST_GUILESS_MAIN(TestNotifier)

#include "tst_notifier.moc"
