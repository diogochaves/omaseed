// DECISIONS C45 V3 (#163): io.github.diogochaves.omaseed.Agent on a private bus
// (`dbus-daemon --session`, never the desktop's): beside org.freedesktop.Application and the
// widget's Status on the app's object path, Status's introspection unchanged by it, its read
// answering later (a delayed reply), and nothing exported without a connection.

#include "integration/AgentBus.hpp"
#include "integration/AppIdentity.hpp"
#include "integration/SingleInstance.hpp"
#include "integration/StatusBus.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QElapsedTimer>
#include <QProcess>
#include <QTest>
#include <QTimer>

namespace Identity = omaseed::integration::AppIdentity;
namespace AgentBus = omaseed::integration::AgentBus;
namespace StatusBus = omaseed::integration::StatusBus;
using omaseed::integration::AgentHandler;
using omaseed::integration::SingleInstance;
using omaseed::integration::StatusHandler;

namespace {

/// Answers Status a moment later with the options it was given, the others at once.
class FakeAgent : public AgentHandler
{
public:
    QStringList calls;

    void status(const QString &options, Reply reply) override
    {
        calls << QStringLiteral("status ") + options;
        QTimer::singleShot(150, [reply, options]() {
            reply(QStringLiteral(R"({"v":1,"command":"status","options":%1})").arg(options));
        });
    }

    void errors(const QString &options, Reply reply) override
    {
        calls << QStringLiteral("errors ") + options;
        reply(QStringLiteral(R"({"v":1,"command":"errors"})"));
    }

    void log(const QString &options, Reply reply) override
    {
        calls << QStringLiteral("log ") + options;
        reply(QStringLiteral(R"({"v":1,"command":"log"})"));
    }

    void moves(const QString &options, Reply reply) override
    {
        calls << QStringLiteral("moves ") + options;
        reply(QStringLiteral(R"({"v":1,"command":"moves"})"));
    }
};

/// The widget's handler, only so Status is there to compare.
class QuietStatus : public StatusHandler
{
public:
    bool toggleAltSpeed() override { return false; }
    bool setAltSpeed(bool) override { return false; }
    bool pickAltSpeed(const QString &) override { return false; }
    void describeSources(const QStringList &, Reply reply) override { reply(QStringLiteral("[]")); }
    void addSources(const QStringList &, const QString &, bool, Reply reply) override
    {
        reply(QStringLiteral("{}"));
    }
    void openAddSheet(const QStringList &) override {}
    void showTorrent(const QString &) override {}
    void openFolder(const QString &) override {}
    bool switchServer(const QString &) override { return false; }
    bool invoke(const QString &) override { return false; }
};

/// One interface's block of an introspection.
QString block(const QString &xml, const QString &interface)
{
    const QString from =
        xml.mid(xml.indexOf(QStringLiteral("interface name=\"%1\"").arg(interface)));
    return from.left(from.indexOf(QStringLiteral("</interface>")));
}

} // namespace

class TestAgentBus : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void agentBesideStatusAndApplication();
    void statusUnchanged();
    void delayedReplyArrives();
    void notExportedWithoutConnection();

private:
    QString introspect();
    QDBusMessage call(const QString &method, const QVariantList &arguments);

    QProcess m_dbusDaemon;
    SingleInstance *m_primary = nullptr;
    FakeAgent m_agent;
    QuietStatus m_status;
    QDBusConnection m_caller{QStringLiteral("unset")};
};

void TestAgentBus::initTestCase()
{
    m_dbusDaemon.setProgram(QStringLiteral("dbus-daemon"));
    m_dbusDaemon.setArguments({QStringLiteral("--session"),
                               QStringLiteral("--print-address"),
                               QStringLiteral("--nofork")});
    m_dbusDaemon.start();
    QVERIFY2(m_dbusDaemon.waitForStarted(5000), "dbus-daemon did not start");
    QVERIFY2(m_dbusDaemon.waitForReadyRead(5000), "dbus-daemon printed no address");
    const QByteArray address = m_dbusDaemon.readLine().trimmed();
    QVERIFY(!address.isEmpty());
    // SingleInstance reads the session address from the environment: our private bus.
    qputenv("DBUS_SESSION_BUS_ADDRESS", address);
    m_caller = QDBusConnection::connectToBus(QString::fromUtf8(address), QStringLiteral("agent"));
    QVERIFY(m_caller.isConnected());
}

void TestAgentBus::cleanupTestCase()
{
    QDBusConnection::disconnectFromBus(QStringLiteral("agent"));
    if (m_dbusDaemon.state() != QProcess::NotRunning) {
        m_dbusDaemon.terminate();
        if (!m_dbusDaemon.waitForFinished(2000))
            m_dbusDaemon.kill();
    }
}

void TestAgentBus::init()
{
    m_agent = FakeAgent();
    m_primary = new SingleInstance(this);
    QVERIFY(m_primary->start({}));
    QVERIFY(m_primary->connection().isConnected());
    // As main.cpp does: the widget's interface, then the agents' (each test adds it).
    QVERIFY(StatusBus::exportOn(m_primary, m_primary->connection(), &m_status));
}

void TestAgentBus::cleanup()
{
    delete m_primary;
    m_primary = nullptr;
    QTest::qWait(50); // the name is released asynchronously
}

