// DECISIONS C45 V2, V4, V5, V13 and C37 U10: `omaseed WORD …`, the real binary. The words
// need no display, so it runs with none; the bus is a private dbus-daemon or none at all,
// never the desktop's, and its homes and runtime directory are its own. Where the test owns
// omaseed's name itself, it plays an omaseed from before the Agent interface (too old) and
// the hand-off's receiver (a file named like a word).

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

namespace {

const QString kService = QStringLiteral("io.github.diogochaves.omaseed");
const QString kPath = QStringLiteral("/io/github/diogochaves/omaseed");

/// tst_quit's kind of qBittorrent: one seeding torrent, every poll a full update.
class SeedServer : public QTcpServer
{
protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, buffer]() {
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
            buffer->clear();

            QByteArray body = "{}";
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.4";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = "2.15.1";
            } else if (target.startsWith("/api/v2/log/")) {
                body = "[]";
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                const QJsonObject torrent{{QStringLiteral("name"), QStringLiteral("t")},
                                          {QStringLiteral("state"), QStringLiteral("uploading")},
                                          {QStringLiteral("progress"), 1.0},
                                          {QStringLiteral("added_on"), 1700000000}};
                const QJsonObject data{
                    {QStringLiteral("rid"), 1},
                    {QStringLiteral("full_update"), true},
                    {QStringLiteral("torrents"),
                     QJsonObject{
                         {QStringLiteral("cccccccccccccccccccccccccccccccccccccccc"), torrent}}},
                    {QStringLiteral("server_state"),
                     QJsonObject{
                         {QStringLiteral("connection_status"), QStringLiteral("connected")}}}};
                body = QJsonDocument(data).toJson(QJsonDocument::Compact);
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

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
    void statusFromARunningOmaseed();
    void doOnARunningOmaseed();

    // The skins' words (DECISIONS C37 U3, U6; #223).
    void aFileNamedCheckOpensAsDotSlash();
    void skinListWithNoDisplayOrBus();
    void skinCloneThenList();
    void skinUseWithNoneRunningSavesTheChoice();
    void skinUseNeverWritesABrokenState();
    void skinUseOnARunningOmaseed();
    void skinUseOnAnOldOneIs3();
    void skinUseOddNames();
    void skillPrintsTheMap();
    void skillPagePrintsIt();
    void skillJson();
    void aFileNamedSkillOpensAsDotSlash();
    void skillIsTheSameAsDashDashSkill();
    void skillLinkThenStatus();
    void skillLinkWritesTheGuideFiles();
    void skillUnlink();
    void skillLinkJson();
    void skillLinkWithNowhereIs5();
    void theMapSaysLinkOnlyWhenUnlinked();

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
    /// The test's state.json: its path, its bytes ("" when there is none), written.
    QString statePath() const;
    QByteArray state() const;
    void writeState(const QByteArray &bytes);

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
    // skill link finds the source tree's skill, never an installed one (DECISIONS C48).
    env.insert(QStringLiteral("XDG_DATA_DIRS"), home + QStringLiteral("/usr-share"));
    env.insert(QStringLiteral("OMASEED_SKILL_DIR"), QStringLiteral(OMASEED_SOURCE_SKILL_DIR));
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

QString TestWords::statePath() const
{
    return m_home->filePath(QStringLiteral("config/omaseed/state.json"));
}

QByteArray TestWords::state() const
{
    QFile file(statePath());
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

void TestWords::writeState(const QByteArray &bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(statePath()).absolutePath()));
    QFile file(statePath());
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(bytes);
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
    for (const char *word :
         {"status", "errors", "log", "moves", "do", "skin list", "skin clone", "check", "skin use"})
        QVERIFY(page.contains(QStringLiteral("\n  %1 ").arg(QString::fromLatin1(word))));
    QVERIFY(page.contains(QLatin1String("\nYour skins, in ~/.config/omaseed/skins/:\n")));
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
    for (const char *word :
         {"status", "errors", "log", "moves", "help", "check", "skin", "skill", "do"})
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

    // do's usage is the CLI's, before any call (C45 V15, C50 GT1).
    QVERIFY(run({QStringLiteral("do"), QStringLiteral("pause"), QStringLiteral("3f9a1c0e")},
                QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.contains(QLatin1String("do pause takes no torrents")), qPrintable(m_err));
    QVERIFY(run({QStringLiteral("do"),
                 QStringLiteral("remove"),
                 QStringLiteral("9c40e7a2"),
                 QStringLiteral("--delete-files")},
                QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.contains(QLatin1String("omaseed do never deletes files")), qPrintable(m_err));
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

void TestWords::statusFromARunningOmaseed()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    SeedServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    const QString address = QStringLiteral("127.0.0.1:%1").arg(server.serverPort());

    // The real app, offscreen, on the private bus, on the stand-in qBittorrent.
    QProcess app;
    QProcessEnvironment env = environment(bus);
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("OMASEED_SERVER_URL"), QStringLiteral("http://") + address);
    app.setProcessEnvironment(env);
    app.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    app.start(QStringLiteral(OMASEED_BINARY), QStringList());
    QVERIFY(app.waitForStarted(5000));
    struct Stop
    {
        QProcess &app;
        ~Stop()
        {
            app.terminate();
            if (!app.waitForFinished(10000))
                app.kill();
            app.waitForFinished(2000);
        }
    } stop{app};

    // Asked until it answers with the server reached (its name taken, its first sync done).
    QJsonObject object;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 30000) {
        QVERIFY(run({QStringLiteral("status"), QStringLiteral("--json")}, bus));
        object = json();
        if (m_exitCode == 0 &&
            object.value(QLatin1String("server"))
                .toObject()
                .value(QLatin1String("reachable"))
                .toBool() &&
            object.value(QLatin1String("questions"))
                    .toObject()
                    .value(QLatin1String("all"))
                    .toInt() == 1)
            break;
        QTest::qWait(300);
    }
    QVERIFY2(m_exitCode == 0, qPrintable(m_out + m_err));
    QCOMPARE(m_out.count(QLatin1Char('\n')), 1);
    QCOMPARE(object.value(QLatin1String("v")).toInt(), 1);
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("status"));
    QVERIFY(!object.value(QLatin1String("omaseed")).toString().isEmpty());
    QCOMPARE(object.value(QLatin1String("window")).toString(), QStringLiteral("shown"));
    const QJsonObject s = object.value(QLatin1String("server")).toObject();
    QCOMPARE(s.value(QLatin1String("address")).toString(), address);
    QCOMPARE(s.value(QLatin1String("version")).toString(), QStringLiteral("5.2.4"));
    QCOMPARE(s.value(QLatin1String("engine")).toString(), QStringLiteral("qbittorrent"));
    QCOMPARE(s.value(QLatin1String("connection")).toString(), QStringLiteral("connected"));
    QCOMPARE(
        object.value(QLatin1String("questions")).toObject().value(QLatin1String("all")).toInt(), 1);
    QVERIFY(object.value(QLatin1String("servers")).isArray());

