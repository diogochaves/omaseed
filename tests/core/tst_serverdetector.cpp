// T4.6 — ServerDetector: first-run detection of a local qBittorrent, and since #84 of a
// Transmission and a deluge-web too (C25 K1, K2). CLAUDE.md is explicit
// that the real library's port, 127.0.0.1:8080, is only ever probed with a bare TCP
// connect-and-close, never an HTTP request or a login — so every test here points the
// detector at a fake `QTcpServer` of its own on a random port (`setProbePort`), never at
// 8080, and at fake docker/pgrep commands (`setDockerCommand`/`setPgrepCommand`) rather than
// this machine's real ones.

#include "core/ServerDetector.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <utility>

using namespace omaseed::core;

namespace {

/// Writes a tiny shell script that a QProcess can run as a stand-in for `docker`/`pgrep`,
/// returns its absolute path. `exitCode` 0 with `stdout` printed, or nonzero with nothing —
/// enough to cover "found" and "not found"/"missing binary" alike.
QString
writeFakeCommand(QTemporaryDir &dir, const QString &name, int exitCode, const QString &stdoutText)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Text);
    QTextStream out(&file);
    out << "#!/bin/sh\n";
    if (!stdoutText.isEmpty())
        out << "echo '" << stdoutText << "'\n";
    out << "exit " << exitCode << "\n";
    file.close();
    file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return path;
}

/// A port nothing listens on.
quint16 closedPortNow()
{
    QTcpServer probe;
    [[maybe_unused]] const bool listening = probe.listen(QHostAddress::LocalHost);
    return probe.serverPort();
}

/// Transmission's and Deluge's probes away from this machine's real ports and config files:
/// the tests below are about one engine at a time unless they say otherwise.
void isolateOthers(ServerDetector &detector)
{
    static const quint16 closed = closedPortNow();
    for (const EngineKind engine : {EngineKind::Transmission, EngineKind::Deluge}) {
        detector.setProbePort(engine, closed);
        detector.setConfigPaths(engine, {});
    }
}

} // namespace

class TestServerDetector : public QObject
{
    Q_OBJECT

private slots:
    void openPortIsFound();
    void closedPortIsNotFound();
    void dockerFoundGivesCandidate();
    void dockerMissingBinaryIsNotFound();
    void pgrepFoundGivesCandidate();
    void noneFoundGivesNoCandidates();
    void aHangingCommandTimesOut();
    void pgrepNamesTheMatchedProcess();
    void aDesktopClientsConfigGivesItsPortAndSwitch();
    void everyRunningProcessIsCounted();
    void oneRowPerEngineInOrder(); // C25 K1, K2
    void eachEnginesProcessAndConfigGiveItsAddress();
    void aDockerContainerNamesItsEngine();
};

void TestServerDetector::openPortIsFound()
{
    QTcpServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    fake.setMaxPendingConnections(1);

    QTemporaryDir dir;
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(fake.serverPort());
    detector.setDockerCommand(noCmd);
    detector.setPgrepCommand(noCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));

    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().kind, QStringLiteral("port"));
    QVERIFY(candidates.first().detail.contains(QString::number(fake.serverPort())));
}

void TestServerDetector::closedPortIsNotFound()
{
    // Bind a server, learn a free port, then close it: nothing is listening there.
    quint16 freePort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        freePort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(freePort);
    detector.setProbeTimeoutMs(500);
    detector.setDockerCommand(noCmd);
    detector.setPgrepCommand(noCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));

    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 0);
}

void TestServerDetector::dockerFoundGivesCandidate()
{
    QTcpServer fake;
    QVERIFY(fake.listen(QHostAddress::LocalHost));
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString dockerCmd = writeFakeCommand(
        dir,
        QStringLiteral("docker.sh"),
        0,
        QStringLiteral("qbittorrent linuxserver/qbittorrent 0.0.0.0:9090->8080/tcp"));
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(500);
    detector.setDockerCommand(dockerCmd);
    detector.setPgrepCommand(noCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));

    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().kind, QStringLiteral("docker"));
    QVERIFY(candidates.first().detail.contains(QStringLiteral("qbittorrent")));
    QVERIFY(candidates.first().url.contains(QStringLiteral("9090")));
}