QString TestAgentBus::introspect()
{
    const QDBusMessage message =
        QDBusMessage::createMethodCall(Identity::dbusServiceName(),
                                       Identity::dbusObjectPath(),
                                       QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                       QStringLiteral("Introspect"));
    QDBusPendingCall pending = m_caller.asyncCall(message, 5000);
    QElapsedTimer timer;
    timer.start();
    while (!pending.isFinished() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return pending.reply().arguments().value(0).toString();
}

QDBusMessage TestAgentBus::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(
        Identity::dbusServiceName(), Identity::dbusObjectPath(), AgentBus::interfaceName(), method);
    message.setArguments(arguments);
    // A blocking call would never let this process answer itself: wait in the event loop.
    QDBusPendingCall pending = m_caller.asyncCall(message, 5000);
    QElapsedTimer timer;
    timer.start();
    while (!pending.isFinished() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return pending.reply();
}

void TestAgentBus::agentBesideStatusAndApplication()
{
    QVERIFY(AgentBus::exportOn(m_primary, m_primary->connection(), &m_agent));
    QCOMPARE(AgentBus::interfaceName(), QStringLiteral("io.github.diogochaves.omaseed.Agent"));
    const QString xml = introspect();
    QVERIFY(xml.contains(QStringLiteral("interface name=\"org.freedesktop.Application\"")));
    QVERIFY(
        xml.contains(QStringLiteral("interface name=\"io.github.diogochaves.omaseed.Status\"")));
    const QString agent = block(xml, AgentBus::interfaceName());
    QVERIFY2(!agent.isEmpty(), qPrintable(xml));
    // Status, Errors, Log and Moves, each (s) → s (C45 V3; Do comes with the actions).
    QCOMPARE(agent.count(QStringLiteral("<method ")), 4);
    for (const char *method : {"Status", "Errors", "Log", "Moves"})
        QVERIFY(agent.contains(QStringLiteral("method name=\"%1\"").arg(QLatin1String(method))));
    QCOMPARE(agent.count(QStringLiteral("direction=\"in\"")), 4);
    QCOMPARE(agent.count(QStringLiteral("direction=\"out\"")), 4);
    QCOMPARE(agent.count(QStringLiteral("type=\"s\"")), 8);
    QVERIFY(!agent.contains(QStringLiteral("<property")));
    QVERIFY(!agent.contains(QStringLiteral("<signal")));
}

void TestAgentBus::statusUnchanged()
{
    // C45 V3: the widget's interface is the same with the agents' beside it.
    const QString before = block(introspect(), StatusBus::interfaceName());
    QVERIFY(!before.isEmpty());
    QVERIFY(AgentBus::exportOn(m_primary, m_primary->connection(), &m_agent));
    const QString after = introspect();
    QVERIFY(after.contains(QStringLiteral("interface name=\"%1\"").arg(AgentBus::interfaceName())));
    QCOMPARE(block(after, StatusBus::interfaceName()), before);
}

void TestAgentBus::delayedReplyArrives()
{
    QVERIFY(AgentBus::exportOn(m_primary, m_primary->connection(), &m_agent));
    QElapsedTimer timer;
    timer.start();
    const QDBusMessage reply = call(QStringLiteral("Status"), {QStringLiteral("{}")});
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.signature(), QStringLiteral("s"));
    QCOMPARE(reply.arguments().value(0).toString(),
             QStringLiteral(R"({"v":1,"command":"status","options":{}})"));
    QVERIFY(timer.elapsed() >= 140); // the handler's answer, not an empty one at once
    QCOMPARE(m_agent.calls, QStringList{QStringLiteral("status {}")});

    // Errors goes to its own handler method (C45 V9).
    const QDBusMessage errors = call(QStringLiteral("Errors"), {QString()});
    QCOMPARE(errors.arguments().value(0).toString(),
             QStringLiteral(R"({"v":1,"command":"errors"})"));
    QCOMPARE(m_agent.calls.last(), QStringLiteral("errors "));

    // And Log and Moves to theirs (C45 V10, V11).
    QCOMPARE(call(QStringLiteral("Log"), {QStringLiteral(R"({"lines":3})")})
                 .arguments()
                 .value(0)
                 .toString(),
             QStringLiteral(R"({"v":1,"command":"log"})"));
    QCOMPARE(m_agent.calls.last(), QStringLiteral(R"(log {"lines":3})"));
    QCOMPARE(call(QStringLiteral("Moves"), {QStringLiteral("{}")}).arguments().value(0).toString(),
             QStringLiteral(R"({"v":1,"command":"moves"})"));
    QCOMPARE(m_agent.calls.last(), QStringLiteral("moves {}"));
}

void TestAgentBus::notExportedWithoutConnection()
{
    QObject object;
    const QDBusConnection none(QStringLiteral("never-connected"));
    QVERIFY(!none.isConnected());
    QVERIFY(!AgentBus::exportOn(&object, none, &m_agent));
    QVERIFY(!AgentBus::exportOn(&object, m_primary->connection(), nullptr));
    QVERIFY(!AgentBus::exportOn(nullptr, m_primary->connection(), &m_agent));
    QVERIFY(object.findChildren<QObject *>().isEmpty());
    // Not exported on the app's path either: a call there finds no such interface.
    const QDBusMessage reply = call(QStringLiteral("Status"), {QStringLiteral("{}")});
    QCOMPARE(reply.type(), QDBusMessage::ErrorMessage);
}

QTEST_GUILESS_MAIN(TestAgentBus)
#include "tst_agentbus.moc"
