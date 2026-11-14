// A Transmission server driven in the app (#85, E4 step 3; DECISIONS C14 E4, C25): App on a
// fake Transmission 4.1.3 (FakeTransmission, the daemon's recorded answers) polls the library
// into its models, sends each everyday action as Transmission's method, holds a pause whose
// stop lands a moment after its answer (C36 M10), refuses a daemon older than 4.1 in K8's words,
// and offers none of what Transmission lacks (the feature record, the search and the creator).

#include "FakeTransmission.hpp"

#include "core/AltPresets.hpp"
#include "core/AltSpeed.hpp"
#include "core/GlobalState.hpp"
#include "core/MoveJob.hpp"
#include "core/OmaseedBackup.hpp"
#include "core/PauseHold.hpp"
#include "core/ProblemLog.hpp"
#include "core/StoryRecord.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentDetails.hpp"
#include "core/TorrentFilesModel.hpp"
#include "core/TorrentModel.hpp"
#include "core/engines/EngineClient.hpp"
#include "core/engines/qbt/ServerPreferences.hpp"
#include "ui/app/App.hpp"
#include "ui/app/MoveController.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <algorithm>
#include <memory>

using omaseed::app::App;
using namespace faketransmission;

namespace {

const QString kPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kSingle = QStringLiteral("2b86112ae20c6ca831cbfb3e4333c8acc6d21088");
const QString kError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");

/// The recorded library as a daemon that changes: stops and starts move a torrent's status
/// (after `stopLandsAfterMs` for a stop, as 4.1.3 does), a remove takes it out, a rename of its
/// top renames it, and a torrent_get for some hashes answers only those.
class Daemon
{
public:
    FakeServer server;
    FakeTransmission wire;
    PollAnswers library;
    int stopLandsAfterMs = 0;

    Daemon()
    {
        server.setHandler(wire.handler());
        wire.answer = [this](const QString &method, const QJsonObject &params) {
            return answer(method, params);
        };
    }

    QUrl url() const
    {
        QUrl url = server.url();
        url.setPath(QStringLiteral("/transmission/web/"));
        return url;
    }

    /// The `ids` each call of `method` carried, in order.
    QList<QStringList> idsOf(const QString &method) const
    {
        QList<QStringList> out;
        for (const Call &call : wire.calls)
            if (call.method == method)
                out.append(hashes(call.params));
        return out;
    }

    const Call *lastOf(const QString &method) const
    {
        for (qsizetype i = wire.calls.size() - 1; i >= 0; --i)
            if (wire.calls.at(i).method == method)
                return &wire.calls.at(i);
        return nullptr;
    }

    int status(const QString &hash) { return library.byHash(hash).value("status").toInt(); }

    /// Gone from the daemon: out of the list, its id in recently_active's `removed`.
    void remove(const QString &hash)
    {
        for (qsizetype i = 0; i < library.torrents.size(); ++i) {
            const QJsonObject &t = library.torrents.at(i);
            if (t.value(QStringLiteral("hash_string")).toString() != hash)
                continue;
            library.removed.append(t.value(QStringLiteral("id")));
            library.torrents.removeAt(i);
            return;
        }
    }

private:
    static QStringList hashes(const QJsonObject &params)
    {
        QStringList out;
        for (const QJsonValue &id : params.value(QStringLiteral("ids")).toArray())
            out.append(id.toString());
        return out;
    }

    void setStatus(const QStringList &ids, int status)
    {
        for (const QString &hash : ids)
            for (QJsonObject &t : library.torrents)
                if (t.value(QStringLiteral("hash_string")).toString() == hash)
                    t.insert(QStringLiteral("status"), status);
    }

public:
    /// The daemon's own answer, for a test that answers one method itself.
    QJsonObject answer(const QString &method, const QJsonObject &params)
    {
        const QStringList ids = hashes(params);
        if (method == QLatin1String("torrent_get") && !ids.isEmpty()) {
            // Only those asked for, as Transmission does with hashes for ids.
            PollAnswers some = library;
            some.torrents.removeIf([&ids](const QJsonObject &t) {
                return !ids.contains(t.value(QStringLiteral("hash_string")).toString());
            });
            return some.answer(method, params);
        }
        if (method == QLatin1String("torrent_stop")) {
            if (stopLandsAfterMs <= 0)
                setStatus(ids, 0);
            else
                QTimer::singleShot(stopLandsAfterMs, [this, ids]() { setStatus(ids, 0); });
            return {};
        }
        if (method == QLatin1String("torrent_start") ||
            method == QLatin1String("torrent_start_now")) {
            setStatus(ids, 4);
            return {};
        }
        if (method == QLatin1String("torrent_remove")) {
            for (const QString &hash : ids)
                remove(hash);
            return {};
        }
        if (method == QLatin1String("torrent_rename_path")) {
            // The torrent's own name (its top folder) renames the torrent, as 4.1.3 does.
            for (const QString &hash : ids) {
                QJsonObject &t = library.byHash(hash);
                if (t.value(QStringLiteral("name")) == params.value(QStringLiteral("path")))
                    t.insert(QStringLiteral("name"), params.value(QStringLiteral("name")));
            }
            return fixture("torrent_rename_path.json").value(QStringLiteral("result")).toObject();
        }
        if (method == QLatin1String("torrent_add"))
            return fixture("torrent_add.json").value(QStringLiteral("result")).toObject();
        if (method == QLatin1String("session_set")) {
            for (auto it = params.constBegin(); it != params.constEnd(); ++it)
                library.sessionGet.insert(it.key(), it.value());
            return {};
        }
        return library.answer(method, params);
    }
};

} // namespace