void TestServerDetector::dockerMissingBinaryIsNotFound()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(500);
    // No such binary: QProcess::errorOccurred must still resolve the probe as "not found".
    detector.setDockerCommand(QStringLiteral("/nonexistent/omaseed-fake-docker"));
    detector.setPgrepCommand(QStringLiteral("/nonexistent/omaseed-fake-pgrep"));

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));

    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 0);
}

void TestServerDetector::pgrepFoundGivesCandidate()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString pgrepCmd =
        writeFakeCommand(dir, QStringLiteral("pgrep.sh"), 0, QStringLiteral("4242"));
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(500);
    detector.setDockerCommand(noCmd);
    detector.setPgrepCommand(pgrepCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));

    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().kind, QStringLiteral("process"));
}

void TestServerDetector::noneFoundGivesNoCandidates()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(500);
    detector.setDockerCommand(noCmd);
    detector.setPgrepCommand(noCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));

    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 0);
}

// NET-11: `docker ps` against a wedged daemon never exits; the first-run screen must not
// wait for it forever.
void TestServerDetector::aHangingCommandTimesOut()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString hang = dir.filePath(QStringLiteral("hang.sh"));
    {
        QFile file(hang);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
        file.write("#!/bin/sh\nexec sleep 30\n");
        file.close();
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    }
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(300);
    detector.setCommandTimeoutMs(400);
    detector.setDockerCommand(hang);
    detector.setPgrepCommand(noCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    QElapsedTimer clock;
    clock.start();
    detector.start();
    QVERIFY(spy.wait(3000));
    QVERIFY2(clock.elapsed() < 2500, "the hanging command was waited for");
    QCOMPARE(spy.first().first().value<QList<DetectedServer>>().size(), 0);
}

void TestServerDetector::pgrepNamesTheMatchedProcess()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    // What `pgrep -l -x 'qbittorrent(-nox)?'` prints for the desktop client.
    const QString pgrepCmd =
        writeFakeCommand(dir, QStringLiteral("pgrep.sh"), 0, QStringLiteral("4242 qbittorrent"));
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({}); // never this machine's own qBittorrent config
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(300);
    detector.setDockerCommand(noCmd);
    detector.setPgrepCommand(pgrepCmd);

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));
    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().kind, QStringLiteral("process"));
    QVERIFY(candidates.first().detail.endsWith(QStringLiteral("qbittorrent running")));
}

