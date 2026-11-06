// A Deluge server driven in the app (#87, E6 step 11a; DECISIONS C14 E6, C25): App on a fake
// deluge-web 2.2 (FakeDeluge, answering from the daemon's recorded answers) signs in with its one
// password (K7), polls the library into its models, sends each everyday action as Deluge's
// method, offers none of what Deluge lacks (the feature record), reads deluge-web without a
// daemon as a server that is down (K10, C12), and connects the daemon stored with the server
// when deluge-web has none, never another one. Step 11b: the Label plugin's labels in the
// categories place and words (K13, K15), a ratio of each torrent's own (K18), the add's
// options (K27) and a magnet's files from the prefetch (K26), details without web seeds (H).

#include "FakeDeluge.hpp"

#include "core/ActionOutcome.hpp"
#include "core/CategoryModel.hpp"
#include "core/GlobalState.hpp"
#include "core/SecretStore.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentWebSeedsModel.hpp"
#include "ui/app/App.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <memory>

using omaseed::app::App;
using omaseed::core::SecretStore;
using namespace fakedeluge;

namespace {

const QString kPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kSingle = QStringLiteral("ed6dc254e7756302401a59491e69f6a53ac157be");
const QString kError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
const QString kHost = QStringLiteral("omaseed0test0daemon"); // web_get_hosts.json's

/// The recorded daemon behind a deluge-web that changes: a pause or a resume moves a torrent's
/// state, a remove takes it out, web.connect connects it.
class Daemon
{
public:
    FakeServer server;
    FakeDeluge web;
    RecordedDeluge recorded;
    /// What core.prefetch_magnet_metadata answers ([hash, base64 info]).
    QJsonValue prefetch;

    Daemon()
    {
        server.setHandler(web.handler());
        web.answer = [this](const QString &method, const QJsonArray &params) {
            return answer(method, params);
        };
    }

    QUrl url() const
    {
        QUrl url = server.url();
        url.setPath(QStringLiteral("/"));
        return url;
    }

    /// The ids (the first parameter) each call of `method` carried, in order.
    QList<QStringList> idsOf(const QString &method) const
    {
        QList<QStringList> out;
        for (const Call &call : web.callsOf(method)) {
            QStringList ids;
            for (const QJsonValue &id : call.params.at(0).toArray())
                ids.append(id.toString());
            out.append(ids);
        }
        return out;
    }

    std::optional<Call> lastOf(const QString &method) const
    {
        const QList<Call> calls = web.callsOf(method);
        if (calls.isEmpty())
            return std::nullopt;
        return calls.last();
    }

    QString state(const QString &hash) const
    {
        return recorded.torrents.value(hash).toObject().value(QStringLiteral("state")).toString();
    }

private:
    void setPaused(const QJsonArray &ids, bool paused)
    {
        for (const QJsonValue &id : ids) {
            QJsonObject torrent = recorded.torrents.value(id.toString()).toObject();
            if (torrent.isEmpty())
                continue;
            const bool done = torrent.value(QStringLiteral("progress")).toDouble() >= 100.0;
            torrent.insert(QStringLiteral("paused"), paused);
            torrent.insert(QStringLiteral("state"),
                           paused ? QStringLiteral("Paused")
                           : done ? QStringLiteral("Seeding")
                                  : QStringLiteral("Downloading"));
            recorded.torrents.insert(id.toString(), torrent);
        }
    }

    QJsonValue answer(const QString &method, const QJsonArray &params)
    {
        if (method == QLatin1String("core.pause_torrents")) {
            setPaused(params.at(0).toArray(), true);
            return {};
        }
        if (method == QLatin1String("core.resume_torrents")) {
            setPaused(params.at(0).toArray(), false);
            return {};
        }
        if (method == QLatin1String("core.remove_torrents")) {
            for (const QJsonValue &id : params.at(0).toArray())
                recorded.torrents.remove(id.toString());
            return QJsonArray{};
        }
        if (method == QLatin1String("web.connect")) {
            recorded.connected = true;
            return result("web_connect.json");
        }
        if (method == QLatin1String("core.add_torrent_magnet"))
            return result("core_add_torrent_magnet.json");
        if (method == QLatin1String("core.prefetch_magnet_metadata"))
            return prefetch;
        if (method == QLatin1String("label.set_torrent") || method == QLatin1String("label.add"))
            return {};
        if (method == QLatin1String("core.set_torrent_options") ||
            method == QLatin1String("core.force_recheck") ||
            method == QLatin1String("core.force_reannounce") ||
            method == QLatin1String("core.queue_up") || method == QLatin1String("core.queue_down"))
            return {};
        return recorded.answer(method, params);
    }
};

} // namespace

