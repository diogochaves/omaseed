// DelugeEngine and DelugeRpc (#87, E6): Deluge 2.2 through deluge-web behind EngineClient,
// against a small fake deluge-web of the test's own (FakeDeluge). What leaves is checked call
// by call (method names and params, as a real 2.2 takes them: tst_delugeengine_live); what
// comes back is deluge-web's recorded answers (tests/fixtures/deluge/). The password and its
// cookie, a session that runs out, a refused password latched, the daemon step (K10), the
// refusal below 2.1, the poll through SnapshotDiffer with its trackers and content roots, the
// Label plugin on and off, the prefetch's lookup (K26), and every action's call.

#include "FakeDeluge.hpp"

#include "core/engines/deluge/DelugeEngine.hpp"
#include "core/engines/deluge/DelugeRpc.hpp"
#include "core/engines/deluge/DelugeTranslate.hpp"

#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#include <optional>

using namespace fakehttp;
using namespace fakedeluge;
using namespace omaseed::core;
namespace DT = omaseed::core::DelugeTranslate;

namespace {

const QString kPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kSingle = QStringLiteral("ed6dc254e7756302401a59491e69f6a53ac157be");
const QString kError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");
const QString kStream = QStringLiteral("a8701869a74f56c5c3a16f4673c037a682619afb");
const QString kQueued = QStringLiteral("6f2336946faaaa1532ae3e8b3216a00bb08e55be");
const QString kHost = QStringLiteral("omaseed0test0daemon");

QJsonArray strings(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

QStringList stringsOf(const QJsonArray &array)
{
    QStringList list;
    for (const QJsonValue &value : array)
        list.append(value.toString());
    return list;
}

} // namespace

class TestDelugeEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    // the wire
    void signsInFirstAndKeepsTheCookie();
    void aSessionThatRanOutSignsInAgainOnce();
    void aRefusedPasswordIsNotTriedAgain();
    void withoutAPasswordDelugeWebWantsOne();
    void errorsBecomeApiErrors();
    void readsAreRetriedMutationsAreNot();
    void theEndpointFollowsTheAddressGiven();

    // what the server is
    void probeReadsVersionPluginsAndLibtorrent();
    void probeRefusesADelugeOlderThan21();
    void probeWithoutADaemon();
    void theDaemonStep();

    // the poll
    void theFirstPollIsFullWithTrackersRootsAndLabels();
    void laterPollsAreDiffsOfTheWholeList();
    void aNewTorrentsTrackersAreAskedAtOnce();
    void aTorrentGoneIsRemovedWithWhatWasKept();
    void labelsComeAndGoWithThePlugin();
    void aLabelPluginFlipMarksTwoDeltas();
    void aPollWithoutADaemonSaysSo();
    void lastActivityDoesNotWobble();
    void anotherServerStartsOver();

    // reads
    void stoppedAmongAndRunningGoByTheState();
    void detailsAskForTheirKeys();
    void categoriesAreTheLabels();

    // actions
    void actionsSpellDelugesWords();
    void noTorrentsNeverReachTheWire();
    void forceStartTakesItOutOfTheQueue();
    void filePrioritiesAreReadAndSentWhole();
    void renamesFindTheirFileAndForgetTheRoot();
    void limitsLeaveInKibibytes();
    void shareLimitsAreARatioThenStopOrRemove();
    void globalLimitsAndPreferences();
    void labelsAreLowerCaseAndNeedThePlugin();
    void trackersAreEditedWhole();
    void togglesAreReadThenSet();
    void peersAreAddedNeverBanned();
    void addSendsOptionsThenTheLabel();
    void addSaysWhenEverythingWasThereAlready();
    void removeTakesTorrentsAlreadyGone();
    void whatDelugeLacksIsNotSupported();

    // moves and room (board 47 E)
    void freeSpaceIsReadOnTheNearestFolderThere();
    void aFolderIsListedOrMissing();

    // a magnet's files before the add (K26)
    void aPrefetchRunsOnceWhileThePollsAsk();
    void anEmptyPrefetchIsAskedAgain();
    void aRefusedMagnetEndsTheLookup();
    void linksAreNotPrefetched();

private:
    template<class T>
    Result<T> wait(Reply<T> *reply)
    {
        return await(this, reply);
    }
    /// Asks for `magnet`'s files again and again, as MetadataLookup does, until `done` holds
    /// of an answer or 5 s pass; the last answer.
    Result<FetchedMetadata>
    fetchUntil(const QString &magnet,
               const std::function<bool(const Result<FetchedMetadata> &)> &done)
    {
        Result<FetchedMetadata> fetched;
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < 5000) {
            fetched = wait(m_engine->fetchMetadata(magnet));
            if (done(fetched))
                break;
            QTest::qWait(20);
        }
        return fetched;
    }
    /// The engine signed in, the calls so far forgotten.
    void signIn();
    SyncDelta poll(int cursor);

    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeDeluge> m_web;
    std::unique_ptr<RecordedDeluge> m_daemon;
    std::unique_ptr<DelugeEngine> m_engine;
    qint64 m_nowMs = 1793960200000;
};

void TestDelugeEngine::init()
{
    m_server = std::make_unique<FakeServer>();
    m_web = std::make_unique<FakeDeluge>();
    m_daemon = std::make_unique<RecordedDeluge>();
    m_web->answer = [this](const QString &method, const QJsonArray &params) {
        return m_daemon->answer(method, params);
    };
    m_server->setHandler(m_web->handler());
    m_engine = std::make_unique<DelugeEngine>();
    m_engine->setBaseUrl(m_server->url());
    m_engine->setCredentials(QString(), m_web->password);
    m_engine->setTimeout(std::chrono::seconds(5));
    m_engine->rpc()->setRetryBackoff(std::chrono::milliseconds(10));
    m_engine->setClock([this]() { return m_nowMs; });
}

void TestDelugeEngine::cleanup()
{
    m_engine.reset();
    m_daemon.reset();
    m_web.reset();
    m_server.reset();
}

void TestDelugeEngine::signIn()
{
    QVERIFY(wait(m_engine->probe()).isValid());
    m_web->calls.clear();
}

SyncDelta TestDelugeEngine::poll(int cursor)
{
    const Result<SyncDelta> result = wait(m_engine->sync(cursor));
    [&]() { QVERIFY2(result.isValid(), qPrintable(result.error().toString())); }();
    return result.value();
}

// --- the wire -------------------------------------------------------------------------------

void TestDelugeEngine::signsInFirstAndKeepsTheCookie()
{
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    QCOMPARE(m_web->methods(), QStringList({"auth.login", "core.resume_torrents"}));
    QCOMPARE(m_web->calls.at(0).params, QJsonArray({m_web->password}));
    QCOMPARE(m_server->requests.at(1).target, QByteArray("/json"));
    QVERIFY(m_server->requests.at(1).header("Content-Type").startsWith("application/json"));
    QVERIFY(m_server->requests.at(1).header("Cookie").contains("_session_id=s1"));
    QVERIFY(m_engine->rpc()->signedIn());
    // From then on the cookie goes with every call; no second sign-in.
    QVERIFY(wait(m_engine->stop({kPack})).isValid());
    QCOMPARE(m_web->logins, 1);
    QVERIFY(!m_engine->hasApiKey());
    m_engine->setApiKey(QStringLiteral("ignored")); // deluge-web has no keys
    QVERIFY(!m_engine->hasApiKey());
}

void TestDelugeEngine::aSessionThatRanOutSignsInAgainOnce()
{
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    m_web->expireSessions();
    m_web->calls.clear();
    QVERIFY(wait(m_engine->stop({kPack})).isValid());
    // Refused ("Not authenticated"), signed in again, sent again: once.
    QCOMPARE(m_web->methods(),
             QStringList({"core.pause_torrents", "auth.login", "core.pause_torrents"}));
    QCOMPARE(m_web->logins, 2);

    // Several calls refused at once share one sign-in.
    m_web->expireSessions();
    m_web->calls.clear();
    auto *a = m_engine->start({kPack});
    auto *b = m_engine->stop({kSingle});
    // b may finish while a is waited on, and a reply that has finished tells nobody who asks
    // later: listen to it first (CI failed here now and then, "test never finished").
    std::optional<Result<Done>> second;
    b->then(this, [&second](const Result<Done> &finished) { second = finished; });
    QVERIFY(wait(a).isValid());
    QTRY_VERIFY(second.has_value());
    QVERIFY(second->isValid());
    QCOMPARE(m_web->callsOf(QStringLiteral("auth.login")).size(), 1);
}