class TestAppTransmission : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    void aTransmissionServerIsPolledIntoTheLibrary();
    void whatTransmissionLacksIsNotOffered();
    void theEverydayActionsAreTransmissionsMethods();
    void aRenameSeenOnTransmissionSaysItsFolderToo();
    void aRenameReadsInTheRowAtItsOk();
    void anAddCarriesItsFolderAndLabels();
    void labelsAreLabelledLines();
    void detailsAreAskedOfTransmission();
    void resumeRightAfterPauseWaitsForTheStopToLand();
    void anOldTransmissionIsRefusedInK8sWords();
    void filesStepThroughTransmissionsPriorities();
    void turtleAndItsClockAreTransmissionsSession();
    void settingsAreTransmissionsSessionAndItsPortTest();
    void aLongMoveIsBusyNotLost();
    void aFailedMoveIsLearntFromTheTorrent();
    void aFoldersSummaryOffersToStartWhatTransmissionStopped();
    void theMoveSheetRunsTransmissionsChecks();
    void anErroredTorrentReadsTransmissionsWords();
    void anErrorInPlaceReadsItsCauseAndItsNotice();
    void aTrackerErrorIsTrackerTrouble();
    void omaseedsBackupHoldsNoTransmissionSettings();

private:
    /// Starts `app` on `daemon` through the environment, polling every 100 ms, and waits for
    /// the four recorded torrents.
    bool startOn(App &app, Daemon &daemon);

    std::unique_ptr<QTemporaryDir> m_home;
};

void TestAppTransmission::init()
{
    m_home = std::make_unique<QTemporaryDir>();
    qputenv("XDG_CONFIG_HOME", m_home->path().toUtf8());
}

void TestAppTransmission::cleanup()
{
    qunsetenv("OMASEED_SERVER_URL");
    qunsetenv("OMASEED_SERVER_ENGINE");
    m_home.reset();
}

// C25 K17: the Files tab and the add sheet get Transmission's set, low included.
void TestAppTransmission::filesStepThroughTransmissionsPriorities()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QCOMPARE(app.details()->files()->prioritySetList(), (QVariantList{0, 2, 1, 6}));
    QCOMPARE(app.details()->files()->raised(6), 6);
    QCOMPARE(app.featuresMap().value(QStringLiteral("filePriorities")).toList(),
             (QVariantList{0, 2, 1, 6}));
    app.engine()->stop();
}

// C25 K20, C32 D8 a: turtle, its clock and the presets read and write Transmission's session
// (session_get / session_set), as they do qBittorrent's preferences.
void TestAppTransmission::turtleAndItsClockAreTransmissionsSession()
{
    Daemon daemon;
    // Turtle by the clock, every day 09:00–17:00 (the recording's window, switched on).
    daemon.library.sessionGet.insert(QStringLiteral("alt_speed_time_enabled"), true);
    App app;
    QVERIFY(startOn(app, daemon));
    QTRY_VERIFY_WITH_TIMEOUT(app.serverPrefs()->isLoaded(), 3000);
    QVERIFY(app.serverPrefs()->isSession());
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("alt_dl_limit")).toLongLong(), 50000);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("scheduler_day_mask")).toInt(), 127);
    QVERIFY(app.altSpeed()->isScheduled());
    QVERIFY(app.altSpeed()->until().isValid());
    // 50 kB/s against no normal limit: the set is the slow one.
    QCOMPARE(app.altSpeed()->kind(), QStringLiteral("turtle"));
    QVERIFY(app.altPresets()->isLoaded());

    // Turtle on: the set stays, the mode goes on.
    daemon.wire.calls.clear();
    QVERIFY(app.pickAltSpeed(QStringLiteral("turtle"), false));
    QTRY_VERIFY_WITH_TIMEOUT(
        daemon.library.sessionGet.value(QStringLiteral("alt_speed_enabled")).toBool(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(app.globalState()->useAltSpeedLimits(), 3000);
    QCOMPARE(daemon.library.sessionGet.value(QStringLiteral("alt_speed_down")).toInt(), 50);

    // Burst: its limits written over the set (2 MiB/s up in kB/s), turtle's kept aside...
    QVERIFY(app.pickAltSpeed(QStringLiteral("burst"), false));
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.library.sessionGet.value(QStringLiteral("alt_speed_up")).toInt(), 2098, 3000);
    QCOMPARE(daemon.library.sessionGet.value(QStringLiteral("alt_speed_down")).toInt(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(app.altSpeed()->kind(), QStringLiteral("burst"), 3000);
    QVERIFY(app.altPresets()->burstOn());
    // ...and written back when it ends.
    QVERIFY(app.pickAltSpeed(QStringLiteral("off"), false));
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.library.sessionGet.value(QStringLiteral("alt_speed_up")).toInt(), 50, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!app.altPresets()->burstOn(), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(
        !daemon.library.sessionGet.value(QStringLiteral("alt_speed_enabled")).toBool(), 3000);
    app.engine()->stop();
}

// Board 47 §J and §6 E5 (C25 K31, C14 E6): Settings › Transmission reads and writes the
// session as the rest of Settings does qBittorrent's preferences, and "test port" asks the
// daemon.
void TestAppTransmission::settingsAreTransmissionsSessionAndItsPortTest()
{
    Daemon daemon;
    daemon.wire.answer = [&daemon](const QString &method, const QJsonObject &params) {
        if (method == QLatin1String("port_test"))
            return QJsonObject{{QStringLiteral("port_is_open"), true}};
        return daemon.answer(method, params);
    };
    App app;
    QVERIFY(startOn(app, daemon));
    QVERIFY(app.featuresMap().value(QStringLiteral("sessionSettings")).toBool());
    QVERIFY(!app.featuresMap().value(QStringLiteral("qbtSettings")).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(app.serverPrefs()->isLoaded(), 3000);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("listen_port")).toInt(), 26994);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("dht")).toBool(), false);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("max_inactive_seeding_time")).toInt(), 30);

    app.serverPrefs()->apply({{QStringLiteral("listen_port"), 51413},
                              {QStringLiteral("dht"), true},
                              {QStringLiteral("temp_path_enabled"), true}});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.library.sessionGet.value(QStringLiteral("peer_port")).toInt(), 51413, 3000);
    QVERIFY(daemon.library.sessionGet.value(QStringLiteral("dht_enabled")).toBool());
    QVERIFY(daemon.library.sessionGet.value(QStringLiteral("incomplete_dir_enabled")).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(
        app.serverPrefs()->value(QStringLiteral("listen_port")).toInt(), 51413, 3000);

    // Settings › Transmission (E5, #86): the rest of the session by its own names.
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("download_queue_size")).toInt(), 5);
    QCOMPARE(app.serverPrefs()->value(QStringLiteral("encryption")).toString(),
             QStringLiteral("preferred"));
    app.serverPrefs()->apply({{QStringLiteral("seed_queue_enabled"), true},
                              {QStringLiteral("seed_queue_size"), 4},
                              {QStringLiteral("encryption"), QStringLiteral("required")},
                              {QStringLiteral("pex_enabled"), true}});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.library.sessionGet.value(QStringLiteral("seed_queue_size")).toInt(), 4, 3000);
    QVERIFY(daemon.library.sessionGet.value(QStringLiteral("seed_queue_enabled")).toBool());
    QVERIFY(daemon.library.sessionGet.value(QStringLiteral("pex_enabled")).toBool());
    QCOMPARE(daemon.library.sessionGet.value(QStringLiteral("encryption")).toString(),
             QStringLiteral("required"));
    // A queue of none is refused before it is sent, and the row goes back.
    app.serverPrefs()->apply({{QStringLiteral("download_queue_size"), 0}});
    QTRY_COMPARE_WITH_TIMEOUT(
        app.serverPrefs()->value(QStringLiteral("download_queue_size")).toInt(), 5, 3000);
    QCOMPARE(daemon.library.sessionGet.value(QStringLiteral("download_queue_size")).toInt(), 5);

    QSignalSpy tested(&app, &App::portTestChanged);
    app.testPort(51413);
    QCOMPARE(app.portTest().value(QStringLiteral("state")).toString(), QStringLiteral("testing"));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.portTest().value(QStringLiteral("state")).toString(), QStringLiteral("open"), 3000);
    QCOMPARE(app.portTest().value(QStringLiteral("text")).toString(),
             QStringLiteral("port 51413 is open: peers can reach it"));
    QCOMPARE(tested.count(), 2);
    app.engine()->stop();
}