    // The interface by hand (busctl's call) says the same bytes, but for the time.
    QDBusConnection caller = QDBusConnection::connectToBus(bus, QStringLiteral("tst-words-busctl"));
    QVERIFY(caller.isConnected());
    QDBusMessage call =
        QDBusMessage::createMethodCall(kService,
                                       kPath,
                                       QStringLiteral("io.github.diogochaves.omaseed.Agent"),
                                       QStringLiteral("Status"));
    call.setArguments({QStringLiteral("{}")});
    const QDBusMessage reply = caller.call(call, QDBus::Block, 10000);
    QDBusConnection::disconnectFromBus(QStringLiteral("tst-words-busctl"));
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QJsonObject byHand =
        QJsonDocument::fromJson(reply.arguments().value(0).toString().toUtf8()).object();
    QJsonObject byWord = object;
    byHand.remove(QStringLiteral("at"));
    byWord.remove(QStringLiteral("at"));
    QCOMPARE(byHand, byWord);

    // Without --json: the lines, made from that JSON, and no colour when piped.
    QVERIFY(run({QStringLiteral("status")}, bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.contains(QStringLiteral(" · qBittorrent 5.2.4 · connected\n")),
             qPrintable(m_out));
    QVERIFY(m_out.contains(QStringLiteral("\n  1 torrent: 1 giving back\n")));
    QVERIFY(!m_out.contains(QLatin1Char('\x1b')));
}

// #292 (C45 V13, V15): do through the real binary on a running omaseed whose Agents row lets
// fixes run: the fix answered (0), never (5), an unknown torrent (4), the list, the lines.
void TestWords::doOnARunningOmaseed()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    SeedServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    writeState(R"({"agents":{"reads":"on","fixes":"allow","moves":"off","remove":"off"}})");