void TestDelugeEngine::aRefusedPasswordIsNotTriedAgain()
{
    QSignalSpy refused(m_engine->rpc(), &DelugeRpc::loginRefusedChanged);
    m_engine->setCredentials(QString(), QStringLiteral("wrong"));
    Result<Done> result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    QVERIFY(m_engine->rpc()->loginRefused());
    QCOMPARE(refused.count(), 1);
    const qsizetype sent = m_server->requests.size();
    QCOMPARE(sent, 1); // only the sign-in; the call itself never went
    // Latched: nothing goes out until someone asks for another try.
    result = wait(m_engine->stop({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(m_server->requests.size(), sent);

    m_engine->allowLoginAgain();
    result = wait(m_engine->stop({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(m_server->requests.size(), sent + 1);

    m_engine->setCredentials(QString(), m_web->password); // the right one, typed again
    QVERIFY(wait(m_engine->stop({kPack})).isValid());
    QVERIFY(!m_engine->rpc()->loginRefused());
}

void TestDelugeEngine::withoutAPasswordDelugeWebWantsOne()
{
    m_engine->clearAuth();
    const Result<Done> result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(m_web->methods(), QStringList({"core.resume_torrents"})); // no sign-in to try
}

void TestDelugeEngine::errorsBecomeApiErrors()
{
    m_web->answer = [](const QString &method, const QJsonArray &) -> QJsonValue {
        if (method == QLatin1String("core.force_recheck"))
            return errorOf("unknown_method.json");
        if (method == QLatin1String("label.add"))
            return errorOf("error_label_invalid.json");
        return errorOf("error_add_duplicate.json");
    };
    Result<Done> result = wait(m_engine->recheck({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::NotFound);
    QVERIFY(result.error().message().contains(QLatin1String("Unknown method")));
    result = wait(m_engine->reannounce({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    // The exception's own words, out of the traceback.
    QCOMPARE(result.error().message(),
             QStringLiteral("core.force_reannounce: Torrent already in session (%1).").arg(kPack));

    QCOMPARE(DelugeRpc::failureWords(fixture("error_label_invalid.json")
                                         .value(QStringLiteral("error"))
                                         .toObject()
                                         .value(QStringLiteral("message"))
                                         .toString()),
             QStringLiteral("Invalid label, valid characters:[a-z0-9_-]"));
    QCOMPARE(DelugeRpc::failureWords(fixture("error_prefetch_v2.json")
                                         .value(QStringLiteral("error"))
                                         .toObject()
                                         .value(QStringLiteral("message"))
                                         .toString()),
             QStringLiteral("missing 'info_hash'"));
    QCOMPARE(DelugeRpc::failureWords(QStringLiteral("plain words")), QStringLiteral("plain words"));
    // Not deluge-web at all.
    QCOMPARE(DelugeRpc::answerOf(QStringLiteral("x"), "<html>").error().kind(),
             ApiError::Kind::Parse);
}

void TestDelugeEngine::readsAreRetriedMutationsAreNot()
{
    signIn();
    m_web->unavailable = 1;
    QVERIFY(wait(m_engine->properties(kPack)).isValid());
    QCOMPARE(m_web->callsOf(QStringLiteral("core.get_torrent_status")).size(), 2);

    m_web->calls.clear();
    m_web->unavailable = 1;
    const Result<Done> result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Server);
    QCOMPARE(m_web->methods(), QStringList({"core.resume_torrents"}));
}

void TestDelugeEngine::theEndpointFollowsTheAddressGiven()
{
    DelugeRpc rpc;
    rpc.setBaseUrl(QUrl(QStringLiteral("https://nas.lan/deluge/")));
    QCOMPARE(rpc.endpoint(), QUrl(QStringLiteral("https://nas.lan/deluge/json")));
    rpc.setBaseUrl(QUrl(QStringLiteral("http://127.0.0.1:8112/json")));
    QCOMPARE(rpc.endpoint(), QUrl(QStringLiteral("http://127.0.0.1:8112/json")));
    QCOMPARE(m_engine->reachableUrl(QUrl(QStringLiteral("http://h:8112"))),
             QUrl(QStringLiteral("http://h:8112/json")));
    // A server URL with /json already is taken as it is.
    m_engine->setBaseUrl(QUrl(m_server->url().toString() + QStringLiteral("/json")));
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    QCOMPARE(m_server->requests.constLast().target, QByteArray("/json"));
}

// --- what the server is ---------------------------------------------------------------------

void TestDelugeEngine::probeReadsVersionPluginsAndLibtorrent()
{
    QSignalSpy plugin(m_engine.get(), &DelugeEngine::labelPluginChanged);
    const Result<ServerInfo> info = wait(m_engine->probe());
    QVERIFY2(info.isValid(), qPrintable(info.error().toString()));
    QCOMPARE(info.value().version, QStringLiteral("2.2.0"));
    QCOMPARE(info.value().apiVersion, QString());
    QCOMPARE(m_engine->libtorrentVersion(), QStringLiteral("2.0.14.0"));
    QVERIFY(m_engine->labelPlugin());
    QCOMPARE(plugin.count(), 1);
    QVERIFY(m_engine->daemonConnected());
    QCOMPARE(m_web->methods().mid(0, 2), QStringList({"auth.login", "web.connected"}));
    QVERIFY(m_web->methods().contains(QStringLiteral("daemon.get_version")));
    QVERIFY(m_web->methods().contains(QStringLiteral("core.get_enabled_plugins")));
    QVERIFY(m_engine->features(info.value()).categories);

    m_daemon->labelPlugin = false;
    QVERIFY(wait(m_engine->probe()).isValid());
    QVERIFY(!m_engine->labelPlugin());
    QVERIFY(!m_engine->features(info.value()).categories);
    QVERIFY(m_engine->features(info.value()).categoriesAreLabels);
}

void TestDelugeEngine::probeRefusesADelugeOlderThan21()
{
    m_web->answer = [this](const QString &method, const QJsonArray &params) -> QJsonValue {
        if (method == QLatin1String("daemon.get_version"))
            return QStringLiteral("2.0.5");
        return m_daemon->answer(method, params);
    };
    const Result<ServerInfo> info = wait(m_engine->probe());
    QCOMPARE(info.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(info.error().message(),
             QStringLiteral("Deluge 2.0.5 is too old: omaseed needs 2.1 or later"));
}

void TestDelugeEngine::probeWithoutADaemon()
{
    QSignalSpy connected(m_engine.get(), &DelugeEngine::daemonConnectedChanged);
    m_daemon->connected = false;
    Result<ServerInfo> info = wait(m_engine->probe());
    QCOMPARE(info.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(info.error().message().contains(QLatin1String("isn't connected to a Deluge daemon")));
    QVERIFY(!m_engine->daemonConnected());
    QCOMPARE(connected.count(), 1);
    // Never a core call while there's no daemon: deluge-web would never answer it.
    QVERIFY(!m_web->methods().contains(QStringLiteral("daemon.get_version")));

    // With the server's own daemon stored, and none connected, that one is connected (K10).
    m_engine->setDaemon(kHost);
    m_web->calls.clear();
    m_web->answer = [this](const QString &method, const QJsonArray &params) -> QJsonValue {
        if (method == QLatin1String("web.connect"))
            m_daemon->connected = true;
        return m_daemon->answer(method, params);
    };
    info = wait(m_engine->probe());
    QVERIFY2(info.isValid(), qPrintable(info.error().toString()));
    QCOMPARE(m_web->callsOf(QStringLiteral("web.connect")).size(), 1);
    QCOMPARE(m_web->callsOf(QStringLiteral("web.connect")).first().params, QJsonArray({kHost}));
    QVERIFY(m_engine->daemonConnected());
}

void TestDelugeEngine::theDaemonStep()
{
    const Result<QList<DelugeHost>> hosts = wait(m_engine->hosts());
    QVERIFY2(hosts.isValid(), qPrintable(hosts.error().toString()));
    QCOMPARE(hosts.value().size(), 1);
    const DelugeHost &host = hosts.value().first();
    QCOMPARE(host.id, kHost);
    QCOMPARE(host.host, QStringLiteral("127.0.0.1"));
    QCOMPARE(host.port, 58847);
    QCOMPARE(host.user, QStringLiteral("localclient"));
    QCOMPARE(host.status, QStringLiteral("Connected"));
    QCOMPARE(host.version, QStringLiteral("2.2.0"));
    QCOMPARE(m_web->callsOf(QStringLiteral("web.get_host_status")).first().params,
             QJsonArray({kHost}));

    // Picked: connected for everyone (the step said so first) and stored with the server.
    QVERIFY(poll(0).full);
    QVERIFY(wait(m_engine->connectDaemon(kHost)).isValid());
    QCOMPARE(m_web->callsOf(QStringLiteral("web.connect")).first().params, QJsonArray({kHost}));
    QCOMPARE(m_engine->daemon(), kHost);
    // Another daemon's torrents: the next poll starts over.
    QVERIFY(poll(1).full);
    QCOMPARE(wait(m_engine->connectDaemon(QString())).error().kind(), ApiError::Kind::Conflict);
}

// --- the poll -------------------------------------------------------------------------------

void TestDelugeEngine::theFirstPollIsFullWithTrackersRootsAndLabels()
{
    signIn();
    const SyncDelta delta = poll(0);
    QVERIFY(delta.full);
    QCOMPARE(delta.cursor, std::optional<int>(1));
    QCOMPARE(delta.torrents.size(), 6);

    // web.update_ui with the list's keys and the trackers, then the session's totals, the
    // files and trackers of every torrent with metadata, and each label's options.
    const QList<Call> updateUi = m_web->callsOf(QStringLiteral("web.update_ui"));
    QCOMPARE(updateUi.size(), 1);
    const QStringList keys = stringsOf(updateUi.first().params.at(0).toArray());
    QCOMPARE(keys.mid(0, DT::listFields().size()), DT::listFields());
    QVERIFY(keys.contains(QStringLiteral("trackers")));
    QVERIFY(!keys.contains(QStringLiteral("files")));
    QCOMPARE(updateUi.first().params.at(1).toObject(), QJsonObject());
    QCOMPARE(m_web->callsOf(QStringLiteral("core.get_session_status")).size(), 1);
    const QList<Call> roots = m_web->callsOf(QStringLiteral("core.get_torrents_status"));
    QCOMPARE(roots.size(), 1);
    const QStringList asked =
        stringsOf(roots.first().params.at(0).toObject().value(QStringLiteral("id")).toArray());
    QCOMPARE(asked.size(), 5); // not the magnet: it has no files yet
    QVERIFY(!asked.contains(kMagnet));
    QCOMPARE(stringsOf(roots.first().params.at(1).toArray()), QStringList({"files", "trackers"}));
    QCOMPARE(m_web->callsOf(QStringLiteral("label.get_options")).size(), 2);

    // The pack, whole: its content path from its files, its label, the trackers' map.
    TorrentPatch pack;
    for (const TorrentPatch &patch : delta.torrents) {
        if (patch.hash == kPack)
            pack = patch;
    }
    QCOMPARE(pack.values.contentPath,
             QStringLiteral("/srv/deluge/fixtures-recording/data/Fixture Pack"));
    QCOMPARE(pack.values.category, QStringLiteral("fixtures"));
    // The pack, the single, the queued one and the magnet announce to the dead one.
    QCOMPARE(delta.trackers.value(QStringLiteral("http://127.0.0.1:9/announce")).size(), 4);
    QVERIFY(delta.tags.isEmpty());
    QCOMPARE(delta.categories.size(), 2);
    QCOMPARE(delta.categories.at(1).name, QStringLiteral("fixtures"));
    QCOMPARE(delta.categories.at(1).savePath,
             std::optional<QString>(QStringLiteral("/srv/deluge/fixtures-recording/done")));
    QVERIFY(delta.serverState);
    QCOMPARE(delta.serverState->dlSession, std::optional<qint64>(383683));
    QCOMPARE(delta.serverState->freeSpaceOnDisk, std::optional<qint64>(400000000000));
}

void TestDelugeEngine::laterPollsAreDiffsOfTheWholeList()
{
    signIn();
    const int cursor = *poll(0).cursor;
    m_web->calls.clear();

    // Nothing moved: nothing handed out, and none of the heavy keys asked again.
    SyncDelta delta = poll(cursor);
    QVERIFY(!delta.full);
    QCOMPARE(delta.cursor, std::optional<int>(cursor + 1));
    QVERIFY(delta.torrents.isEmpty());
    QVERIFY(delta.removedTorrents.isEmpty());
    QVERIFY(delta.categories.isEmpty() && delta.removedCategories.isEmpty());
    QVERIFY(
        !stringsOf(m_web->callsOf(QStringLiteral("web.update_ui")).first().params.at(0).toArray())
             .contains(QStringLiteral("trackers")));
    QVERIFY(m_web->callsOf(QStringLiteral("core.get_torrents_status")).isEmpty());
    QVERIFY(m_web->callsOf(QStringLiteral("label.get_options")).isEmpty());

    // The single resumed: only it, only what moved.
    QJsonObject single = m_daemon->torrents.value(kSingle).toObject();
    single.insert(QStringLiteral("state"), QStringLiteral("Downloading"));
    single.insert(QStringLiteral("paused"), false);
    m_daemon->torrents.insert(kSingle, single);
    delta = poll(*delta.cursor);
    QCOMPARE(delta.torrents.size(), 1);
    QCOMPARE(delta.torrents.first().hash, kSingle);
    QVERIFY(delta.torrents.first().has(TorrentField::Status));
    QVERIFY(!delta.torrents.first().has(TorrentField::Name));
    QCOMPARE(delta.torrents.first().values.status.phase, TorrentPhase::Downloading);

    // Every kTrackersEveryPolls polls the trackers are asked again.
    for (int i = 0; i < DelugeEngine::kTrackersEveryPolls; ++i)
        delta = poll(*delta.cursor);
    bool askedTrackers = false;
    for (const Call &call : m_web->callsOf(QStringLiteral("web.update_ui")))
        askedTrackers |=
            stringsOf(call.params.at(0).toArray()).contains(QStringLiteral("trackers"));
    QVERIFY(askedTrackers);
}

// Deluge's `tracker` is empty until a torrent announces: a torrent new to the engine, seen
// without its trackers, has them asked on the next poll, so a paused one isn't trackerless.
void TestDelugeEngine::aNewTorrentsTrackersAreAskedAtOnce()
{
    signIn();
    int cursor = *poll(0).cursor;
    const QString fresh = QStringLiteral("ab").repeated(20);
    QJsonObject copy = m_daemon->torrents.value(kMagnet).toObject();
    copy.insert(QStringLiteral("tracker"), QString());
    m_daemon->torrents.insert(fresh, copy);
    m_web->calls.clear();
    SyncDelta delta = poll(cursor);
    const auto patchOf = [](const SyncDelta &d, const QString &hash) {
        for (const TorrentPatch &patch : d.torrents) {
            if (patch.hash == hash)
                return patch;
        }
        return TorrentPatch();
    };
    QCOMPARE(patchOf(delta, fresh).hash, fresh);
    QVERIFY(!patchOf(delta, fresh).has(TorrentField::TrackersCount));
    delta = poll(*delta.cursor);
    QVERIFY(stringsOf(
                m_web->callsOf(QStringLiteral("web.update_ui")).constLast().params.at(0).toArray())
                .contains(QStringLiteral("trackers")));
    QVERIFY(patchOf(delta, fresh).has(TorrentField::TrackersCount));
    QVERIFY(patchOf(delta, fresh).values.trackersCount > 0);
}

void TestDelugeEngine::aTorrentGoneIsRemovedWithWhatWasKept()
{
    signIn();
    const int cursor = *poll(0).cursor;
    m_daemon->torrents.remove(kPack);
    const SyncDelta delta = poll(cursor);
    QCOMPARE(delta.removedTorrents, QStringList({kPack}));
    // Its trackers went with it: three of four left on the dead one, the udp one gone.
    QCOMPARE(delta.trackers.value(QStringLiteral("http://127.0.0.1:9/announce")).size(), 3);
    QCOMPARE(delta.removedTrackers, QStringList({"udp://127.0.0.1:9/announce"}));
}

void TestDelugeEngine::labelsComeAndGoWithThePlugin()
{
    signIn();
    int cursor = *poll(0).cursor;

    // A new label in the filter tree: its options read, handed out alone.
    QJsonObject filters = m_daemon->updateUi.value(QStringLiteral("filters")).toObject();
    QJsonArray labels = filters.value(QStringLiteral("label")).toArray();
    labels.append(QJsonArray{QStringLiteral("films"), 0});
    filters.insert(QStringLiteral("label"), labels);
    m_daemon->updateUi.insert(QStringLiteral("filters"), filters);
    m_web->calls.clear();
    SyncDelta delta = poll(cursor);
    QCOMPARE(m_web->callsOf(QStringLiteral("label.get_options")).size(), 3);
    QCOMPARE(delta.categories.size(), 1);
    QCOMPARE(delta.categories.first().name, QStringLiteral("films"));
    QCOMPARE(delta.categories.first().savePath, std::optional<QString>(QString()));
    cursor = *delta.cursor;

    // The plugin turned off: every label goes, and the torrents' labels with it.
    QSignalSpy plugin(m_engine.get(), &DelugeEngine::labelPluginChanged);
    m_daemon->labelPlugin = false;
    delta = poll(cursor);
    QCOMPARE(plugin.count(), 1);
    QVERIFY(!m_engine->labelPlugin());
    QCOMPARE(delta.removedCategories, QStringList({"empty", "films", "fixtures"}));
    bool packLostItsLabel = false;
    for (const TorrentPatch &patch : delta.torrents)
        packLostItsLabel |= patch.hash == kPack && patch.has(TorrentField::Category) &&
                            patch.values.category.isEmpty();
    QVERIFY(packLostItsLabel);
    QVERIFY(!m_engine->features(ServerInfo{QStringLiteral("2.2.0"), {}}).categories);
}

void TestDelugeEngine::aLabelPluginFlipMarksTwoDeltas()
{
    // DECISIONS C43: the plugin off or on refiles every torrent; no one filed it.
    signIn();
    SyncDelta delta = poll(0);
    QVERIFY(!delta.filingReset); // the first reading of the plugin is no flip
    delta = poll(*delta.cursor);
    QVERIFY(!delta.filingReset);

    m_daemon->labelPlugin = false;
    delta = poll(*delta.cursor);
    QVERIFY(delta.filingReset);
    delta = poll(*delta.cursor);
    QVERIFY(delta.filingReset);
    delta = poll(*delta.cursor);
    QVERIFY(!delta.filingReset);

    m_daemon->labelPlugin = true;
    delta = poll(*delta.cursor);
    QVERIFY(delta.filingReset);
    delta = poll(*delta.cursor);
    QVERIFY(delta.filingReset);
    delta = poll(*delta.cursor);
    QVERIFY(!delta.filingReset);
}

void TestDelugeEngine::aPollWithoutADaemonSaysSo()
{
    signIn();
    m_daemon->connected = false;
    m_web->answer = [this](const QString &method, const QJsonArray &params) -> QJsonValue {
        if (method.startsWith(QLatin1String("core.")))
            qFatal("a core call with no daemon: deluge-web would never answer it");
        return m_daemon->answer(method, params);
    };
    Result<SyncDelta> result = wait(m_engine->sync(0));
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(!m_engine->daemonConnected());
    QVERIFY(m_web->callsOf(QStringLiteral("web.connect")).isEmpty()); // none stored

    // The server's own daemon, stored: connected again, at most every 10 s.
    m_engine->setDaemon(kHost);
    result = wait(m_engine->sync(0));
    QVERIFY(result.isError());
    QTRY_COMPARE(m_web->callsOf(QStringLiteral("web.connect")).size(), 1);
    result = wait(m_engine->sync(0));
    QTest::qWait(50);
    QCOMPARE(m_web->callsOf(QStringLiteral("web.connect")).size(), 1);
    m_nowMs += 11000;
    result = wait(m_engine->sync(0));
    QTRY_COMPARE(m_web->callsOf(QStringLiteral("web.connect")).size(), 2);

    m_web->answer = [this](const QString &method, const QJsonArray &params) {
        return m_daemon->answer(method, params);
    };
    m_daemon->connected = true;
    QVERIFY(poll(0).full);
    QVERIFY(m_engine->daemonConnected());
}

void TestDelugeEngine::lastActivityDoesNotWobble()
{
    signIn();
    const int cursor = *poll(0).cursor;
    // The stream transferred 0 s ago; a second later Deluge says 1 s ago: the same moment.
    QJsonObject stream = m_daemon->torrents.value(kStream).toObject();
    stream.insert(QStringLiteral("time_since_transfer"), 1);
    m_daemon->torrents.insert(kStream, stream);
    m_nowMs += 2000;
    SyncDelta delta = poll(cursor);
    for (const TorrentPatch &patch : delta.torrents)
        QVERIFY(!patch.has(TorrentField::LastActivity));
    // A minute later, idle since: that is news.
    m_nowMs += 60000;
    delta = poll(*delta.cursor);
    QCOMPARE(delta.torrents.size(), 1);
    QVERIFY(delta.torrents.first().has(TorrentField::LastActivity));
}

void TestDelugeEngine::anotherServerStartsOver()
{
    signIn();
    const int cursor = *poll(0).cursor;
    QVERIFY(!poll(cursor).full);
    QVERIFY(poll(cursor + 7).full); // a cursor the engine didn't hand out
    FakeServer other;
    other.setHandler(m_web->handler());
    m_engine->setBaseUrl(other.url());
    QVERIFY(poll(cursor).full);
}

// --- reads ----------------------------------------------------------------------------------

void TestDelugeEngine::stoppedAmongAndRunningGoByTheState()
{
    Result<QStringList> stopped = wait(m_engine->stoppedAmong({}));
    QVERIFY(stopped.isValid());
    QStringList sorted = stopped.value();
    sorted.sort();
    QCOMPARE(sorted, QStringList({kError, kSingle})); // the queued one is paused, not stopped
    const QList<Call> asked = m_web->callsOf(QStringLiteral("core.get_torrents_status"));
    QCOMPARE(asked.first().params.at(0).toObject(), QJsonObject());

    stopped = wait(m_engine->stoppedAmong({kPack, kSingle}));
    QCOMPARE(stopped.value(), QStringList({kSingle}));
    QCOMPARE(m_web->callsOf(QStringLiteral("core.get_torrents_status")).last().params.at(0),
             QJsonValue(QJsonObject{{QStringLiteral("id"), strings({kPack, kSingle})}}));

    const Result<QList<RunningTorrent>> running = wait(m_engine->running());
    QVERIFY(running.isValid());
    QHash<QString, bool> forced;
    for (const RunningTorrent &torrent : running.value())
        forced.insert(torrent.hash, torrent.forced);
    QCOMPARE(forced.size(), 4);
    QVERIFY(forced.value(kStream) && forced.value(kMagnet)); // auto_managed off
    QVERIFY(!forced.value(kPack) && !forced.value(kQueued));
}

void TestDelugeEngine::detailsAskForTheirKeys()
{
    signIn();
    const Result<TorrentProperties> properties = wait(m_engine->properties(kStream));
    QVERIFY(properties.isValid());
    QCOMPARE(properties.value().piecesNum, 96);
    Call call = m_web->calls.constLast();
    QCOMPARE(call.method, QStringLiteral("core.get_torrent_status"));
    QCOMPARE(call.params.at(0).toString(), kStream);
    QCOMPARE(stringsOf(call.params.at(1).toArray()), DT::propertiesFields());

    QCOMPARE(wait(m_engine->files(kPack)).value().size(), 3);
    QCOMPARE(stringsOf(m_web->calls.constLast().params.at(1).toArray()), DT::filesFields());
    QCOMPARE(wait(m_engine->trackers(kPack)).value().size(), 2);
    QCOMPARE(wait(m_engine->peers(kStream, 5)).value().peers.size(), 1);
    QCOMPARE(wait(m_engine->pieceStates(kPack)).value().size(), 4);
    QCOMPARE(wait(m_engine->torrents({kPack, kStream})).value().size(), 2);

    // A torrent Deluge hasn't: {} comes back, NotFound goes on.
    QCOMPARE(wait(m_engine->properties(QString(40, QLatin1Char('0')))).error().kind(),
             ApiError::Kind::NotFound);
    QCOMPARE(wait(m_engine->properties(QString())).error().kind(), ApiError::Kind::NotFound);
}

void TestDelugeEngine::categoriesAreTheLabels()
{
    signIn();
    const Result<QList<CategoryPatch>> labels = wait(m_engine->categories());
    QVERIFY(labels.isValid());
    QCOMPARE(labels.value().size(), 2);
    QCOMPARE(labels.value().at(0).name, QStringLiteral("empty"));
    QCOMPARE(labels.value().at(1).savePath,
             std::optional<QString>(QStringLiteral("/srv/deluge/fixtures-recording/done")));
    QCOMPARE(wait(m_engine->tags()).value(), QStringList());

    m_daemon->labelPlugin = false;
    QVERIFY(wait(m_engine->probe()).isValid());
    m_web->calls.clear();
    QVERIFY(wait(m_engine->categories()).value().isEmpty());
    QVERIFY(m_web->calls.isEmpty());
}

// --- actions --------------------------------------------------------------------------------

void TestDelugeEngine::actionsSpellDelugesWords()
{
    signIn();
    const QStringList two{kPack, kSingle};
    const QJsonArray ids = strings(two);
    QVERIFY(wait(m_engine->start(two)).isValid());
    QVERIFY(wait(m_engine->stop(two)).isValid());
    QVERIFY(wait(m_engine->recheck(two)).isValid());
    QVERIFY(wait(m_engine->reannounce(two)).isValid());
    QVERIFY(wait(m_engine->queueUp(two)).isValid());
    QVERIFY(wait(m_engine->queueDown(two)).isValid());
    QVERIFY(wait(m_engine->setLocation(two, QStringLiteral("/srv/elsewhere"))).isValid());
    QVERIFY(wait(m_engine->setSuperSeeding(two, true)).isValid());
    QVERIFY(wait(m_engine->rename(kPack, QStringLiteral("Shown Name"))).isValid());
    QVERIFY(wait(m_engine->remove(two, true)).isValid());
    QVERIFY(wait(m_engine->startAll()).isValid());
    QVERIFY(wait(m_engine->stopAll()).isValid());
    QCOMPARE(m_web->methods(),
             QStringList({"core.resume_torrents",
                          "core.pause_torrents",
                          "core.force_recheck",
                          "core.force_reannounce",
                          "core.queue_up",
                          "core.queue_down",
                          "core.move_storage",
                          "core.set_torrent_options",
                          "core.set_torrent_options",
                          "core.remove_torrents",
                          "core.resume_torrents",
                          "core.pause_torrents"}));
    const QList<Call> &c = m_web->calls;
    for (int i = 0; i < 6; ++i)
        QCOMPARE(c.at(i).params, QJsonArray({QJsonValue(ids)}));
    QCOMPARE(c.at(6).params, QJsonArray({ids, QStringLiteral("/srv/elsewhere")}));
    QCOMPARE(c.at(7).params,
             QJsonArray({ids, QJsonObject{{QStringLiteral("super_seeding"), true}}}));
    QCOMPARE(c.at(8).params,
             QJsonArray({strings({kPack}),
                         QJsonObject{{QStringLiteral("name"), QStringLiteral("Shown Name")}}}));
    QCOMPARE(c.at(9).params, QJsonArray({ids, true}));
    QCOMPARE(c.at(10).params, QJsonArray()); // no ids: every torrent
    QCOMPARE(wait(m_engine->rename(kPack, QStringLiteral("  "))).error().kind(),
             ApiError::Kind::Conflict);
}

void TestDelugeEngine::noTorrentsNeverReachTheWire()
{
    signIn();
    QVERIFY(wait(m_engine->start({})).isValid());
    QVERIFY(wait(m_engine->stop({})).isValid());
    QVERIFY(wait(m_engine->remove({}, true)).isValid());
    QVERIFY(wait(m_engine->setLocation({}, QStringLiteral("/x"))).isValid());
    QVERIFY(wait(m_engine->setForceStart({}, true)).isValid());
    QVERIFY(wait(m_engine->setCategory({}, QStringLiteral("x"))).isValid());
    QVERIFY(wait(m_engine->setFilePriority(kPack, {}, FilePriority::Skip)).isValid());
    QVERIFY(wait(m_engine->toggleSequentialDownload({})).isValid());
    QVERIFY(wait(m_engine->setShareLimits({}, RatioLimit::of(1), {}, {}, {})).isValid());
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::forceStartTakesItOutOfTheQueue()
{
    signIn();
    QVERIFY(wait(m_engine->setForceStart({kPack}, true)).isValid());
    QCOMPARE(m_web->methods(), QStringList({"core.set_torrent_options", "core.resume_torrents"}));
    QCOMPARE(m_web->calls.at(0).params.at(1).toObject(), QJsonObject({{"auto_managed", false}}));
    m_web->calls.clear();
    QVERIFY(wait(m_engine->setForceStart({kPack}, false)).isValid());
    QCOMPARE(m_web->methods(), QStringList({"core.set_torrent_options"}));
    QCOMPARE(m_web->calls.at(0).params.at(1).toObject(), QJsonObject({{"auto_managed", true}}));
}

void TestDelugeEngine::filePrioritiesAreReadAndSentWhole()
{
    signIn();
    // Recorded: high, low, skip (7, 1, 0). The low one skipped, the skipped one high.
    QVERIFY(wait(m_engine->setFilePriority(kPack, {1}, FilePriority::Skip)).isValid());
    QVERIFY(wait(m_engine->setFilePriority(kPack, {2}, FilePriority::Maximal)).isValid());
    const QList<Call> sets = m_web->callsOf(QStringLiteral("core.set_torrent_file_priorities"));
    QCOMPARE(sets.size(), 2);
    QCOMPARE(sets.at(0).params, QJsonArray({kPack, QJsonArray{7, 0, 0}}));
    QCOMPARE(sets.at(1).params, QJsonArray({kPack, QJsonArray{7, 1, 7}}));
    QCOMPARE(wait(m_engine->setFilePriority(kPack, {9}, FilePriority::Low)).error().kind(),
             ApiError::Kind::NotFound);
}

void TestDelugeEngine::renamesFindTheirFileAndForgetTheRoot()
{
    signIn();
    const int cursor = *poll(0).cursor;
    m_web->calls.clear();
    QVERIFY(wait(m_engine->renameFile(kPack,
                                      QStringLiteral("Fixture Pack/notes.txt"),
                                      QStringLiteral("Fixture Pack/readme.txt")))
                .isValid());
    QCOMPARE(m_web->callsOf(QStringLiteral("core.rename_files")).first().params,
             QJsonArray({kPack,
                         QJsonArray{QJsonValue(
                             QJsonArray{1, QStringLiteral("Fixture Pack/readme.txt")})}}));
    QVERIFY(wait(m_engine->renameFolder(kPack,
                                        QStringLiteral("Fixture Pack/disc 1"),
                                        QStringLiteral("Fixture Pack/disc one")))
                .isValid());
    QCOMPARE(m_web->callsOf(QStringLiteral("core.rename_folder")).first().params,
             QJsonArray({kPack,
                         QStringLiteral("Fixture Pack/disc 1"),
                         QStringLiteral("Fixture Pack/disc one")}));
    QCOMPARE(wait(m_engine->renameFile(kPack, QStringLiteral("nope"), QStringLiteral("x")))
                 .error()
                 .kind(),
             ApiError::Kind::NotFound);
    // Where its content starts is asked again at the next poll.
    m_web->calls.clear();
    poll(cursor);
    const QList<Call> roots = m_web->callsOf(QStringLiteral("core.get_torrents_status"));
    QCOMPARE(roots.size(), 1);
    QCOMPARE(roots.first().params.at(0).toObject().value(QStringLiteral("id")).toArray(),
             strings({kPack}));
}

void TestDelugeEngine::limitsLeaveInKibibytes()
{
    signIn();
    QVERIFY(wait(m_engine->setDownloadLimit({kPack}, 102400)).isValid());
    QVERIFY(wait(m_engine->setUploadLimit({kPack}, 0)).isValid());
    QCOMPARE(m_web->calls.at(0).params.at(1).toObject(),
             QJsonObject({{"max_download_speed", 100.0}}));
    QCOMPARE(m_web->calls.at(1).params.at(1).toObject(), QJsonObject({{"max_upload_speed", -1.0}}));
}

void TestDelugeEngine::shareLimitsAreARatioThenStopOrRemove()
{
    signIn();
    QVERIFY(wait(m_engine->setShareLimits({kPack},
                                          RatioLimit::of(1.5),
                                          MinutesLimit::global(),
                                          MinutesLimit::unlimited(),
                                          QStringLiteral("Remove")))
                .isValid());
    QCOMPARE(
        m_web->calls.constLast().params.at(1).toObject(),
        QJsonObject({{"stop_at_ratio", true}, {"stop_ratio", 1.5}, {"remove_at_ratio", true}}));
    QVERIFY(wait(m_engine->setShareLimits({kPack}, RatioLimit::unlimited(), {}, {}, std::nullopt))
                .isValid());
    QCOMPARE(m_web->calls.constLast().params.at(1).toObject(),
             QJsonObject({{"stop_at_ratio", false}}));

    // "The server's": read, then copied onto the torrent.
    m_web->calls.clear();
    QVERIFY(wait(m_engine->setShareLimits(
                     {kPack}, RatioLimit::global(), {}, {}, QStringLiteral("Default")))
                .isValid());
    QCOMPARE(m_web->methods(), QStringList({"core.get_config_values", "core.set_torrent_options"}));
    QCOMPARE(
        m_web->calls.at(1).params.at(1).toObject(),
        QJsonObject({{"stop_at_ratio", false}, {"stop_ratio", 2.0}, {"remove_at_ratio", false}}));

    // No time limits and no other action (K18): refused before anything is sent.
    m_web->calls.clear();
    QCOMPARE(
        wait(m_engine->setShareLimits({kPack}, {}, MinutesLimit::of(60), {}, {})).error().kind(),
        ApiError::Kind::NotFound);
    QCOMPARE(
        wait(m_engine->setShareLimits({kPack}, {}, {}, MinutesLimit::of(60), {})).error().kind(),
        ApiError::Kind::NotFound);
    QCOMPARE(
        wait(m_engine->setShareLimits({kPack}, {}, {}, {}, QStringLiteral("EnableSuperSeeding")))
            .error()
            .kind(),
        ApiError::Kind::NotFound);
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::globalLimitsAndPreferences()
{
    signIn();
    const Result<GlobalLimits> limits = wait(m_engine->globalLimits());
    QCOMPARE(limits.value().download, std::optional<qint64>(0));
    QVERIFY(
        wait(m_engine->setGlobalLimits(GlobalLimits{2048, std::nullopt, 4096, 4096})).isValid());
    // The alternative ones dropped (K20).
    QCOMPARE(m_web->calls.constLast().method, QStringLiteral("core.set_config"));
    QCOMPARE(m_web->calls.constLast().params,
             QJsonArray({QJsonObject{{"max_download_speed", 2.0}}}));
    m_web->calls.clear();
    QVERIFY(
        wait(m_engine->setGlobalLimits(GlobalLimits{std::nullopt, std::nullopt, 1, 1})).isValid());
    QVERIFY(m_web->calls.isEmpty());

    const Result<QVariantMap> prefs = wait(m_engine->sessionPreferences());
    QCOMPARE(prefs.value().value(QStringLiteral("listen_port")).toInt(), 26993);
    QCOMPARE(m_web->calls.constLast().method, QStringLiteral("core.get_config"));
    QCOMPARE(prefs.value().value(QStringLiteral("enabled_plugins")).toStringList(),
             QStringList({QStringLiteral("Label")}));
    QCOMPARE(prefs.value().value(QStringLiteral("max_active_seeding")).toInt(), 5);
    QCOMPARE(prefs.value().value(QStringLiteral("encryption")).toString(),
             QStringLiteral("enabled"));
    // Settings › Deluge's own keys go back under their names; the encryption both ways.
    QVERIFY(wait(m_engine->setSessionPreferences(
                     {{QStringLiteral("max_active_seeding"), 7},
                      {QStringLiteral("encryption"), QStringLiteral("forced")}}))
                .isValid());
    QCOMPARE(m_web->calls.constLast().method, QStringLiteral("core.set_config"));
    QCOMPARE(m_web->calls.constLast().params,
             QJsonArray({QJsonObject{
                 {"max_active_seeding", 7}, {"enc_in_policy", 0}, {"enc_out_policy", 0}}}));
    // The plugins are Deluge's own preferences' to turn on or off (K15).
    QCOMPARE(wait(m_engine->setSessionPreferences(
                      {{QStringLiteral("enabled_plugins"), QStringList{QStringLiteral("Label")}}}))
                 .error()
                 .kind(),
             ApiError::Kind::Conflict);
    QVERIFY(wait(m_engine->setSessionPreferences({{QStringLiteral("dht"), true}})).isValid());
    QCOMPARE(m_web->calls.constLast().params, QJsonArray({QJsonObject{{"dht", true}}}));
    m_web->calls.clear();
    QCOMPARE(
        wait(m_engine->setSessionPreferences({{QStringLiteral("alt_dl_limit"), 1}})).error().kind(),
        ApiError::Kind::Conflict);
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::labelsAreLowerCaseAndNeedThePlugin()
{
    signIn();
    QVERIFY(wait(m_engine->setCategory({kPack, kSingle}, QStringLiteral("Films"))).isValid());
    const QList<Call> sets = m_web->callsOf(QStringLiteral("label.set_torrent"));
    QCOMPARE(sets.size(), 2);
    QCOMPARE(sets.at(0).params, QJsonArray({kPack, QStringLiteral("films")}));
    QVERIFY(wait(m_engine->setCategory({kPack}, QString())).isValid()); // taken off
    QCOMPARE(m_web->calls.constLast().params, QJsonArray({kPack, QString()}));

    m_web->calls.clear();
    QVERIFY(wait(m_engine->createCategory(QStringLiteral("New"), QStringLiteral("/srv/new")))
                .isValid());
    QCOMPARE(m_web->methods(), QStringList({"label.add", "label.set_options"}));
    QCOMPARE(m_web->calls.at(0).params, QJsonArray({QStringLiteral("new")}));
    QCOMPARE(m_web->calls.at(1).params.at(1).toObject(),
             QJsonObject({{"apply_move_completed", true},
                          {"move_completed", true},
                          {"move_completed_path", "/srv/new"}}));
    QVERIFY(
        wait(m_engine->removeCategories({QStringLiteral("New"), QStringLiteral("old")})).isValid());
    QCOMPARE(m_web->callsOf(QStringLiteral("label.remove")).size(), 2);

    // Deluge's own check, before anything is sent (C25 K13: lower case shown before apply).
    m_web->calls.clear();
    QCOMPARE(wait(m_engine->setCategory({kPack}, QStringLiteral("two words"))).error().kind(),
             ApiError::Kind::Conflict);
    QCOMPARE(wait(m_engine->createCategory(QStringLiteral("a/b"))).error().kind(),
             ApiError::Kind::Conflict);
    QVERIFY(m_web->calls.isEmpty());

    m_daemon->labelPlugin = false;
    QVERIFY(wait(m_engine->probe()).isValid());
    m_web->calls.clear();
    QCOMPARE(wait(m_engine->setCategory({kPack}, QStringLiteral("x"))).error().kind(),
             ApiError::Kind::NotFound);
    QCOMPARE(wait(m_engine->createCategory(QStringLiteral("x"))).error().kind(),
             ApiError::Kind::NotFound);
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::trackersAreEditedWhole()
{
    signIn();
    const int cursor = *poll(0).cursor;
    m_web->calls.clear();
    QVERIFY(wait(m_engine->addTrackers(kPack, {QStringLiteral("http://new/announce")})).isValid());
    QCOMPARE(m_web->methods(),
             QStringList({"core.get_torrent_status", "core.set_torrent_trackers"}));
    const QJsonArray sent = m_web->calls.constLast().params.at(1).toArray();
    QCOMPARE(sent.size(), 3);
    QCOMPARE(sent.at(2).toObject(), QJsonObject({{"url", "http://new/announce"}, {"tier", 1}}));
    m_web->calls.clear();
    QVERIFY(wait(m_engine->removeTrackers(kPack, {QStringLiteral("udp://127.0.0.1:9/announce")}))
                .isValid());
    QCOMPARE(m_web->calls.constLast().params.at(1).toArray().size(), 1);
    // Nothing to change: nothing written.
    m_web->calls.clear();
    QVERIFY(wait(m_engine->removeTrackers(kPack, {QStringLiteral("http://not-there/")})).isValid());
    QCOMPARE(m_web->methods(), QStringList({"core.get_torrent_status"}));
    // The next poll reads the trackers again.
    m_web->calls.clear();
    poll(cursor);
    QVERIFY(
        stringsOf(m_web->callsOf(QStringLiteral("web.update_ui")).first().params.at(0).toArray())
            .contains(QStringLiteral("trackers")));
}

void TestDelugeEngine::togglesAreReadThenSet()
{
    signIn();
    // The single is sequential, the pack isn't: each gets the opposite.
    QVERIFY(wait(m_engine->toggleSequentialDownload({kPack, kSingle})).isValid());
    const QList<Call> sets = m_web->callsOf(QStringLiteral("core.set_torrent_options"));
    QCOMPARE(sets.size(), 2);
    QCOMPARE(sets.at(0).params,
             QJsonArray({strings({kPack}), QJsonObject{{"sequential_download", true}}}));
    QCOMPARE(sets.at(1).params,
             QJsonArray({strings({kSingle}), QJsonObject{{"sequential_download", false}}}));
    m_web->calls.clear();
    QVERIFY(wait(m_engine->toggleFirstLastPiece({kPack})).isValid());
    QCOMPARE(stringsOf(m_web->calls.at(0).params.at(1).toArray()),
             QStringList({"prioritize_first_last"}));
    QCOMPARE(m_web->calls.at(1).params.at(1).toObject(),
             QJsonObject({{"prioritize_first_last_pieces", true}}));
}

void TestDelugeEngine::peersAreAddedNeverBanned()
{
    signIn();
    QVERIFY(wait(m_engine->addPeers(
                     {kPack}, {QStringLiteral("10.0.0.2:6881"), QStringLiteral("[::1]:51413")}))
                .isValid());
    const QList<Call> calls = m_web->callsOf(QStringLiteral("core.connect_peer"));
    QCOMPARE(calls.size(), 2);
    QCOMPARE(calls.at(0).params, QJsonArray({kPack, QStringLiteral("10.0.0.2"), 6881}));
    QCOMPARE(calls.at(1).params, QJsonArray({kPack, QStringLiteral("::1"), 51413}));
    m_web->calls.clear();
    QCOMPARE(wait(m_engine->addPeers({kPack}, {QStringLiteral("no-port")})).error().kind(),
             ApiError::Kind::Conflict);
    QCOMPARE(wait(m_engine->addPeers({kPack}, {QStringLiteral("::1:80")})).error().kind(),
             ApiError::Kind::Conflict);
    QCOMPARE(wait(m_engine->banPeers({QStringLiteral("10.0.0.2:6881")})).error().kind(),
             ApiError::Kind::NotFound);
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::addSendsOptionsThenTheLabel()
{
    signIn();
    m_web->answer = [this](const QString &method, const QJsonArray &params) -> QJsonValue {
        if (method == QLatin1String("core.add_torrent_file"))
            return result("core_add_torrent_file.json");
        if (method == QLatin1String("core.add_torrent_magnet"))
            return result("core_add_torrent_magnet.json");
        return m_daemon->answer(method, params);
    };
    AddOptions options;
    options.savePath = QStringLiteral("/srv/in");
    options.category = QStringLiteral("Films");
    options.tags = {QStringLiteral("ignored")};
    options.stopped = true;
    options.skipChecking = true;
    options.rename = QStringLiteral("Better Name");
    options.contentLayout = ContentLayout::NoSubfolder; // none on Deluge (K27): dropped
    options.sequentialDownload = true;
    options.firstLastPiece = false;
    options.dlLimit = 2048;
    options.filePriorities = {FilePriority::High, FilePriority::Skip, FilePriority::Low};
    const Result<AddResult> added =
        wait(m_engine->addFile(QStringLiteral("pack.torrent"), QByteArray("d4:infodee"), options));
    QVERIFY2(added.isValid(), qPrintable(added.error().toString()));
    QCOMPARE(added.value().addedIds, QStringList({kPack}));
    QVERIFY(!added.value().refused);
    QCOMPARE(m_web->methods(), QStringList({"core.add_torrent_file", "label.set_torrent"}));
    const QJsonArray params = m_web->calls.at(0).params;
    QCOMPARE(params.at(0).toString(), QStringLiteral("pack.torrent"));
    QCOMPARE(params.at(1).toString(), QString::fromLatin1(QByteArray("d4:infodee").toBase64()));
    QCOMPARE(params.at(2).toObject(),
             QJsonObject({{"download_location", "/srv/in"},
                          {"add_paused", true},
                          {"seed_mode", true},
                          {"name", "Better Name"},
                          {"sequential_download", true},
                          {"prioritize_first_last_pieces", false},
                          {"max_download_speed", 2.0},
                          {"file_priorities", QJsonArray{7, 0, 1}}}));
    QCOMPARE(m_web->calls.at(1).params, QJsonArray({kPack, QStringLiteral("films")}));

    // Magnets and links each by their own call, under the same options.
    m_web->calls.clear();
    QVERIFY(wait(m_engine->addUrls({QStringLiteral("magnet:?xt=urn:btih:%1").arg(kMagnet),
                                    QStringLiteral("https://example.org/x.torrent")},
                                   AddOptions()))
                .isValid());
    QCOMPARE(m_web->methods(), QStringList({"core.add_torrent_magnet", "core.add_torrent_url"}));
    QCOMPARE(m_web->calls.at(1).params,
             QJsonArray({QStringLiteral("https://example.org/x.torrent"), QJsonObject()}));

    // A label Deluge can't keep: refused before anything is sent.
    m_web->calls.clear();
    options.category = QStringLiteral("two words");
    QCOMPARE(wait(m_engine->addFile(QStringLiteral("x"), QByteArray("x"), options)).error().kind(),
             ApiError::Kind::Conflict);
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::addSaysWhenEverythingWasThereAlready()
{
    signIn();
    m_web->answer = [](const QString &, const QJsonArray &) -> QJsonValue {
        return errorOf("error_add_duplicate.json");
    };
    Result<AddResult> added = wait(m_engine->addFile(QStringLiteral("p"), QByteArray("x"), {}));
    QVERIFY(added.isValid());
    QVERIFY(added.value().refused);
    QVERIFY(added.value().addedIds.isEmpty());

    // Any other refusal fails the add, in Deluge's words.
    m_web->answer = [](const QString &, const QJsonArray &) -> QJsonValue {
        return errorOf("error_add_v2_magnet.json");
    };
    added = wait(m_engine->addUrls({QStringLiteral("magnet:?xt=urn:btmh:1220ab")}, {}));
    QCOMPARE(added.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(added.error().message().contains(QLatin1String("invalid magnet info")));
}

void TestDelugeEngine::removeTakesTorrentsAlreadyGone()
{
    signIn();
    m_web->answer = [](const QString &, const QJsonArray &) -> QJsonValue {
        return result("core_remove_torrents_missing.json");
    };
    QVERIFY(wait(m_engine->remove({QString(40, QLatin1Char('0'))}, false)).isValid());
    m_web->answer = [](const QString &, const QJsonArray &) -> QJsonValue {
        return QJsonArray{QJsonArray{kPack, QStringLiteral("Permission denied")}};
    };
    const Result<Done> result = wait(m_engine->remove({kPack}, true));
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(result.error().message().contains(QLatin1String("Permission denied")));
}

void TestDelugeEngine::whatDelugeLacksIsNotSupported()
{
    signIn();
    const auto notSupported = [this](auto *reply) {
        const auto result = wait(reply);
        QCOMPARE(result.error().kind(), ApiError::Kind::NotFound);
        QVERIFY(result.error().message().endsWith(QLatin1String("not supported by this engine")));
    };
    notSupported(m_engine->addTags({kPack}, {QStringLiteral("t")}));
    notSupported(m_engine->removeTags({kPack}, {QStringLiteral("t")}));
    notSupported(m_engine->createTags({QStringLiteral("t")}));
    notSupported(m_engine->deleteTags({QStringLiteral("t")}));
    notSupported(m_engine->toggleAltSpeed());
    notSupported(m_engine->setAltSpeed(true));
    notSupported(m_engine->webSeeds(kPack));
    notSupported(m_engine->addWebSeeds(kPack, {QStringLiteral("http://w/")}));
    notSupported(m_engine->removeWebSeeds(kPack, {QStringLiteral("http://w/")}));
    notSupported(m_engine->setAutoManagement({kPack}, true));
    notSupported(m_engine->log(-1, false));
    QVERIFY(m_web->calls.isEmpty());
}

// --- moves and room -------------------------------------------------------------------------

void TestDelugeEngine::freeSpaceIsReadOnTheNearestFolderThere()
{
    signIn();
    m_web->answer = [](const QString &method, const QJsonArray &params) -> QJsonValue {
        if (method != QLatin1String("core.get_free_space"))
            return QJsonValue();
        return params.at(0).toString() == QLatin1String("/srv")
                   ? result("core_get_free_space.json")
                   : result("core_get_free_space_missing.json");
    };
    const Result<FreeSpaceAt> at = wait(m_engine->freeSpace(QStringLiteral("/srv/new/deeper/")));
    QVERIFY(at.isValid());
    QCOMPARE(at.value().path, QStringLiteral("/srv"));
    QCOMPARE(at.value().bytesFree, 400000000000);
    QCOMPARE(at.value().bytesTotal, 0); // Deluge doesn't say
    QCOMPARE(m_web->callsOf(QStringLiteral("core.get_free_space")).size(), 3);
    QCOMPARE(wait(m_engine->freeSpace(QStringLiteral("relative"))).error().kind(),
             ApiError::Kind::Conflict);
}

void TestDelugeEngine::aFolderIsListedOrMissing()
{
    signIn();
    m_web->answer = [](const QString &method, const QJsonArray &params) -> QJsonValue {
        const QString path = params.at(0).toString();
        if (method == QLatin1String("core.glob"))
            return path.startsWith(QLatin1String("/srv/data")) ? result("core_glob.json")
                                                               : QJsonArray();
        if (method == QLatin1String("core.get_path_size"))
            return path == QLatin1String("/srv/empty") ? 0 : -1;
        return QJsonValue();
    };
    ServerFolder folder = wait(m_engine->folderContent(QStringLiteral("/srv/data/"))).value();
    QCOMPARE(folder.state, ServerFolder::State::Listed);
    QVERIFY(folder.has(QStringLiteral("Fixture Pack")));
    QCOMPARE(m_web->calls.at(0).params, QJsonArray({QStringLiteral("/srv/data/*")}));

    folder = wait(m_engine->folderContent(QStringLiteral("/srv/empty"))).value();
    QCOMPARE(folder.state, ServerFolder::State::Listed);
    QVERIFY(folder.names.isEmpty());
    folder = wait(m_engine->folderContent(QStringLiteral("/srv/gone"))).value();
    QCOMPARE(folder.state, ServerFolder::State::Missing);

    // A folder whose name holds glob characters is matched as it is.
    m_web->calls.clear();
    wait(m_engine->folderContent(QStringLiteral("/srv/[2024] what?*")));
    QCOMPARE(m_web->calls.at(0).params, QJsonArray({QStringLiteral("/srv/[[]2024] what[?][*]/*")}));
}

// --- a magnet's files before the add (K26) --------------------------------------------------

void TestDelugeEngine::aPrefetchRunsOnceWhileThePollsAsk()
{
    signIn();
    const QByteArray info =
        "d6:lengthi12e4:name5:a.bin12:piece lengthi16384e6:pieces20:xxxxxxxxxxxxxxxxxxxxe";
    const QString hash =
        QString::fromLatin1(QCryptographicHash::hash(info, QCryptographicHash::Sha1).toHex());
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1&dn=a.bin").arg(hash);
    m_web->answer = [this, hash, info](const QString &method, const QJsonArray &) -> QJsonValue {
        if (method == QLatin1String("core.prefetch_magnet_metadata"))
            m_server->hold = true; // a swarm that takes its time
        return QJsonArray{hash, QString::fromLatin1(info.toBase64())};
    };
    Result<FetchedMetadata> fetched = wait(m_engine->fetchMetadata(magnet));
    QVERIFY(fetched.isValid());
    QVERIFY(!fetched.value().ready);
    QTRY_COMPARE(m_server->heldCount(), 1);
    const Call call = m_web->callsOf(QStringLiteral("core.prefetch_magnet_metadata")).first();
    QCOMPARE(call.params, QJsonArray({magnet, DelugeEngine::kPrefetchSeconds}));

    // Asked again while the call runs: still looking, and no second call.
    fetched = wait(m_engine->fetchMetadata(magnet));
    QVERIFY(!fetched.value().ready);
    QCOMPARE(m_web->callsOf(QStringLiteral("core.prefetch_magnet_metadata")).size(), 1);

    m_server->release();
    fetched = fetchUntil(magnet, [](const Result<FetchedMetadata> &answer) {
        return answer.isValid() && answer.value().ready;
    });
    QVERIFY(fetched.value().metadata);
    QCOMPARE(fetched.value().metadata->name, QStringLiteral("a.bin"));
    QCOMPARE(fetched.value().metadata->files.size(), 1);
    QCOMPARE(fetched.value().metadata->infoHashV1, hash);
    QCOMPARE(m_web->callsOf(QStringLiteral("core.prefetch_magnet_metadata")).size(), 1);
}

void TestDelugeEngine::anEmptyPrefetchIsAskedAgain()
{
    signIn();
    const QString magnet =
        QStringLiteral("magnet:?xt=urn:btih:%1").arg(QString(40, QLatin1Char('a')));
    m_web->answer = [](const QString &, const QJsonArray &) -> QJsonValue {
        return result("core_prefetch_empty.json"); // its wait passed with nothing
    };
    QVERIFY(!wait(m_engine->fetchMetadata(magnet)).value().ready);
    QTRY_VERIFY(m_web->callsOf(QStringLiteral("core.prefetch_magnet_metadata")).size() == 1);
    fetchUntil(magnet, [this](const Result<FetchedMetadata> &) {
        return m_web->callsOf(QStringLiteral("core.prefetch_magnet_metadata")).size() == 2;
    });
    QCOMPARE(m_web->callsOf(QStringLiteral("core.prefetch_magnet_metadata")).size(), 2);
}

void TestDelugeEngine::aRefusedMagnetEndsTheLookup()
{
    signIn();
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:%1").arg(kPack);
    m_web->answer = [](const QString &, const QJsonArray &) -> QJsonValue {
        return errorOf(4,
                       QStringLiteral("Failure: [Failure instance: Traceback (failure with no "
                                      "frames): <class 'deluge.error.WrappedException'>: torrent "
                                      "already exists in session [libtorrent:19]\n]"));
    };
    QVERIFY(!wait(m_engine->fetchMetadata(magnet)).value().ready);
    Result<FetchedMetadata> fetched;
    fetched =
        fetchUntil(magnet, [](const Result<FetchedMetadata> &answer) { return answer.isError(); });
    // A refusal of the source: the lookup goes back to today's sheet (MetadataLookup: Failed).
    QCOMPARE(fetched.error().kind(), ApiError::Kind::Parse);
    QVERIFY(fetched.error().message().contains(QLatin1String("already exists in session")));

    // A v2-only magnet never leaves (K25).
    m_web->calls.clear();
    fetched = wait(m_engine->fetchMetadata(
        QStringLiteral("magnet:?xt=urn:btmh:1220%1").arg(QString(64, QLatin1Char('b')))));
    QCOMPARE(fetched.error().kind(), ApiError::Kind::Parse);
    QVERIFY(m_web->calls.isEmpty());
}

void TestDelugeEngine::linksAreNotPrefetched()
{
    signIn();
    const Result<FetchedMetadata> fetched =
        wait(m_engine->fetchMetadata(QStringLiteral("https://example.org/x.torrent")));
    // NotFound: MetadataLookup's "unsupported", today's sheet.
    QCOMPARE(fetched.error().kind(), ApiError::Kind::NotFound);
    QVERIFY(m_web->calls.isEmpty());
}

QTEST_GUILESS_MAIN(TestDelugeEngine)
#include "tst_delugeengine.moc"
