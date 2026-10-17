// #13 — the bar widget's actions against the :8081 playground: AddSources adds a .torrent and
// a magnet with the category (its save path, automatic management) and stopped, remembers the
// category, and a second add of the same torrent is a duplicate; SetAltSpeed and
// ToggleAltSpeed move the server's alternative limits, which are put back afterwards. Each
// effect is also checked where the widget sees it: the next status file write.
// Skipped when the playground is not answering; never touches :8080.

#include "QbtWire.hpp"
#include "core/SyncEngine.hpp"
#include "integration/StatusWriter.hpp"
#include "ui/app/App.hpp"
#include "ui/app/StatusService.hpp"

#include "LiveServer.hpp"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>

#include <functional>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace omaseed::core;
using omaseed::app::App;
using omaseed::app::StatusService;

namespace {

const char *kFixtureFile = OMASEED_FIXTURES_DIR "/single_file_v1.torrent";
const char *kFixtureHash = "857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04";
/// Nobody seeds it; its own hash so no other live test collides with it.
const char *kMagnetHash = "0102030405060708090a0b0c0d0e0f1011121362";
#define kCategory "omaseed-t62"
#define kSavePath "/tmp/omaseed-t62"

template<class T>
Result<T> await(QObject *context, Reply<T> *reply, int timeoutMs = 20000)
{
    Result<T> result{ApiError(ApiError::Kind::Unknown, QStringLiteral("never finished"))};
    QEventLoop loop;
    reply->then(context, [&](const Result<T> &finished) {
        result = finished;
        loop.quit();
    });
    QTimer::singleShot(timeoutMs, &loop, [&loop]() { loop.quit(); });
    loop.exec();
    return result;
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return predicate();
}

QJsonDocument answer(const std::function<void(StatusService::Reply)> &start)
{
    QString json;
    bool done = false;
    start([&](const QString &reply) {
        json = reply;
        done = true;
    });
    waitFor([&done]() { return done; }, 20000);
    return QJsonDocument::fromJson(json.toUtf8());
}

/// The status file a StatusWriter keeps at `path`, read now.
QJsonObject readStatus(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return QJsonObject();
    return QJsonDocument::fromJson(file.readAll()).object();
}

} // namespace

class TestStatusServiceLive : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    void addsSourcesWithTheCategoryAndStopped();
    void altSpeedFollowsTheCalls();

private:
    QJsonObject infoFor(const QString &hash);
    bool altSpeedOnServer();
    void deleteFixtures();

    QUrl m_url;
    bool m_live = false;
    QTemporaryDir m_config;
    QTemporaryDir m_runtime; ///< where the test's StatusWriter writes
    QByteArray m_savedConfigHome;
    QbtClient m_probe;
};

void TestStatusServiceLive::initTestCase()
{
    m_url = LiveServer::playground();
    QTcpSocket socket;
    socket.connectToHost(m_url.host(), quint16(m_url.port(80)));
    m_live = socket.waitForConnected(2000);
    if (!m_live)
        QSKIP("the :8081 playground is not answering: scripts/dev/test-server.sh start");

    // Never the developer's own server list or state.json.
    QVERIFY(m_config.isValid());
    QVERIFY(m_runtime.isValid());
    m_savedConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_config.path().toUtf8());
    for (const char *name : {"OMASEED_API_KEY", "OMASEED_USERNAME", "OMASEED_PASSWORD"})
        qunsetenv(name);
    qputenv("OMASEED_SERVER_URL", m_url.toString().toUtf8());
    qputenv("OMASEED_SERVER_NAME", QByteArrayLiteral("playground"));

    m_probe.setBaseUrl(m_url);
    deleteFixtures();
    await(this,
          m_probe.torrentsCreateCategory(QStringLiteral(kCategory), QStringLiteral(kSavePath)));
}

void TestStatusServiceLive::cleanupTestCase()
{
    if (!m_live)
        return;
    deleteFixtures();
    await(this, m_probe.torrentsRemoveCategories({QStringLiteral(kCategory)}));
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_NAME");
    if (m_savedConfigHome.isEmpty())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_savedConfigHome);
}

void TestStatusServiceLive::deleteFixtures()
{
    QUrlQuery form;
    form.addQueryItem(QStringLiteral("hashes"),
                      QStringLiteral("%1|%2").arg(QString::fromLatin1(kFixtureHash),
                                                  QString::fromLatin1(kMagnetHash)));
    form.addQueryItem(QStringLiteral("deleteFiles"), QStringLiteral("true"));
    await(this, m_probe.post(QStringLiteral("torrents/delete"), form));
}

QJsonObject TestStatusServiceLive::infoFor(const QString &hash)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("hashes"), hash);
    const Result<QByteArray> result =
        await(this, m_probe.get(QStringLiteral("torrents/info"), query));
    if (result.isError())
        return QJsonObject();
    const QJsonArray rows = QJsonDocument::fromJson(result.value()).array();
    return rows.isEmpty() ? QJsonObject() : rows.first().toObject();
}

bool TestStatusServiceLive::altSpeedOnServer()
{
    const Result<bool> result = await(this, m_probe.transferSpeedLimitsMode());
    return !result.isError() && result.value();
}

