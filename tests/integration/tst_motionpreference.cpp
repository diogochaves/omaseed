// #24 T5 (docs/22 §6): "off under reduced motion". MotionPreference reads the desktop's
// reduced-motion setting from the settings portal and follows it when it changes; a portal
// that is missing or does not know the key reads as no preference.

#include "integration/MotionPreference.hpp"

#include <QDBusConnection>
#include <QDBusVariant>
#include <QFile>
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
        MotionPreference motion(m_bus.connection());
        QTest::qWait(100);
        QCOMPARE(motion.reduced(), false);
    }

    void itReadsThePortalAndFollowsIt()
    {
        FakePortal portal;
        portal.values.insert(QStringLiteral("org.freedesktop.appearance reduced-motion"),
                             QVariant(1u));
        QVERIFY(m_bus.serve(&portal));

        MotionPreference motion(m_bus.connection());
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

private:
    PrivateBus m_bus;
};

QTEST_GUILESS_MAIN(TestMotionPreference)
#include "tst_motionpreference.moc"
