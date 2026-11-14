// A Deluge server driven in the app (#87, E6 step 11a; DECISIONS C14 E6, C25): App on a fake
// deluge-web 2.2 (FakeDeluge, answering from the daemon's recorded answers) signs in with its one
// password (K7), polls the library into its models, sends each everyday action as Deluge's
// method, offers none of what Deluge lacks (the feature record), reads deluge-web without a
// daemon as a server that is down (K10, C12), and connects the daemon stored with the server
// when deluge-web has none, never another one. Step 11b: the Label plugin's labels in the
// categories place and words (K13, K15), a ratio of each torrent's own (K18), the add's
// options (K27) and a magnet's files from the prefetch (K26), details without web seeds (H).
// Step 11c: moves with Deluge's own % (K21), the move it refuses without a word (board 47 §E),
// and its errors in its words, paused, cleared only by a recheck (K24, §F).

#include "FakeDeluge.hpp"

#include "core/ActionOutcome.hpp"
#include "core/CategoryModel.hpp"
#include "core/GlobalState.hpp"
#include "core/MoveJob.hpp"
#include "core/ProblemLog.hpp"
#include "core/SecretStore.hpp"
#include "core/SeedConsole.hpp"
#include "core/SeedLedger.hpp"
#include "core/StoryRecord.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilterModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/TorrentWebSeedsModel.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <functional>
#include <memory>

using omaseed::app::App;
using omaseed::core::SecretStore;
using omaseed::core::StoryEntry;
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
    /// What core.move_storage does to the torrents it names; nothing by default, as Deluge
    /// does when it can't make the folder (its log says why, its answer doesn't).
    std::function<void(const QJsonArray &ids, const QString &dest)> onMove;
    /// Torrents in Deluge's forced error (a failed move): a resume leaves them in Error, only
    /// a recheck clears it (restart_to_resume).
    QSet<QString> forced;
    /// core.conf once Settings changed it (empty: the recorded one).
    QJsonObject config;

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

    void set(const QString &hash, const char *key, const QJsonValue &value)
    {
        QJsonObject torrent = recorded.torrents.value(hash).toObject();
        torrent.insert(QString::fromLatin1(key), value);
        recorded.torrents.insert(hash, torrent);
    }

    /// Deluge's storage_moved_failed: Error with its words, paused, forced.
    void failMove(const QString &hash)
    {
        set(hash, "state", QStringLiteral("Error"));
        set(hash, "message", QStringLiteral("Failed to move download folder: Permission denied"));
        set(hash, "paused", true);
        forced.insert(hash);
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
            if (forced.contains(id.toString())) {
                recorded.torrents.insert(id.toString(), torrent); // still Error
                continue;
            }
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
        if (method == QLatin1String("core.move_storage")) {
            if (onMove)
                onMove(params.at(0).toArray(), params.at(1).toString());
            return {};
        }
        if (method == QLatin1String("core.force_recheck")) {
            for (const QJsonValue &id : params.at(0).toArray()) {
                if (!forced.remove(id.toString()))
                    continue;
                set(id.toString(), "state", QStringLiteral("Seeding"));
                set(id.toString(), "message", QStringLiteral("OK"));
                set(id.toString(), "paused", false);
            }
            return {};
        }
        if (method == QLatin1String("core.add_torrent_magnet"))
            return result("core_add_torrent_magnet.json");
        if (method == QLatin1String("core.prefetch_magnet_metadata"))
            return prefetch;
        if (method == QLatin1String("label.set_torrent") || method == QLatin1String("label.add"))
            return {};
        // core.conf, as Settings › Deluge changes it.
        if (method == QLatin1String("core.set_config")) {
            if (config.isEmpty())
                config = recorded.answer(QStringLiteral("core.get_config"), {}).toObject();
            const QJsonObject changes = params.at(0).toObject();
            for (auto it = changes.begin(); it != changes.end(); ++it)
                config.insert(it.key(), it.value());
            return {};
        }
        if (method == QLatin1String("core.get_config") && !config.isEmpty())
            return config;
        if (method == QLatin1String("core.set_torrent_options") ||
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
    void aMoveIsSentAfterARestartWhileLost();
    void labelsAreTheCategoriesInDelugesWords();
    void aLabelIsLowerCaseAndALine();
    void aTorrentKeepsARatioOfItsOwn();
    void anAddTakesDelugesOptions();
    void aMagnetsFilesComeFromThePrefetch();
    void aMoveShowsDelugesOwnPercent();
    void aMoveDelugeNeverMadeHasFailed();
    void theStoryTellsOfAMoveDelugeNeverMade();
    void aFolderMoveSaysWhatDelugeDidToEach();
    void aFailedMoveIsPausedUntilARecheck();
    void anErroredTorrentReadsDelugesWords();
    void theMoveSheetSaysAMoveEndsMoveWhenDone();
    void settingsAreDelugesCoreConf();

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
    // K34: no all-time totals; the console counts what omaseed saw, from its ledger's start.
    QTRY_VERIFY_WITH_TIMEOUT(app.ledger()->start() > 0, 3000);
    app.console()->refresh();
    QVERIFY2(app.console()
                 ->totals()
                 .value(QStringLiteral("sinceLine"))
                 .toString()
                 .startsWith(QLatin1String("Deluge keeps no all-time totals. These count from ")),
             qPrintable(app.console()->totals().value(QStringLiteral("sinceLine")).toString()));

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
                           "listFolders",
                           "sessionSettings",
                           // C40 X1: its torrents go to a file and come from one.
                           "torrentsBackup",
                           "torrentsExport",
                           "torrentsImport"})
        QVERIFY2(can.value(QString::fromLatin1(on)).toBool(), on);
    // …and what it lacks, hidden by the views that read these.
    for (const char *off : {"tags",
                            "altSpeeds",
                            "autoManagement",
                            "renameOnDisk",
                            "seedingTimeLimit",
                            "engineLog",
                            "qbtSettings",
                            "rss",
                            "search",
                            "createTorrent",
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
    // DECISIONS C38 R1, R7: the story's record says the old name; Deluge's is the name shown
    // only (K28), so not its folder on disk.
    QTRY_COMPARE_WITH_TIMEOUT(app.storyRecord().recordsFor(kPack).size(), 1, 3000);
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().was, QStringLiteral("Fixture Pack"));
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().now, QStringLiteral("Renamed Pack"));
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().disk, false);
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
    // The status line says "no daemon", as the band and the server's row do (11a's leftover).
    QVERIFY(problem.value(QStringLiteral("noDaemon")).toBool());
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

