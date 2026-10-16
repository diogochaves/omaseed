// #13 — io.github.diogochaves.omaseed.Status on a private bus (`dbus-daemon --session`, never
// the desktop's): the interface sits on the app's object path beside
// org.freedesktop.Application, has exactly the methods of docs/20 §5, reaches its handler by
// the app's well-known name, and answers the two slow methods later (delayed replies). And
// ActivateAction("settings") reaches the app as a signal.

#include "integration/AppIdentity.hpp"
#include "integration/SingleInstance.hpp"
#include "integration/StatusBus.hpp"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QProcess>
#include <QSignalSpy>
#include <QTest>
#include <QTimer>

namespace Identity = omaseed::integration::AppIdentity;
using omaseed::integration::SingleInstance;
using omaseed::integration::StatusHandler;
namespace StatusBus = omaseed::integration::StatusBus;

namespace {

/// Records every call; the two slow methods answer after a short wait, as the real ones do.
class FakeHandler : public StatusHandler
{
public:
    bool altSpeed = false;
    QStringList calls;

    bool toggleAltSpeed() override
    {
        calls << QStringLiteral("toggle");
        altSpeed = !altSpeed;
        return altSpeed;
    }
    bool setAltSpeed(bool on) override
    {
        calls << QStringLiteral("set %1").arg(on);
        altSpeed = on;
        return on;
    }
    void describeSources(const QStringList &sources, Reply reply) override
    {
        calls << QStringLiteral("describe %1").arg(sources.join(QLatin1Char(',')));
        QTimer::singleShot(100,
                           [reply, n = sources.size()]() { reply(QStringLiteral("[%1]").arg(n)); });
    }
    void addSources(const QStringList &sources,
                    const QString &category,
                    bool stopped,
                    Reply reply) override
    {
        calls << QStringLiteral("add %1 %2 %3")
                     .arg(sources.join(QLatin1Char(',')), category)
                     .arg(stopped);
        QTimer::singleShot(100, [reply]() { reply(QStringLiteral("{\"added\":[]}")); });
    }
    void openAddSheet(const QStringList &sources) override
    {
        calls << QStringLiteral("sheet %1").arg(sources.join(QLatin1Char(',')));
    }
    void showTorrent(const QString &hash) override { calls << QStringLiteral("show ") + hash; }
    void openFolder(const QString &hash) override { calls << QStringLiteral("folder ") + hash; }
    bool switchServer(const QString &id) override
    {
        calls << QStringLiteral("switch ") + id;
        return id != QLatin1String("nowhere");
    }
    bool invoke(const QString &id) override
    {
        calls << QStringLiteral("invoke ") + id;
        return id != QLatin1String("bogus");
    }
};

} // namespace

class TestStatusBus : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void introspectionShowsTheInterface();
    void altSpeedAnswersAtOnce();
    void describeAndAddAnswerLater();
    void theWindowMethodsReachTheHandler();
    void theSharedMenuAnswersAtOnce();
    void theWidgetsActionsAreSignals();

private:
    QDBusMessage call(const QString &method, const QVariantList &arguments = {});

    QByteArray m_busAddress;
    QProcess m_dbusDaemon;
    SingleInstance *m_primary = nullptr;
    FakeHandler m_handler;
    QDBusConnection m_caller{QStringLiteral("unset")};
};

void TestStatusBus::initTestCase()
{
    m_dbusDaemon.setProgram(QStringLiteral("dbus-daemon"));
    m_dbusDaemon.setArguments({QStringLiteral("--session"),
                               QStringLiteral("--print-address"),
                               QStringLiteral("--nofork")});
    m_dbusDaemon.start();
    QVERIFY2(m_dbusDaemon.waitForStarted(5000), "dbus-daemon did not start");
    QVERIFY2(m_dbusDaemon.waitForReadyRead(5000), "dbus-daemon printed no address");
    m_busAddress = m_dbusDaemon.readLine().trimmed();
    QVERIFY(!m_busAddress.isEmpty());
    // SingleInstance reads the session address from the environment: our private bus.
    qputenv("DBUS_SESSION_BUS_ADDRESS", m_busAddress);

    m_caller =
        QDBusConnection::connectToBus(QString::fromUtf8(m_busAddress), QStringLiteral("widget"));
    QVERIFY(m_caller.isConnected());
}