class TestAppDeluge : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void aDelugeServerIsPolledIntoTheLibrary();
    void whatDelugeLacksIsNotOffered();
    void theEverydayActionsAreDelugesMethods();
    void aPlainAddIsDelugesAdd();
    void detailsAreAskedOfDeluge();
    void delugeWebWithoutADaemonIsDown();
    void theStoredDaemonIsConnectedWhenDelugeWebHasNone();
    void labelsAreTheCategoriesInDelugesWords();
    void aTorrentKeepsARatioOfItsOwn();
    void anAddTakesDelugesOptions();
    void aMagnetsFilesComeFromThePrefetch();

private:
    /// Starts `app` on `daemon` through the environment with deluge-web's password, polling
    /// every 100 ms, and waits for the six recorded torrents.
    bool startOn(App &app, Daemon &daemon);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppDeluge::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppDeluge::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_ENGINE");
    qunsetenv("OMASEED_PASSWORD");
    m_home.reset();
}

bool TestAppDeluge::startOn(App &app, Daemon &daemon)
{
    qputenv("OMASEED_SERVER_URL", daemon.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_ENGINE", "deluge");
    qputenv("OMASEED_PASSWORD", daemon.web.password.toUtf8());
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.torrents()->count() == 6; }, 5000);
}

void TestAppDeluge::aDelugeServerIsPolledIntoTheLibrary()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QCOMPARE(app.serverEngineName(), QStringLiteral("Deluge"));
    QCOMPARE(app.globalState()->serverVersion(), QStringLiteral("2.2.0"));
    // K7: one password, signed in once.
    QCOMPARE(daemon.web.logins, 1);
    // The poll is web.update_ui, through deluge-web's /json alone.
    QVERIFY(daemon.web.methods().contains(QStringLiteral("web.update_ui")));
    for (const auto &request : std::as_const(daemon.server.requests))
        QCOMPARE(request.target, QByteArray("/json"));
    QVERIFY(!app.torrentRow(kPack).value(QStringLiteral("stopped")).toBool());
    QVERIFY(app.torrentRow(kSingle).value(QStringLiteral("stopped")).toBool());
    QCOMPARE(app.torrentRow(kPack).value(QStringLiteral("name")).toString(),
             QStringLiteral("Fixture Pack"));
    QVERIFY(!app.isServerLost());

    // A torrent removed on the daemon goes from the list at the next poll.
    daemon.recorded.torrents.remove(kMagnet);
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 5, 3000);
    app.engine()->stop();
}

void TestAppDeluge::whatDelugeLacksIsNotOffered()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const QVariantMap can = app.featuresMap();
    // Board 47's Deluge column (C25): what it has, with the Label plugin on…
    for (const char *on : {"categories",
                           "queue",
                           "forceStart",
                           "sequentialDownload",
                           "firstLastPiece",
                           "superSeeding",
                           "fetchMetadata",
                           "addOptions",
                           "moveProgress",
                           "moveStopsOnFailure",
                           "roomOnServer",
                           "listFolders"})
        QVERIFY2(can.value(QString::fromLatin1(on)).toBool(), on);
    // …and what it lacks, hidden by the views that read these.
    for (const char *off : {"tags",
                            "altSpeeds",
                            "autoManagement",
                            "renameOnDisk",
                            "seedingTimeLimit",
                            "engineLog",
                            "qbtSettings",
                            "torrentsBackup",
                            "rss",
                            "search",
                            "createTorrent",
                            "sessionSettings",
                            "movesHoldAnswers",
                            "editWebSeeds"})
        QVERIFY2(!can.value(QString::fromLatin1(off)).toBool(), off);

    // The Label plugin turned off on the server: the categories go at the next poll.
    daemon.recorded.labelPlugin = false;
    QTRY_VERIFY_WITH_TIMEOUT(!app.featuresMap().value(QStringLiteral("categories")).toBool(), 3000);
    app.engine()->stop();
}

