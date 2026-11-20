// DECISIONS C45 V2, V4, V5, V13 and C37 U10: `omaseed WORD …`, the real binary. The words
// need no display, so it runs with none; the bus is a private dbus-daemon or none at all,
// never the desktop's, and its homes and runtime directory are its own. Where the test owns
// omaseed's name itself, it plays an omaseed from before the Agent interface (too old) and
// the hand-off's receiver (a file named like a word).

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

const QString kService = QStringLiteral("io.github.diogochaves.omaseed");
const QString kPath = QStringLiteral("/io/github/diogochaves/omaseed");

} // namespace

/// An omaseed from before C45: org.freedesktop.Application only, as SingleInstance exports it.
class OldOmaseed : public QObject
{
    Q_OBJECT

public:
    QStringList opened;
};

class OldApplicationAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Application")

public:
    explicit OldApplicationAdaptor(OldOmaseed *owner)
        : QDBusAbstractAdaptor(owner)
        , m_owner(owner)
    {}

public slots:
    void Activate(const QVariantMap &platform_data) { Q_UNUSED(platform_data); }

    void Open(const QStringList &uris, const QVariantMap &platform_data)
    {
        Q_UNUSED(platform_data);
        m_owner->opened << uris;
    }

    void ActivateAction(const QString &action_name,
                        const QVariantList &parameter,
                        const QVariantMap &platform_data)
    {
        Q_UNUSED(action_name);
        Q_UNUSED(parameter);
        Q_UNUSED(platform_data);
    }

private:
    OldOmaseed *m_owner;
};

class TestWords : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void helpListsTheWords();
    void helpWordAndWordHelpMatch_data();
    void helpWordAndWordHelpMatch();
    void usageExits2WithJsonOnStdout();
    void notRunningExits3();
    void noBusExits3();
    void tooOldExits3();
    void aFileNamedStatusOpensAsDotSlash();

private:
    /// The environment of every run: its own homes and runtime directory, no display, and
    /// `bus` as the session bus ("" none at all: no address, no socket in the runtime dir).
    QProcessEnvironment environment(const QString &bus) const;
    /// Runs omaseed, pumping this test's events meanwhile (it may own the name being called);
    /// false when it didn't finish in time.
    bool run(const QStringList &arguments, const QString &bus, bool display = false);
    /// A private session bus; its address, or "" when dbus-daemon can't start.
    QString startBus();
    /// Takes omaseed's name on `bus` as an omaseed from before the Agent interface.
    bool ownAsOld(const QString &bus);
    /// The single JSON object on stdout.
    QJsonObject json() const;

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QProcess> m_bus;
    std::unique_ptr<OldOmaseed> m_old;
    QString m_ownerConnection;
    int m_exitCode = -1;
    QProcess::ExitStatus m_exitStatus = QProcess::CrashExit;
    QString m_out;
    QString m_err;
};

void TestWords::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
    QDir(m_home->path()).mkpath(QStringLiteral("runtime"));
}

void TestWords::cleanup()
{
    if (!m_ownerConnection.isEmpty()) {
        QDBusConnection::disconnectFromBus(m_ownerConnection);
        m_ownerConnection.clear();
    }
    m_old.reset();
    if (m_bus) {
        m_bus->kill();
        m_bus->waitForFinished(3000);
        m_bus.reset();
    }
    m_home.reset();
}

QProcessEnvironment TestWords::environment(const QString &bus) const
{
    const QString home = m_home->path();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("HOME"), home);
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/data"));
    env.insert(QStringLiteral("XDG_STATE_HOME"), home + QStringLiteral("/state"));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), home + QStringLiteral("/cache"));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), home + QStringLiteral("/runtime"));
    env.remove(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"));
    if (!bus.isEmpty())
        env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"), bus);
    for (const char *name : {"QT_QPA_PLATFORM",
                             "WAYLAND_DISPLAY",
                             "WAYLAND_SOCKET",
                             "DISPLAY",
                             "HYPRLAND_INSTANCE_SIGNATURE",
                             "OMASEED_TEST_PLATFORM",
                             "OMASEED_API_KEY",
                             "OMASEED_USERNAME",
                             "OMASEED_PASSWORD"})
        env.remove(QString::fromLatin1(name));
    return env;
}

