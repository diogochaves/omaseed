// #13 — StatusService, the app's side of the bar widget's D-Bus methods, without a server:
// how each kind of source is described, duplicates against the (demo) library, what an add
// answers without a server and in a demo, and the window hand-offs; PickAltSpeed's presets
// against a small stand-in qBittorrent (DECISIONS C36 M8). The real adds and the alt-speed
// switch against the :8081 playground are tst_statusservice_live.

#include "core/AltPresets.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"
#include "ui/app/StatusService.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUrlQuery>

#include <chrono>
#include <functional>
#include <memory>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using omaseed::app::App;
using omaseed::app::StatusService;
using omaseed::core::TorrentModel;

namespace {

const char *kFixtureFile = OMASEED_FIXTURES_DIR "/single_file_v1.torrent";
const char *kFixtureHash = "857ddb9df19f876e5a5e3f5b9c4f3991cbce1b04";

/// Runs one of the answering-later calls and returns its JSON.
QJsonDocument answer(const std::function<void(StatusService::Reply)> &start)
{
    QString json;
    bool done = false;
    start([&](const QString &reply) {
        json = reply;
        done = true;
    });
    [&]() { QTRY_VERIFY_WITH_TIMEOUT(done, 10000); }();
    return QJsonDocument::fromJson(json.toUtf8());
}

QString magnetFor(const QString &hash, const QString &name = QString())
{
    QString magnet = QStringLiteral("magnet:?xt=urn:btih:") + hash;
    if (!name.isEmpty())
        magnet += QStringLiteral("&dn=") + name;
    return magnet;
}

/// Just enough qBittorrent for the alternative limits: the mode (setSpeedLimitsMode, read
/// back by the sync) and the set (app/preferences, app/setPreferences), each write kept.
class AltServer : public QTcpServer
{
public:
    bool altOn = false;
    /// qBittorrent's untouched defaults: 10 KiB/s each way.
    QJsonObject preferences{{QStringLiteral("alt_dl_limit"), 10240},
                            {QStringLiteral("alt_up_limit"), 10240},
                            {QStringLiteral("dl_limit"), 0},
                            {QStringLiteral("up_limit"), 0},
                            {QStringLiteral("scheduler_enabled"), false}};
    QStringList modes;
    QList<QJsonObject> sets;

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
            const QUrlQuery form(QString::fromUtf8(buffer->mid(headerEnd + 4, length)));
            buffer->clear();

            QByteArray body = "Ok.";
            if (target.startsWith("/api/v2/app/version")) {
                body = "v5.2.3";
            } else if (target.startsWith("/api/v2/app/webapiVersion")) {
                body = "2.15.1";
            } else if (target.startsWith("/api/v2/sync/maindata")) {
                body = QJsonDocument(
                           QJsonObject{{QStringLiteral("rid"), 1},
                                       {QStringLiteral("full_update"), true},
                                       {QStringLiteral("server_state"),
                                        QJsonObject{{QStringLiteral("use_alt_speed_limits"), altOn},
                                                    {QStringLiteral("connection_status"),
                                                     QStringLiteral("connected")}}},
                                       {QStringLiteral("torrents"), QJsonObject()}})
                           .toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/app/preferences")) {
                body = QJsonDocument(preferences).toJson(QJsonDocument::Compact);
            } else if (target.startsWith("/api/v2/app/setPreferences")) {
                const QJsonObject set =
                    QJsonDocument::fromJson(
                        form.queryItemValue(QStringLiteral("json"), QUrl::FullyDecoded).toUtf8())
                        .object();
                sets << set;
                for (auto it = set.constBegin(); it != set.constEnd(); ++it)
                    preferences.insert(it.key(), it.value());
            } else if (target.startsWith("/api/v2/transfer/setSpeedLimitsMode")) {
                const QString mode = form.queryItemValue(QStringLiteral("mode"));
                modes << mode;
                altOn = mode == QLatin1String("1");
            } else if (target.startsWith("/api/v2/transfer/toggleSpeedLimitsMode")) {
                modes << QStringLiteral("toggle");
                altOn = !altOn;
            }
            socket->write("HTTP/1.1 200 OK\r\nContent-Length: " + QByteArray::number(body.size()) +
                          "\r\nConnection: close\r\n\r\n" + body);
            socket->disconnectFromHost();
        });
    }
};

} // namespace

