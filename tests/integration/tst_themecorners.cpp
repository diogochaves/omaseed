#include "integration/HyprlandEvents.hpp"
#include "integration/Theme.hpp"

#include <QDeadlineTimer>
#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using omaseed::integration::HyprlandEvents;
using omaseed::integration::Theme;

namespace {

/// `hyprctl -j getoption` as Hyprland 0.56 prints it.
QByteArray roundingReply(int value)
{
    return QStringLiteral("{\"option\": \"decoration:rounding\", \"int\": %1, \"set\": true }")
        .arg(value)
        .toUtf8();
}

QByteArray powerReply(const char *value)
{
    return QByteArray("{\"option\": \"decoration:rounding_power\", \"float\": ") + value +
           ", \"set\": true }";
}

/// A stand-in for Hyprland: a `hyprctl` that prints whatever `<dir>/<option>` holds (as
/// `hyprctl -j getoption <option>` would), and the event socket, a local server.
class FakeHyprland
{
public:
    FakeHyprland()
    {
        QFile file(hyprctl());
        if (file.open(QIODevice::WriteOnly)) {
            file.write("#!/bin/sh\ncat \"$(dirname \"$0\")/$3\" 2>/dev/null || echo 'no such option'\n");
            file.close();
            file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        }
        m_server.listen(socketPath());
    }

    QString hyprctl() const { return m_dir.filePath(QStringLiteral("hyprctl")); }
    QString socketPath() const { return m_dir.filePath(QStringLiteral("socket2.sock")); }
    QLocalServer &server() { return m_server; }

    void setRounding(int rounding, const char *power)
    {
        write(QStringLiteral("decoration:rounding"), roundingReply(rounding));
        write(QStringLiteral("decoration:rounding_power"), powerReply(power));
    }

    /// The next connection to the event socket, once it has come in. Waits with the event
    /// loop running: the client lives in this thread, and its retry is a timer.
    QLocalSocket *accept(int timeoutMs = 5000)
    {
        QDeadlineTimer deadline(timeoutMs);
        while (!m_server.hasPendingConnections() && !deadline.hasExpired())
            QTest::qWait(20);
        return m_server.nextPendingConnection();
    }

private:
    void write(const QString &option, const QByteArray &reply)
    {
        QFile file(m_dir.filePath(option));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(reply);
    }

    QTemporaryDir m_dir;
    QLocalServer m_server;
};

} // namespace

/// Under Hyprland the compositor owns the window's edge and corners: omaseed draws no edge
/// of its own, and rounds its surfaces only when Hyprland rounds (rounding_power >= 2).
class TestThemeCorners : public QObject
{
    Q_OBJECT

private slots:
    void corners_data();
    void corners();
    void followsConfigReload();
    void eventLines();
    void noSocketIsInert();
};

void TestThemeCorners::corners_data()
{
    QTest::addColumn<QByteArray>("rounding");
    QTest::addColumn<QByteArray>("power");
    QTest::addColumn<int>("radius");
    QTest::addColumn<bool>("edge");

    // No Hyprland: hyprctl failed or printed nothing. omaseed's edge is the only one.
    QTest::newRow("no hyprland") << QByteArray() << QByteArray() << 0 << true;
    QTest::newRow("not json") << QByteArray("no such option") << QByteArray() << 0 << true;
    // Stock Omarchy: square corners, Hyprland's border.
    QTest::newRow("stock") << roundingReply(0) << powerReply("2.000000") << 0 << false;
    // The console's desk: rounding 24 at power 1 is a 12 px chamfer, not an arc.
    QTest::newRow("chamfer") << roundingReply(24) << powerReply("1.000000") << 0 << false;
    QTest::newRow("just under round") << roundingReply(8) << powerReply("1.990000") << 0 << false;
    // A round desk keeps today's radius.
    QTest::newRow("round") << roundingReply(8) << powerReply("2.000000") << 8 << false;
    QTest::newRow("squircle") << roundingReply(12) << powerReply("4.000000") << 12 << false;
    // A Hyprland without the power option only rounds.
    QTest::newRow("no power option")
        << roundingReply(8) << QByteArray("no such option") << 8 << false;
    QTest::newRow("power not asked") << roundingReply(8) << QByteArray() << 8 << false;
    QTest::newRow("negative rounding") << roundingReply(-3) << powerReply("2.000000") << 0 << false;
}

