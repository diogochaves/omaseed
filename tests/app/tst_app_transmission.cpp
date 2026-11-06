// A Transmission server driven in the app (#85, E4 step 3; DECISIONS C14 E4, C25): App on a
// fake Transmission 4.1.3 (FakeTransmission, the daemon's recorded answers) polls the library
// into its models, sends each everyday action as Transmission's method, holds a pause whose
// stop lands a moment after its answer (C36 M10), refuses a daemon older than 4.1 in K8's words,
// and offers none of what Transmission lacks (the feature record, the search and the creator).

#include "FakeTransmission.hpp"

#include "core/GlobalState.hpp"
#include "core/PauseHold.hpp"
#include "core/SyncEngine.hpp"
#include "core/TorrentModel.hpp"
#include "ui/app/App.hpp"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <memory>

using omaseed::app::App;
using namespace faketransmission;

namespace {

const QString kPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kSingle = QStringLiteral("2b86112ae20c6ca831cbfb3e4333c8acc6d21088");
const QString kError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");

/// The recorded library as a daemon that changes: stops and starts move a torrent's status
/// (after `stopLandsAfterMs` for a stop, as 4.1.3 does), a remove takes it out, and a torrent_get
/// for some hashes answers only those.
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
        if (method == QLatin1String("torrent_add"))
            return fixture("torrent_add.json").value(QStringLiteral("result")).toObject();
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
    void anAddCarriesItsFolderAndLabels();
    void detailsAreAskedOfTransmission();
    void resumeRightAfterPauseWaitsForTheStopToLand();
    void anOldTransmissionIsRefusedInK8sWords();

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
    for (const char *on :
         {"tags", "queue", "forceStart", "sequentialDownload", "altSpeeds", "renameOnDisk"})
        QVERIFY2(can.value(QString::fromLatin1(on)).toBool(), on);
    // …and what it lacks, hidden by the views that read these.
    for (const char *off : {"categories",
                            "firstLastPiece",
                            "superSeeding",
                            "autoManagement",
                            "fetchMetadata",
                            "addOptions",
                            "peerControl",
                            "editWebSeeds",
                            "seedingTimeLimit",
                            "engineLog",
                            "qbtSettings",
                            "torrentsBackup",
                            "rss",
                            "addAgain",
                            "search",
                            "createTorrent"})
        QVERIFY2(!can.value(QString::fromLatin1(off), true).toBool(), off);
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

#include "tst_app_transmission.moc"