    QProcess app;
    QProcessEnvironment env = environment(bus);
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("OMASEED_SERVER_URL"),
               QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    app.setProcessEnvironment(env);
    app.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    app.start(QStringLiteral(OMASEED_BINARY), QStringList());
    QVERIFY(app.waitForStarted(5000));
    struct Stop
    {
        QProcess &app;
        ~Stop()
        {
            app.terminate();
            if (!app.waitForFinished(10000))
                app.kill();
            app.waitForFinished(2000);
        }
    } stop{app};

    // Asked until the first sync is done: before it, do answers first-sync (4).
    const QStringList recheck{
        QStringLiteral("do"), QStringLiteral("recheck"), QStringLiteral("cccccccc")};
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 30000) {
        QVERIFY(run(recheck + QStringList{QStringLiteral("--json")}, bus));
        if (m_exitCode == 0)
            break;
        QTest::qWait(300);
    }
    QVERIFY2(m_exitCode == 0, qPrintable(m_out + m_err));
    QJsonObject object = json();
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("do"));
    QCOMPARE(object.value(QLatin1String("answer")).toString(), QStringLiteral("not-asked"));
    QCOMPARE(object.value(QLatin1String("done")).toArray().size(), 1);

    QVERIFY(run(recheck, bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out, QStringLiteral("t sent to recheck\n"));

    QVERIFY(run({QStringLiteral("do"), QStringLiteral("delete"), QStringLiteral("cccccccc")}, bus));
    QCOMPARE(m_exitCode, 5);
    QVERIFY2(m_err.contains(QLatin1String("doesn't delete, rename or change settings")),
             qPrintable(m_err));

    QVERIFY(
        run({QStringLiteral("do"), QStringLiteral("recheck"), QStringLiteral("deadbeef")}, bus));
    QCOMPARE(m_exitCode, 4);

    QVERIFY(run({QStringLiteral("do"), QStringLiteral("--list")}, bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(QStringLiteral("Settings › General › Agents\n")), qPrintable(m_out));
    QVERIFY(m_out.contains(QLatin1String("  Fixes    allow run at once\n")));

    QVERIFY(run(recheck + QStringList{QStringLiteral("--dry-run")}, bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out, QStringLiteral("Dry run, nothing done: recheck would run · t\n"));

    // C50 GT4, GT5: the CLI reads the plan and sends its text; a file it can't read is usage.
    QTemporaryDir plans;
    QFile file(plans.filePath(QStringLiteral("plan.json")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(R"({"v":1,"lines":[{"action":"recheck","torrents":["cccccccc"]}]})");
    file.close();
    QVERIFY(run({QStringLiteral("do"),
                 QStringLiteral("--plan"),
                 file.fileName(),
                 QStringLiteral("--dry-run")},
                bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(QStringLiteral("Plan for ")) &&
                 m_out.contains(QLatin1String("dry run, nothing done")) &&
                 m_out.endsWith(QLatin1String("Every line would run.\n")),
             qPrintable(m_out));
    QVERIFY(run(
        {QStringLiteral("do"), QStringLiteral("--plan"), file.fileName(), QStringLiteral("--json")},
        bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    object = json();
    QVERIFY(object.value(QLatin1String("plan")).toBool());
    QCOMPARE(object.value(QLatin1String("outcome")).toString(),
             QStringLiteral("Done: 1 of 1 · by an agent"));
    QVERIFY(run({QStringLiteral("do"),
                 QStringLiteral("--plan"),
                 plans.filePath(QStringLiteral("none.json"))},
                bus));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.contains(QLatin1String("do --plan can't read")), qPrintable(m_err));
}

void TestWords::aFileNamedCheckOpensAsDotSlash()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(ownAsOld(bus));
    // C37 U10: check is a word now, and ./check is still a file to open.
    QVERIFY(run({QStringLiteral("./check")}, bus, true));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_old->opened.size(), 1);
    QVERIFY2(m_old->opened.first().endsWith(QLatin1String("/check")),
             qPrintable(m_old->opened.first()));
}

void TestWords::skinListWithNoDisplayOrBus()
{
    // C37 U3: the CLI answers itself: no display, no bus, nothing running.
    QVERIFY(
        run({QStringLiteral("skin"), QStringLiteral("list"), QStringLiteral("--json")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out.count(QLatin1Char('\n')), 1);
    const QJsonObject object = json();
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("skin list"));
    QVERIFY(!object.value(QLatin1String("omaseed")).toString().isEmpty());
    QCOMPARE(object.value(QLatin1String("choice")).toString(), QStringLiteral("omarchy"));
    const QJsonArray skins = object.value(QLatin1String("skins")).toArray();
    QCOMPARE(skins.size(), 2);
    QCOMPARE(skins.at(0).toObject().value(QLatin1String("name")).toString(),
             QStringLiteral("omarchy"));
    QCOMPARE(skins.at(1).toObject().value(QLatin1String("name")).toString(),
             QStringLiteral("console"));
    // Nothing written: no skins folder made, no state.json.
    QVERIFY(!QFileInfo::exists(m_home->filePath(QStringLiteral("config/omaseed"))));
}

void TestWords::skinCloneThenList()
{
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("clone"),
                 QStringLiteral("console"),
                 QStringLiteral("warm")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out,
             QStringLiteral("warm: a copy of console in ~/config/omaseed/skins/warm/skin.json. "
                            "omaseed check warm draws it.\n"));
    QVERIFY(
        QFileInfo::exists(m_home->filePath(QStringLiteral("config/omaseed/skins/warm/skin.json"))));

    // Again: the name is taken, the folder untouched.
    QFile file(m_home->filePath(QStringLiteral("config/omaseed/skins/warm/skin.json")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray before = file.readAll();
    file.close();
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("clone"),
                 QStringLiteral("omarchy"),
                 QStringLiteral("warm"),
                 QStringLiteral("--json")},
                QString()));
    QCOMPARE(m_exitCode, 2);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("skin-exists"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), before);
    file.close();

    QVERIFY(run({QStringLiteral("skin"), QStringLiteral("list")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out,
             QStringLiteral("* omarchy   built in\n"
                            "  console   built in\n"
                            "  warm      ~/config/omaseed/skins/warm\n"));
}

void TestWords::skinUseWithNoneRunningSavesTheChoice()
{
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("clone"),
                 QStringLiteral("console"),
                 QStringLiteral("warm")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    writeState(R"({"version": 1, "notifications": {"finished": false},
                   "appearance": {"baseSize": 15, "skin": "console"}})");
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    // Nobody owns the name on this bus (not-running), then no bus at all (#283: the same).
    for (const QString &on : {bus, QString()}) {
        QVERIFY(run({QStringLiteral("skin"),
                     QStringLiteral("use"),
                     QStringLiteral("warm"),
                     QStringLiteral("--json")},
                    on));
        QVERIFY2(m_exitCode == 0, qPrintable(m_out + m_err));
        const QJsonObject object = json();
        QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("skin use"));
        QCOMPARE(object.value(QLatin1String("live")).toBool(true), false);
        QCOMPARE(
            object.value(QLatin1String("skin")).toObject().value(QLatin1String("name")).toString(),
            QStringLiteral("warm"));
        const QJsonObject saved = QJsonDocument::fromJson(state()).object();
        QCOMPARE(saved.value(QLatin1String("appearance")).toObject(),
                 (QJsonObject{{QStringLiteral("baseSize"), 15},
                              {QStringLiteral("skin"), QStringLiteral("warm")}}));
        QCOMPARE(saved.value(QLatin1String("notifications")).toObject(),
                 (QJsonObject{{QStringLiteral("finished"), false}}));
        QCOMPARE(saved.value(QLatin1String("version")).toInt(), 1);
    }
    QVERIFY(
        run({QStringLiteral("skin"), QStringLiteral("use"), QStringLiteral("omarchy")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out, QStringLiteral("omarchy is saved: omaseed draws it when it next starts.\n"));
}

void TestWords::skinUseNeverWritesABrokenState()
{
    const QByteArray broken = "{ \"version\": 1, \"appearance\": ";
    writeState(broken);
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("use"),
                 QStringLiteral("console"),
                 QStringLiteral("--json")},
                QString()));
    QCOMPARE(m_exitCode, 5);
    const QJsonObject error = json().value(QLatin1String("error")).toObject();
    QCOMPARE(error.value(QLatin1String("code")).toString(), QStringLiteral("not-written"));
    QCOMPARE(error.value(QLatin1String("message")).toString(),
             QStringLiteral("state.json doesn't parse, so the choice isn't saved: omaseed would "
                            "write over it. Open omaseed and pick the skin in Settings."));
    QCOMPARE(state(), broken);
}

void TestWords::skinUseOnARunningOmaseed()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("clone"),
                 QStringLiteral("console"),
                 QStringLiteral("warm")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    SeedServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));

    QProcess app;
    QProcessEnvironment env = environment(bus);
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    env.insert(QStringLiteral("OMASEED_SERVER_URL"),
               QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    // The log line this test reads, on stderr wherever it runs: CI's container got none of
    // the app's output without these (#224's first CI run on the branch).
    env.insert(QStringLiteral("QT_FORCE_STDERR_LOGGING"), QStringLiteral("1"));
    env.insert(QStringLiteral("QT_LOGGING_RULES"), QStringLiteral("omaseed.theme.info=true"));
    app.setProcessEnvironment(env);
    app.setProcessChannelMode(QProcess::MergedChannels);
    app.start(QStringLiteral(OMASEED_BINARY), QStringList());
    QVERIFY(app.waitForStarted(5000));
    struct Stop
    {
        QProcess &app;
        ~Stop()
        {
            app.terminate();
            if (!app.waitForFinished(10000))
                app.kill();
            app.waitForFinished(2000);
        }
    } stop{app};

    // Asked until its name is taken (status answers).
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 30000) {
        QVERIFY(run({QStringLiteral("status"), QStringLiteral("--json")}, bus));
        if (m_exitCode == 0)
            break;
        QTest::qWait(300);
    }
    QVERIFY2(m_exitCode == 0, qPrintable(m_out + m_err));

    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("use"),
                 QStringLiteral("warm"),
                 QStringLiteral("--json")},
                bus));
    QVERIFY2(m_exitCode == 0, qPrintable(m_out + m_err));
    const QJsonObject object = json();
    QCOMPARE(object.value(QLatin1String("command")).toString(), QStringLiteral("skin use"));
    QCOMPARE(object.value(QLatin1String("live")).toBool(), true);
    const QJsonObject skin = object.value(QLatin1String("skin")).toObject();
    QCOMPARE(skin.value(QLatin1String("name")).toString(), QStringLiteral("warm"));
    QCOMPARE(skin.value(QLatin1String("drawn")).toBool(), true);
    QCOMPARE(skin.value(QLatin1String("dropped")).toInt(), 0);
    // The app saved it (App::setSkinChoice), and repainted: its log line names the skin.
    QCOMPARE(QJsonDocument::fromJson(state())
                 .object()
                 .value(QLatin1String("appearance"))
                 .toObject()
                 .value(QLatin1String("skin"))
                 .toString(),
             QStringLiteral("warm"));
    QString log;
    timer.restart();
    while (!log.contains(QLatin1String("skin: warm")) && timer.elapsed() < 5000) {
        QTest::qWait(100);
        log += QString::fromUtf8(app.readAll());
    }
    QVERIFY2(log.contains(QLatin1String("skin: warm")), qPrintable(log.right(2000)));

    // One the app can't draw is refused, and nothing changes.
    QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral("config/omaseed/skins/paper"))));
    QVERIFY(run({QStringLiteral("skin"), QStringLiteral("use"), QStringLiteral("paper")}, bus));
    QCOMPARE(m_exitCode, 1);
    QVERIFY2(m_err.startsWith(QLatin1String("paper doesn't read (missing)")), qPrintable(m_err));
}