void TestThemeCorners::corners()
{
    QFETCH(QByteArray, rounding);
    QFETCH(QByteArray, power);
    QFETCH(int, radius);
    QFETCH(bool, edge);

    const Theme::WindowCorners corners = Theme::windowCorners(rounding, power);
    QCOMPARE(corners.radius, radius);
    QCOMPARE(corners.edge, edge);
}

/// #37: a `configreloaded` on the event socket asks Hyprland again, both ways between round
/// and chamfered, and again after Hyprland comes back from a restart.
void TestThemeCorners::followsConfigReload()
{
    FakeHyprland hypr;
    QVERIFY(hypr.server().isListening());
    hypr.setRounding(10, "2.000000");

    Theme theme(nullptr, Theme::Hyprland{hypr.hyprctl(), hypr.socketPath()});
    QTRY_COMPARE(theme.cornerRadius(), 10);
    QCOMPARE(theme.windowEdge(), false);
    QLocalSocket *events = hypr.accept();
    QVERIFY(events);

    // Round to chamfered: a profile switch reloads the config with rounding_power 1.
    QSignalSpy radius(&theme, &Theme::cornerRadiusChanged);
    hypr.setRounding(24, "1.000000");
    events->write("workspace>>2\nconfigreloaded>>\n");
    events->flush();
    QTRY_COMPARE(theme.cornerRadius(), 0);
    QCOMPARE(radius.count(), 1);

    // And back, the line arriving in two pieces.
    hypr.setRounding(14, "2.000000");
    events->write("configrel");
    events->flush();
    QTest::qWait(100);
    QCOMPARE(theme.cornerRadius(), 0);
    events->write("oaded>>\n");
    events->flush();
    QTRY_COMPARE(theme.cornerRadius(), 14);

    // Other events never ask.
    hypr.setRounding(6, "2.000000");
    events->write("activewindow>>omaseed,omaseed\nopenwindow>>abc,1,x,y\n");
    events->flush();
    QTest::qWait(300);
    QCOMPARE(theme.cornerRadius(), 14);

    // Hyprland restarts: the socket drops and comes back; the theme reconnects by itself.
    events->disconnectFromServer();
    delete events;
    events = hypr.accept(8000);
    QVERIFY(events);
    events->write("configreloaded>>\n");
    events->flush();
    QTRY_COMPARE(theme.cornerRadius(), 6);
}

void TestThemeCorners::eventLines()
{
    FakeHyprland hypr;
    HyprlandEvents listener(hypr.socketPath());
    QSignalSpy all(&listener, &HyprlandEvents::event);
    QSignalSpy reloaded(&listener, &HyprlandEvents::configReloaded);
    QLocalSocket *events = hypr.accept();
    QVERIFY(events);
    QTRY_VERIFY(listener.isConnected());

    events->write("workspace>>2\nnot an event\nconfigreloaded>>\nmonitoradded>>DP-1");
    events->flush();
    QTRY_COMPARE(reloaded.count(), 1);
    // "monitoradded" has no newline yet: it waits for one.
    QCOMPARE(all.count(), 2);
    QCOMPARE(all.at(0).at(0).toString(), QStringLiteral("workspace"));
    QCOMPARE(all.at(0).at(1).toString(), QStringLiteral("2"));
    events->write("\n");
    events->flush();
    QTRY_COMPARE(all.count(), 3);
    QCOMPARE(all.at(2).at(1).toString(), QStringLiteral("DP-1"));
}

/// Not under Hyprland: no socket, nothing to connect to, the edge is omaseed's own.
void TestThemeCorners::noSocketIsInert()
{
    HyprlandEvents listener{QString()};
    QVERIFY(!listener.isConnected());
    QVERIFY(listener.findChildren<QLocalSocket *>().isEmpty());

    Theme theme(nullptr, Theme::Hyprland{QStringLiteral("/nonexistent/hyprctl"), QString()});
    QTRY_COMPARE(theme.windowEdge(), true);
    QCOMPARE(theme.cornerRadius(), 0);
    QVERIFY(theme.findChildren<HyprlandEvents *>().isEmpty());
}

QTEST_GUILESS_MAIN(TestThemeCorners)
#include "tst_themecorners.moc"