// Board 47 §E, C25 K22: Transmission answers nothing while a move's copy runs. The poll is held
// and says "busy moving" (status, title, band), no server is lost, one torrent goes at a time,
// and the list comes back when the move answers.
void TestAppTransmission::aLongMoveIsBusyNotLost()
{
    Daemon daemon;
    const QString to = QStringLiteral("/srv/transmission/moved");
    daemon.wire.answer = [&daemon](const QString &method, const QJsonObject &params) {
        if (method == QLatin1String("torrent_set_location")) {
            // The copy starts: nothing answers until it ends; then the torrent is there.
            daemon.server.hold = true;
            daemon.library.byHash(kPack).insert(QStringLiteral("download_dir"),
                                                params.value(QStringLiteral("location")));
            return QJsonObject();
        }
        return daemon.library.answer(method, params);
    };
    App app;
    QVERIFY(startOn(app, daemon));
    // Polls time out quickly, so a held one would read as lost were it not the move.
    app.engine()->client()->setTimeout(std::chrono::milliseconds(400));
    QSignalSpy lost(app.engine(), &omaseed::core::SyncEngine::serverLost);

    QVERIFY(app.move()->startSelection({kPack}, to, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("torrent_set_location")) != nullptr,
                             3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("torrent_set_location"))
                 ->params.value(QStringLiteral("move"))
                 .toBool(),
             true);
    QTRY_COMPARE_WITH_TIMEOUT(app.globalState()->busyMoving(), QStringList({kPack}), 3000);
    // Past the poll's timeout: still busy, never lost, and no poll waits behind the copy.
    QTest::qWait(1200);
    QCOMPARE(lost.count(), 0);
    QVERIFY(app.globalState()->connected());
    QCOMPARE(app.globalState()->busyMoving(), QStringList({kPack}));
    QVERIFY(daemon.server.heldCount() <= 2); // the move, and a poll already out at most
    // The band says it in board 47's words once the quiet move shows (C16 L6, 3 s).
    QTRY_VERIFY_WITH_TIMEOUT(app.move()->shown(), 5000);
    const QVariantMap band = app.move()->band();
    QCOMPARE(band.value(QStringLiteral("kind")).toString(), QStringLiteral("yellow"));
    QVERIFY2(band.value(QStringLiteral("headline"))
                 .toString()
                 .contains(QStringLiteral("is busy moving Fixture Pack")),
             qPrintable(band.value(QStringLiteral("headline")).toString()));
    QVERIFY(
        band.value(QStringLiteral("line"))
            .toString()
            .contains(QStringLiteral("Transmission answers again once this torrent has moved")));

    // The copy ends: the move answers, the poll goes at once and finds it there.
    daemon.server.release();
    QTRY_VERIFY_WITH_TIMEOUT(app.globalState()->busyMoving().isEmpty(), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->torrent(kPack).savePath, to, 3000);
    QTRY_COMPARE_WITH_TIMEOUT(app.move()->job()->moved(), 1, 3000);
    QCOMPARE(lost.count(), 0);
    app.engine()->stop();
}