void TestServerDetector::aDesktopClientsConfigGivesItsPortAndSwitch()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString pgrepCmd =
        writeFakeCommand(dir, QStringLiteral("pgrep.sh"), 0, QStringLiteral("4242 qbittorrent"));
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());
    const QString conf = dir.filePath(QStringLiteral("qBittorrent.conf"));
    const auto writeConf = [&conf](const QByteArray &body) {
        QFile file(conf);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(body);
    };

    const auto detect = [&]() {
        ServerDetector detector;
        detector.setConfigPaths({dir.filePath(QStringLiteral("missing.conf")), conf});
        isolateOthers(detector);
        detector.setProbePort(closedPort);
        detector.setProbeTimeoutMs(300);
        detector.setDockerCommand(noCmd);
        detector.setPgrepCommand(pgrepCmd);
        QSignalSpy spy(&detector, &ServerDetector::finished);
        detector.start();
        [&spy]() { QVERIFY(spy.wait(3000)); }();
        return spy.first().first().value<QList<DetectedServer>>().value(0);
    };

    // Its Web UI on another port: that is the URL to try.
    writeConf("[Preferences]\nWebUI\\Enabled=true\nWebUI\\Port=9090\n");
    DetectedServer found = detect();
    QCOMPARE(found.url, QStringLiteral("http://127.0.0.1:9090"));
    QCOMPARE(found.detail, QStringLiteral("127.0.0.1:9090 · qbittorrent running"));

    // Off, as the desktop client ships: say so, and where to turn it on.
    writeConf("[Preferences]\nWebUI\\Enabled=false\n");
    found = detect();
    QCOMPARE(found.url, QStringLiteral("http://127.0.0.1:8080"));
    QVERIFY2(found.detail.contains(QStringLiteral("Web UI is off")), qPrintable(found.detail));

    // qBittorrent 5.3 (#140): its first start moves the Web UI keys to a [WebUI] section of
    // their own, and the [Preferences] ones are gone.
    writeConf("[Preferences]\nGeneral\\Locale=en\n\n[WebUI]\nEnabled=true\nPort=9191\n");
    found = detect();
    QCOMPARE(found.url, QStringLiteral("http://127.0.0.1:9191"));
    QCOMPARE(found.detail, QStringLiteral("127.0.0.1:9191 · qbittorrent running"));

    writeConf("[Meta]\nMigrationVersion=12\n\n[WebUI]\nEnabled=false\nPort=9191\n");
    found = detect();
    QVERIFY2(found.detail.contains(QStringLiteral("Web UI is off")), qPrintable(found.detail));

    // Only [WebUI], with no Port: 5.3 leaves the default out, as 5.2 did.
    writeConf("[WebUI]\nEnabled=true\n");
    found = detect();
    QCOMPARE(found.url, QStringLiteral("http://127.0.0.1:8080"));
    QVERIFY2(!found.detail.contains(QStringLiteral("Web UI is off")), qPrintable(found.detail));

    // Both, as no qBittorrent leaves it but a hand edit could: 5.3 reads [WebUI], so do we.
    writeConf("[Preferences]\nWebUI\\Enabled=false\nWebUI\\Port=9090\n\n"
              "[WebUI]\nEnabled=true\nPort=9191\n");
    found = detect();
    QCOMPARE(found.url, QStringLiteral("http://127.0.0.1:9191"));
    QVERIFY2(!found.detail.contains(QStringLiteral("Web UI is off")), qPrintable(found.detail));
}

// Q26: two instances (the second with its own --profile) give one candidate, but the count
// says there is another for first run to mention.
void TestServerDetector::everyRunningProcessIsCounted()
{
    quint16 closedPort = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        closedPort = probe.serverPort();
    }

    QTemporaryDir dir;
    const QString twoCmd =
        writeFakeCommand(dir,
                         QStringLiteral("pgrep2.sh"),
                         0,
                         QStringLiteral("4242 qbittorrent-nox\n4343 qbittorrent-nox"));
    const QString oneCmd = writeFakeCommand(
        dir, QStringLiteral("pgrep1.sh"), 0, QStringLiteral("4242 qbittorrent-nox"));
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    detector.setConfigPaths({});
    isolateOthers(detector);
    detector.setProbePort(closedPort);
    detector.setProbeTimeoutMs(300);
    detector.setDockerCommand(noCmd);
    QCOMPARE(detector.runningProcesses(), 0);

    for (const auto &[command, expected] :
         {std::pair{twoCmd, 2}, std::pair{oneCmd, 1}, std::pair{noCmd, 0}}) {
        detector.setPgrepCommand(command);
        QSignalSpy spy(&detector, &ServerDetector::finished);
        detector.start();
        QVERIFY(spy.wait(3000));
        QCOMPARE(detector.runningProcesses(), expected);
        const auto candidates = spy.first().first().value<QList<DetectedServer>>();
        QCOMPARE(candidates.size(), expected > 0 ? 1 : 0);
    }
}