void TestStatusServiceLive::addsSourcesWithTheCategoryAndStopped()
{
    App app;
    app.start();
    QVERIFY(app.hasServer());
    QVERIFY(waitFor([&app]() { return app.globalState()->connected(); }, 15000));
    StatusService service(&app);
    omaseed::integration::StatusWriter writer;
    writer.setPath(m_runtime.filePath(QStringLiteral("add/status.json")));
    writer.setIntervals(100, 1000);
    app.attachStatusWriter(&writer);
    // main.cpp's wiring: the category the add remembered reaches the next write.
    connect(&service,
            &StatusService::addCategoryRemembered,
            &writer,
            &omaseed::integration::StatusWriter::reloadAddCategory);
    // A widget add stays in the bar: the window is not asked for.
    QSignalSpy raised(&service, &StatusService::raiseRequested);

    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=omaseed-t62-magnet")
                               .arg(QString::fromLatin1(kMagnetHash));
    const QJsonObject result = answer([&](StatusService::Reply reply) {
                                   service.addSources({QString::fromLatin1(kFixtureFile), magnet},
                                                      QStringLiteral(kCategory),
                                                      true,
                                                      reply);
                               }).object();
    const QJsonArray added = result.value(QStringLiteral("added")).toArray();
    QVERIFY2(added.size() == 2, QJsonDocument(result).toJson().constData());
    QCOMPARE(added.at(0).toObject().value(QStringLiteral("hash")).toString(),
             QString::fromLatin1(kFixtureHash));
    QCOMPARE(added.at(1).toObject().value(QStringLiteral("hash")).toString(),
             QString::fromLatin1(kMagnetHash));
    QCOMPARE(raised.count(), 0);
    QCOMPARE(StatusService::rememberedCategory(), QStringLiteral(kCategory));
    QVERIFY2(waitFor(
                 [&]() {
                     return readStatus(writer.path())
                                .value(QStringLiteral("add"))
                                .toObject()
                                .value(QStringLiteral("category"))
                                .toString() == QStringLiteral(kCategory);
                 },
                 5000),
             "the status file never named the category the add used");

    for (const char *hash : {kFixtureHash, kMagnetHash}) {
        QJsonObject info;
        QVERIFY2(waitFor(
                     [&]() {
                         info = infoFor(QString::fromLatin1(hash));
                         const QString state = info.value(QStringLiteral("state")).toString();
                         return state.startsWith(QStringLiteral("stopped")) ||
                                state.startsWith(QStringLiteral("paused"));
                     },
                     15000),
                 hash);
        QCOMPARE(info.value(QStringLiteral("category")).toString(), QStringLiteral(kCategory));
        QVERIFY(info.value(QStringLiteral("auto_tmm")).toBool());
        QCOMPARE(QDir::cleanPath(info.value(QStringLiteral("save_path")).toString()),
                 QStringLiteral(kSavePath));
    }

    // The same .torrent again: on the server already (by the library, or the server's 409).
    const QJsonObject again =
        answer([&](StatusService::Reply reply) {
            service.addSources({QString::fromLatin1(kFixtureFile)}, QString(), false, reply);
        }).object();
    QCOMPARE(again.value(QStringLiteral("added")).toArray().size(), 0);
    QCOMPARE(again.value(QStringLiteral("duplicates")).toArray().size(), 1);
    QCOMPARE(again.value(QStringLiteral("failed")).toArray().size(), 0);

    // Once the library has it, DescribeSources says so.
    QVERIFY(waitFor(
        [&app]() { return app.torrents()->indexOfHash(QString::fromLatin1(kFixtureHash)) >= 0; },
        15000));
    const QJsonArray described =
        answer([&](StatusService::Reply reply) {
            service.describeSources({QString::fromLatin1(kFixtureFile)}, reply);
        }).array();
    QCOMPARE(described.size(), 1);
    QVERIFY(described.at(0).toObject().value(QStringLiteral("duplicate")).toBool());
    QCOMPARE(described.at(0).toObject().value(QStringLiteral("duplicateOn")).toString(),
             QStringLiteral("playground"));
    app.engine()->stop();
}

void TestStatusServiceLive::altSpeedFollowsTheCalls()
{
    const bool before = altSpeedOnServer();
    App app;
    app.start();
    QVERIFY(waitFor([&app]() { return app.globalState()->connected(); }, 15000));
    StatusService service(&app);
    omaseed::integration::StatusWriter writer;
    writer.setPath(m_runtime.filePath(QStringLiteral("alt/status.json")));
    writer.setIntervals(100, 1000);
    app.attachStatusWriter(&writer);
    const auto altInFile = [&writer]() {
        return readStatus(writer.path())
            .value(QStringLiteral("alt"))
            .toObject()
            .value(QStringLiteral("on"))
            .toBool();
    };

    QCOMPARE(service.setAltSpeed(!before), !before);
    QVERIFY(waitFor([&]() { return altSpeedOnServer() == !before; }, 10000));
    QVERIFY2(waitFor([&]() { return altInFile() == !before; }, 10000),
             "the status file never showed the alt-speed change");
    // Asking again for the same state changes nothing (unlike a toggle).
    QCOMPARE(service.setAltSpeed(!before), !before);
    QTest::qWait(500);
    QCOMPARE(altSpeedOnServer(), !before);

    // The engine's view catches up; then the toggle answers the state it asked for.
    QVERIFY(waitFor([&]() { return app.globalState()->useAltSpeedLimits() == !before; }, 10000));
    QCOMPARE(service.toggleAltSpeed(), before);
    QVERIFY(waitFor([&]() { return altSpeedOnServer() == before; }, 10000));
    QVERIFY(waitFor([&]() { return altInFile() == before; }, 10000));

    service.setAltSpeed(before); // whatever happened above, leave the server as it was
    QVERIFY(waitFor([&]() { return altSpeedOnServer() == before; }, 10000));
    app.engine()->stop();
}

QTEST_MAIN(TestStatusServiceLive)
#include "tst_statusservice_live.moc"