// Board 47 §4: a failed move is learnt from the torrent (error 3 "Couldn't move…", stopped,
// its folder unchanged) and reaches the same failure as qBittorrent's log-learnt one, in
// Transmission's name.
void TestAppTransmission::aFailedMoveIsLearntFromTheTorrent()
{
    Daemon daemon;
    const QString to = QStringLiteral("/srv/locked/dest");
    const QString from =
        daemon.library.byHash(kPack).value(QStringLiteral("download_dir")).toString();
    daemon.wire.answer = [&](const QString &method, const QJsonObject &params) {
        if (method == QLatin1String("torrent_set_location")) {
            QJsonObject &pack = daemon.library.byHash(kPack);
            pack.insert(QStringLiteral("error"), 3);
            pack.insert(QStringLiteral("error_string"),
                        QStringLiteral("Couldn't move '%1/Fixture Pack' to '%2/Fixture Pack': "
                                       "Permission denied (13)")
                            .arg(from, to));
            pack.insert(QStringLiteral("status"), 0);
            return QJsonObject();
        }
        return daemon.library.answer(method, params);
    };
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy notes(app.move(), &omaseed::app::MoveController::note);
    QSignalSpy outcomes(app.move(), &omaseed::app::MoveController::outcome);
    QVERIFY(app.move()->startSelection({kPack}, to, QString()));
    QTRY_VERIFY_WITH_TIMEOUT(outcomes.count() == 1, 5000);
    const QVariantMap outcome = outcomes.at(0).at(0).toMap();
    QCOMPARE(outcome.value(QStringLiteral("failedHashes")).toStringList(), QStringList({kPack}));
    QCOMPARE(outcome.value(QStringLiteral("cause")).toString(),
             QStringLiteral("Transmission can't write to %1 · it stopped the torrent").arg(to));
    // C25 K24 (board 47's strip): "start it here" in place of details.
    QVERIFY(outcome.value(QStringLiteral("startHere")).toBool());
    QVERIFY(outcome.value(QStringLiteral("details"))
                .toString()
                .startsWith(QStringLiteral("Couldn't move")));
    QVERIFY(!notes.isEmpty());
    QVERIFY(notes.constLast().at(2).toBool());
    // Sent once: the failure isn't a quiet torrent sent again.
    QCOMPARE(daemon.idsOf(QStringLiteral("torrent_set_location")).size(), 1);
    app.engine()->stop();
}

// #238, C25 K24: a folder's move where Transmission stopped the torrents it couldn't move. The
// summary band says they are stopped, in the strip's words, and offers starting them where
// they are ("start it here") next to the move's retry.
void TestAppTransmission::aFoldersSummaryOffersToStartWhatTransmissionStopped()
{
    Daemon daemon;
    const QString root = QStringLiteral("/srv/transmission/fixtures-recording");
    const QString to = QStringLiteral("/srv/locked/dest");
    const QStringList refused{kPack, kSingle};
    daemon.wire.answer = [&](const QString &method, const QJsonObject &params) {
        if (method == QLatin1String("torrent_set_location")) {
            const QJsonArray ids = params.value(QStringLiteral("ids")).toArray();
            for (const QJsonValue &id : ids) {
                QJsonObject &torrent = daemon.library.byHash(id.toString());
                if (!refused.contains(id.toString())) {
                    torrent.insert(QStringLiteral("download_dir"),
                                   params.value(QStringLiteral("location")));
                    continue;
                }
                const QString from = torrent.value(QStringLiteral("download_dir")).toString();
                const QString name = torrent.value(QStringLiteral("name")).toString();
                torrent.insert(QStringLiteral("error"), 3);
                torrent.insert(
                    QStringLiteral("error_string"),
                    QStringLiteral("Couldn't move '%1/%2' to '%3/%2': "
                                   "Permission denied (13)")
                        .arg(from, name, params.value(QStringLiteral("location")).toString()));
                torrent.insert(QStringLiteral("status"), 0);
            }
            return QJsonObject();
        }
        return daemon.library.answer(method, params);
    };
    App app;
    QVERIFY(startOn(app, daemon));
    QVERIFY(app.move()->start(root, to, false));
    QTRY_COMPARE_WITH_TIMEOUT(app.move()->stateName(), QStringLiteral("done"), 8000);
    QCOMPARE(app.move()->failedHashes().size(), 2);

    const QVariantMap band = app.move()->band();
    QVERIFY2(band.value(QStringLiteral("line"))
                 .toString()
                 .contains(QStringLiteral("2 failed · Transmission stopped the torrents")),
             qPrintable(band.value(QStringLiteral("line")).toString()));
    QStringList ids;
    QStringList texts;
    for (const QVariant &item : band.value(QStringLiteral("actions")).toList()) {
        ids << item.toMap().value(QStringLiteral("id")).toString();
        texts << item.toMap().value(QStringLiteral("text")).toString();
    }
    QCOMPARE(
        ids.mid(0, 3),
        QStringList(
            {QStringLiteral("startHere"), QStringLiteral("retry"), QStringLiteral("recheck")}));
    QCOMPARE(texts.mid(0, 2),
             QStringList({QStringLiteral("start the 2 here"), QStringLiteral("retry the 2")}));

    // "start the 2 here" is the strip's start: Transmission's start on the two, in place.
    app.retryTorrents(app.move()->failedHashes());
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("torrent_start")) != nullptr, 3000);
    QStringList started = daemon.idsOf(QStringLiteral("torrent_start")).constLast();
    started.sort();
    QStringList expected = refused;
    expected.sort();
    QCOMPARE(started, expected);
    QCOMPARE(daemon.idsOf(QStringLiteral("torrent_set_location")).size(), 4);
    app.engine()->stop();
}

