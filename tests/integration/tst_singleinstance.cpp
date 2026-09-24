#include "integration/AppIdentity.hpp"
#include "integration/SingleInstance.hpp"
#include "integration/ThemeReader.hpp"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusContext>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

namespace Identity = omaseed::integration::AppIdentity;
using omaseed::integration::SingleInstance;
using omaseed::integration::ThemeReader;

/// Owns the app's name on its own connection and answers Open/Activate with an error after
/// giving the name up: an omaseed that was quitting when the second launch called it.
class QuittingOwner : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit QuittingOwner(const QString &connectionName)
        : m_connectionName(connectionName)
    {}

public slots:
    void Activate(const QVariantMap &) { quit(); }
    void Open(const QStringList &, const QVariantMap &) { quit(); }

private:
    void quit()
    {
        QDBusConnection(m_connectionName).unregisterService(Identity::dbusServiceName());
        sendErrorReply(QDBusError::Failed, QStringLiteral("quitting"));
    }

    QString m_connectionName;
};

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

    // INT-1: the theme-set hook calls Reload on the app's *name*.
    void themeReloadReachesThePrimaryByName();
    // INT-3: the owner of the name failed the hand-off; the URIs must not be dropped.
    void secondaryTakesOverFromAQuittingOwner();
    void secondaryGivesUpOnAnOwnerThatIsNotTheApp();
    // INT-4 / APP-8: paths are resolved in the launching process.
    void relativePathsBecomeAbsoluteFileUris();
    // INT-10: the fallback without a session bus.
    void localSocketFallbackLivesInTheRuntimeDir();

    void desktopFileHasTheRequiredKeys();
    void desktopFileValidates();

private:
    QByteArray m_busAddress;
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
    m_busAddress = addressLine;
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

void TestSingleInstance::themeReloadReachesThePrimaryByName()
{
    QTemporaryDir themeHome;
    QVERIFY(themeHome.isValid());
    ThemeReader::Paths paths;
    paths.stateCurrentDir = themeHome.path() + QStringLiteral("/state");
    paths.configOmarchyDir = themeHome.path() + QStringLiteral("/config");
    ThemeReader reader(paths);

    SingleInstance primary;
    QVERIFY(primary.start({}));
    QVERIFY(primary.connection().isConnected());
    QVERIFY(reader.exportToDBus(primary.connection()));

    // A new theme lands; the hook's Reload must apply it at once, not after the debounce.
    QDir().mkpath(paths.stateCurrentDir + QStringLiteral("/theme"));
    QFile colors(paths.stateCurrentDir + QStringLiteral("/theme/colors.toml"));
    QVERIFY(colors.open(QIODevice::WriteOnly));
    colors.write("mode = \"light\"\naccent = \"#3264eb\"\nbackground = \"#fafafa\"\n"
                 "foreground = \"#212121\"\ndark_foreground = \"#757575\"\n");
    colors.close();
    QSignalSpy changed(&reader, &ThemeReader::changed);

    // The hook's `busctl call io.github.diogochaves.omaseed …/Theme … Reload`, from a
    // connection of its own, addressed to the well-known name.
    QDBusConnection hook =
        QDBusConnection::connectToBus(QString::fromUtf8(m_busAddress), QStringLiteral("hook"));
    QVERIFY(hook.isConnected());
    const QDBusMessage call = QDBusMessage::createMethodCall(Identity::dbusServiceName(),
                                                             Identity::dbusThemeObjectPath(),
                                                             Identity::dbusThemeInterface(),
                                                             QStringLiteral("Reload"));
    QDBusPendingCall pending = hook.asyncCall(call, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(pending.isFinished(), 3000);
    QVERIFY2(!pending.isError(), qPrintable(pending.error().message()));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(reader.mode(), QStringLiteral("light"));
    QDBusConnection::disconnectFromBus(QStringLiteral("hook"));
}

void TestSingleInstance::secondaryTakesOverFromAQuittingOwner()
{
    const QString ownerName = QStringLiteral("quitting-owner");
    QDBusConnection owner =
        QDBusConnection::connectToBus(QString::fromUtf8(m_busAddress), ownerName);
    QVERIFY(owner.registerService(Identity::dbusServiceName()));
    QuittingOwner object(ownerName);
    QVERIFY(
        owner.registerObject(Identity::dbusObjectPath(), &object, QDBusConnection::ExportAllSlots));

    const QStringList uris{QStringLiteral("magnet:?xt=urn:btih:quit")};
    SingleInstance second;
    QSignalSpy openSpy(&second, &SingleInstance::openRequested);
    QVERIFY(second.start(uris));
    QVERIFY(second.isPrimary());
    QVERIFY(openSpy.wait(2000));
    QCOMPARE(openSpy.constFirst().constFirst().toStringList(), uris);

    owner.unregisterObject(Identity::dbusObjectPath());
    QDBusConnection::disconnectFromBus(ownerName);
}

void TestSingleInstance::secondaryGivesUpOnAnOwnerThatIsNotTheApp()
{
    // Something holds the name but has no org.freedesktop.Application object (a stray demo
    // of an older build did exactly this). The secondary must answer quickly, not hang.
    const QString ownerName = QStringLiteral("foreign-owner");
    QDBusConnection owner =
        QDBusConnection::connectToBus(QString::fromUtf8(m_busAddress), ownerName);
    QVERIFY(owner.registerService(Identity::dbusServiceName()));

    SingleInstance second;
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!second.start({QStringLiteral("magnet:?xt=urn:btih:lost")}));
    QVERIFY(!second.isPrimary());
    QVERIFY(timer.elapsed() < 3000);

    QDBusConnection::disconnectFromBus(ownerName);
}