// C25 K1–K2: each engine's usual port, one row each, qBittorrent, Transmission, Deluge.
void TestServerDetector::oneRowPerEngineInOrder()
{
    QTcpServer qbt;
    QTcpServer transmission;
    QTcpServer deluge;
    QVERIFY(qbt.listen(QHostAddress::LocalHost));
    QVERIFY(transmission.listen(QHostAddress::LocalHost));
    QVERIFY(deluge.listen(QHostAddress::LocalHost));
    QTemporaryDir dir;
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());

    ServerDetector detector;
    for (const EngineKind engine :
         {EngineKind::Qbittorrent, EngineKind::Transmission, EngineKind::Deluge})
        detector.setConfigPaths(engine, {});
    // Deluge's listens, but its probe is the last to be set: the order is the engines', not the
    // answers'.
    detector.setProbePort(EngineKind::Deluge, deluge.serverPort());
    detector.setProbePort(EngineKind::Transmission, transmission.serverPort());
    detector.setProbePort(qbt.serverPort());
    detector.setDockerCommand(noCmd);
    detector.setPgrepCommand(noCmd);
    QCOMPARE(ServerDetector::usualPort(EngineKind::Qbittorrent), quint16(8080));
    QCOMPARE(ServerDetector::usualPort(EngineKind::Transmission), quint16(9091));
    QCOMPARE(ServerDetector::usualPort(EngineKind::Deluge), quint16(8112));

    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));
    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 3);
    QCOMPARE(candidates.at(0).engine, QStringLiteral("qbittorrent"));
    QCOMPARE(candidates.at(1).engine, QStringLiteral("transmission"));
    QCOMPARE(candidates.at(2).engine, QStringLiteral("deluge"));
    QCOMPARE(candidates.at(0).label, QStringLiteral("Use qBittorrent on this machine"));
    QCOMPARE(candidates.at(1).label, QStringLiteral("Use Transmission on this machine"));
    QCOMPARE(candidates.at(2).label, QStringLiteral("Use Deluge on this machine"));
    QCOMPARE(candidates.at(1).url,
             QStringLiteral("http://127.0.0.1:%1").arg(transmission.serverPort()));
    // C14 E8: a connect and nothing else; nothing was read or written on any of them.
    for (QTcpServer *server : {&qbt, &transmission, &deluge}) {
        QTRY_VERIFY(server->hasPendingConnections());
        QTcpSocket *socket = server->nextPendingConnection();
        QVERIFY(!socket->waitForReadyRead(200));
        QCOMPARE(socket->bytesAvailable(), qint64(0));
    }
}