void TestWords::skinUseOnAnOldOneIs3()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(ownAsOld(bus));
    // C37 U6: a running omaseed that can't draw it would write over a saved choice, so
    // nothing is written.
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("use"),
                 QStringLiteral("console"),
                 QStringLiteral("--json")},
                bus));
    QCOMPARE(m_exitCode, 3);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("too-old"));
    QVERIFY(!QFileInfo::exists(statePath()));
}

void TestWords::skinUseOddNames()
{
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("clone"),
                 QStringLiteral("omarchy"),
                 QStringLiteral("warm.v2")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY(
        run({QStringLiteral("skin"), QStringLiteral("use"), QStringLiteral("warm.v2")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(QJsonDocument::fromJson(state())
                 .object()
                 .value(QLatin1String("appearance"))
                 .toObject()
                 .value(QLatin1String("skin"))
                 .toString(),
             QStringLiteral("warm.v2"));
    const QByteArray before = state();
    // A refused name and an unknown one: usage's exit, nothing saved.
    QVERIFY(
        run({QStringLiteral("skin"), QStringLiteral("use"), QStringLiteral(".warm")}, QString()));
    QCOMPARE(m_exitCode, 2);
    QCOMPARE(m_err.trimmed(), QStringLiteral(".warm can't be a skin's name: hidden."));
    QVERIFY(run({QStringLiteral("skin"),
                 QStringLiteral("use"),
                 QStringLiteral("neon"),
                 QStringLiteral("--json")},
                QString()));
    QCOMPARE(m_exitCode, 2);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("unknown-skin"));
    QCOMPARE(state(), before);
}

// DECISIONS C48 (docs/65 §4, §5): the word skill prints the guide with no display, bus or
// running omaseed, and writes nothing.
void TestWords::skillPrintsTheMap()
{
    QVERIFY(run({QStringLiteral("skill")}, QString()));
    QCOMPARE(m_exitStatus, QProcess::NormalExit);
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY(m_err.isEmpty());
    QVERIFY(!m_out.startsWith(QLatin1String("---")));
    QVERIFY(m_out.contains(QLatin1String(": omaseed skill library\n")));
    QVERIFY(!m_out.contains(QLatin1String("ref: ")));
    // Nothing is linked in this home, so the last line says how.
    QVERIFY(m_out.endsWith(QLatin1String("\n\nPages: omaseed skill library, skins, files.\n"
                                         "Coding agents load this by themselves once it is "
                                         "linked: omaseed skill link.\n")));
    QVERIFY(!QFileInfo::exists(m_home->filePath(QStringLiteral("config/omaseed"))));
}

void TestWords::skillPagePrintsIt()
{
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("skins")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QFile source(QStringLiteral(OMASEED_SOURCE_SKILL_DIR "/ref/skins.md"));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(m_out.toUtf8(), source.readAll());
    QVERIFY(!m_out.contains(QLatin1String("Pages: ")));
    for (const char *name : {"library", "files"}) {
        QVERIFY(run({QStringLiteral("skill"), QString::fromLatin1(name)}, QString()));
        QVERIFY2(m_exitCode == 0, qPrintable(m_err));
        QVERIFY(m_out.size() > 100);
    }
    // An unknown page is usage, with the pages named.
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("layoutz")}, QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY(m_out.isEmpty());
    QVERIFY2(m_err.contains(QLatin1String("skill takes a page (library, skins, files), link, "
                                          "unlink or status")),
             qPrintable(m_err));
    // A page links nothing.
    QVERIFY(!QFileInfo::exists(m_home->filePath(QStringLiteral(".claude"))));
    QVERIFY(!QFileInfo::exists(m_home->filePath(QStringLiteral(".agents"))));
}

void TestWords::skillJson()
{
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("--json")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out.count(QLatin1Char('\n')), 1);
    const QJsonObject map = json();
    QCOMPARE(map.keys(),
             (QStringList{QStringLiteral("at"),
                          QStringLiteral("command"),
                          QStringLiteral("page"),
                          QStringLiteral("text"),
                          QStringLiteral("v")}));
    QVERIFY(m_out.startsWith(QLatin1String("{\"v\":1,\"command\":\"skill\",\"at\":\"")));
    QCOMPARE(map.value(QLatin1String("page")).toString(), QStringLiteral("map"));
    const QString text = map.value(QLatin1String("text")).toString();
    QVERIFY(text.endsWith(QLatin1String("\n\nPages: omaseed skill library, skins, files.\n"
                                        "Coding agents load this by themselves once it is "
                                        "linked: omaseed skill link.\n")));
    QVERIFY(run({QStringLiteral("skill")}, QString()));
    QCOMPARE(text, m_out);

    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("library"), QStringLiteral("--json")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    const QJsonObject library = json();
    QCOMPARE(library.value(QLatin1String("command")).toString(), QStringLiteral("skill library"));
    QCOMPARE(library.value(QLatin1String("page")).toString(), QStringLiteral("library"));
    QVERIFY(library.value(QLatin1String("text")).toString().contains(QLatin1String("not-running")));

    // A usage failure with --json is one object on stdout.
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("nope"), QStringLiteral("--json")},
                QString()));
    QCOMPARE(m_exitCode, 2);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("usage"));
}

