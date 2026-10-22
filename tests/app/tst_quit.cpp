// #128: omaseed quits cleanly from outside. SIGTERM, SIGINT and SIGHUP, and `omaseed --quit`
// from a second process, go through the tray's quit, so what omaseed writes on quit is written.
// The seed ledger is the witness: it writes a poll's totals every five minutes, and on quit, so
// totals seen after the first write are on disk only if the quit ran.
//
// Runs the real binary, offscreen, against a fake qBittorrent, with every home, the runtime
// directory and the session bus its own: a private dbus-daemon, or none (the local socket).
// Never the desktop's bus, so a running omaseed is never asked anything.

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <csignal>
#include <memory>

#include <sys/types.h>

namespace {

/// One seeding torrent whose uploaded total the test moves; every poll a full update.
class SeedServer : public QTcpServer
{
public:
    QString hash = QStringLiteral("cccccccccccccccccccccccccccccccccccccccc");
    qint64 uploaded = 1000;
    int polls = 0;

protected:
    void incomingConnection(qintptr descriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(descriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
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
                ++polls;
                const QJsonObject torrent{{QStringLiteral("name"), QStringLiteral("t")},
                                          {QStringLiteral("state"), QStringLiteral("uploading")},
                                          {QStringLiteral("uploaded"), uploaded},
                                          {QStringLiteral("downloaded"), 0},
                                          {QStringLiteral("added_on"), 1700000000}};
                const QJsonObject data{{QStringLiteral("rid"), polls},
                                       {QStringLiteral("full_update"), true},
                                       {QStringLiteral("torrents"), QJsonObject{{hash, torrent}}},
                                       {QStringLiteral("server_state"),
                                        QJsonObject{{QStringLiteral("alltime_ul"), uploaded},
                                                    {QStringLiteral("alltime_dl"), 0}}}};
                body = QJsonDocument(data).toJson(QJsonDocument::Compact);
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class TestQuit : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void sigtermWritesWhatQuitWrites_data();
    void sigtermWritesWhatQuitWrites();
    void quitOptionOverTheBus();
    void quitOptionOverTheLocalSocket();
    void quitOptionWithNothingRunning();

private:
    /// The environment of every omaseed this test starts: its own homes and runtime
    /// directory, offscreen, on `bus` (a private bus, or "" for none), on `server`.
    QProcessEnvironment environment(const QString &bus) const;
    /// Starts omaseed on `server` and waits for its first write of the ledger (the first poll)
    /// and a few polls with a higher total after it.
    bool startAndSeed(QProcess &app, SeedServer &server, const QString &bus);
    /// The ledger's baseline for the torrent: what it last wrote of its uploaded total.
    qint64 ledgerUploaded(const QString &hash) const;
    /// A private session bus; its address, or "" when dbus-daemon can't start.
    QString startBus();

    std::unique_ptr<QTemporaryDir> m_home;
    std::unique_ptr<QProcess> m_bus;
};

void TestQuit::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    QVERIFY(m_home->isValid());
}

void TestQuit::cleanup()
{
    if (m_bus) {
        m_bus->kill();
        m_bus->waitForFinished(3000);
        m_bus.reset();
    }
    m_home.reset();
}

QProcessEnvironment TestQuit::environment(const QString &bus) const
{
    const QString home = m_home->path();
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("HOME"), home);
    env.insert(QStringLiteral("XDG_CONFIG_HOME"), home + QStringLiteral("/config"));
    env.insert(QStringLiteral("XDG_DATA_HOME"), home + QStringLiteral("/data"));
    env.insert(QStringLiteral("XDG_STATE_HOME"), home + QStringLiteral("/state"));
    env.insert(QStringLiteral("XDG_CACHE_HOME"), home + QStringLiteral("/cache"));
    env.insert(QStringLiteral("XDG_RUNTIME_DIR"), home);
    env.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    // Never the desktop's bus: a private one, or an address nobody listens on.
    env.insert(QStringLiteral("DBUS_SESSION_BUS_ADDRESS"),
               bus.isEmpty() ? QStringLiteral("unix:path=%1/no-bus").arg(home) : bus);
    for (const char *name : {"WAYLAND_DISPLAY",
                             "DISPLAY",
                             "HYPRLAND_INSTANCE_SIGNATURE",
                             "OMASEED_API_KEY",
                             "OMASEED_USERNAME",
                             "OMASEED_PASSWORD"})
        env.remove(QString::fromLatin1(name));
    return env;
}

bool TestQuit::startAndSeed(QProcess &app, SeedServer &server, const QString &bus)
{
    if (!server.isListening() && !server.listen(QHostAddress::LocalHost))
        return false;
    QProcessEnvironment env = environment(bus);
    env.insert(QStringLiteral("OMASEED_SERVER_URL"),
               QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()));
    app.setProcessEnvironment(env);
    app.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    // Shown (offscreen), so it polls every 1.5 s rather than 5.
    app.start(QStringLiteral(OMASEED_BINARY), QStringList());
    if (!app.waitForStarted(5000))
        return false;
    // The first poll is written at once (the ledger writes when it has never written).
    if (!QTest::qWaitFor([&server]() { return server.polls >= 2; }, 20000))
        return false;
    // Then the total moves; the ledger holds it in memory for five minutes.
    server.uploaded = 5000;
    const int polls = server.polls;
    return QTest::qWaitFor([&server, polls]() { return server.polls >= polls + 2; }, 20000);
}