class TestStatusService : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void describesEachKindOfSource();
    void marksADuplicateOfTheLibrary();
    void anAddWithoutAServerFailsEverySource();
    void aDemoAddSkipsDuplicatesAndNonsense();
    void altSpeedWithoutAServerKeepsTheState();
    void pickAltSpeedGoesThroughThePresets();
    void openAddSheetRaisesTheWindowAndQueuesTheSources();
    void showTorrentGoesWhereANoticeGoes();

private:
    QTemporaryDir m_dir;
    QByteArray m_previousConfigHome;
};

void TestStatusService::initTestCase()
{
    // Nothing here may reach the desktop's session bus (keychain, notices).
    QVERIFY(m_dir.isValid());
    qputenv("DBUS_SESSION_BUS_ADDRESS",
            QStringLiteral("unix:path=%1/no-bus").arg(m_dir.path()).toUtf8());
    for (const char *name : {"OMASEED_SERVER_URL", "OMASEED_API_KEY", "OMASEED_USERNAME"})
        qunsetenv(name);
}

void TestStatusService::init()
{
    m_previousConfigHome = qgetenv("XDG_CONFIG_HOME");
    qputenv("XDG_CONFIG_HOME", m_dir.path().toUtf8());
}

void TestStatusService::cleanup()
{
    if (m_previousConfigHome.isNull())
        qunsetenv("XDG_CONFIG_HOME");
    else
        qputenv("XDG_CONFIG_HOME", m_previousConfigHome);
}

void TestStatusService::describesEachKindOfSource()
{
    App app;
    StatusService service(&app);
    const QString fileUri = QUrl::fromLocalFile(QString::fromLatin1(kFixtureFile)).toString();
    const QStringList sources{
        QStringLiteral("magnet:?xt=urn:btih:9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071"
                       "&dn=Fedora-44.iso&xl=2512345678"),
        QString::fromLatin1(kFixtureFile),
        fileUri,
        QStringLiteral("https://example.org/iso/arch%20linux.torrent"),
        QStringLiteral("not a link"),
        QStringLiteral("/nowhere/missing.torrent"),
    };
    const QJsonArray list = answer([&](StatusService::Reply reply) {
                                service.describeSources(sources, reply);
                            }).array();
    QCOMPARE(list.size(), sources.size());

    const QJsonObject magnet = list.at(0).toObject();
    QCOMPARE(magnet.value(QStringLiteral("source")).toString(), sources.at(0));
    QCOMPARE(magnet.value(QStringLiteral("kind")).toString(), QStringLiteral("magnet"));
    QVERIFY(magnet.value(QStringLiteral("valid")).toBool());
    QCOMPARE(magnet.value(QStringLiteral("name")).toString(), QStringLiteral("Fedora-44.iso"));
    QCOMPARE(magnet.value(QStringLiteral("size")).toInteger(), 2512345678);
    QCOMPARE(magnet.value(QStringLiteral("hash")).toString(),
             QStringLiteral("9a4f6c1e2b3d4f50617283940a1b2c3d4e5f6071"));
    QCOMPARE(magnet.value(QStringLiteral("duplicate")).toBool(), false);
    QCOMPARE(magnet.value(QStringLiteral("duplicateOn")).toString(), QString());

    for (int at : {1, 2}) {
        const QJsonObject file = list.at(at).toObject();
        QCOMPARE(file.value(QStringLiteral("kind")).toString(), QStringLiteral("file"));
        QVERIFY2(file.value(QStringLiteral("valid")).toBool(),
                 qPrintable(file.value(QStringLiteral("error")).toString()));
        QCOMPARE(file.value(QStringLiteral("name")).toString(), QStringLiteral("Single.bin"));
        QVERIFY(file.value(QStringLiteral("size")).toInteger() > 0);
        QCOMPARE(file.value(QStringLiteral("hash")).toString(), QString::fromLatin1(kFixtureHash));
    }

    const QJsonObject url = list.at(3).toObject();
    QCOMPARE(url.value(QStringLiteral("kind")).toString(), QStringLiteral("url"));
    QVERIFY(url.value(QStringLiteral("valid")).toBool());
    QCOMPARE(url.value(QStringLiteral("name")).toString(), QStringLiteral("arch linux.torrent"));
    QCOMPARE(url.value(QStringLiteral("size")).toInteger(), 0);
    QCOMPARE(url.value(QStringLiteral("hash")).toString(), QString());

    for (int at : {4, 5}) {
        const QJsonObject none = list.at(at).toObject();
        QCOMPARE(none.value(QStringLiteral("kind")).toString(), QStringLiteral("none"));
        QVERIFY(!none.value(QStringLiteral("valid")).toBool());
        QVERIFY(!none.value(QStringLiteral("error")).toString().isEmpty());
    }
}