bool TestWords::run(const QStringList &arguments, const QString &bus, bool display)
{
    QProcessEnvironment env = environment(bus);
    // Only the hand-off starts the GUI application; offscreen is a platform with no display.
    if (display)
        env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    QProcess process;
    process.setProcessEnvironment(env);
    process.setWorkingDirectory(m_home->path());
    process.start(QStringLiteral(OMASEED_BINARY), arguments);
    if (!process.waitForStarted(5000))
        return false;
    QElapsedTimer timer;
    timer.start();
    while (process.state() != QProcess::NotRunning && timer.elapsed() < 40000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        process.waitForFinished(20);
    }
    if (process.state() != QProcess::NotRunning) {
        process.kill();
        process.waitForFinished(2000);
        return false;
    }
    m_exitCode = process.exitCode();
    m_exitStatus = process.exitStatus();
    m_out = QString::fromUtf8(process.readAllStandardOutput());
    m_err = QString::fromUtf8(process.readAllStandardError());
    return true;
}

QString TestWords::startBus()
{
    m_bus = std::make_unique<QProcess>();
    m_bus->start(QStringLiteral("dbus-daemon"),
                 {QStringLiteral("--session"),
                  QStringLiteral("--print-address"),
                  QStringLiteral("--nofork")});
    if (!m_bus->waitForStarted(5000) || !m_bus->waitForReadyRead(5000))
        return QString();
    return QString::fromUtf8(m_bus->readLine().trimmed());
}

bool TestWords::ownAsOld(const QString &bus)
{
    m_ownerConnection = QStringLiteral("tst-words-owner");
    QDBusConnection connection = QDBusConnection::connectToBus(bus, m_ownerConnection);
    if (!connection.isConnected())
        return false;
    m_old = std::make_unique<OldOmaseed>();
    new OldApplicationAdaptor(m_old.get());
    return connection.registerObject(kPath, m_old.get(), QDBusConnection::ExportAdaptors) &&
           connection.registerService(kService);
}