qint64 TestQuit::ledgerUploaded(const QString &hash) const
{
    // The server from the environment keeps its ledger under its host and port.
    const QDir dir(m_home->path() + QStringLiteral("/data/omaseed/ledger"));
    const QStringList files = dir.entryList({QStringLiteral("*.sqlite")}, QDir::Files);
    if (files.size() != 1)
        return -1;
    const QString path = dir.filePath(files.constFirst());
    qint64 uploaded = -1;
    {
        QSqlDatabase db =
            QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("read"));
        db.setDatabaseName(path);
        db.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (db.open()) {
            QSqlQuery query(db);
            query.prepare(QStringLiteral("SELECT uploaded FROM baselines WHERE hash = ?"));
            query.addBindValue(hash);
            if (query.exec() && query.next())
                uploaded = query.value(0).toLongLong();
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(QStringLiteral("read"));
    return uploaded;
}

QString TestQuit::startBus()
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

void TestQuit::sigtermWritesWhatQuitWrites_data()
{
    QTest::addColumn<int>("signal");
    QTest::newRow("SIGTERM") << int(SIGTERM);
    QTest::newRow("SIGINT") << int(SIGINT);
    QTest::newRow("SIGHUP") << int(SIGHUP);
    // The witness's control: a process that can't quit leaves the first write only.
    QTest::newRow("SIGKILL") << int(SIGKILL);
}

void TestQuit::sigtermWritesWhatQuitWrites()
{
    QFETCH(int, signal);
    SeedServer server;
    QProcess app;
    QVERIFY(startAndSeed(app, server, QString()));
    const QString status = m_home->path() + QStringLiteral("/omaseed/status.json");
    QVERIFY2(QFile::exists(status), "omaseed wrote no status file");

    QCOMPARE(::kill(pid_t(app.processId()), signal), 0);
    QVERIFY(app.waitForFinished(15000));
    if (signal == SIGKILL) {
        QCOMPARE(app.exitStatus(), QProcess::CrashExit);
        QCOMPARE(ledgerUploaded(server.hash), qint64(1000));
        QVERIFY(QFile::exists(status));
        return;
    }
    QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    QCOMPARE(app.exitCode(), 0);
    // The quit ran: the ledger has the total it held in memory, and the status file is gone.
    QCOMPARE(ledgerUploaded(server.hash), qint64(5000));
    QVERIFY2(!QFile::exists(status), "the status file outlived the quit");
}

void TestQuit::quitOptionOverTheBus()
{
    const QString bus = startBus();
    if (bus.isEmpty())
        QSKIP("dbus-daemon did not start");
    SeedServer server;
    QProcess app;
    QVERIFY(startAndSeed(app, server, bus));

    QProcess quit;
    quit.setProcessEnvironment(environment(bus));
    quit.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    quit.start(QStringLiteral(OMASEED_BINARY), {QStringLiteral("--quit")});
    QVERIFY(quit.waitForFinished(20000));
    QCOMPARE(quit.exitCode(), 0);
    // `--quit` returns once the running one is gone, its quit done.
    QVERIFY(app.waitForFinished(1000));
    QCOMPARE(app.exitStatus(), QProcess::NormalExit);
    QCOMPARE(app.exitCode(), 0);
    QCOMPARE(ledgerUploaded(server.hash), qint64(5000));
}

void TestQuit::quitOptionOverTheLocalSocket()
{
    SeedServer server;
    QProcess app;
    QVERIFY(startAndSeed(app, server, QString()));

    QProcess quit;
    quit.setProcessEnvironment(environment(QString()));
    quit.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    quit.start(QStringLiteral(OMASEED_BINARY), {QStringLiteral("--quit")});
    QVERIFY(quit.waitForFinished(20000));
    QCOMPARE(quit.exitCode(), 0);
    QVERIFY(app.waitForFinished(1000));
    QCOMPARE(app.exitCode(), 0);
    QCOMPARE(ledgerUploaded(server.hash), qint64(5000));
}

void TestQuit::quitOptionWithNothingRunning()
{
    QProcess quit;
    quit.setProcessEnvironment(environment(QString()));
    quit.start(QStringLiteral(OMASEED_BINARY), {QStringLiteral("--quit")});
    QVERIFY(quit.waitForFinished(10000));
    QCOMPARE(quit.exitCode(), 0);
    QVERIFY(quit.readAllStandardError().contains("not running"));
}

QTEST_GUILESS_MAIN(TestQuit)
#include "tst_quit.moc"