// The Deluge walk's first step: deluge-web had no daemon (the server lost), the server sheet
// connected one and saved the server, which restarts the engine (setDefaultServer) while it is
// still lost. The restart forgets the loss without a serverBack, so the move job kept waiting
// for the server ("waiting for deluge") and never sent a move. A sync means it answers.
void TestAppDeluge::aMoveIsSentAfterARestartWhileLost()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    daemon.recorded.connected = false;
    QTRY_VERIFY_WITH_TIMEOUT(app.isServerLost(), 3000);
    QVERIFY(!app.move()->job()->isConnected());
    daemon.recorded.connected = true;
    app.engine()->stop();
    app.engine()->start();
    QTRY_VERIFY_WITH_TIMEOUT(!app.isServerLost(), 5000);
    QCOMPARE(app.torrents()->count(), 6);
    QTRY_VERIFY_WITH_TIMEOUT(app.move()->job()->isConnected(), 3000);
    const QString to = QStringLiteral("/srv/other/films");
    QVERIFY(app.move()->startSelection({kPack}, to, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("core.move_storage")).has_value(), 5000);
    QCOMPARE(daemon.lastOf(QStringLiteral("core.move_storage"))->params.at(1).toString(), to);
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
        app.scopeSentence().contains(
            QStringLiteral("a label · moves to %1 when done")
                .arg(app.categories()->savePathOf(QStringLiteral("fixtures")))),
        3000);
    app.filtered()->setCategory(QStringLiteral("empty"));
    QTRY_VERIFY_WITH_TIMEOUT(
        app.scopeSentence().endsWith(QStringLiteral("a label · no folder of its own")), 3000);
    app.filtered()->clearCategory();

    // The plugin off: no labels at all, and the views say why (K15).
    daemon.recorded.labelPlugin = false;
    QTRY_VERIFY_WITH_TIMEOUT(app.featuresMap().value(QStringLiteral("labelPluginOff")).toBool(),
                             3000);
    QVERIFY(!app.featuresMap().value(QStringLiteral("categories")).toBool());
    app.engine()->stop();
}