QJsonObject TestWords::json() const
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(m_out.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

void TestWords::helpListsTheWords()
{
    QVERIFY(run({QStringLiteral("help")}, QString()));
    QCOMPARE(m_exitStatus, QProcess::NormalExit);
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    const QString page = m_out;
    QVERIFY2(
        page.startsWith(QLatin1String("Usage: omaseed [options] [magnet: links or .torrent "
                                      "files...]\n       omaseed <word> [arguments] [--json]\n")),
        qPrintable(page));
    for (const char *word : {"status", "errors", "log", "moves"})
        QVERIFY(page.contains(QStringLiteral("\n  %1 ").arg(QString::fromLatin1(word))));
    QVERIFY(page.contains(QLatin1String("Options:")));
    QVERIFY(page.contains(QLatin1String("--quit")));
    // `help` and `--help` are one page.
    QVERIFY(run({QStringLiteral("--help")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out, page);
}

void TestWords::helpWordAndWordHelpMatch_data()
{
    QTest::addColumn<QString>("word");
    for (const char *word : {"status", "errors", "log", "moves", "help"})
        QTest::newRow(word) << QString::fromLatin1(word);
}

void TestWords::helpWordAndWordHelpMatch()
{
    QFETCH(QString, word);
    QVERIFY(run({QStringLiteral("help"), word}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    const QString page = m_out;
    QVERIFY2(page.startsWith(QStringLiteral("Usage: omaseed %1").arg(word)), qPrintable(page));
    QVERIFY(page.contains(QLatin1String("\nExit: 0 ")));
    QVERIFY(run({word, QStringLiteral("--help")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out, page);
}

void TestWords::usageExits2WithJsonOnStdout()
{
    QVERIFY(run({QStringLiteral("errors"), QStringLiteral("--bogus"), QStringLiteral("--json")},
                QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.isEmpty(), qPrintable(m_err));
    QCOMPARE(m_out.count(QLatin1Char('\n')), 1);
    const QJsonObject object = json();
    QCOMPARE(object.value(QLatin1String("v")).toInt(), 1);
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("errors"));
    QVERIFY(object.value(QLatin1String("at")).toString().endsWith(QLatin1Char('Z')));
    QVERIFY(!object.contains(QLatin1String("omaseed")));
    const QJsonObject error = object.value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("usage"));
    QCOMPARE(error.value(QLatin1String("message")).toString(),
             QStringLiteral("omaseed: errors doesn't take \"--bogus\". omaseed help errors says "
                            "what it takes."));

    // Without --json the words go to stderr, and stdout stays empty.
    QVERIFY(run({QStringLiteral("log"), QStringLiteral("--lines=0")}, QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY(m_out.isEmpty());
    QVERIFY2(m_err.contains(QLatin1String("log doesn't take \"--lines=0\"")), qPrintable(m_err));

    // A reserved word answers usage too.
    QVERIFY(run({QStringLiteral("skin"), QStringLiteral("list")}, QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.contains(QLatin1String("skin isn't in this omaseed yet")), qPrintable(m_err));
}

void TestWords::notRunningExits3()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(run({QStringLiteral("status"), QStringLiteral("--json")}, bus));
    QCOMPARE(m_exitCode, 3);
    const QJsonObject error = json().value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("not-running"));
    QVERIFY(error.value(QLatin1String("message"))
                .toString()
                .startsWith(QLatin1String("omaseed isn't running, so it can't answer.")));
    // A word never starts omaseed.
    QVERIFY(!QFile::exists(m_home->filePath(QStringLiteral("runtime/omaseed/status.json"))));
}

void TestWords::noBusExits3()
{
    QVERIFY(run({QStringLiteral("errors")}, QString()));
    QCOMPARE(m_exitCode, 3);
    QVERIFY(m_out.isEmpty());
    QCOMPARE(m_err.trimmed(),
             QStringLiteral("omaseed can't be reached: there's no session bus here. Run this in "
                            "the person's desktop session."));
    QVERIFY(run({QStringLiteral("errors"), QStringLiteral("--json")}, QString()));
    QCOMPARE(m_exitCode, 3);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("no-bus"));
}

void TestWords::tooOldExits3()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(ownAsOld(bus));
    // The status file the old omaseed wrote: only app.version is read, for the sentence.
    QDir(m_home->path()).mkpath(QStringLiteral("runtime/omaseed"));
    QFile status(m_home->filePath(QStringLiteral("runtime/omaseed/status.json")));
    QVERIFY(status.open(QIODevice::WriteOnly));
    status.write(R"({"v":1,"app":{"version":"0.12.0"}})");
    status.close();

    QVERIFY(run({QStringLiteral("moves"), QStringLiteral("--json")}, bus));
    QCOMPARE(m_exitCode, 3);
    const QJsonObject error = json().value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("too-old"));
    const QString message = error.value(QLatin1String("message")).toString();
    QVERIFY2(message.startsWith(QLatin1String("The omaseed that's running is 0.12.0, older than "
                                              "this one (")),
             qPrintable(message));
    QVERIFY(message.endsWith(QLatin1String("). Quit it and open it again to answer.")));

    // Without the file, the sentence leaves the old version out.
    QVERIFY(status.remove());
    QVERIFY(run({QStringLiteral("status")}, bus));
    QCOMPARE(m_exitCode, 3);
    QVERIFY2(m_err.startsWith(QLatin1String("The omaseed that's running is older than this one (")),
             qPrintable(m_err));
}

void TestWords::aFileNamedStatusOpensAsDotSlash()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(ownAsOld(bus));
    // C37 U10: only a first argument that is exactly a word is one; ./status is a file to open,
    // handed to the running omaseed as it always was.
    QVERIFY(run({QStringLiteral("./status")}, bus, true));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_old->opened.size(), 1);
    QVERIFY2(m_old->opened.first().endsWith(QLatin1String("status")),
             qPrintable(m_old->opened.first()));
}

QTEST_GUILESS_MAIN(TestWords)
#include "tst_words.moc"