// Board 47 §E, K23: on a Transmission whose disks omaseed can't see, room is read on the server
// (free_space), names at the destination can't be checked (said once), and the order goes one
// at a time.
void TestAppTransmission::theMoveSheetRunsTransmissionsChecks()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const QString to = QStringLiteral("/srv/other/games");
    const auto texts = [&]() {
        QStringList out;
        const QVariantMap plan = app.move()->planSelection({kPack, kSingle}, to, QString());
        for (const QVariant &line : plan.value(QStringLiteral("checks")).toList())
            out << line.toMap().value(QStringLiteral("text")).toString() + QStringLiteral(" | ") +
                       line.toMap().value(QStringLiteral("note")).toString();
        return out;
    };
    QStringList lines = texts();
    // Free space is asked of the server for the destination, then the plan reads it.
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("free_space")) != nullptr &&
                                 daemon.lastOf(QStringLiteral("free_space"))
                                         ->params.value(QStringLiteral("path"))
                                         .toString() == to,
                             3000);
    QTRY_VERIFY_WITH_TIMEOUT((lines = texts())
                                 .join(QLatin1Char('\n'))
                                 .contains(QStringLiteral("by Transmission's count")),
                             3000);
    const QString all = lines.join(QLatin1Char('\n'));
    QVERIFY2(all.contains(QStringLiteral("through Transmission: room on that drive")),
             qPrintable(all));
    QVERIFY2(
        all.contains(QStringLiteral("Names at the destination can't be checked on Transmission.")),
        qPrintable(all));
    QVERIFY2(all.contains(QStringLiteral("may be replaced by it")), qPrintable(all));
    QVERIFY2(all.contains(QStringLiteral("the rest follow one at a time")), qPrintable(all));
    QVERIFY2(!all.contains(QStringLiteral("qBittorrent")), qPrintable(all));
    QVERIFY(daemon.lastOf(QStringLiteral("torrent_set_location")) == nullptr);
    app.engine()->stop();
}

// Board 47 §F, C25 K24: the recorded torrent in error 3 (a move into a read-only folder) reads
// Transmission's own text in the row, the hero, the block and the story, with no log; "start it
// here" is a start.
void TestAppTransmission::anErroredTorrentReadsTransmissionsWords()
{
    Daemon daemon;
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
    QCOMPARE(problem.value(QStringLiteral("engine")).toString(), QStringLiteral("Transmission"));
    QVERIFY(problem.value(QStringLiteral("failedMove")).toBool());
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(), QStringLiteral("Didn't move"));
    const QString text =
        daemon.library.byHash(kError).value(QStringLiteral("error_string")).toString();
    QCOMPARE(problem.value(QStringLiteral("detail")).toString(),
             QStringLiteral("Transmission: “%1”").arg(text));
    QCOMPARE(problem.value(QStringLiteral("line")).toString(), text);
    QCOMPARE(problem.value(QStringLiteral("actions")).toStringList(),
             QStringList({QStringLiteral("startHere"), QStringLiteral("location")}));
    QVERIFY(problem.value(QStringLiteral("note"))
                .toString()
                .startsWith(QStringLiteral("Transmission stopped it · its files are still in ")));

    // The row, as the rows display and the details' hero read it: stopped, in its words.
    QTRY_COMPARE_WITH_TIMEOUT(app.torrentRow(kError).value(QStringLiteral("rowCaption")).toString(),
                              QStringLiteral("stopped"),
                              3000);
    const QVariantMap row = app.torrentRow(kError);
    QCOMPARE(row.value(QStringLiteral("rowFact")).toString(), QStringLiteral("didn't move"));
    QVERIFY(row.value(QStringLiteral("rowSentence"))
                .toString()
                .startsWith(QStringLiteral("Transmission couldn't move it to ")));
    QVERIFY(row.value(QStringLiteral("rowSentence"))
                .toString()
                .endsWith(QStringLiteral(" · it stopped the torrent")));
    for (const QVariant &event : app.torrentStory(kError)) {
        const QVariantMap e = event.toMap();
        if (e.value(QStringLiteral("kind")).toString() == QLatin1String("error"))
            QCOMPARE(e.value(QStringLiteral("detail")).toString(),
                     QStringLiteral("Transmission stopped it"));
    }

    // "start it here": Transmission's start.
    daemon.wire.calls.clear();
    app.retryTorrents({kError});
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("torrent_start")) != nullptr, 3000);
    QCOMPARE(daemon.idsOf(QStringLiteral("torrent_start")).constLast(), QStringList({kError}));
    app.engine()->stop();
}

// Board 47 `unw`: an error in place reads its cause ("no access"), retry first; one going into
// error while watched gets its notice in Transmission's words.
void TestAppTransmission::anErrorInPlaceReadsItsCauseAndItsNotice()
{
    Daemon daemon;
    daemon.library.byHash(kError).insert(QStringLiteral("error_string"),
                                         QStringLiteral("Permission denied (13)"));
    App app;
    QVERIFY(startOn(app, daemon));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.engine()->torrents()->problem(kError).value(QStringLiteral("headline")).toString(),
        QStringLiteral("Transmission can't write to its save folder"),
        3000);
    const QVariantMap problem = app.engine()->torrents()->problem(kError);
    QCOMPARE(problem.value(QStringLiteral("actions")).toStringList().constFirst(),
             QStringLiteral("start"));
    QTRY_COMPARE_WITH_TIMEOUT(app.torrentRow(kError).value(QStringLiteral("rowFact")).toString(),
                              QStringLiteral("no access"),
                              3000);

    QSignalSpy errored(app.engine()->problems(), &omaseed::core::ProblemLog::torrentErrored);
    QJsonObject &single = daemon.library.byHash(kSingle);
    single.insert(QStringLiteral("error"), 3);
    single.insert(QStringLiteral("error_string"), QStringLiteral("No space left on device (28)"));
    QTRY_COMPARE_WITH_TIMEOUT(errored.count(), 1, 3000);
    QCOMPARE(errored.at(0).at(0).toString(), kSingle);
    QCOMPARE(errored.at(0).at(2).value<omaseed::core::TorrentProblem>().headline,
             QStringLiteral("The disk is full"));
    app.engine()->stop();
}

// Board 47 §F/H: errors 1 and 2 are the trackers': tracker trouble, never "needs you".
void TestAppTransmission::aTrackerErrorIsTrackerTrouble()
{
    Daemon daemon;
    QJsonObject &single = daemon.library.byHash(kSingle);
    single.insert(QStringLiteral("error"), 2);
    single.insert(QStringLiteral("error_string"),
                  QStringLiteral("Tracker gave HTTP response code 404 (Not Found)"));
    App app;
    QVERIFY(startOn(app, daemon));
    const omaseed::core::Torrent t = app.engine()->torrents()->torrent(kSingle);
    QVERIFY(t.hasTrackerError);
    QVERIFY(t.status.phase != omaseed::core::TorrentPhase::Errored);
    QVERIFY(t.errorMessage.isEmpty());
    QVERIFY(app.engine()->torrents()->problem(kSingle).isEmpty());
    // The pack's trackers all failed to answer: tracker trouble too, and nothing more.
    QVERIFY(app.engine()->torrents()->torrent(kPack).hasTrackerError);
    QVERIFY(app.engine()->torrents()->problem(kPack).isEmpty());
    app.engine()->stop();
}

