// T4.6 — ServerDetector: first-run detection of a local qBittorrent. CLAUDE.md is explicit
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
#include <QTemporaryDir>
#include <QTest>

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
}

QTEST_GUILESS_MAIN(TestServerDetector)
#include "tst_serverdetector.moc"