void TestStatusBus::cleanupTestCase()
{
    QDBusConnection::disconnectFromBus(QStringLiteral("widget"));
    if (m_dbusDaemon.state() != QProcess::NotRunning) {
        m_dbusDaemon.terminate();
        if (!m_dbusDaemon.waitForFinished(2000))
            m_dbusDaemon.kill();
    }
}

void TestStatusBus::init()
{
    m_handler = FakeHandler();
    m_primary = new SingleInstance(this);
    QVERIFY(m_primary->start({}));
    QVERIFY(m_primary->connection().isConnected());
    // Added after the name is taken, the way main.cpp does once App exists.
    QVERIFY(StatusBus::exportOn(m_primary, m_primary->connection(), &m_handler));
}

void TestStatusBus::cleanup()
{
    delete m_primary;
    m_primary = nullptr;
    QTest::qWait(50); // the name is released asynchronously
}

QDBusMessage TestStatusBus::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(Identity::dbusServiceName(),
                                                          Identity::dbusObjectPath(),
                                                          StatusBus::interfaceName(),
                                                          method);
    message.setArguments(arguments);
    // A blocking call would never let this process answer itself: wait in the event loop.
    QDBusPendingCall pending = m_caller.asyncCall(message, 5000);
    QElapsedTimer timer;
    timer.start();
    while (!pending.isFinished() && timer.elapsed() < 5000)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return pending.reply();
}

void TestStatusBus::introspectionShowsTheInterface()
{
    const QDBusMessage message =
        QDBusMessage::createMethodCall(Identity::dbusServiceName(),
                                       Identity::dbusObjectPath(),
                                       QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                       QStringLiteral("Introspect"));
    QDBusPendingCall pending = m_caller.asyncCall(message, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(pending.isFinished(), 5000);
    QVERIFY2(!pending.isError(), qPrintable(pending.error().message()));
    const QString xml = pending.reply().arguments().value(0).toString();

    QVERIFY(xml.contains(QStringLiteral("interface name=\"org.freedesktop.Application\"")));
    QVERIFY(
        xml.contains(QStringLiteral("interface name=\"io.github.diogochaves.omaseed.Status\"")));

    // Exactly the methods of docs/20 §5 and board 33 §11 change 6 (SwitchServer, Invoke), and
    // nothing that pauses or reads the clipboard.
    const QString status = xml.mid(xml.indexOf(QStringLiteral("omaseed.Status")));
    const QString block = status.left(status.indexOf(QStringLiteral("</interface>")));
    for (const char *method : {"ToggleAltSpeed",
                               "SetAltSpeed",
                               "DescribeSources",
                               "AddSources",
                               "OpenAddSheet",
                               "ShowTorrent",
                               "OpenFolder",
                               "SwitchServer",
                               "Invoke"})
        QVERIFY2(block.contains(QStringLiteral("method name=\"%1\"").arg(QLatin1String(method))),
                 method);
    QCOMPARE(block.count(QStringLiteral("<method ")), 9);
    QVERIFY(!block.contains(QStringLiteral("PauseAll")));
    QVERIFY(!block.contains(QStringLiteral("AddFromClipboard")));
    QVERIFY(!block.contains(QStringLiteral("<property")));
    QVERIFY(!block.contains(QStringLiteral("<signal")));
    // The delayed methods' QDBusMessage is Qt's, not an argument: AddSources is (as, s, b) → s.
    const QString add = block.mid(block.indexOf(QStringLiteral("\"AddSources\"")));
    const QString addMethod = add.left(add.indexOf(QStringLiteral("</method>")));
    QCOMPARE(addMethod.count(QStringLiteral("direction=\"in\"")), 3);
    QCOMPARE(addMethod.count(QStringLiteral("direction=\"out\"")), 1);
    QVERIFY(addMethod.contains(QStringLiteral("type=\"as\"")));
}

void TestStatusBus::altSpeedAnswersAtOnce()
{
    QDBusMessage reply = call(QStringLiteral("ToggleAltSpeed"));
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.arguments().value(0).toBool(), true);

    reply = call(QStringLiteral("SetAltSpeed"), {false});
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.arguments().value(0).toBool(), false);
    QCOMPARE(m_handler.calls, QStringList({QStringLiteral("toggle"), QStringLiteral("set 0")}));
}