bool TestAppTransmission::startOn(App &app, Daemon &daemon)
{
    qputenv("OMASEED_SERVER_URL", daemon.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_ENGINE", "transmission");
    using namespace std::chrono_literals;
    app.engine()->setIntervals(100ms, 100ms, 100ms, 100ms);
    app.start();
    return QTest::qWaitFor([&app]() { return app.torrents()->count() == 4; }, 5000);
}

void TestAppTransmission::aTransmissionServerIsPolledIntoTheLibrary()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QCOMPARE(app.serverEngineName(), QStringLiteral("Transmission"));
    QCOMPARE(app.globalState()->serverVersion(), QStringLiteral("4.1.3"));
    // The first poll lists everything; the next ask only what moved (recently_active).
    QTRY_VERIFY_WITH_TIMEOUT(
        std::any_of(daemon.wire.calls.cbegin(),
                    daemon.wire.calls.cend(),
                    [](const Call &call) {
                        return call.method == QLatin1String("torrent_get") &&
                               call.params.value(QStringLiteral("ids")).toString() ==
                                   QLatin1String("recently_active");
                    }),
        3000);
    // Nothing of qBittorrent's Web API was asked.
    for (const auto &request : std::as_const(daemon.server.requests))
        QVERIFY(request.target.startsWith("/transmission/rpc"));
    // The rows read as Transmission's: the pack seeding, the single stopped.
    QVERIFY(!app.torrentRow(kPack).value(QStringLiteral("stopped")).toBool());
    QVERIFY(app.torrentRow(kSingle).value(QStringLiteral("stopped")).toBool());
    QCOMPARE(app.torrentRow(kPack).value(QStringLiteral("name")).toString(),
             QStringLiteral("Fixture Pack"));

    // A torrent removed on the daemon goes from the list at the next poll.
    daemon.remove(kMagnet);
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 3, 3000);
    app.engine()->stop();
}

void TestAppTransmission::whatTransmissionLacksIsNotOffered()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const QVariantMap can = app.featuresMap();
    // C14 E4, board 47 §1: what Transmission has…
    for (const char *on : {"tags",
                           "queue",
                           "forceStart",
                           "sequentialDownload",
                           "altSpeeds",
                           "renameOnDisk",
                           // Board 47 §E (C25 K21–K23): moves.
                           "moveStopsOnFailure",
                           "movesHoldAnswers",
                           "roomOnServer",
                           "moveReplacesFile"})
        QVERIFY2(can.value(QString::fromLatin1(on)).toBool(), on);
    // …and what it lacks, hidden by the views that read these.
    for (const char *off : {"categories",
                            "firstLastPiece",
                            "superSeeding",
                            "autoManagement",
                            "fetchMetadata",
                            "addOptions",
                            "v2Only",
                            "peerControl",
                            "editWebSeeds",
                            "seedingTimeLimit",
                            "engineLog",
                            "qbtSettings",
                            "rss",
                            "addAgain",
                            "search",
                            "createTorrent",
                            "moveProgress",
                            "listFolders"})
        QVERIFY2(!can.value(QString::fromLatin1(off), true).toBool(), off);
    // docs/DECISIONS.md C40 X9: Backup is there for its export.
    QVERIFY(can.value(QStringLiteral("torrentsBackup")).toBool());
    QVERIFY(can.value(QStringLiteral("torrentsExport")).toBool());
    QVERIFY(can.value(QStringLiteral("torrentsImport")).toBool());
    // Board 47 §B (C25 K13–K14): its tags are labels, named so wherever a person reads them.
    QVERIFY(can.value(QStringLiteral("tagsAreLabels")).toBool());
    QCOMPARE(can.value(QStringLiteral("tagWord")).toString(), QStringLiteral("label"));
    QCOMPARE(can.value(QStringLiteral("tagsWord")).toString(), QStringLiteral("labels"));
    QCOMPARE(can.value(QStringLiteral("untaggedWord")).toString(), QStringLiteral("no label"));
    QVERIFY(!app.searchOffered());
    QVERIFY(!app.createOffered());
    QVERIFY(!app.supportsShareLimitAction());
    // qBittorrent's own surfaces are never reached through Transmission's engine.
    QCOMPARE(app.qbtEngine(), nullptr);
    app.engine()->stop();
}

void TestAppTransmission::theEverydayActionsAreTransmissionsMethods()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));

    app.stopStart({kPack}); // running: a stop
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("torrent_stop")), QList<QStringList>{{kPack}}, 3000);
    app.stopStart({kSingle}); // stopped: a start
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("torrent_start")), QList<QStringList>{{kSingle}}, 3000);
    app.forceStart({kSingle});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("torrent_start_now")), QList<QStringList>{{kSingle}}, 3000);
    app.recheck({kPack});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("torrent_verify")), QList<QStringList>{{kPack}}, 3000);
    app.reannounce({kPack});
    QTRY_COMPARE_WITH_TIMEOUT(
        daemon.idsOf(QStringLiteral("torrent_reannounce")), QList<QStringList>{{kPack}}, 3000);
    app.rename(kPack, QStringLiteral("Renamed Pack"));
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("torrent_rename_path")) != nullptr, 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("torrent_rename_path"))
                 ->params.value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("Renamed Pack"));
    // DECISIONS C38 R1, R7: the story's record says the old name, and that Transmission renamed
    // the folder on disk too (K28).
    QTRY_COMPARE_WITH_TIMEOUT(app.storyRecord().recordsFor(kPack).size(), 1, 3000);
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().was, QStringLiteral("Fixture Pack"));
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().now, QStringLiteral("Renamed Pack"));
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().disk, true);

    // Delete, with and without the data.
    app.remove({kMagnet}, false);
    QTRY_VERIFY_WITH_TIMEOUT(daemon.lastOf(QStringLiteral("torrent_remove")) != nullptr, 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("torrent_remove"))
                 ->params.value(QStringLiteral("delete_local_data"))
                 .toBool(),
             false);
    app.remove({kError}, true);
    QTRY_COMPARE_WITH_TIMEOUT(daemon.idsOf(QStringLiteral("torrent_remove")).size(), 2, 3000);
    QCOMPARE(daemon.lastOf(QStringLiteral("torrent_remove"))
                 ->params.value(QStringLiteral("delete_local_data"))
                 .toBool(),
             true);
    QTRY_COMPARE_WITH_TIMEOUT(app.torrents()->count(), 2, 3000);
    app.engine()->stop();
}