void TestWords::aFileNamedSkillOpensAsDotSlash()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    QVERIFY(ownAsOld(bus));
    // C37 U10: skill is a word now, and ./skill is still a file to open.
    QVERIFY(run({QStringLiteral("./skill")}, bus, true));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_old->opened.size(), 1);
    QVERIFY2(m_old->opened.first().endsWith(QLatin1String("/skill")),
             qPrintable(m_old->opened.first()));
}

void TestWords::skillIsTheSameAsDashDashSkill()
{
    // AS9 a: --skill stays, a spelling of the word.
    QVERIFY(run({QStringLiteral("skill")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    const QString word = m_out;
    QVERIFY(run({QStringLiteral("--skill")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out, word);
}

// DECISIONS C48 AS6–AS8 (docs/65 §5): skill link, unlink and status, the real binary in this
// test's own HOME (environment(): HOME, XDG_* and OMASEED_SKILL_DIR all scratch), no display
// or bus.
void TestWords::skillLinkThenStatus()
{
    QVERIFY(m_home->path() != QDir::homePath());
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link")}, QString()));
    QCOMPARE(m_exitStatus, QProcess::NormalExit);
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(
                 QString::fromUtf8("linked   ~/.agents/skills/omaseed   → " OMASEED_SOURCE_SKILL_DIR
                                   "\nlinked   ~/.claude/skills/omaseed\n")),
             qPrintable(m_out));
    QVERIFY(m_out.contains(QLatin1String("skipped  Codex, pi, Gemini, Hermes: not installed\n")));
    QVERIFY(m_out.endsWith(QLatin1String("omaseed skill unlink takes it out.\n")));
    for (const char *where : {".agents/skills/omaseed", ".claude/skills/omaseed"})
        QCOMPARE(QFileInfo(m_home->filePath(QString::fromLatin1(where))).readSymLink(),
                 QStringLiteral(OMASEED_SOURCE_SKILL_DIR));

    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("status")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY2(m_out.startsWith(QLatin1String("linked   Claude Code, the shared ~/.agents\n")),
             qPrintable(m_out));
    QVERIFY(m_out.contains(QLatin1String("skipped  Codex, pi, Gemini, Hermes: not installed\n")));
}

void TestWords::skillLinkWritesTheGuideFiles()
{
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    // AS6: the schema the skill names is there before omaseed's first start.
    const QDir config(m_home->filePath(QStringLiteral("config/omaseed")));
    QVERIFY(QFileInfo::exists(config.filePath(QStringLiteral("AGENTS.md"))));
    QVERIFY(QFileInfo::exists(config.filePath(QStringLiteral("skin.schema.json"))));
    QFile claude(config.filePath(QStringLiteral("CLAUDE.md")));
    QVERIFY(claude.open(QIODevice::ReadOnly));
    QVERIFY(claude.readAll().startsWith("@AGENTS.md"));
}

void TestWords::skillUnlink()
{
    QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral(".codex/skills/omaseed"))));
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QVERIFY(m_out.contains(
        QLatin1String("left     ~/.codex/skills/omaseed: a folder of yours, not a link\n")));
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("unlink")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out,
             QStringLiteral("removed  ~/.agents/skills/omaseed\n"
                            "removed  ~/.claude/skills/omaseed\n"
                            "left     ~/.codex/skills/omaseed: a folder of yours, not a link\n"
                            "omaseed skill link puts it back.\n"));
    QVERIFY(!QFileInfo(m_home->filePath(QStringLiteral(".claude/skills/omaseed"))).isSymLink());
    QVERIFY(QFileInfo(m_home->filePath(QStringLiteral(".claude/skills"))).isDir());
    QVERIFY(QFileInfo(m_home->filePath(QStringLiteral(".codex/skills/omaseed"))).isDir());
    // Again: nothing to take out, still 0.
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("unlink")}, QString()));
    QCOMPARE(m_exitCode, 0);
    QVERIFY2(m_out.endsWith(
                 QLatin1String("Nothing to take out: no agent's folder holds omaseed's link.\n")),
             qPrintable(m_out));
}

