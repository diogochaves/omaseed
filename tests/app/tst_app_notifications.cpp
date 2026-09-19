// T4.7 — App wired to Notifier: the four notice-worthy SyncEngine events become the right
// desktop notices, the per-event toggles suppress them, a flapping server produces exactly one
// "lost" and one "back", and the "reannounce" action reaches the real torrents/reannounce
// endpoint. SyncEngine itself (when to fire, how to group) is covered by
// tests/core/tst_syncengine_fixtures.cpp; this file only checks what App does with those
// events once they arrive.

#include "core/Format.hpp"
#include "integration/Notifier.hpp"
#include "ui/app/App.hpp"

#include "../integration/FakeNotificationServer.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <chrono>
#include <functional>

using omaseed::app::App;
using omaseed::integration::Notifier;

namespace {

const QString kCompletedHash = QStringLiteral("4a4e3b1d2d0c1a8d1e6a9f0b3c2d4e5f6a7b8c9d");
const QString kTrackerHash = QStringLiteral("d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1d1");

QByteArray httpResponse(int code, const QByteArray &reason, const QByteArray &body)
{
    QByteArray response = "HTTP/1.1 " + QByteArray::number(code) + " " + reason + "\r\n";
    response += "Content-Type: application/json\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    return response;
}

/// The three GET endpoints SyncEngine uses, plus POST torrents/reannounce, which this file
/// needs to check the "reannounce" notification action reaches the real client.
class FakeQbtServer : public QTcpServer
{
public:
    explicit FakeQbtServer(QObject *parent = nullptr)
        : QTcpServer(parent)
    {}

    bool start(quint16 port = 0) { return listen(QHostAddress::LocalHost, port); }
    QUrl url() const { return QUrl(QStringLiteral("http://127.0.0.1:%1").arg(serverPort())); }

    /// One maindata body per poll; the last one repeats.
    void setMainData(const QList<QByteArray> &bodies) { m_bodies = bodies; }

    int mainDataRequests = 0;
    QList<QByteArray> reannounceBodies;

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(socketDescriptor);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, socket, [this, socket, buffer]() {
            buffer->append(socket->readAll());
            const qsizetype headerEnd = buffer->indexOf("\r\n\r\n");
            if (headerEnd < 0)
                return;
            const QByteArray head = buffer->left(headerEnd);
            const QByteArray requestLine = head.split('\n').value(0);
            const QByteArray method = requestLine.split(' ').value(0);
            const QByteArray target = requestLine.split(' ').value(1);

            qsizetype contentLength = 0;
            for (const QByteArray &line : head.split('\n')) {
                if (line.toLower().startsWith("content-length:"))
                    contentLength = line.mid(15).trimmed().toLongLong();
            }
            const qsizetype bodyStart = headerEnd + 4;
            if (buffer->size() < bodyStart + contentLength)
                return; // wait for the rest of the body
            const QByteArray body = buffer->mid(bodyStart, contentLength);
            buffer->clear();

            QByteArray response;
            if (target.startsWith("/api/v2/app/version")) {
                response = httpResponse(200, "OK", "v5.2.3");
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                response = httpResponse(200, "OK", "2.15.1");
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                const int index = mainDataRequests++;
                const QByteArray responseBody =
                    m_bodies.isEmpty()
                        ? QByteArray("{}")
                        : m_bodies.at(std::min(qsizetype(index), m_bodies.size() - 1));
                response = httpResponse(200, "OK", responseBody);
            } else if (method == "POST" && target.startsWith("/api/v2/torrents/reannounce")) {
                reannounceBodies.append(body);
                response = httpResponse(200, "OK", "Ok.");
            } else {
                response = httpResponse(404, "Not Found", "no");
            }

            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    }

private:
    QList<QByteArray> m_bodies;
};

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    return predicate();
}

QJsonObject torrentRow(const QString &name,
                       const QString &category,
                       double progress,
                       qint64 size,
                       bool hasTrackerError = false,
                       const QString &tracker = QString())
{
    QJsonObject row;
    row.insert(QStringLiteral("name"), name);
    row.insert(QStringLiteral("category"), category);
    row.insert(QStringLiteral("progress"), progress);
    row.insert(QStringLiteral("size"), size);
    row.insert(QStringLiteral("total_size"), size);
    row.insert(QStringLiteral("state"),
               progress >= 1.0 ? QStringLiteral("uploading") : QStringLiteral("downloading"));
    row.insert(QStringLiteral("has_tracker_error"), hasTrackerError);
    row.insert(QStringLiteral("tracker"), tracker);
    return row;
}

} // namespace

class TestAppNotifications : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void init();
    void cleanup();

    void completedNoticeMatchesSpecBodyAndOffersOpenFolderWhenTheFolderExists();
    void completedNoticeOmitsOpenFolderWhenTheFolderIsUnknown();
    void trackerErrorGroupsByHostOnePerHost();
    void diskAlmostFullMatchesQ2Wording();
    void disabledToggleSuppressesTheNotice();
    void flappingServerFiresOneLostAndOneBack();
    void reannounceActionReachesTheRealEndpoint();