// DECISIONS C38 R4, R7: a name changed on the daemon by someone else is a line, and on
// Transmission its folder on disk was renamed with it (K28).
void TestAppTransmission::aRenameSeenOnTransmissionSaysItsFolderToo()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QVERIFY(app.storyRecord().recordsFor(kPack).isEmpty());
    daemon.library.byHash(kPack).insert(QStringLiteral("name"), QStringLiteral("Outside Pack"));
    QTRY_COMPARE_WITH_TIMEOUT(app.storyRecord().recordsFor(kPack).size(), 1, 3000);
    const omaseed::core::StoryEntry line = app.storyRecord().recordsFor(kPack).constFirst();
    QCOMPARE(line.by, omaseed::core::StoryEntry::By::Elsewhere);
    QCOMPARE(line.was, QStringLiteral("Fixture Pack"));
    QCOMPARE(line.now, QStringLiteral("Outside Pack"));
    QVERIFY(line.disk);
    app.engine()->stop();
}

// #249: the strip and the story tell a rename at its OK; the row and the details' header read the
// name from the polls, so the OK asks for one at once rather than an interval later. Its poll is
// recently_active's, which carries the new name; the story stays one line, omaseed's.
void TestAppTransmission::aRenameReadsInTheRowAtItsOk()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    // From here on the polls are a minute apart: only the rename's own can bring the name.
    using namespace std::chrono_literals;
    app.engine()->setIntervals(60s, 60s, 60s, 60s);
    const auto polls = [&daemon]() {
        return std::count_if(daemon.wire.calls.cbegin(),
                             daemon.wire.calls.cend(),
                             [](const Call &call) { return call.method == "session_stats"; });
    };
    const auto before = polls();
    QTRY_VERIFY_WITH_TIMEOUT(polls() > before, 3000);
    QTest::qWait(300); // that poll applied: the next is a minute away

    const auto beforeRename = polls();
    app.rename(kPack, QStringLiteral("Renamed Pack"));
    QTRY_COMPARE_WITH_TIMEOUT(
        app.torrents()->torrent(kPack).name, QStringLiteral("Renamed Pack"), 3000);
    QCOMPARE(polls(), beforeRename + 1);
    // The poll's torrent_get is the batch's (the errored torrent's stop read after it is not).
    const auto poll =
        std::find_if(daemon.wire.calls.crbegin(), daemon.wire.calls.crend(), [](const Call &call) {
            return call.method == "torrent_get" && call.inBatch;
        });
    QVERIFY(poll != daemon.wire.calls.crend());
    QCOMPARE(poll->params.value(QStringLiteral("ids")).toString(),
             QStringLiteral("recently_active"));
    QCOMPARE(app.storyRecord().recordsFor(kPack).size(), 1);
    QCOMPARE(app.storyRecord().recordsFor(kPack).constFirst().by,
             omaseed::core::StoryEntry::By::Omaseed);
    app.engine()->stop();
}

void TestAppTransmission::anAddCarriesItsFolderAndLabels()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy finished(&app, &App::addFinished);
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=new").arg(kMagnet);
    app.addTorrent({{QStringLiteral("kind"), QStringLiteral("magnet")},
                    {QStringLiteral("value"), magnet},
                    {QStringLiteral("infoHashV1"), kMagnet},
                    {QStringLiteral("savePath"), QStringLiteral("/srv/transmission/new")},
                    {QStringLiteral("tags"), QStringList{QStringLiteral("fresh")}}});
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 3000);
    const Call *add = daemon.lastOf(QStringLiteral("torrent_add"));
    QVERIFY(add != nullptr);
    QCOMPARE(add->params.value(QStringLiteral("filename")).toString(), magnet);
    QCOMPARE(add->params.value(QStringLiteral("download_dir")).toString(),
             QStringLiteral("/srv/transmission/new"));
    QCOMPARE(add->params.value(QStringLiteral("labels")).toArray(), QJsonArray{"fresh"});
    app.engine()->stop();
}