void TestWords::skillLinkJson()
{
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link"), QStringLiteral("--json")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    QCOMPARE(m_out.count(QLatin1Char('\n')), 1);
    QVERIFY(m_out.startsWith(QLatin1String("{\"v\":1,\"command\":\"skill link\",\"at\":\"")));
    const QJsonObject link = json();
    QVERIFY(!link.contains(QLatin1String("omaseed")));
    QCOMPARE(link.value(QLatin1String("source")).toString(),
             QStringLiteral(OMASEED_SOURCE_SKILL_DIR));
    QCOMPARE(link.value(QLatin1String("changed")).toArray().size(), 2);
    const QJsonArray folders = link.value(QLatin1String("folders")).toArray();
    QCOMPARE(folders.size(), 6);
    const QJsonObject claude = folders.at(1).toObject();
    QCOMPARE(claude.value(QLatin1String("agent")).toString(), QStringLiteral("Claude Code"));
    QCOMPARE(claude.value(QLatin1String("state")).toString(), QStringLiteral("linked"));
    QCOMPARE(claude.value(QLatin1String("path")).toString(),
             m_home->filePath(QStringLiteral(".claude/skills/omaseed")));
    QCOMPARE(claude.value(QLatin1String("target")).toString(),
             QStringLiteral(OMASEED_SOURCE_SKILL_DIR));
    QCOMPARE(folders.at(2).toObject().value(QLatin1String("state")).toString(),
             QStringLiteral("skipped"));
    QVERIFY(folders.at(2).toObject().value(QLatin1String("target")).isNull());

    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("status"), QStringLiteral("--json")},
                QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    const QJsonObject status = json();
    QCOMPARE(status.value(QLatin1String("command")).toString(), QStringLiteral("skill status"));
    QVERIFY(!status.contains(QLatin1String("changed")));
    QCOMPARE(status.value(QLatin1String("folders")).toArray().size(), 6);
}