void TestAppDeluge::theEverydayActionsAreDelugesMethods()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));

    app.stopStart({kPack}); // seeding: a pause
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.pause_torrents")), QList<QStringList>{{kPack}}, 3000);
    app.stopStart({kSingle}); // paused: a resume
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.resume_torrents")), QList<QStringList>{{kSingle}}, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!app.torrentRow(kSingle).value(QStringLiteral("stopped")).toBool(),
                             3000);
    app.recheck({kPack});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.force_recheck")), QList<QStringList>{{kPack}}, 3000);
    app.reannounce({kPack});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.force_reannounce")), QList<QStringList>{{kPack}}, 3000);
    app.queueUp({kSingle});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.queue_up")), QList<QStringList>{{kSingle}}, 3000);
    app.queueDown({kSingle});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.queue_down")), QList<QStringList>{{kSingle}}, 3000);
    // K28: the name shown only.
    app.rename(kPack, QStringLiteral("Renamed Pack"));
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("core.set_torrent_options")).has_value(),
                             3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("core.set_torrent_options"))
                 ->params.at(1)
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("Renamed Pack"));
    // Force start: out of the queue's hands, then running.
    const qsizetype resumesBefore = daemon.idsOf(QStringLiteral("core.resume_torrents")).size();
    app.forceStart({kError});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("core.resume_torrents")).size(), resumesBefore + 1, 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("core.set_torrent_options"))
                 ->params.at(1)
                 .toObject()
                 .value(QStringLiteral("auto_managed")),
             QJsonValue(false));

    // Delete, with and without the data.
    app.remove({kMagnet}, false);
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("core.remove_torrents")).has_value(),
                             3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("core.remove_torrents"))->params.at(1),
             QJsonValue(false));
    app.remove({kError}, true);
    QTRY_COMPARE_WITH_TIMEOUT(daemon.idsOf(QStringLiteral("core.remove_torrents")).size(), 2, 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("core.remove_torrents"))->params.at(1), QJsonValue(true));
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 4, 3000);
    app.engine()->stop();
}

void TestAppDeluge::aPlainAddIsDelugesAdd()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy finished(&app, &App::addFinished);
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=new").arg(kMagnet);
    app.addTorrent({{QStringLiteral("kind"), QStringLiteral("magnet")},
                    {QStringLiteral("value"), magnet},
                    {QStringLiteral("infoHashV1"), kMagnet},
                    {QStringLiteral("savePath"), QStringLiteral("/srv/deluge/new")}});
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
    const std::optional<Call> add = daemon.lastOf(QStringLiteral("core.add_torrent_magnet"));
    QVERIFY(add.has_value());
    QCOMPARE(add->params.at(0).toString(), magnet);
    QCOMPARE(add->params.at(1).toObject().value(QStringLiteral("download_location")).toString(),
             QStringLiteral("/srv/deluge/new"));
    app.engine()->stop();
}

void TestAppDeluge::detailsAreAskedOfDeluge()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const qsizetype before = daemon.web.calls.size();
    app.selectTorrent(kPack);
    app.setDetailsVisible(true);
    // Its files and trackers, by hash, as core.get_torrent_status with those keys.
    const auto asked = [&daemon, before](const char *key) {
        for (qsizetype i = before; i < daemon.web.calls.size(); ++i) {
            const Call &call = daemon.web.calls.at(i);
            if (call.method == QLatin1String("core.get_torrent_status") &&
                call.params.at(0).toString() == kPack &&
                call.params.at(1).toArray().contains(QString::fromLatin1(key)))
                return true;
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(asked("files"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(asked("trackers"), 3000);

    // Board 47 H: Deluge reports no web seeds, so none are asked for, and the pane keeps
    // asking for the rest past the tenth tick (where it used to ask, and then stopped).
    const auto statusCalls = [&daemon]() {
        return daemon.web.callsOf(QStringLiteral("core.get_torrent_status")).size();
    };
    QTest::qWait(1500);
    const qsizetype later = statusCalls();
    QTRY_VERIFY_WITH_TIMEOUT(statusCalls() > later + 2, 3000);
    QCOMPARE(app.details()->webSeeds()->count(), 0);
    app.engine()->stop();
}

void TestAppDeluge::delugeWebWithoutADaemonIsDown()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    // K10, C12: the daemon goes; deluge-web answers, with nothing behind it.
    daemon.recorded.connected = false;
    QTRY_VERIFY_WITH_TIMEOUT(app.isServerLost(), 3000);
    QVERIFY(!app.serverRefused());
    const QVariantMap problem =
        app.serverProblem(app.globalState()->lastError(), app.globalState()->retryInSeconds());
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(),
             QStringLiteral("%1's deluge-web has no daemon").arg(app.serverName()));
    QCOMPARE(problem.value(QStringLiteral("kind")).toString(), QStringLiteral("unreachable"));
    // No daemon was stored (the environment's server), so nobody switches deluge-web.
    QVERIFY(daemon.web.callsOf(QStringLiteral("web.connect")).isEmpty());

    // Connected again elsewhere: the library is back.
    daemon.recorded.connected = true;
    QTRY_VERIFY_WITH_TIMEOUT(!app.isServerLost(), 5000);
    QCOMPARE(app.torrents()->count(), 6);
    app.engine()->stop();
}

void TestAppDeluge::theStoredDaemonIsConnectedWhenDelugeWebHasNone()
{
    // A Deluge saved with the daemon its step picked (K10) and its one password (K7, in the
    // file-backed store under the test's own folder), on a deluge-web that has no daemon.
    Daemon daemon;
    daemon.recorded.connected = false;
    qunsetenv("OMASEED_SERVER_URL");
    App app;
    app.secretStore()->setForcedBackend(SecretStore::Backend::File);
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    const QString id = app.addServer({{QStringLiteral("name"), QStringLiteral("nas")},
                                      {QStringLiteral("url"), daemon.url().toString()},
                                      {QStringLiteral("authMode"), QStringLiteral("password")},
                                      {QStringLiteral("engine"), QStringLiteral("deluge")},
                                      {QStringLiteral("daemon"), kHost}},
                                     daemon.web.password);
    QVERIFY(!id.isEmpty());
    QCOMPARE(app.serverName(), QStringLiteral("nas"));
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 6, 5000);
    const QList<Call> connects = daemon.web.callsOf(QStringLiteral("web.connect"));
    QVERIFY(!connects.isEmpty());
    for (const Call &call : connects)
        QCOMPARE(call.params.at(0).toString(), kHost);
    app.engine()->stop();
}

