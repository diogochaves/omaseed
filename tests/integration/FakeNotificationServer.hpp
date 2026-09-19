#pragma once

// FakeNotificationServer — stands in for the shell's org.freedesktop.Notifications service on
// a private session bus, so a test never touches whatever notification daemon runs on the
// real desktop. Shared between tst_notifier.cpp and tst_app_notifications.cpp.

#include <QDBusConnection>
#include <QList>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

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

/// A private `dbus-daemon --session` plus a registered FakeNotificationServer: everything a
/// test needs to construct a real `omaseed::integration::Notifier` against, with no dependency
/// on the desktop's own notification daemon. One instance per test class; call start() from
/// initTestCase() and stop() from cleanupTestCase().
class FakeNotificationBus
{
public:
    bool start()
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

        const QString address = QString::fromUtf8(line.split('\n').constFirst()).trimmed();
        if (address.isEmpty())
            return false;

        m_connection =
            QDBusConnection::connectToBus(address, QStringLiteral("omaseedFakeNotifyBus"));
        if (!m_connection.isConnected())
            return false;

        m_server = new FakeNotificationServer();
        if (!m_connection.registerObject(QStringLiteral("/org/freedesktop/Notifications"),
                                         QStringLiteral("org.freedesktop.Notifications"),
                                         m_server,
                                         QDBusConnection::ExportAllSlots |
                                             QDBusConnection::ExportAllSignals))
            return false;
        return m_connection.registerService(QStringLiteral("org.freedesktop.Notifications"));
    }

    void stop()
    {
        if (m_connection.isConnected()) {
            m_connection.unregisterService(QStringLiteral("org.freedesktop.Notifications"));
            m_connection.unregisterObject(QStringLiteral("/org/freedesktop/Notifications"));
        }
        delete m_server;
        m_server = nullptr;
        QDBusConnection::disconnectFromBus(QStringLiteral("omaseedFakeNotifyBus"));

        m_daemon.terminate();
        if (!m_daemon.waitForFinished(2000))
            m_daemon.kill();
    }

    QDBusConnection connection() const { return m_connection; }
    FakeNotificationServer *server() const { return m_server; }

private:
    QProcess m_daemon;
    QDBusConnection m_connection{QStringLiteral("uninitialised")};
    FakeNotificationServer *m_server = nullptr;
};
