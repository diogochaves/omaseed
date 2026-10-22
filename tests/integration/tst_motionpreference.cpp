// #24 T5 (docs/22 §6): "off under reduced motion". MotionPreference reads the desktop's
// reduced-motion setting from the settings portal and follows it when it changes; a portal
// that is missing or does not know the key reads as no preference. #111: Hyprland's
// animations turned off (Omarchy's "no animations" toggle) reduce motion too, and a
// `configreloaded` asks again.

#include "integration/MotionPreference.hpp"

#include <QDBusConnection>
#include <QDBusVariant>
#include <QDeadlineTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::integration::MotionPreference;

namespace {

const QString kService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPath = QStringLiteral("/org/freedesktop/portal/desktop");

/// The settings portal's two calls a reader needs, answering from `values`.
class FakePortal : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.portal.Settings")

public:
    QHash<QString, QVariant> values; ///< "namespace key" -> value; 0 when not there

    void change(const QString &ns, const QString &key, const QVariant &value)
    {
        values.insert(ns + u' ' + key, value);
        emit SettingChanged(ns, key, QDBusVariant(value));
    }

public slots:
    QDBusVariant ReadOne(const QString &ns, const QString &key)
    {
        return QDBusVariant(values.value(ns + u' ' + key, QVariant(0u)));
    }

signals:
    void SettingChanged(const QString &ns, const QString &key, const QDBusVariant &value);
};

/// A stand-in for Hyprland: a `hyprctl` that prints `<dir>/<option>` (as `hyprctl -j getoption
/// <option>` would), and the event socket, a local server.
class FakeHyprland
{
public:
    FakeHyprland()
    {
        QFile file(hyprctl());
        if (file.open(QIODevice::WriteOnly)) {
            file.write(
                "#!/bin/sh\ncat \"$(dirname \"$0\")/$3\" 2>/dev/null || echo 'no such option'\n");
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        m_server.listen(socketPath());
    }

    QString hyprctl() const { return m_dir.filePath(QStringLiteral("hyprctl")); }
    QString socketPath() const { return m_dir.filePath(QStringLiteral("socket2.sock")); }

    /// As Hyprland 0.56 prints it.
    void setAnimations(bool enabled)
    {
        QFile file(m_dir.filePath(QStringLiteral("animations:enabled")));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QByteArray("{\"option\": \"animations:enabled\", \"bool\": ") +
                       (enabled ? "true" : "false") + ", \"set\": true }");
    }

    QLocalSocket *accept(int timeoutMs = 5000)
    {
        QDeadlineTimer deadline(timeoutMs);
        while (!m_server.hasPendingConnections() && !deadline.hasExpired())
            QTest::qWait(20);
        return m_server.nextPendingConnection();
    }

private:
    QTemporaryDir m_dir;
    QLocalServer m_server;
};

/// No Hyprland: the portal alone.
const MotionPreference::Hyprland kNoHyprland{QString(), QString()};

class PrivateBus
{
public:
    bool start()
    {
        // A bus of its own with no service files: a call to the portal must not start the real
        // xdg-desktop-portal on it (a stock session bus would activate it).
        if (!m_dir.isValid())
            return false;
        const QString config = m_dir.filePath(QStringLiteral("bus.conf"));
        QFile file(config);
        if (!file.open(QIODevice::WriteOnly))
            return false;
        file.write(QStringLiteral("<busconfig><type>session</type>"
                                  "<listen>unix:dir=%1</listen>"
                                  "<policy context=\"default\"><allow send_destination=\"*\"/>"
                                  "<allow eavesdrop=\"true\"/><allow own=\"*\"/></policy>"
                                  "</busconfig>")
                       .arg(m_dir.path())
                       .toUtf8());
        file.close();
        m_daemon.setProgram(QStringLiteral("dbus-daemon"));
        m_daemon.setArguments({QStringLiteral("--config-file=") + config,
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
        const QString address = QString::fromUtf8(line.split('\n').constFirst()).trimmed();
        if (address.isEmpty())
            return false;
        m_connection = QDBusConnection::connectToBus(address, QStringLiteral("omaseedMotionBus"));
        return m_connection.isConnected();
    }

    bool serve(FakePortal *portal)
    {
        return m_connection.registerObject(kPath,
                                           QStringLiteral("org.freedesktop.portal.Settings"),
                                           portal,
                                           QDBusConnection::ExportAllSlots |
                                               QDBusConnection::ExportAllSignals) &&
               m_connection.registerService(kService);
    }

    void stop()
    {
        QDBusConnection::disconnectFromBus(QStringLiteral("omaseedMotionBus"));
        m_daemon.terminate();
        if (!m_daemon.waitForFinished(2000))
            m_daemon.kill();
    }

    QDBusConnection connection() const { return m_connection; }

private:
    QTemporaryDir m_dir;
    QProcess m_daemon;
    QDBusConnection m_connection{QStringLiteral("uninitialised")};
};

} // namespace

class TestMotionPreference : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase() { QVERIFY2(m_bus.start(), "no private dbus-daemon"); }
    void cleanupTestCase() { m_bus.stop(); }