void TestAppDeluge::labelsAreTheCategoriesInDelugesWords()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    // K13, K15: the Label plugin's labels, in the categories place and called labels; no tags.
    QVariantMap can = app.featuresMap();
    QVERIFY(can.value(QStringLiteral("categoriesAreLabels")).toBool());
    QCOMPARE(can.value(QStringLiteral("categoryWord")).toString(), QStringLiteral("label"));
    QCOMPARE(can.value(QStringLiteral("categoriesWord")).toString(), QStringLiteral("labels"));
    QCOMPARE(can.value(QStringLiteral("uncategorizedWord")).toString(), QStringLiteral("no label"));
    QVERIFY(!can.value(QStringLiteral("labelPluginOff")).toBool());
    QVERIFY(!can.value(QStringLiteral("tags")).toBool());
    // H, G, D: what the views leave out on Deluge, and the one line its trackers tab says.
    for (const char *off : {"peerBan", "webSeeds", "addContentLayout", "idleLimit", "renameOnDisk"})
        QVERIFY2(!can.value(QString::fromLatin1(off)).toBool(), off);
    QVERIFY(can.value(QStringLiteral("peerControl")).toBool());
    QVERIFY(can.value(QStringLiteral("trackerStatusInUseOnly")).toBool());
    QVERIFY(can.value(QStringLiteral("renameNameOnly")).toBool());

    // A label set from the app is kept lower case, and the strip says the name Deluge kept.
    app.setCategory({kPack}, QStringLiteral("Fixtures"));
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("label.set_torrent")).has_value(), 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("label.set_torrent"))->params,
             QJsonArray({kPack, QStringLiteral("fixtures")}));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.outcomes()->current().value(QStringLiteral("headline")).toString(),
        QStringLiteral("Fixture Pack labelled fixtures"),
        3000);

    // K15: a label's scope says where it moves torrents when done, or that it has no folder.
    QTRY_VERIFY_WITH_TIMEOUT(!app.categories()->savePathOf(QStringLiteral("fixtures")).isEmpty(),
                             3000);
    app.filtered()->setCategory(QStringLiteral("fixtures"));
    QTRY_VERIFY_WITH_TIMEOUT(
        app.scopeSentence().contains(QStringLiteral("a label · moves to %1 when done")
                                         .arg(app.categories()->savePathOf(
                                             QStringLiteral("fixtures")))),
        3000);
    app.filtered()->setCategory(QStringLiteral("empty"));
    QTRY_VERIFY_WITH_TIMEOUT(
        app.scopeSentence().endsWith(QStringLiteral("a label · no folder of its own")), 3000);
    app.filtered()->clearCategory();

    // The plugin off: no labels at all, and the views say why (K15).
    daemon.recorded.labelPlugin = false;
    QTRY_VERIFY_WITH_TIMEOUT(
        app.featuresMap().value(QStringLiteral("labelPluginOff")).toBool(), 3000);
    QVERIFY(!app.featuresMap().value(QStringLiteral("categories")).toBool());
    app.engine()->stop();
}

