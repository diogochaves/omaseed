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

    // The skins' words (DECISIONS C37 U3, U6; #223).
    void aFileNamedCheckOpensAsDotSlash();
    void skinListWithNoDisplayOrBus();
    void skinCloneThenList();
    void skinUseWithNoneRunningSavesTheChoice();
    void skinUseNeverWritesABrokenState();
    void skinUseOnARunningOmaseed();
    void skinUseOnAnOldOneIs3();
    void skinUseOddNames();

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
         {"status", "errors", "log", "moves", "skin list", "skin clone", "check", "skin use"})
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
    for (const char *word : {"status", "errors", "log", "moves", "help", "check", "skin"})
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
    QVERIFY(run({QStringLiteral("do"), QStringLiteral("pause")}, QString()));
    QCOMPARE(m_exitCode, 2);
    QVERIFY2(m_err.contains(QLatin1String("do isn't in this omaseed yet")), qPrintable(m_err));
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

QTEST_GUILESS_MAIN(TestWords)
#include "tst_words.moc"