void TestWords::skillLinkWithNowhereIs5()
{
    // Both folders that are always linked hold a folder of the person's: nothing can take it.
    QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral(".agents/skills/omaseed"))));
    QVERIFY(QDir().mkpath(m_home->filePath(QStringLiteral(".claude/skills/omaseed"))));
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link")}, QString()));
    QCOMPARE(m_exitCode, 5);
    QVERIFY(m_out.isEmpty());
    QVERIFY2(m_err.contains(QLatin1String("no folder could take the skill")), qPrintable(m_err));
    QVERIFY(!QFileInfo::exists(m_home->filePath(QStringLiteral("config/omaseed"))));
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link"), QStringLiteral("--json")},
                QString()));
    QCOMPARE(m_exitCode, 5);
    QCOMPARE(
        json().value(QLatin1String("error")).toObject().value(QLatin1String("code")).toString(),
        QStringLiteral("not-written"));
}

void TestWords::theMapSaysLinkOnlyWhenUnlinked()
{
    const QString line = QStringLiteral(
        "Coding agents load this by themselves once it is linked: omaseed skill link.\n");
    QVERIFY(run({QStringLiteral("skill")}, QString()));
    QVERIFY(m_out.endsWith(line));
    QVERIFY(run({QStringLiteral("skill"), QStringLiteral("link")}, QString()));
    QVERIFY2(m_exitCode == 0, qPrintable(m_err));
    for (const char *spelling : {"skill", "--skill"}) {
        QVERIFY(run({QString::fromLatin1(spelling)}, QString()));
        QVERIFY2(m_exitCode == 0, qPrintable(m_err));
        QVERIFY(!m_out.contains(line));
        QVERIFY(m_out.endsWith(QLatin1String("\n\nPages: omaseed skill library, skins, files.\n")));
    }
    // AGENTS.md never carries it.
    QFile agents(m_home->filePath(QStringLiteral("config/omaseed/AGENTS.md")));
    QVERIFY(agents.open(QIODevice::ReadOnly));
    QVERIFY(!agents.readAll().contains("omaseed skill link."));
}

QTEST_GUILESS_MAIN(TestWords)
#include "tst_words.moc"