void TestAppDeluge::aTorrentKeepsARatioOfItsOwn()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const auto lastOptions = [&daemon]() {
        return daemon.lastOf(QStringLiteral("core.set_torrent_options"))->params.at(1).toObject();
    };
    // D, K18: a ratio of its own, then remove (keeping the files); a speed in Deluge's KiB/s.
    app.setLimits({kPack}, 300 * 1024, -1, 1.5, -2, QStringLiteral("Remove"), -2);
    QTRY_COMPARE_WITH_TIMEOUT(daemon.web.callsOf(QStringLiteral("core.set_torrent_options")).size(),
                              2,
                              3000);
    const QList<Call> sets = daemon.web.callsOf(QStringLiteral("core.set_torrent_options"));
    QJsonObject merged;
    for (const Call &call : sets) {
        const QJsonObject options = call.params.at(1).toObject();
        for (auto it = options.begin(); it != options.end(); ++it)
            merged.insert(it.key(), it.value());
    }
    QCOMPARE(merged.value(QStringLiteral("max_download_speed")).toDouble(), 300.0);
    QCOMPARE(merged.value(QStringLiteral("stop_at_ratio")), QJsonValue(true));
    QCOMPARE(merged.value(QStringLiteral("stop_ratio")).toDouble(), 1.5);
    QCOMPARE(merged.value(QStringLiteral("remove_at_ratio")), QJsonValue(true));

    // "global": the server's defaults are copied onto the torrent (core.conf's).
    app.setLimits({kPack}, -1, -1, -2, -2, QStringLiteral("Stop"), -2);
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("core.get_config_values")).has_value(),
                             3000);
    QTRY_COMPARE_WITH_TIMEOUT(daemon.web.callsOf(QStringLiteral("core.set_torrent_options")).size(),
                              3,
                              3000);
    QCOMPARE(lastOptions().value(QStringLiteral("stop_at_ratio")), QJsonValue(false));
    QCOMPARE(lastOptions().value(QStringLiteral("stop_ratio")).toDouble(), 2.0);
    app.engine()->stop();
}

void TestAppDeluge::anAddTakesDelugesOptions()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy finished(&app, &App::addFinished);
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=new").arg(kMagnet);
    // K27: a name and skip check at add, the label right after; no layout.
    app.addTorrent({{QStringLiteral("kind"), QStringLiteral("magnet")},
                    {QStringLiteral("value"), magnet},
                    {QStringLiteral("infoHashV1"), kMagnet},
                    {QStringLiteral("name"), QStringLiteral("new")},
                    {QStringLiteral("rename"), QStringLiteral("Night Sky")},
                    {QStringLiteral("skipChecking"), true},
                    {QStringLiteral("contentLayout"), QStringLiteral("Subfolder")},
                    {QStringLiteral("category"), QStringLiteral("fixtures")}});
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
    const std::optional<Call> add = daemon.lastOf(QStringLiteral("core.add_torrent_magnet"));
    QVERIFY(add.has_value());
    const QJsonObject options = add->params.at(1).toObject();
    QCOMPARE(options.value(QStringLiteral("name")).toString(), QStringLiteral("Night Sky"));
    QCOMPARE(options.value(QStringLiteral("seed_mode")), QJsonValue(true));
    for (auto it = options.begin(); it != options.end(); ++it)
        QVERIFY2(!it.key().contains(QLatin1String("layout")), qPrintable(it.key()));
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("label.set_torrent")).has_value(), 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("label.set_torrent"))->params,
             QJsonArray({kMagnet, QStringLiteral("fixtures")}));
    app.engine()->stop();
}

void TestAppDeluge::aMagnetsFilesComeFromThePrefetch()
{
    Daemon daemon;
    const QByteArray info =
        "d6:lengthi12e4:name5:a.bin12:piece lengthi16384e6:pieces20:xxxxxxxxxxxxxxxxxxxxe";
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex());
    daemon.prefetch = QJsonArray{hash, QString::fromLatin1(info.toBase64())};
    App app;
    QVERIFY(startOn(app, daemon));
    // K26: C5's lookup, through Deluge's prefetch: the files before the add.
    QSignalSpy fetched(&app, &App::metadataFetched);
    app.lookUpMetadata(
        app.parseAddable(QStringLiteral("magnet:?xt=urn:btih:%1&dn=a.bin").arg(hash)));
    QTRY_COMPARE_WITH_TIMEOUT(fetched.count(), 1, 5000);
    const QVariantMap map = fetched.first().first().toMap();
    QCOMPARE(map.value(QStringLiteral("name")).toString(), QStringLiteral("a.bin"));
    QCOMPARE(map.value(QStringLiteral("fileCount")).toInt(), 1);
    QVERIFY(!daemon.web.callsOf(QStringLiteral("core.prefetch_magnet_metadata")).isEmpty());
    app.engine()->stop();
}

QTEST_MAIN(TestAppDeluge)
#include "tst_app_deluge.moc"