    void theValueIsUnwrapped_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<bool>("reduced");
        QTest::newRow("1") << QVariant(1u) << true;
        QTest::newRow("0") << QVariant(0u) << false;
        QTest::newRow("unknown 2") << QVariant(2u) << false;
        QTest::newRow("wrapped") << QVariant::fromValue(QDBusVariant(QVariant(1u))) << true;
        QTest::newRow("twice") << QVariant::fromValue(
                                      QDBusVariant(QVariant::fromValue(QDBusVariant(QVariant(1u)))))
                               << true;
        QTest::newRow("nothing") << QVariant() << false;
    }

    void theValueIsUnwrapped()
    {
        QFETCH(QVariant, value);
        QFETCH(bool, reduced);
        QCOMPARE(MotionPreference::parse(value), reduced);
    }

    void noPortalIsNoPreference()
    {
        MotionPreference motion(m_bus.connection(), kNoHyprland);
        QTest::qWait(100);
        QCOMPARE(motion.reduced(), false);
    }

    void itReadsThePortalAndFollowsIt()
    {
        FakePortal portal;
        portal.values.insert(QStringLiteral("org.freedesktop.appearance reduced-motion"),
                             QVariant(1u));
        QVERIFY(m_bus.serve(&portal));

        MotionPreference motion(m_bus.connection(), kNoHyprland);
        QSignalSpy changed(&motion, &MotionPreference::reducedChanged);
        QTRY_COMPARE(motion.reduced(), true);

        portal.change(
            QStringLiteral("org.freedesktop.appearance"), QStringLiteral("reduced-motion"), 0u);
        QTRY_COMPARE(motion.reduced(), false);
        QCOMPARE(changed.size(), 2);

        // Another key in the namespace changes nothing.
        portal.change(
            QStringLiteral("org.freedesktop.appearance"), QStringLiteral("color-scheme"), 1u);
        QTest::qWait(100);
        QCOMPARE(motion.reduced(), false);
        QCOMPARE(changed.size(), 2);

        m_bus.connection().unregisterService(kService);
        m_bus.connection().unregisterObject(kPath);
    }

    void hyprlandsAnswer_data()
    {
        QTest::addColumn<QByteArray>("answer");
        QTest::addColumn<bool>("off");
        QTest::newRow("off")
            << QByteArray("{\"option\": \"animations:enabled\", \"bool\": false, \"set\": true }")
            << true;
        QTest::newRow("on")
            << QByteArray("{\"option\": \"animations:enabled\", \"bool\": true, \"set\": true }")
            << false;
        QTest::newRow("int 0") << QByteArray("{\"option\": \"animations:enabled\", \"int\": 0}")
                               << true;
        QTest::newRow("int 1") << QByteArray("{\"option\": \"animations:enabled\", \"int\": 1}")
                               << false;
        QTest::newRow("no such option") << QByteArray("no such option") << false;
        QTest::newRow("nothing") << QByteArray() << false;
    }

    void hyprlandsAnswer()
    {
        QFETCH(QByteArray, answer);
        QFETCH(bool, off);
        QCOMPARE(MotionPreference::animationsOff(answer), off);
    }

    // #111: no portal preference, but Hyprland's animations off (Omarchy's "no animations",
    // on by itself in a VM); a reload that turns them on again, and the portal still counts.
    void hyprlandsAnimationsOffReduceMotion()
    {
        FakeHyprland hypr;
        hypr.setAnimations(false);
        MotionPreference motion(m_bus.connection(),
                                MotionPreference::Hyprland{hypr.hyprctl(), hypr.socketPath()});
        QSignalSpy changed(&motion, &MotionPreference::reducedChanged);
        QTRY_COMPARE(motion.reduced(), true);
        QLocalSocket *events = hypr.accept();
        QVERIFY(events);

        // The toggle turns them back on: Hyprland reloads its config.
        hypr.setAnimations(true);
        events->write("configreloaded>>\n");
        events->flush();
        QTRY_COMPARE(motion.reduced(), false);
        QCOMPARE(changed.size(), 2);

        // Other events never ask.
        hypr.setAnimations(false);
        events->write("workspace>>2\n");
        events->flush();
        QTest::qWait(300);
        QCOMPARE(motion.reduced(), false);

        // The portal asks for less motion while Hyprland animates: reduced.
        FakePortal portal;
        QVERIFY(m_bus.serve(&portal));
        hypr.setAnimations(true);
        portal.change(
            QStringLiteral("org.freedesktop.appearance"), QStringLiteral("reduced-motion"), 1u);
        QTRY_COMPARE(motion.reduced(), true);
        // Hyprland's answer on a reload does not undo the portal's.
        events->write("configreloaded>>\n");
        events->flush();
        QTest::qWait(300);
        QCOMPARE(motion.reduced(), true);

        m_bus.connection().unregisterService(kService);
        m_bus.connection().unregisterObject(kPath);
    }

    void noHyprctlIsNoPreference()
    {
        MotionPreference motion(
            m_bus.connection(),
            MotionPreference::Hyprland{QStringLiteral("/nonexistent/hyprctl"), QString()});
        QTest::qWait(200);
        QCOMPARE(motion.reduced(), false);
    }

private:
    PrivateBus m_bus;
};

QTEST_GUILESS_MAIN(TestMotionPreference)
#include "tst_motionpreference.moc"