private:
    FakeNotificationBus m_bus;
    QTemporaryDir m_configDir;
    QByteArray m_previousConfigHome;
};

void TestAppNotifications::initTestCase()
{
    QVERIFY2(m_bus.start(), "could not start a private session dbus-daemon for the test");
    qRegisterMetaType<omaseed::core::ApiError>("omaseed::core::ApiError");
}

void TestAppNotifications::cleanupTestCase()
{
    m_bus.stop();
}

void TestAppNotifications::init()
{
    m_bus.server()->calls.clear();
    m_bus.server()->nextId = 1;

    QVERIFY(m_configDir.isValid());
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_configDir.path().toUtf8());
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_NAME");
}

void TestAppNotifications::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestAppNotifications::completedNoticeMatchesSpecBodyAndOffersOpenFolderWhenTheFolderExists()
{
    QTemporaryDir contentDir;
    QVERIFY(contentDir.isValid());
    const QString contentPath = contentDir.filePath(QStringLiteral("fixture-one"));
    QVERIFY(QDir().mkpath(contentPath));

    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seedTorrents;
    seedTorrents.insert(
        kCompletedHash,
        torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1404304589));
    QJsonObject seed;
    seed.insert(QStringLiteral("full_update"), true);
    seed.insert(QStringLiteral("rid"), 1);
    seed.insert(QStringLiteral("torrents"), seedTorrents);
    app.applyDemoData(seed);

    QJsonObject completion;
    completion.insert(QStringLiteral("rid"), 2);
    QJsonObject diff{{QStringLiteral("progress"), 1.0},
                     {QStringLiteral("completion_on"), 1789834338},
                     {QStringLiteral("content_path"), contentPath}};
    completion.insert(QStringLiteral("torrents"), QJsonObject{{kCompletedHash, diff}});
    app.applyDemoData(completion);

    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Download complete"));
    const QString expectedBody = QStringLiteral("%1 · %2 · %3")
                                     .arg(QStringLiteral("fixture-one"),
                                          omaseed::core::Format::size(1404304589),
                                          QStringLiteral("linux"));
    QCOMPARE(call.body, expectedBody);
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("open-folder"),
                          QStringLiteral("open folder"),
                          QStringLiteral("show"),
                          QStringLiteral("show")}));
}

void TestAppNotifications::completedNoticeOmitsOpenFolderWhenTheFolderIsUnknown()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kCompletedHash,
              torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1000)}}}};
    app.applyDemoData(seed);

    QJsonObject completion{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{{kCompletedHash,
                      QJsonObject{{QStringLiteral("progress"), 1.0},
                                  {QStringLiteral("completion_on"), 1789834338}}}}}};
    app.applyDemoData(completion);

    QCOMPARE(m_bus.server()->calls.size(), 1);
    QCOMPARE(m_bus.server()->calls.first().actions,
             QStringList({QStringLiteral("show"), QStringLiteral("show")}));
}

void TestAppNotifications::trackerErrorGroupsByHostOnePerHost()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kTrackerHash, torrentRow(QStringLiteral("fixture-two"), QString(), 0.3, 1000)}}},
        {QStringLiteral("trackers"),
         QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                      QJsonArray{kTrackerHash}}}}};
    app.applyDemoData(seed);

    QJsonObject diff{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{{kTrackerHash, QJsonObject{{QStringLiteral("has_tracker_error"), true}}}}}};
    app.applyDemoData(diff);

    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Tracker error"));
    QCOMPARE(call.body, QStringLiteral("tracker.opentrackr.org · timed out for 1 torrent"));
    QCOMPARE(call.actions,
             QStringList({QStringLiteral("show-trackers"),
                          QStringLiteral("show trackers"),
                          QStringLiteral("reannounce"),
                          QStringLiteral("reannounce")}));
}

void TestAppNotifications::diskAlmostFullMatchesQ2Wording()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("server_state"),
         QJsonObject{{QStringLiteral("free_space_on_disk"), 1LL * 1024 * 1024 * 1024}}}};
    app.applyDemoData(seed);

    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &call = m_bus.server()->calls.first();
    QCOMPARE(call.summary, QStringLiteral("Disk almost full"));
    QCOMPARE(call.body,
             QStringLiteral("%1 free").arg(omaseed::core::Format::size(1LL * 1024 * 1024 * 1024)));
    QCOMPARE(call.actions, QStringList({QStringLiteral("settings"), QStringLiteral("settings")}));
}