void TestSingleInstance::relativePathsBecomeAbsoluteFileUris()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QDir().mkpath(dir.path() + QStringLiteral("/sub"));
    QFile file(dir.path() + QStringLiteral("/sub/ubuntu 24.torrent"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    const QString absolute = QFileInfo(file).absoluteFilePath();
    const QString expected = QUrl::fromLocalFile(absolute).toString();

    const QStringList in{
        QStringLiteral("sub/ubuntu 24.torrent"),
        QStringLiteral("./sub/../sub/ubuntu 24.torrent"),
        absolute,
        QStringLiteral("magnet:?xt=urn:btih:abc"),
        QStringLiteral("https://example.org/x.torrent"),
        QStringLiteral("file:///elsewhere/y.torrent"),
        QStringLiteral("missing.torrent"),
    };
    const QStringList out = SingleInstance::absoluteUris(in, dir.path());
    QCOMPARE(out.size(), in.size());
    QCOMPARE(out.at(0), expected);
    QCOMPARE(out.at(1), expected);
    QCOMPARE(out.at(2), expected);
    QCOMPARE(out.at(3), in.at(3));
    QCOMPARE(out.at(4), in.at(4));
    QCOMPARE(out.at(5), in.at(5));
    QCOMPARE(out.at(6), in.at(6));
}

void TestSingleInstance::localSocketFallbackLivesInTheRuntimeDir()
{
    QTemporaryDir runtime;
    QVERIFY(runtime.isValid());
    const QByteArray savedBus = qgetenv("DBUS_SESSION_BUS_ADDRESS");
    const QByteArray savedRuntime = qgetenv("XDG_RUNTIME_DIR");
    qunsetenv("DBUS_SESSION_BUS_ADDRESS");
    qputenv("XDG_RUNTIME_DIR", runtime.path().toUtf8());

    {
        SingleInstance primary;
        QVERIFY(primary.start({}));
        QVERIFY(primary.isPrimary());
        QVERIFY(!primary.connection().isConnected());
        const QString socket = runtime.path() + QLatin1Char('/') + Identity::applicationId() +
                               QStringLiteral(".instance");
        QVERIFY2(QFileInfo::exists(socket), qPrintable(socket));
        QCOMPARE(QFileInfo(socket).permissions() & (QFile::ReadOther | QFile::WriteOther),
                 QFile::Permissions());

        QSignalSpy openSpy(&primary, &SingleInstance::openRequested);
        const QStringList uris{QStringLiteral("magnet:?xt=urn:btih:local")};
        SingleInstance secondary;
        QVERIFY(!secondary.start(uris));
        QVERIFY(openSpy.count() > 0 || openSpy.wait(2000));
        QCOMPARE(openSpy.constFirst().constFirst().toStringList(), uris);
    }

    qputenv("DBUS_SESSION_BUS_ADDRESS", savedBus);
    if (savedRuntime.isEmpty())
        qunsetenv("XDG_RUNTIME_DIR");
    else
        qputenv("XDG_RUNTIME_DIR", savedRuntime);
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