// DECISIONS C43 Y2 (C25 K15): a label set from the app is a line with the name Deluge kept,
// lower case, and what it was before; the story says it in Deluge's words.
void TestAppDeluge::aLabelIsLowerCaseAndALine()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const QString was = app.torrents()->torrent(kPack).category;
    app.setCategory({kPack}, QStringLiteral("Walked"));
    QTRY_COMPARE_WITH_TIMEOUT(app.storyRecord().recordsFor(kPack).size(), 1, 3000);
    const StoryEntry line = app.storyRecord().recordsFor(kPack).constFirst();
    QCOMPARE(line.what, StoryEntry::What::Category);
    QCOMPARE(line.by, StoryEntry::By::Omaseed);
    QCOMPARE(line.was, was);
    QCOMPARE(line.now, QStringLiteral("walked"));
    QString title;
    for (const QVariant &event : app.torrentStory(kPack))
        if (event.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("edit"))
            title = event.toMap().value(QStringLiteral("title")).toString();
    QCOMPARE(title, QStringLiteral("Labelled walked"));
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
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.web.callsOf(QStringLiteral("core.set_torrent_options")).size(), 2, 3000);
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
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.web.callsOf(QStringLiteral("core.set_torrent_options")).size(), 3, 3000);
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

// Board 47 §E, K21: Deluge answers the move at once; the torrent reads Moving with Deluge's own
// %, which the band shows though omaseed can't see the server's disks, then it lands.
void TestAppDeluge::aMoveShowsDelugesOwnPercent()
{
    Daemon daemon;
    const QString to = QStringLiteral("/srv/other/films");
    daemon.onMove = [&](const QJsonArray &ids, const QString &) {
        for (const QJsonValue &id : ids) {
            daemon.set(id.toString(), "state", QStringLiteral("Moving"));
            daemon.set(id.toString(), "progress", 40.0);
        }
    };
    App app;
    QVERIFY(startOn(app, daemon));
    QVERIFY(app.move()->startSelection({kPack}, to, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(app.engine()->torrents()->torrent(kPack).status.phase ==
                                 omaseed::core::TorrentPhase::Moving,
                             3000);
    // 40 % of the pack's 64 440 bytes done, from Deluge's count (no path map, no disk seen).
    QTRY_COMPARE_WITH_TIMEOUT(app.move()->job()->liveBytes(kPack), qint64(25776), 5000);
    QTRY_VERIFY_WITH_TIMEOUT(!app.move()->band().isEmpty(), 6000);
    const QString line = app.move()->band().value(QStringLiteral("line")).toString();
    QVERIFY2(line.contains(QStringLiteral("40 %")), qPrintable(line));
    QVERIFY2(!line.contains(QStringLiteral("doesn't report")), qPrintable(line));

    // Moved: where it is now is the destination (and Deluge turned its move-when-done off).
    daemon.set(kPack, "state", QStringLiteral("Seeding"));
    daemon.set(kPack, "progress", 100.0);
    daemon.set(kPack, "save_path", to);
    daemon.set(kPack, "move_completed", false);
    QTRY_COMPARE_WITH_TIMEOUT(app.move()->stateName(), QStringLiteral("done"), 5000);
    QCOMPARE(app.move()->failedHashes(), QStringList());
    const std::optional<Call> sent = daemon.lastOf(QStringLiteral("core.move_storage"));
    QVERIFY(sent.has_value());
    QCOMPARE(sent->params.at(1).toString(), to);
    QCOMPARE(daemon.idsOf(QStringLiteral("core.move_storage")),
             QList<QStringList>({QStringList{kPack}}));
    app.engine()->stop();
}

// Board 47 §E: a move into a folder Deluge can't make is taken and dropped, said only in
// deluged's log. Never Moving and still in its folder after the bound, it reads as failed,
// in words that say Deluge didn't move it and doesn't say why; sent once, nothing paused.
void TestAppDeluge::aMoveDelugeNeverMadeHasFailed()
{
    Daemon daemon; // onMove unset: Deluge's silent refusal
    const QString to = QStringLiteral("/srv/locked/new/films");
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy outcomes(app.move(), &omaseed::app::MoveController::outcome);
    QElapsedTimer clock;
    clock.start();
    QVERIFY(app.move()->startSelection({kPack}, to, QString()));
    // Quick, it ends quiet (the strip, the job closed); past L6's 3 s, in its summary band.
    QTRY_VERIFY_WITH_TIMEOUT(
        !outcomes.isEmpty() || app.move()->stateName() == QLatin1String("done"), 8000);
    // Within the bound (two polls, 3 s), long before the quiet rule's resend (10 s).
    QVERIFY2(clock.elapsed() < 8000, qPrintable(QString::number(clock.elapsed())));
    if (outcomes.isEmpty()) {
        QCOMPARE(app.move()->failedHashes(), QStringList({kPack}));
        // The bound outlasts L6's 3 s, so the job showed: its summary band says it.
        const QString line = app.move()->band().value(QStringLiteral("line")).toString();
        QVERIFY2(
            line.contains(QStringLiteral("1 failed · Deluge didn't move it and doesn't say why")),
            qPrintable(line));
        QVERIFY2(!line.contains(QStringLiteral("paused")), qPrintable(line));
        // Nothing moved: red, "didn't move", and only a retry (nothing to recheck).
        const QVariantMap band = app.move()->band();
        QCOMPARE(band.value(QStringLiteral("kind")).toString(), QStringLiteral("red"));
        QCOMPARE(band.value(QStringLiteral("headline")).toString(),
                 QStringLiteral("Fixture Pack didn't move"));
        QStringList ids;
        for (const QVariant &item : band.value(QStringLiteral("actions")).toList())
            ids << item.toMap().value(QStringLiteral("id")).toString();
        QVERIFY2(ids.contains(QStringLiteral("retry")) && !ids.contains(QStringLiteral("recheck")),
                 qPrintable(ids.join(QLatin1Char(','))));
    } else {
        // Done under 3 s on a fast poll: the strip says it.
        const QVariantMap outcome = outcomes.constLast().at(0).toMap();
        QCOMPARE(outcome.value(QStringLiteral("cause")).toString(),
                 QStringLiteral("Deluge didn't move it and doesn't say why"));
        QVERIFY(!outcome.value(QStringLiteral("startHere")).toBool());
        QVERIFY(!outcome.value(QStringLiteral("recheckFirst")).toBool());
        QCOMPARE(outcome.value(QStringLiteral("failedHashes")).toStringList(),
                 QStringList({kPack}));
        QVERIFY(outcome.value(QStringLiteral("details"))
                    .toString()
                    .contains(QStringLiteral("never started moving")));
    }
    QCOMPARE(daemon.idsOf(QStringLiteral("core.move_storage")).size(), 1);
    QCOMPARE(daemon.state(kPack), QStringLiteral("Seeding"));
    app.engine()->stop();
}

// #244: the move Deluge dropped without a word is in the details' story, as a failed move
// is on qBittorrent (board 46's words, the strip's reason under them), and stays there once
// its strip or band has gone.
void TestAppDeluge::theStoryTellsOfAMoveDelugeNeverMade()
{
    Daemon daemon; // onMove unset: Deluge's silent refusal
    App app;
    QVERIFY(startOn(app, daemon));
    const auto notMoved = [&app]() {
        for (const QVariant &item : app.torrentStory(kPack)) {
            const QVariantMap event = item.toMap();
            if (event.value(QStringLiteral("title"))
                    .toString()
                    .contains(QStringLiteral("didn't happen")))
                return event;
        }
        return QVariantMap();
    };
    QVERIFY(notMoved().isEmpty());
    QVERIFY(app.move()->startSelection({kPack}, QStringLiteral("/srv/locked/new/films"), {}));
    QTRY_VERIFY_WITH_TIMEOUT(!notMoved().isEmpty(), 8000);
    const QVariantMap event = notMoved();
    QCOMPARE(event.value(QStringLiteral("title")).toString(),
             QStringLiteral("The move to new/films didn't happen"));
    QCOMPARE(event.value(QStringLiteral("detail")).toString(),
             QStringLiteral("Deluge didn't move it and doesn't say why"));
    QCOMPARE(event.value(QStringLiteral("kind")).toString(), QStringLiteral("warn"));
    // Newest first: it leads the story.
    QCOMPARE(app.torrentStory(kPack).constFirst().toMap(), event);
    // The job's summary closed (quiet or by hand): the story still has it.
    QTRY_VERIFY_WITH_TIMEOUT(app.move()->stateName() == QLatin1String("done") ||
                                 app.move()->stateName() == QLatin1String("idle"),
                             8000);
    app.move()->closeSummary();
    QTRY_COMPARE_WITH_TIMEOUT(app.move()->stateName(), QStringLiteral("idle"), 3000);
    QCOMPARE(notMoved(), event);
    // Another torrent's story doesn't have it.
    for (const QVariant &item : app.torrentStory(kSingle))
        QVERIFY(!item.toMap()
                     .value(QStringLiteral("title"))
                     .toString()
                     .contains(QStringLiteral("didn't happen")));
    app.engine()->stop();
}

// Board 47 §E/§F, the summary band on Deluge: of a folder's three, one moved, one Deluge
// paused in error (a recheck clears it, so recheck leads) and one it never moved.
void TestAppDeluge::aFolderMoveSaysWhatDelugeDidToEach()
{
    Daemon daemon;
    const QString root = QStringLiteral("/srv/deluge/fixtures-recording");
    const QString to = QStringLiteral("/srv/locked/dest");
    // The recorded torrent in error isn't part of this story: it seeds.
    daemon.set(kError, "state", QStringLiteral("Seeding"));
    daemon.set(kError, "message", QStringLiteral("OK"));
    // The first sent moves (the job's probe), the second fails in Error, the rest Deluge
    // takes and drops, as when it can't make their folder.
    int calls = 0;
    daemon.onMove = [&](const QJsonArray &ids, const QString &dest) {
        for (const QJsonValue &id : ids) {
            ++calls;
            if (calls == 1)
                daemon.set(id.toString(), "save_path", dest);
            else if (calls == 2)
                daemon.failMove(id.toString());
        }
    };
    App app;
    QVERIFY(startOn(app, daemon));
    // The sheet's plan asks Deluge the names at the destination (core.glob) and its room;
    // the move starts once they are in.
    (void)app.move()->plan(root, to, false);
    bool started = false; // QTRY's check runs its expression once more: start once
    QTRY_VERIFY_WITH_TIMEOUT(started || (started = app.move()->start(root, to, false)), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(app.move()->stateName(), QStringLiteral("done"), 15000);
    QVERIFY2(calls >= 3, qPrintable(QString::number(calls)));
    QCOMPARE(app.move()->failedHashes().size(), calls - 1);
    QTRY_VERIFY_WITH_TIMEOUT(!app.move()->band().isEmpty(), 3000);
    const QVariantMap band = app.move()->band();
    const QString line = band.value(QStringLiteral("line")).toString();
    QVERIFY2(
        line.contains(QStringLiteral("1 failed · Deluge paused the torrent; a recheck clears it")),
        qPrintable(line));
    QVERIFY2(line.contains(
                 calls == 3 ? QStringLiteral("1 failed · Deluge didn't move it and doesn't say why")
                            : QStringLiteral("failed · Deluge didn't move them and doesn't say "
                                             "why")),
             qPrintable(line));
    QVERIFY2(!line.contains(QStringLiteral("stopped")), qPrintable(line));
    QStringList ids;
    for (const QVariant &item : band.value(QStringLiteral("actions")).toList())
        ids << item.toMap().value(QStringLiteral("id")).toString();
    QCOMPARE(ids.mid(0, 2), QStringList({QStringLiteral("recheck"), QStringLiteral("retry")}));
    QVERIFY(!ids.contains(QStringLiteral("startHere")));
    // Each was sent once: the silent ones weren't sent again.
    QCOMPARE(daemon.idsOf(QStringLiteral("core.move_storage")).size(), calls);
    app.engine()->stop();
}

// Board 47 §F `sf07` on Deluge (K24): a move into a read-only folder puts the torrent in Error
// with Deluge's words, paused. The strip says so and offers recheck; a resume leaves the
// error; only the recheck clears it.
void TestAppDeluge::aFailedMoveIsPausedUntilARecheck()
{
    Daemon daemon;
    const QString to = QStringLiteral("/srv/locked/dest");
    daemon.onMove = [&](const QJsonArray &ids, const QString &) {
        for (const QJsonValue &id : ids)
            daemon.failMove(id.toString());
    };
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy outcomes(app.move(), &omaseed::app::MoveController::outcome);
    QVERIFY(app.move()->startSelection({kPack}, to, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(outcomes.count() == 1, 5000);
    const QVariantMap outcome = outcomes.at(0).at(0).toMap();
    QCOMPARE(outcome.value(QStringLiteral("failedHashes")).toStringList(), QStringList({kPack}));
    QCOMPARE(outcome.value(QStringLiteral("cause")).toString(),
             QStringLiteral("Deluge can't write to %1 · it paused the torrent; a recheck clears it")
                 .arg(to));
    QVERIFY(outcome.value(QStringLiteral("recheckFirst")).toBool());
    QVERIFY(!outcome.value(QStringLiteral("startHere")).toBool());
    QCOMPARE(outcome.value(QStringLiteral("details")).toString(),
             QStringLiteral("Failed to move download folder: Permission denied"));
    QCOMPARE(daemon.idsOf(QStringLiteral("core.move_storage")).size(), 1);

    // The row: paused, Deluge's sentence, recheck its fix.
    QTRY_COMPARE_WITH_TIMEOUT(app.torrentRow(kPack).value(QStringLiteral("rowCaption")).toString(),
                              QStringLiteral("paused"),
                              3000);
    QCOMPARE(app.engine()
                 ->torrents()
                 ->problem(kPack)
                 .value(QStringLiteral("actions"))
                 .toStringList()
                 .constFirst(),
             QStringLiteral("recheck"));

    // A resume leaves Deluge's forced error; the recheck clears it.
    app.retryTorrents({kPack});
    QTRY_VERIFY_WITH_TIMEOUT(!daemon.idsOf(QStringLiteral("core.resume_torrents")).isEmpty(), 3000);
    QCOMPARE(daemon.state(kPack), QStringLiteral("Error"));
    QTRY_COMPARE_WITH_TIMEOUT(app.engine()->torrents()->torrent(kPack).status.phase,
                              omaseed::core::TorrentPhase::Errored,
                              3000);
    // #244: Deluge says paused false and still Error. The hero (the row's words) no longer
    // says "paused": still in error, and the recheck still leads.
    QTRY_VERIFY_WITH_TIMEOUT(
        app.engine()
            ->torrents()
            ->data(app.engine()->torrents()->index(app.engine()->torrents()->indexOfHash(kPack)),
                   omaseed::core::TorrentModel::ErrorRunningRole)
            .toBool(),
        3000);
    const QVariantMap hero = app.torrentRow(kPack);
    QCOMPARE(hero.value(QStringLiteral("rowFact")).toString(), QStringLiteral("didn't move"));
    QCOMPARE(hero.value(QStringLiteral("rowCaption")).toString(), QStringLiteral("needs you"));
    QCOMPARE(hero.value(QStringLiteral("rowSentence")).toString(),
             QStringLiteral("Deluge couldn't move its files · still in error until a recheck"));
    QVERIFY(!hero.value(QStringLiteral("errorStopped")).toBool());
    QCOMPARE(app.engine()
                 ->torrents()
                 ->problem(kPack)
                 .value(QStringLiteral("actions"))
                 .toStringList()
                 .constFirst(),
             QStringLiteral("recheck"));
    app.recheck({kPack});
    QTRY_VERIFY_WITH_TIMEOUT(!daemon.idsOf(QStringLiteral("core.force_recheck")).isEmpty(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(app.engine()->torrents()->torrent(kPack).status.phase !=
                                 omaseed::core::TorrentPhase::Errored,
                             3000);
    app.engine()->stop();
}

// Board 47 §F on Deluge (K24): the recorded torrent in Error reads Deluge's own words in the
// block, the row and the story, paused, with recheck first and no log.
void TestAppDeluge::anErroredTorrentReadsDelugesWords()
{
    Daemon daemon;
    daemon.forced.insert(kError);
    App app;
    QVERIFY(startOn(app, daemon));
    QTRY_VERIFY_WITH_TIMEOUT(!app.engine()
                                  ->torrents()
                                  ->problem(kError)
                                  .value(QStringLiteral("headline"))
                                  .toString()
                                  .isEmpty(),
                             3000);
    const QVariantMap problem = app.engine()->torrents()->problem(kError);
    QCOMPARE(problem.value(QStringLiteral("engine")).toString(), QStringLiteral("Deluge"));
    QVERIFY(problem.value(QStringLiteral("failedMove")).toBool());
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(), QStringLiteral("Didn't move"));
    QCOMPARE(problem.value(QStringLiteral("detail")).toString(),
             QStringLiteral("Deluge: “Failed to move download folder: Permission denied”"));
    QCOMPARE(problem.value(QStringLiteral("actions")).toStringList(),
             QStringList({QStringLiteral("recheck"), QStringLiteral("location")}));
    QCOMPARE(problem.value(QStringLiteral("note")).toString(),
             QStringLiteral("Deluge paused it · only a recheck clears this error"));

    // The row, as the rows display and the details' hero read it.
    QTRY_COMPARE_WITH_TIMEOUT(app.torrentRow(kError).value(QStringLiteral("rowCaption")).toString(),
                              QStringLiteral("paused"),
                              3000);
    const QVariantMap row = app.torrentRow(kError);
    QCOMPARE(row.value(QStringLiteral("rowFact")).toString(), QStringLiteral("didn't move"));
    QCOMPARE(row.value(QStringLiteral("rowSentence")).toString(),
             QStringLiteral("Deluge couldn't move its files · it paused the torrent"));
    bool told = false;
    for (const QVariant &event : app.torrentStory(kError)) {
        const QVariantMap e = event.toMap();
        if (e.value(QStringLiteral("kind")).toString() == QLatin1String("error")) {
            QCOMPARE(e.value(QStringLiteral("detail")).toString(),
                     QStringLiteral("Deluge paused it"));
            told = true;
        }
    }
    QVERIFY(told);
    app.engine()->stop();
}

// Board 47 §E on Deluge (assumed): a move turns a torrent's move-when-done folder off, so the
// Move sheet says so for an unfinished one that has it.
void TestAppDeluge::theMoveSheetSaysAMoveEndsMoveWhenDone()
{
    Daemon daemon;
    QString stream;
    for (auto it = daemon.recorded.torrents.constBegin(); it != daemon.recorded.torrents.constEnd();
         ++it) {
        if (it.value().toObject().value(QStringLiteral("name")).toString() ==
            QLatin1String("fixture-stream.bin"))
            stream = it.key();
    }
    QVERIFY(!stream.isEmpty());
    daemon.set(stream, "move_completed", true);
    daemon.set(stream, "move_completed_path", QStringLiteral("/srv/deluge/done"));
    App app;
    QVERIFY(startOn(app, daemon));
    QTRY_COMPARE_WITH_TIMEOUT(app.engine()->torrents()->torrent(stream).savePath,
                              QStringLiteral("/srv/deluge/done"),
                              3000);
    const auto texts = [&](const QStringList &hashes) {
        QStringList out;
        const QVariantMap plan =
            app.move()->planSelection(hashes, QStringLiteral("/srv/other"), QString());
        for (const QVariant &line : plan.value(QStringLiteral("checks")).toList())
            out << line.toMap().value(QStringLiteral("text")).toString();
        return out.join(QLatin1Char('\n'));
    };
    const QString all = texts({stream});
    QVERIFY2(
        all.contains(QStringLiteral("Moves when done. 1 torrent goes to another folder when "
                                    "it finishes. Deluge turns that off for any move it makes")),
        qPrintable(all));
    // A finished one, or one without that folder, isn't named.
    QVERIFY(!texts({kSingle}).contains(QStringLiteral("Moves when done")));
    app.engine()->stop();
}

// Settings › Deluge (E6, #87; C25 K31, K15): the server's own settings, read from and written
// to core.conf through the same preferences the speed surfaces use; its plugins read only.
void TestAppDeluge::settingsAreDelugesCoreConf()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QVERIFY(app.featuresMap().value(QStringLiteral("sessionSettings")).toBool());
    QVERIFY(!app.featuresMap().value(QStringLiteral("qbtSettings")).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(app.serverPrefs()->isLoaded(), 3000);
    QVERIFY(app.serverPrefs()->isSession());
    QVERIFY(daemon.lastOf(QStringLiteral("core.get_config")).has_value());
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("listen_port")).toInt(), 26993);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("max_active_seeding")).toInt(), 5);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("encryption")).toString(),
             QStringLiteral("enabled"));
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("enabled_plugins")).toStringList(),
             QStringList({QStringLiteral("Label")}));

    app.serverPrefs()->apply({{QStringLiteral("max_active_seeding"), 7},
                              {QStringLiteral("utpex"), true},
                              {QStringLiteral("max_connections_per_torrent"), 40},
                              {QStringLiteral("encryption"), QStringLiteral("forced")}});
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("core.set_config")).has_value(), 3000);
    const QJsonObject set =
        daemon.lastOf(QStringLiteral("core.set_config"))->params.at(0).toObject();
    QCOMPARE(set.value(QStringLiteral("max_active_seeding")), QJsonValue(7));
    QCOMPARE(set.value(QStringLiteral("utpex")), QJsonValue(true));
    QCOMPARE(set.value(QStringLiteral("max_connections_per_torrent")), QJsonValue(40));
    QCOMPARE(set.value(QStringLiteral("enc_in_policy")), QJsonValue(0));
    QCOMPARE(set.value(QStringLiteral("enc_out_policy")), QJsonValue(0));
    // Read back from core.conf.
    QTRY_COMPARE_WITH_TIMEOUT(app.serverPrefs()->value(QStringLiteral("encryption")).toString(),
                              QStringLiteral("forced"),
                              3000);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("max_active_seeding")).toInt(), 7);

    // Out of range: refused before sending, the value back as it was.
    const qsizetype sets = daemon.web.callsOf(QStringLiteral("core.set_config")).size();
    QSignalSpy failed(app.serverPrefs(), &omaseed::core::ServerPreferences::applyFailed);
    app.serverPrefs()->apply({{QStringLiteral("max_connections_global"), 0}});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(daemon.web.callsOf(QStringLiteral("core.set_config")).size(), sets);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("max_connections_global")).toInt(), 200);

    // The Label plugin off: the plugins list says so at the next read.
    daemon.recorded.labelPlugin = false;
    daemon.config.insert(QStringLiteral("enabled_plugins"), QJsonArray());
    app.serverPrefs()->reload();
    QTRY_COMPARE_WITH_TIMEOUT(
        app.serverPrefs()->value(QStringLiteral("enabled_plugins")).toStringList(),
        QStringList(),
        3000);
    app.engine()->stop();
}

QTEST_MAIN(TestAppDeluge)
#include "tst_app_deluge.moc"