void TestAppNotifications::disabledToggleSuppressesTheNotice()
{
    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);

    app.setNotificationEnabled(QStringLiteral("completed"), false);
    QCOMPARE(app.notificationsEnabled().value(QStringLiteral("completed")).toBool(), false);

    QJsonObject seed{
        {QStringLiteral("full_update"), true},
        {QStringLiteral("rid"), 1},
        {QStringLiteral("torrents"),
         QJsonObject{
             {kCompletedHash,
              torrentRow(QStringLiteral("fixture-one"), QStringLiteral("linux"), 0.5, 1000)}}}};
    app.applyDemoData(seed);

    QJsonObject completion{
        {QStringLiteral("rid"), 2},
        {QStringLiteral("torrents"),
         QJsonObject{{kCompletedHash,
                      QJsonObject{{QStringLiteral("progress"), 1.0},
                                  {QStringLiteral("completion_on"), 1789834338}}}}}};
    app.applyDemoData(completion);

    QCOMPARE(m_bus.server()->calls.size(), 0);

    // Persisted: a fresh App picks the toggle back up from state.json.
    App second;
    Notifier notifier2(m_bus.connection());
    second.attachNotifier(&notifier2);
    QCOMPARE(second.notificationsEnabled().value(QStringLiteral("completed")).toBool(), false);
}

void TestAppNotifications::flappingServerFiresOneLostAndOneBack()
{
    quint16 port = 0;
    {
        QTcpServer probe;
        QVERIFY(probe.listen(QHostAddress::LocalHost));
        port = probe.serverPort();
    } // nothing listens there now: the first poll gets connection refused

    qputenv("OMASEED_SERVER_URL", QStringLiteral("http://127.0.0.1:%1").arg(port).toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");

    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    app.engine()->setIntervals(std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(30));

    app.start();
    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 1; }));
    QCOMPARE(m_bus.server()->calls.size(), 1);
    const auto &lostCall = m_bus.server()->calls.first();
    QCOMPARE(lostCall.summary, QStringLiteral("fake is unreachable"));
    QVERIFY(lostCall.body.startsWith(QStringLiteral("127.0.0.1:%1 · ").arg(port)));
    QVERIFY(lostCall.body.contains(QStringLiteral("retrying every")));
    // The reason is the underlying network error, not the request that carried it (spec §6
    // Fig. 21: "connection refused", not "GET sync/maindata: Connection refused").
    QVERIFY(!lostCall.body.contains(QStringLiteral("GET")));
    QVERIFY(!lostCall.body.contains(QStringLiteral("sync/maindata")));

    // Several failed retries must not spam a second "lost" notice.
    QTest::qWait(100);
    QCOMPARE(m_bus.server()->calls.size(), 1);

    FakeQbtServer server;
    QVERIFY(server.start(port));
    server.setMainData({QByteArrayLiteral("{\"full_update\":true,\"rid\":1,\"torrents\":{}}")});

    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 2; }));
    QCOMPARE(m_bus.server()->calls.size(), 2);
    QCOMPARE(m_bus.server()->calls.at(1).summary, QStringLiteral("fake is back"));
    QCOMPARE(m_bus.server()->calls.at(1).body, QStringLiteral("connected again"));

    app.engine()->stop();
}

void TestAppNotifications::reannounceActionReachesTheRealEndpoint()
{
    FakeQbtServer server;
    QVERIFY(server.start());

    const QByteArray seed =
        QJsonDocument(
            QJsonObject{
                {QStringLiteral("full_update"), true},
                {QStringLiteral("rid"), 1},
                {QStringLiteral("torrents"),
                 QJsonObject{
                     {kTrackerHash,
                      torrentRow(
                          QStringLiteral("fixture-two"), QString(), 0.3, 1000, false, QString())}}},
                {QStringLiteral("trackers"),
                 QJsonObject{{QStringLiteral("udp://tracker.opentrackr.org:1337/announce"),
                              QJsonArray{kTrackerHash}}}},
            })
            .toJson(QJsonDocument::Compact);
    const QByteArray diff =
        QJsonDocument(
            QJsonObject{{QStringLiteral("rid"), 2},
                        {QStringLiteral("torrents"),
                         QJsonObject{{kTrackerHash,
                                      QJsonObject{{QStringLiteral("has_tracker_error"), true}}}}}})
            .toJson(QJsonDocument::Compact);
    server.setMainData({seed, diff});

    qputenv("OMASEED_SERVER_URL", server.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", "fake");

    App app;
    Notifier notifier(m_bus.connection());
    app.attachNotifier(&notifier);
    app.engine()->setIntervals(std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10),
                               std::chrono::milliseconds(10));
    app.start();

    QVERIFY(waitFor([&] { return m_bus.server()->calls.size() >= 1; }));
    // init() reset nextId to 1 and this is the only notice sent so far in this test.
    const quint32 id = 1;

    m_bus.server()->triggerActionInvoked(id, QStringLiteral("reannounce"));
    QVERIFY(waitFor([&] { return !server.reannounceBodies.isEmpty(); }));
    QVERIFY(server.reannounceBodies.first().contains(kTrackerHash.toUtf8()));

    app.engine()->stop();
}

QTEST_GUILESS_MAIN(TestAppNotifications)

#include "tst_app_notifications.moc"