void TestStatusService::marksADuplicateOfTheLibrary()
{
    App app;
    app.startDemo(16);
    StatusService service(&app);
    const QString known =
        app.torrents()->data(app.torrents()->index(0), TorrentModel::HashRole).toString();

    const QJsonArray list =
        answer([&](StatusService::Reply reply) {
            service.describeSources(
                {magnetFor(known.toUpper()),
                 magnetFor(QStringLiteral("0102030405060708090a0b0c0d0e0f1011121399"))},
                reply);
        }).array();
    QCOMPARE(list.size(), 2);
    QVERIFY(list.at(0).toObject().value(QStringLiteral("duplicate")).toBool());
    QCOMPARE(list.at(0).toObject().value(QStringLiteral("duplicateOn")).toString(),
             app.serverName());
    QVERIFY(!app.serverName().isEmpty());
    QVERIFY(!list.at(1).toObject().value(QStringLiteral("duplicate")).toBool());
}

void TestStatusService::anAddWithoutAServerFailsEverySource()
{
    App app;
    QVERIFY(!app.hasServer());
    StatusService service(&app);
    const QStringList sources{magnetFor(QString::fromLatin1(kFixtureHash)),
                              QStringLiteral("nonsense")};
    const QJsonObject result =
        answer([&](StatusService::Reply reply) {
            service.addSources(sources, QStringLiteral("linux"), false, reply);
        }).object();
    QCOMPARE(result.value(QStringLiteral("added")).toArray().size(), 0);
    QCOMPARE(result.value(QStringLiteral("duplicates")).toArray().size(), 0);
    const QJsonArray failed = result.value(QStringLiteral("failed")).toArray();
    QCOMPARE(failed.size(), 2);
    QCOMPARE(failed.at(0).toObject().value(QStringLiteral("source")).toString(), sources.at(0));
    QVERIFY(!failed.at(0).toObject().value(QStringLiteral("error")).toString().isEmpty());
    // Nothing went out, so nothing is remembered.
    QCOMPARE(StatusService::rememberedCategory(), QString());
}

void TestStatusService::aDemoAddSkipsDuplicatesAndNonsense()
{
    App app;
    app.startDemo(16);
    StatusService service(&app);
    const QString known =
        app.torrents()->data(app.torrents()->index(0), TorrentModel::HashRole).toString();
    const QString fresh = magnetFor(QStringLiteral("0102030405060708090a0b0c0d0e0f1011121398"),
                                    QStringLiteral("fresh"));

    const QJsonObject result =
        answer([&](StatusService::Reply reply) {
            service.addSources(
                {magnetFor(known), fresh, QStringLiteral("nonsense")}, QString(), true, reply);
        }).object();
    const QJsonArray added = result.value(QStringLiteral("added")).toArray();
    QCOMPARE(added.size(), 1);
    QCOMPARE(added.at(0).toObject().value(QStringLiteral("source")).toString(), fresh);
    QCOMPARE(added.at(0).toObject().value(QStringLiteral("name")).toString(),
             QStringLiteral("fresh"));
    const QJsonArray duplicates = result.value(QStringLiteral("duplicates")).toArray();
    QCOMPARE(duplicates.size(), 1);
    QCOMPARE(duplicates.at(0).toObject().value(QStringLiteral("hash")).toString(), known);
    QCOMPARE(result.value(QStringLiteral("failed")).toArray().size(), 1);
}

void TestStatusService::altSpeedWithoutAServerKeepsTheState()
{
    App app;
    StatusService service(&app);
    QCOMPARE(service.toggleAltSpeed(), false);
    QCOMPARE(service.setAltSpeed(true), false);
    for (const QString &kind :
         {QStringLiteral("off"), QStringLiteral("turtle"), QStringLiteral("burst")})
        QVERIFY2(!service.pickAltSpeed(kind), qPrintable(kind));

    // The demo writes nothing to a server: the bus can't pick its presets either.
    App demo;
    demo.startDemo(4);
    StatusService demoService(&demo);
    QVERIFY(!demoService.pickAltSpeed(QStringLiteral("turtle")));
    QCOMPARE(demo.altPresets()->current(), QStringLiteral("off"));
}