void TestStatusBus::describeAndAddAnswerLater()
{
    const QStringList sources{QStringLiteral("magnet:?xt=urn:btih:a"),
                              QStringLiteral("/tmp/b.torrent")};
    QDBusMessage reply = call(QStringLiteral("DescribeSources"), {sources});
    QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
    QCOMPARE(reply.signature(), QStringLiteral("s"));
    QCOMPARE(reply.arguments().value(0).toString(), QStringLiteral("[2]"));

    reply = call(QStringLiteral("AddSources"), {sources, QStringLiteral("linux"), true});
    QVERIFY2(reply.type() == QDBusMessage::ReplyMessage, qPrintable(reply.errorMessage()));
    QCOMPARE(reply.arguments().value(0).toString(), QStringLiteral("{\"added\":[]}"));
    QCOMPARE(m_handler.calls.last(),
             QStringLiteral("add magnet:?xt=urn:btih:a,/tmp/b.torrent linux 1"));
}

void TestStatusBus::theWindowMethodsReachTheHandler()
{
    QCOMPARE(
        call(QStringLiteral("OpenAddSheet"), {QStringList{QStringLiteral("magnet:?x")}}).type(),
        QDBusMessage::ReplyMessage);
    QCOMPARE(call(QStringLiteral("ShowTorrent"), {QStringLiteral("abc")}).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(call(QStringLiteral("OpenFolder"), {QStringLiteral("def")}).type(),
             QDBusMessage::ReplyMessage);
    QCOMPARE(m_handler.calls,
             QStringList({QStringLiteral("sheet magnet:?x"),
                          QStringLiteral("show abc"),
                          QStringLiteral("folder def")}));
}

// docs/33 §11 change 6: SwitchServer(s) → b and Invoke(s) → b, fire-and-forget like the
// others; the answer says whether the app took it.
void TestStatusBus::theSharedMenuAnswersAtOnce()
{
    QDBusMessage reply = call(QStringLiteral("SwitchServer"), {QStringLiteral("private")});
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.signature(), QStringLiteral("b"));
    QCOMPARE(reply.arguments().value(0).toBool(), true);
    reply = call(QStringLiteral("SwitchServer"), {QStringLiteral("nowhere")});
    QCOMPARE(reply.arguments().value(0).toBool(), false);

    reply = call(QStringLiteral("Invoke"), {QStringLiteral("stop-all")});
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.arguments().value(0).toBool(), true);
    reply = call(QStringLiteral("Invoke"), {QStringLiteral("bogus")});
    QCOMPARE(reply.arguments().value(0).toBool(), false);
    QCOMPARE(m_handler.calls,
             QStringList({QStringLiteral("switch private"),
                          QStringLiteral("switch nowhere"),
                          QStringLiteral("invoke stop-all"),
                          QStringLiteral("invoke bogus")}));
}

void TestStatusBus::theWidgetsActionsAreSignals()
{
    QSignalSpy actions(m_primary, &SingleInstance::actionRequested);
    for (const QString &name :
         {QStringLiteral("bogus"), QStringLiteral("settings"), QStringLiteral("retry")}) {
        QDBusMessage message =
            QDBusMessage::createMethodCall(Identity::dbusServiceName(),
                                           Identity::dbusObjectPath(),
                                           QStringLiteral("org.freedesktop.Application"),
                                           QStringLiteral("ActivateAction"));
        message.setArguments({name, QVariant::fromValue(QVariantList()), QVariantMap()});
        QDBusPendingCall pending = m_caller.asyncCall(message, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(pending.isFinished(), 5000);
        QVERIFY2(!pending.isError(), qPrintable(pending.error().message()));
    }
    // An unknown action is accepted and ignored; "settings" and "retry" reach the app.
    QCOMPARE(actions.count(), 2);
    QCOMPARE(actions.at(0).first().toString(), QStringLiteral("settings"));
    QCOMPARE(actions.at(1).first().toString(), QStringLiteral("retry"));
}

QTEST_GUILESS_MAIN(TestStatusBus)
#include "tst_statusbus.moc"
