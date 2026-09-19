#include "integration/AppIdentity.hpp"
#include "integration/SingleInstance.hpp"

#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

namespace Identity = omaseed::integration::AppIdentity;
using omaseed::integration::SingleInstance;

/// SingleInstance's D-Bus path is exercised against a private session bus of its own
/// (started here with `dbus-daemon --session`), never the desktop's real one: DBUS_SESSION_
/// BUS_ADDRESS is pointed at it for the duration of this test binary, and every SingleInstance
/// created below picks a unique D-Bus connection name internally, so two of them can coexist
/// as "primary" and "secondary" in this one process the way two separate `omaseed` processes
/// would on the real bus.
class TestSingleInstance : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void firstInstanceIsPrimary();
    void secondInstanceForwardsOpenToThePrimary();
    void secondInstanceWithNoUrisSendsActivate();
    void primaryForwardsItsOwnUris();

    void desktopFileHasTheRequiredKeys();
    void desktopFileValidates();

private:
    QProcess m_dbusDaemon;
};

void TestSingleInstance::initTestCase()
{
    m_dbusDaemon.setProgram(QStringLiteral("dbus-daemon"));
    m_dbusDaemon.setArguments({QStringLiteral("--session"),
                               QStringLiteral("--print-address"),
                               QStringLiteral("--nofork")});
    m_dbusDaemon.start();
    QVERIFY2(m_dbusDaemon.waitForStarted(5000), "dbus-daemon did not start");
    QVERIFY2(m_dbusDaemon.waitForReadyRead(5000), "dbus-daemon printed no address");
    const QByteArray addressLine = m_dbusDaemon.readLine().trimmed();
    QVERIFY2(!addressLine.isEmpty(), "dbus-daemon's address line was empty");

    // Every SingleInstance in this process reads DBUS_SESSION_BUS_ADDRESS from the process
    // environment, so pointing it at our private bus is what keeps this test off the real
    // session bus, per CLAUDE.md.
    qputenv("DBUS_SESSION_BUS_ADDRESS", addressLine);
}

void TestSingleInstance::cleanupTestCase()
{
    if (m_dbusDaemon.state() != QProcess::NotRunning) {
        m_dbusDaemon.terminate();
        if (!m_dbusDaemon.waitForFinished(2000))
            m_dbusDaemon.kill();
    }
}

void TestSingleInstance::firstInstanceIsPrimary()
{
    SingleInstance instance;
    QVERIFY(instance.start({}));
    QVERIFY(instance.isPrimary());
}

void TestSingleInstance::secondInstanceForwardsOpenToThePrimary()
{
    SingleInstance primary;
    QVERIFY(primary.start({}));
    QSignalSpy openSpy(&primary, &SingleInstance::openRequested);

    const QStringList uris{QStringLiteral("magnet:?xt=urn:btih:abc")};
    SingleInstance secondary;
    QVERIFY(!secondary.start(uris));
    QVERIFY(!secondary.isPrimary());

    QVERIFY(openSpy.count() > 0 || openSpy.wait(2000));
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.constFirst().constFirst().toStringList(), uris);
}

void TestSingleInstance::secondInstanceWithNoUrisSendsActivate()
{
    SingleInstance primary;
    QVERIFY(primary.start({}));
    QSignalSpy activateSpy(&primary, &SingleInstance::activateRequested);

    SingleInstance secondary;
    QVERIFY(!secondary.start({}));
    QVERIFY(!secondary.isPrimary());

    QVERIFY(activateSpy.count() > 0 || activateSpy.wait(2000));
    QCOMPARE(activateSpy.count(), 1);
}

void TestSingleInstance::primaryForwardsItsOwnUris()
{
    SingleInstance primary;
    QSignalSpy openSpy(&primary, &SingleInstance::openRequested);
    const QStringList uris{QStringLiteral("magnet:?xt=urn:btih:def")};
    QVERIFY(primary.start(uris));

    // Its own positional URIs travel the same openRequested() path, once the event loop
    // starts (they are posted with QTimer::singleShot(0)), rather than a separate signal.
    QVERIFY(openSpy.wait(2000));
    QCOMPARE(openSpy.count(), 1);
    QCOMPARE(openSpy.constFirst().constFirst().toStringList(), uris);
}

void TestSingleInstance::desktopFileHasTheRequiredKeys()
{
    // A small line-based parser rather than QSettings(IniFormat): QSettings treats an
    // unescaped ';' as starting a comment, which is exactly the separator .desktop files use
    // for MimeType/Categories/Keywords lists, so it silently truncates them.
    const QString path = QStringLiteral(OMASEED_DATA_DIR "/io.github.diogochaves.omaseed.desktop");
    QFile file(path);
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(path));

    QMap<QString, QString> entries;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int separator = line.indexOf(QLatin1Char('='));
        if (line.isEmpty() || line.startsWith(QLatin1Char('[')) ||
            line.startsWith(QLatin1Char('#')) || separator < 0)
            continue;
        entries.insert(line.left(separator), line.mid(separator + 1));
    }

    QCOMPARE(entries.value(QStringLiteral("Type")), QStringLiteral("Application"));
    QCOMPARE(entries.value(QStringLiteral("Exec")), QStringLiteral("omaseed %U"));
    QCOMPARE(entries.value(QStringLiteral("DBusActivatable")), QStringLiteral("true"));
    const QString mimeType = entries.value(QStringLiteral("MimeType"));
    QVERIFY(mimeType.contains(QStringLiteral("x-scheme-handler/magnet")));
    QVERIFY(mimeType.contains(QStringLiteral("application/x-bittorrent")));
}

void TestSingleInstance::desktopFileValidates()
{
    if (QStandardPaths::findExecutable(QStringLiteral("desktop-file-validate")).isEmpty())
        QSKIP("desktop-file-validate not installed");

    const QString path = QStringLiteral(OMASEED_DATA_DIR "/io.github.diogochaves.omaseed.desktop");
    QProcess validator;
    validator.start(QStringLiteral("desktop-file-validate"), {path});
    QVERIFY(validator.waitForFinished(5000));
    QCOMPARE(validator.exitCode(), 0);
}

QTEST_GUILESS_MAIN(TestSingleInstance)

#include "tst_singleinstance.moc"