// DECISIONS C36 M8: PickAltSpeed(off|turtle|burst) is AltPresets::pick (C32 D8 a): turtle turns
// the server's own set on, burst writes its limits over the set with the turtle kept aside,
// off writes the turtle back and turns them off. Any other word is refused, and nothing sent.
void TestStatusService::pickAltSpeedGoesThroughThePresets()
{
    QTemporaryDir home;
    QVERIFY(home.isValid());
    qputenv("XDG_CONFIG_HOME", home.path().toUtf8());
    AltServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    qputenv("OMASEED_SERVER_URL",
            QStringLiteral("http://127.0.0.1:%1").arg(server.serverPort()).toUtf8());
    struct Unset
    {
        ~Unset() { qunsetenv("OMASEED_SERVER_URL"); }
    } unset;

    App app;
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    StatusService service(&app);
    omaseed::core::AltPresets *presets = app.altPresets();
    QTRY_VERIFY_WITH_TIMEOUT(app.hasServer() && presets->isLoaded(), 5000);
    QCOMPARE(presets->current(), QStringLiteral("off"));

    QVERIFY(!service.pickAltSpeed(QStringLiteral("fast")));
    QVERIFY(!service.pickAltSpeed(QString()));
    QVERIFY(server.modes.isEmpty());
    QVERIFY(server.sets.isEmpty());

    QVERIFY(service.pickAltSpeed(QStringLiteral("turtle")));
    QTRY_COMPARE_WITH_TIMEOUT(presets->current(), QStringLiteral("turtle"), 5000);
    QCOMPARE(server.modes, QStringList{QStringLiteral("1")});
    QVERIFY(server.sets.isEmpty()); // the turtle is the server's set as it is
    QCOMPARE(presets->lastPick(), QStringLiteral("turtle"));

    QVERIFY(service.pickAltSpeed(QStringLiteral("burst")));
    QTRY_COMPARE_WITH_TIMEOUT(presets->current(), QStringLiteral("burst"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(server.sets.size(), 1, 5000);
    QCOMPARE(server.sets.first().value(QStringLiteral("alt_up_limit")).toInteger(),
             omaseed::core::AltPresets::kDefaultBurstUp);
    QCOMPARE(presets->lastPick(), QStringLiteral("burst"));

    QVERIFY(service.pickAltSpeed(QStringLiteral("off")));
    QTRY_COMPARE_WITH_TIMEOUT(presets->current(), QStringLiteral("off"), 5000);
    QTRY_COMPARE_WITH_TIMEOUT(server.sets.size(), 2, 5000);
    // The turtle back as it was.
    QCOMPARE(server.sets.last().value(QStringLiteral("alt_dl_limit")).toInteger(), 10240);
    QCOMPARE(server.sets.last().value(QStringLiteral("alt_up_limit")).toInteger(), 10240);
    QVERIFY(!presets->burstOn());
    QVERIFY(!server.altOn);
}

void TestStatusService::openAddSheetRaisesTheWindowAndQueuesTheSources()
{
    App app;
    StatusService service(&app);
    QSignalSpy raised(&service, &StatusService::raiseRequested);
    QSignalSpy queued(&app, &App::addUrisRequested);
    const QString magnet = magnetFor(QString::fromLatin1(kFixtureHash));
    service.openAddSheet({magnet, QStringLiteral("file:///tmp/a%20b.torrent")});
    QCOMPARE(raised.count(), 1);
    QCOMPARE(queued.count(), 1);
    QCOMPARE(queued.first().first().toStringList(),
             QStringList({magnet, QStringLiteral("/tmp/a b.torrent")}));

    // Nothing ticked in the popup: the add sheet all the same, as `a` opens it (#20).
    QSignalSpy blank(&app, &App::addRequested);
    service.openAddSheet({});
    QCOMPARE(raised.count(), 2);
    QCOMPARE(blank.count(), 1);
    QCOMPARE(queued.count(), 1);
}

void TestStatusService::showTorrentGoesWhereANoticeGoes()
{
    App app;
    StatusService service(&app);
    QSignalSpy shown(&app, &App::showTorrentRequested);
    QSignalSpy raised(&service, &StatusService::raiseRequested);
    service.showTorrent(QString::fromLatin1(kFixtureHash));
    QCOMPARE(shown.count(), 1);
    QCOMPARE(shown.first().first().toString(), QString::fromLatin1(kFixtureHash));
    service.showTorrent(QString());
    QCOMPARE(shown.count(), 1);
    QCOMPARE(raised.count(), 1);
}

QTEST_MAIN(TestStatusService)
#include "tst_statusservice.moc"