// DECISIONS C43 Y3 (C25 K14): Transmission's labels are its tags; omaseed's own edits of them
// are lines in labels' words, a label the torrent had already no part of one.
void TestAppTransmission::labelsAreLabelledLines()
{
    using omaseed::core::StoryEntry;
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const QStringList had = app.torrents()->torrent(kPack).tags;
    QVERIFY(!had.isEmpty());
    const auto editsOf = [&app](const QString &hash) {
        QStringList out;
        for (const QVariant &event : app.torrentStory(hash)) {
            const QVariantMap map = event.toMap();
            if (map.value(QStringLiteral("kind")).toString() == QLatin1String("edit"))
                out << map.value(QStringLiteral("title")).toString() + QStringLiteral(" / ") +
                           map.value(QStringLiteral("detail")).toString();
        }
        return out;
    };

    // Both at once are one line; then a label put on another torrent.
    app.applyTags({kPack}, {QStringLiteral("walk")}, {had.constFirst()});
    QTRY_COMPARE_WITH_TIMEOUT(app.storyRecord().recordsFor(kPack).size(), 1, 3000);
    const StoryEntry pack = app.storyRecord().recordsFor(kPack).constFirst();
    QCOMPARE(pack.what, StoryEntry::What::Tags);
    QCOMPARE(pack.by, StoryEntry::By::Omaseed);
    QCOMPARE(pack.tagged, QStringList{QStringLiteral("walk")});
    QCOMPARE(pack.untagged, QStringList{had.constFirst()});
    QCOMPARE(editsOf(kPack),
             QStringList{QStringLiteral("Labels changed / labelled walk · unlabelled %1")
                             .arg(had.constFirst())});

    app.addTags({kSingle}, {QStringLiteral("walk")});
    QTRY_COMPARE_WITH_TIMEOUT(
        editsOf(kSingle), QStringList{QStringLiteral("Labelled walk / ")}, 3000);
    app.engine()->stop();
}

void TestAppTransmission::detailsAreAskedOfTransmission()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    const qsizetype before = daemon.wire.calls.size();
    app.selectTorrent(kPack);
    app.setDetailsVisible(true);
    // Its files, trackers and peers, by hash, as torrent_get with those fields.
    const auto asked = [&daemon, before](const char *field) {
        for (qsizetype i = before; i < daemon.wire.calls.size(); ++i) {
            const Call &call = daemon.wire.calls.at(i);
            if (call.method == QLatin1String("torrent_get") &&
                call.params.value(QStringLiteral("ids")).toArray() == QJsonArray{kPack} &&
                call.params.value(QStringLiteral("fields"))
                    .toArray()
                    .contains(QString::fromLatin1(field)))
                return true;
        }
        return false;
    };
    QTRY_VERIFY_WITH_TIMEOUT(asked("files"), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(asked("tracker_stats"), 3000);
    app.engine()->stop();
}

void TestAppTransmission::resumeRightAfterPauseWaitsForTheStopToLand()
{
    Daemon daemon;
    daemon.stopLandsAfterMs = 700; // 4.1.3: torrent_stop answers while it still reads running
    App app;
    QVERIFY(startOn(app, daemon));
    QSignalSpy paused(app.pauseHold(), &omaseed::core::PauseHold::paused);
    QSignalSpy resumed(app.pauseHold(), &omaseed::core::PauseHold::resumed);

    app.pauseTorrents();
    QTRY_COMPARE_WITH_TIMEOUT(paused.count(), 1, 3000);
    QCOMPARE(paused.at(0).at(0).toInt(), 2); // the pack and the magnet ran
    QCOMPARE(daemon.status(kPack), 6);       // the stop hasn't landed yet

    app.resumeTorrents();
    QTRY_COMPARE_WITH_TIMEOUT(resumed.count(), 1, 5000);
    // Both started again, none skipped as "started by hand".
    QCOMPARE(resumed.at(0).at(0).toInt(), 2);
    QCOMPARE(resumed.at(0).at(1).toInt(), 0);
    const QList<QStringList> started{{kPack, kMagnet}};
    QCOMPARE(daemon.idsOf(QStringLiteral("torrent_start")), started);
    app.engine()->stop();
}

void TestAppTransmission::anOldTransmissionIsRefusedInK8sWords()
{
    Daemon daemon;
    daemon.wire.rpcVersion = "5.4.0";
    daemon.library.sessionGet.insert(QStringLiteral("version"),
                                     QStringLiteral("4.0.6 (38c164933e)"));
    daemon.library.sessionGet.insert(QStringLiteral("rpc_version_semver"), QStringLiteral("5.4.0"));
    App app;
    qputenv("OMASEED_SERVER_URL", daemon.url().toString().toUtf8());
    qputenv("OMASEED_SERVER_ENGINE", "transmission");
    app.start();
    QTRY_VERIFY_WITH_TIMEOUT(app.serverRefused(), 5000);
    const QVariantMap problem = app.serverProblem(app.globalState()->lastError(), -1, -1);
    QCOMPARE(problem.value(QStringLiteral("headline")).toString(),
             QStringLiteral("Transmission 4.0.6 is too old: omaseed needs 4.1 or later"));
    QCOMPARE(problem.value(QStringLiteral("kind")).toString(), QStringLiteral("engine"));
    QVERIFY(problem.value(QStringLiteral("line")).toString().contains(QStringLiteral("4.1")));
    QCOMPARE(problem.value(QStringLiteral("retry")).toString(), QString());
    // Refused once: polling stopped, the library never asked.
    QVERIFY(!app.engine()->isRunning());
    for (const Call &call : std::as_const(daemon.wire.calls))
        QVERIFY(call.method != QLatin1String("torrent_get"));
    const QString guard = app.guardNote(QStringLiteral("p"));
    QVERIFY2(guard.contains(QStringLiteral("too old")), qPrintable(guard));
}

QTEST_MAIN(TestAppTransmission)

// C25 K33 (board 36 §6 E5's "backups and the weekly copies per engine"): omaseed's own backup
// and its copies carry qBittorrent's settings only; on Transmission nothing is read for them,
// and the summary names no engine settings.
void TestAppTransmission::omaseedsBackupHoldsNoTransmissionSettings()
{
    Daemon daemon;
    App app;
    QVERIFY(startOn(app, daemon));
    app.refreshBackupInfo();
    QVERIFY2(!app.backupSummary().contains(QStringLiteral("qBittorrent")),
             qPrintable(app.backupSummary()));

    const QString path = m_home->filePath(QStringLiteral("now.omaseed"));
    QCOMPARE(app.backUpTo(path), QString());
    const std::optional<omaseed::core::OmaseedBackup::Contents> contents =
        omaseed::core::OmaseedBackup::parseFile(path);
    QVERIFY(contents.has_value());
    QVERIFY(contents->extras.isEmpty());
    app.engine()->stop();
}

#include "tst_app_transmission.moc"