void TestServerDetector::eachEnginesProcessAndConfigGiveItsAddress()
{
    QTemporaryDir dir;
    const quint16 closed = closedPortNow();
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());
    const auto write = [&dir](const QString &name, const QByteArray &body) {
        QFile file(dir.filePath(name));
        [[maybe_unused]] const bool opened = file.open(QIODevice::WriteOnly | QIODevice::Truncate);
        file.write(body);
        return file.fileName();
    };
    // transmission-daemon's settings.json with its RPC moved; deluge-web's two-object web.conf.
    const QString settings = write(
        QStringLiteral("settings.json"),
        R"({"rpc-enabled": true, "rpc-port": 9191, "rpc-url": "/tr/", "rpc-whitelist": "127.0.0.1"})");
    const QString webConf = write(QStringLiteral("web.conf"),
                                  "{\n  \"file\": 2,\n  \"format\": 1\n}{\n  \"base\": \"/\",\n"
                                  "  \"port\": 8113,\n  \"https\": false\n}");

    const auto detect = [&](const QString &processes) {
        const QString pgrep = writeFakeCommand(dir, QStringLiteral("pgrep.sh"), 0, processes);
        ServerDetector detector;
        for (const EngineKind engine :
             {EngineKind::Qbittorrent, EngineKind::Transmission, EngineKind::Deluge})
            detector.setProbePort(engine, closed);
        detector.setConfigPaths({});
        detector.setConfigPaths(EngineKind::Transmission, {settings});
        detector.setConfigPaths(EngineKind::Deluge, {webConf});
        detector.setProbeTimeoutMs(300);
        detector.setDockerCommand(noCmd);
        detector.setPgrepCommand(pgrep);
        QSignalSpy spy(&detector, &ServerDetector::finished);
        detector.start();
        [&spy]() { QVERIFY(spy.wait(3000)); }();
        return std::pair{spy.first().first().value<QList<DetectedServer>>(),
                         detector.runningProcesses(EngineKind::Deluge)};
    };

    // The kernel keeps 15 characters: "transmission-da" is the daemon.
    auto [found, delugeCount] =
        detect(QStringLiteral("100 transmission-da\n200 deluged\n201 deluge-web"));
    QCOMPARE(found.size(), 2);
    QCOMPARE(found.at(0).engine, QStringLiteral("transmission"));
    QCOMPARE(found.at(0).kind, QStringLiteral("process"));
    QCOMPARE(found.at(0).url, QStringLiteral("http://127.0.0.1:9191/tr/rpc"));
    QCOMPARE(found.at(0).detail, QStringLiteral("127.0.0.1:9191 · transmission-daemon running"));
    QCOMPARE(found.at(1).engine, QStringLiteral("deluge"));
    QCOMPARE(found.at(1).url, QStringLiteral("http://127.0.0.1:8113"));
    QCOMPARE(found.at(1).detail, QStringLiteral("127.0.0.1:8113 · deluge-web running"));
    QCOMPARE(delugeCount, 2);

    // The daemon without its web front: omaseed would have nothing to talk to.
    std::tie(found, delugeCount) = detect(QStringLiteral("200 deluged"));
    QCOMPARE(found.size(), 1);
    QVERIFY2(found.first().detail.contains(QStringLiteral("deluge-web isn't")),
             qPrintable(found.first().detail));

    // Remote access off in Transmission's settings: say so.
    write(QStringLiteral("settings.json"), R"({"rpc-enabled": false})");
    std::tie(found, delugeCount) = detect(QStringLiteral("100 transmission-gt"));
    QCOMPARE(found.size(), 1);
    QCOMPARE(found.first().url, QStringLiteral("http://127.0.0.1:9091"));
    QVERIFY2(found.first().detail.contains(
                 QStringLiteral("transmission-gtk running, its remote access is off")),
             qPrintable(found.first().detail));
}

void TestServerDetector::aDockerContainerNamesItsEngine()
{
    QTemporaryDir dir;
    const quint16 closed = closedPortNow();
    const QString docker = writeFakeCommand(
        dir,
        QStringLiteral("docker.sh"),
        0,
        QStringLiteral("seedbox lscr.io/linuxserver/transmission 0.0.0.0:9095->9091/tcp, "
                       "0.0.0.0:51413->51413/tcp"));
    const QString noCmd = writeFakeCommand(dir, QStringLiteral("nocmd.sh"), 1, QString());
    ServerDetector detector;
    for (const EngineKind engine :
         {EngineKind::Qbittorrent, EngineKind::Transmission, EngineKind::Deluge}) {
        detector.setProbePort(engine, closed);
        detector.setConfigPaths(engine, {});
    }
    detector.setProbeTimeoutMs(300);
    detector.setDockerCommand(docker);
    detector.setPgrepCommand(noCmd);
    QSignalSpy spy(&detector, &ServerDetector::finished);
    detector.start();
    QVERIFY(spy.wait(3000));
    const auto candidates = spy.first().first().value<QList<DetectedServer>>();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().engine, QStringLiteral("transmission"));
    QCOMPARE(candidates.first().url, QStringLiteral("http://127.0.0.1:9095"));
    QCOMPARE(candidates.first().detail,
             QStringLiteral("127.0.0.1:9095 · Docker container seedbox"));
}

QTEST_GUILESS_MAIN(TestServerDetector)
#include "tst_serverdetector.moc"
