// TransmissionEngine and TransmissionRpc (#85, E4): Transmission 4.1 behind EngineClient,
// against a small fake daemon of the test's own (FakeHttp). What leaves is checked request by
// request against rpc-spec 4.1.3 (method names and params); what comes back is the daemon's
// recorded answers (tests/fixtures/transmission/). The session id's 409, Basic auth, the
// refusal of a daemon older than 4.1, the poll's recently_active and its 50 s gap, and every
// action's request.

#include "FakeTransmission.hpp"

#include "core/engines/transmission/TransmissionEngine.hpp"
#include "core/engines/transmission/TransmissionRpc.hpp"
#include "core/engines/transmission/TransmissionTranslate.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTest>

#ifndef OMASEED_FIXTURES_DIR
#error "OMASEED_FIXTURES_DIR must be defined by CMake"
#endif

using namespace fakehttp;
using namespace faketransmission;
using namespace omaseed::core;
namespace TT = omaseed::core::TransmissionTranslate;

namespace {

const QString kPack = QStringLiteral("69d539c673763be3e41ba688df820be7657f1c91");
const QString kSingle = QStringLiteral("2b86112ae20c6ca831cbfb3e4333c8acc6d21088");
const QString kError = QStringLiteral("475ad2787551903b6374cb9358f285cbe6fd8c21");
const QString kMagnet = QStringLiteral("a4f76fd25afb6b9952fa7709154af943f91a793f");

QJsonObject errorOf(const char *fixtureName)
{
    return {{QStringLiteral("__error"), fixture(fixtureName).value(QStringLiteral("error"))}};
}

QJsonArray strings(const QStringList &list)
{
    return QJsonArray::fromStringList(list);
}

} // namespace

class TestTransmissionEngine : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    // the wire
    void sessionIdIsFetchedAndKept();
    void anExpiredSessionIdIsReplacedOnce();
    void aSessionIdRefusedTwiceIsAConflict();
    void basicAuthGoesWithEveryRequest();
    void aRefusedLoginIsNotTriedAgain();
    void endpointFollowsTheAddressGiven();
    void jsonRpcErrorsBecomeApiErrors();
    void aLegacyAnswerIsAParseError();
    void readsAreRetriedMutationsAreNot();

    // what the server is
    void probeReadsVersionAndRpcVersion();
    void probeRefusesADaemonOlderThan41();
    void featuresAreTransmissions();

    // the poll
    void firstPollIsFullThenRecentlyActive();
    void aGapOverFiftySecondsAsksForEverything();
    void aCursorNotHandedOutIsFull();
    void anotherServerStartsOver();

    // reads
    void stoppedAmongAndRunningGoByStatus();
    void detailsAskForTheirFields();
    void tagsAreEveryLabelOnATorrent();

    // actions
    void actionsSpellTransmissionsWords();
    void noTorrentsNeverReachTheWire();
    void filePrioritiesBecomeWantedAndPriorityLists();
    void limitsLeaveInKilobytesAndModes();
    void turtleAndGlobalLimits();
    void labelsAreReadChangedAndWrittenWhole();
    void renamingReadsTheNameAndStaysInPlace();
    void trackersAreEditedWhole();
    void sequentialIsReadThenSet();
    void addSendsTorrentAddThenLimits();
    void whatTransmissionLacksIsNotSupported();
    void thePortIsTestedByTransmission();

    // moves (board 47 §E, C25 K22, K23)
    void aMoveIsBusyUntilItsCopyEnds();
    void aMoveGivenUpIsNoLongerBusy();
    void freeSpaceIsReadOnTheNearestFolderThere();

private:
    template<class T>
    Result<T> wait(Reply<T> *reply)
    {
        return await(this, reply);
    }

    std::unique_ptr<FakeServer> m_server;
    std::unique_ptr<FakeTransmission> m_daemon;
    std::unique_ptr<TransmissionEngine> m_engine;
};

void TestTransmissionEngine::init()
{
    m_server = std::make_unique<FakeServer>();
    m_daemon = std::make_unique<FakeTransmission>();
    m_server->setHandler(m_daemon->handler());
    m_engine = std::make_unique<TransmissionEngine>();
    m_engine->setBaseUrl(m_server->url());
    m_engine->setTimeout(std::chrono::seconds(5));
    m_engine->rpc()->setRetryBackoff(std::chrono::milliseconds(10));
}

void TestTransmissionEngine::cleanup()
{
    m_engine.reset();
    m_daemon.reset();
    m_server.reset();
}

// --- the wire -------------------------------------------------------------------------------

void TestTransmissionEngine::sessionIdIsFetchedAndKept()
{
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    // First POST answered 409 with the id, the same POST again with it.
    QCOMPARE(m_server->requests.size(), 2);
    QVERIFY(m_server->requests.at(0).header("X-Transmission-Session-Id").isEmpty());
    QCOMPARE(m_server->requests.at(1).header("X-Transmission-Session-Id"), QByteArray("s3ss10n"));
    QCOMPARE(m_server->requests.at(0).body, m_server->requests.at(1).body);
    QCOMPARE(m_server->requests.at(1).method, QByteArray("POST"));
    QCOMPARE(m_server->requests.at(1).target, QByteArray("/transmission/rpc"));
    QVERIFY(m_server->requests.at(1).header("Content-Type").startsWith("application/json"));
    QCOMPARE(m_engine->rpc()->sessionId(), QByteArray("s3ss10n"));
    QCOMPARE(m_engine->rpc()->rpcVersionHeader(), QStringLiteral("6.0.1"));
    // From then on the id goes with the first try.
    QVERIFY(wait(m_engine->stop({kPack})).isValid());
    QCOMPARE(m_server->requests.size(), 3);
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_start", "torrent_stop"}));
}

void TestTransmissionEngine::anExpiredSessionIdIsReplacedOnce()
{
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    m_daemon->expireNext = 1;
    QVERIFY(wait(m_engine->stop({kPack})).isValid());
    QCOMPARE(m_server->requests.size(), 4);
    QCOMPARE(m_engine->rpc()->sessionId(), QByteArray("s3ss10nx"));
}

void TestTransmissionEngine::aSessionIdRefusedTwiceIsAConflict()
{
    m_daemon->alwaysConflict = true;
    const Result<Done> result = wait(m_engine->start({kPack}));
    QVERIFY(result.isError());
    QCOMPARE(result.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(result.error().httpStatus(), 409);
    QCOMPARE(m_server->requests.size(), 2); // one resend, never a loop
}

void TestTransmissionEngine::basicAuthGoesWithEveryRequest()
{
    m_daemon->login = "seed:b0x p@ss";
    m_engine->setCredentials(QStringLiteral("seed"), QStringLiteral("b0x p@ss"));
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    for (const Recorded &request : std::as_const(m_server->requests))
        QCOMPARE(request.header("Authorization"),
                 QByteArray("Basic ") + QByteArray("seed:b0x p@ss").toBase64());
    QVERIFY(!m_engine->hasApiKey());
    m_engine->setApiKey(QStringLiteral("ignored")); // Transmission has no keys
    QVERIFY(!m_engine->hasApiKey());
}

void TestTransmissionEngine::aRefusedLoginIsNotTriedAgain()
{
    m_daemon->login = "seed:right";
    // No login at all: told one is needed.
    Result<Done> result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(result.error().httpStatus(), 401);

    m_engine->setCredentials(QStringLiteral("seed"), QStringLiteral("wrong"));
    result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    const qsizetype sent = m_server->requests.size();
    // Refused once: not sent again by itself (Transmission bans after enough misses).
    result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Unauthorized);
    QCOMPARE(m_server->requests.size(), sent);
    // A retry the user asked for goes through.
    m_engine->allowLoginAgain();
    QVERIFY(wait(m_engine->start({kPack})).isError());
    QCOMPARE(m_server->requests.size(), sent + 1);
    // So does a new login.
    m_engine->setCredentials(QStringLiteral("seed"), QStringLiteral("right"));
    QVERIFY(wait(m_engine->start({kPack})).isValid());
}

void TestTransmissionEngine::endpointFollowsTheAddressGiven()
{
    const QUrl base = m_server->url();
    QCOMPARE(m_engine->rpc()->endpoint().path(), QStringLiteral("/transmission/rpc"));
    // The web UI's address (K6) reaches the same endpoint.
    m_engine->setBaseUrl(base.resolved(QUrl(QStringLiteral("/transmission/web/"))));
    QVERIFY(wait(m_engine->start({kPack})).isValid());
    QCOMPARE(m_server->requests.constLast().target, QByteArray("/transmission/rpc"));
    QCOMPARE(m_engine->reachableUrl(base).path(), QStringLiteral("/transmission/rpc"));
    // No address: a network error, nothing sent.
    m_engine->setBaseUrl(QUrl());
    const Result<Done> none = wait(m_engine->start({kPack}));
    QCOMPARE(none.error().kind(), ApiError::Kind::Network);
}

void TestTransmissionEngine::jsonRpcErrorsBecomeApiErrors()
{
    m_daemon->answer = [](const QString &method, const QJsonObject &) -> QJsonObject {
        if (method == QLatin1String("torrent_set_location"))
            return errorOf("error_not_absolute.json");
        if (method == QLatin1String("torrent_start"))
            return errorOf("error_method_not_found.json");
        return {};
    };
    const Result<Done> notAbsolute =
        wait(m_engine->setLocation({kPack}, QStringLiteral("relative/path")));
    QCOMPARE(notAbsolute.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(notAbsolute.error().message(),
             QStringLiteral("torrent_set_location: new location path is not absolute"));
    const Result<Done> unknown = wait(m_engine->start({kPack}));
    QCOMPARE(unknown.error().kind(), ApiError::Kind::NotFound);
    QVERIFY(unknown.error().message().contains(QStringLiteral("Method not found")));
}

void TestTransmissionEngine::aLegacyAnswerIsAParseError()
{
    m_daemon->legacy = true;
    const Result<Done> result = wait(m_engine->start({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Parse);
    QVERIFY(result.error().message().contains(QStringLiteral("JSON-RPC 2.0")));
}

void TestTransmissionEngine::readsAreRetriedMutationsAreNot()
{
    int failures = 0;
    m_server->setHandler([this, &failures](const Recorded &request, int index) {
        if (failures < 1 && request.header("X-Transmission-Session-Id") == "s3ss10n") {
            ++failures;
            return httpResponse(503, "Service Unavailable", "busy");
        }
        return m_daemon->handler()(request, index);
    });
    m_daemon->answer = [](const QString &, const QJsonObject &) {
        return QJsonObject{{QStringLiteral("torrents"), QJsonArray()}};
    };
    // A read: the 503 is tried again.
    QVERIFY(wait(m_engine->stoppedAmong({kPack})).isValid());
    QCOMPARE(failures, 1);
    // A mutation: a 5xx is said, not repeated (it may have happened).
    failures = 0;
    const Result<Done> result = wait(m_engine->stop({kPack}));
    QCOMPARE(result.error().kind(), ApiError::Kind::Server);
}

// --- what the server is ---------------------------------------------------------------------

void TestTransmissionEngine::probeReadsVersionAndRpcVersion()
{
    m_daemon->answer = [](const QString &, const QJsonObject &params) {
        const QJsonArray fields = params.value(QStringLiteral("fields")).toArray();
        Q_ASSERT(fields.contains(QStringLiteral("rpc_version_semver")));
        return QJsonObject{{QStringLiteral("version"), QStringLiteral("4.1.3 (838877323f)")},
                           {QStringLiteral("rpc_version_semver"), QStringLiteral("6.0.1")}};
    };
    const Result<ServerInfo> info = wait(m_engine->probe());
    QVERIFY2(info.isValid(), qPrintable(info.error().toString()));
    QCOMPARE(info.value().version, QStringLiteral("4.1.3"));
    QCOMPARE(info.value().apiVersion, QStringLiteral("6.0.1"));
    QCOMPARE(m_daemon->methods(), QStringList({"session_get"}));
}

void TestTransmissionEngine::probeRefusesADaemonOlderThan41()
{
    // 4.0: no RPC version in the 409, and its own envelope back.
    m_daemon->rpcVersion.clear();
    m_daemon->legacy = true;
    m_daemon->answer = [](const QString &, const QJsonObject &) {
        return QJsonObject{{QStringLiteral("version"), QStringLiteral("4.0.6 (38c164933e)")}};
    };
    Result<ServerInfo> info = wait(m_engine->probe());
    QCOMPARE(info.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(info.error().message(),
             QStringLiteral("Transmission 4.0.6 is too old: omaseed needs 4.1 or later"));

    // Something that isn't Transmission at all.
    m_server->setHandler([](const Recorded &, int) { return ok("{\"hello\":1}"); });
    info = wait(m_engine->probe());
    QCOMPARE(info.error().kind(), ApiError::Kind::Parse);
}

void TestTransmissionEngine::featuresAreTransmissions()
{
    const EngineFeatures f = m_engine->features({QStringLiteral("4.1.3"), QStringLiteral("6.0.1")});
    QVERIFY(f.tags);
    QVERIFY(!f.categories);
    QVERIFY(f.altSpeeds);
    QVERIFY(!f.engineLog);
}

// --- the poll -------------------------------------------------------------------------------

void TestTransmissionEngine::firstPollIsFullThenRecentlyActive()
{
    qint64 now = 1000;
    m_engine->setClock([&now]() { return now; });
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };

    Result<SyncDelta> first = wait(m_engine->sync(0));
    QVERIFY2(first.isValid(), qPrintable(first.error().toString()));
    QVERIFY(first.value().full);
    QCOMPARE(first.value().torrents.size(), 4);
    QCOMPARE(first.value().tags, QStringList({"fixtures", "pack"}));
    QCOMPARE(first.value().trackers.value(QStringLiteral("udp://127.0.0.1:9/announce")),
             QStringList({kPack}));
    QVERIFY(first.value().serverState.has_value());
    QCOMPARE(first.value().serverState->allTimeUpload, std::optional<qint64>(2098816));
    QCOMPARE(first.value().cursor, std::optional<int>(1));
    // One batch: every torrent (no ids), the stats, the session; no free space yet (the
    // session hadn't said where torrents go).
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get", "session_stats", "session_get"}));
    QVERIFY(m_daemon->calls.at(0).inBatch);
    QVERIFY(!m_daemon->calls.at(0).params.contains(QStringLiteral("ids")));
    QCOMPARE(m_daemon->calls.at(0).params.value(QStringLiteral("format")).toString(),
             QStringLiteral("table"));
    QVERIFY(m_daemon->calls.at(0)
                .params.value(QStringLiteral("fields"))
                .toArray()
                .contains(QStringLiteral("error_string")));
    QCOMPARE(m_server->requests.size(), 2); // the 409, then the batch

    // 10 s later: the recently active ones. The pack was removed (its id in `removed`); the
    // single one started.
    now += 10000;
    m_daemon->calls.clear();
    const qint64 packId = answers.byHash(kPack).value(QStringLiteral("id")).toInteger();
    answers.removed = QJsonArray{packId};
    answers.torrents.removeIf([](const QJsonObject &t) {
        return t.value(QStringLiteral("hash_string")).toString() != kSingle;
    });
    answers.byHash(kSingle).insert(QStringLiteral("status"), 4);
    const Result<SyncDelta> second = wait(m_engine->sync(1));
    QVERIFY(second.isValid());
    QVERIFY(!second.value().full);
    QCOMPARE(m_daemon->calls.at(0).params.value(QStringLiteral("ids")).toString(),
             QStringLiteral("recently_active"));
    QCOMPARE(m_daemon->methods(),
             QStringList({"torrent_get", "session_stats", "session_get", "free_space"}));
    QCOMPARE(m_daemon->calls.at(3).params.value(QStringLiteral("path")).toString(),
             QStringLiteral("/srv/transmission/downloads"));
    QCOMPARE(second.value().removedTorrents, QStringList({kPack}));
    QCOMPARE(second.value().removedTags, QStringList({"pack"})); // only the pack had it
    QCOMPARE(second.value().removedTrackers, QStringList({"udp://127.0.0.1:9/announce"}));
    QCOMPARE(second.value().torrents.size(), 1);
    const TorrentPatch moved = second.value().torrents.first();
    QCOMPARE(moved.hash, kSingle);
    QVERIFY(moved.has(TorrentField::Status));
    QVERIFY(!moved.has(TorrentField::Name)); // only what moved
    QCOMPARE(moved.values.status.phase, TorrentPhase::Downloading);
    QCOMPARE(second.value().serverState->freeSpaceOnDisk, std::optional<qint64>(400000000000));
    QCOMPARE(second.value().cursor, std::optional<int>(2));
}

void TestTransmissionEngine::aGapOverFiftySecondsAsksForEverything()
{
    qint64 now = 0;
    m_engine->setClock([&now]() { return now; });
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    QVERIFY(wait(m_engine->sync(0)).isValid());

    // 50 s on the dot is still covered by recently_active (60 s).
    now = TransmissionEngine::kFullListGapMs;
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->sync(1)).isValid());
    QCOMPARE(m_daemon->calls.at(0).params.value(QStringLiteral("ids")).toString(),
             QStringLiteral("recently_active"));

    // Past 50 s since the last answered poll: every torrent, diffed. The magnet went away in
    // the gap, and no `removed` would ever have said so.
    now += TransmissionEngine::kFullListGapMs + 1;
    m_daemon->calls.clear();
    answers.torrents.removeIf([](const QJsonObject &t) {
        return t.value(QStringLiteral("hash_string")).toString() == kMagnet;
    });
    const Result<SyncDelta> delta = wait(m_engine->sync(2));
    QVERIFY(delta.isValid());
    QVERIFY(!m_daemon->calls.at(0).params.contains(QStringLiteral("ids")));
    QVERIFY(!delta.value().full);
    QCOMPARE(delta.value().removedTorrents, QStringList({kMagnet}));
    QVERIFY(delta.value().torrents.isEmpty()); // nothing else moved
    QCOMPARE(delta.value().removedTrackers, QStringList());
}

void TestTransmissionEngine::aCursorNotHandedOutIsFull()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    QVERIFY(wait(m_engine->sync(0)).isValid());
    m_daemon->calls.clear();
    const Result<SyncDelta> delta = wait(m_engine->sync(42));
    QVERIFY(delta.value().full);
    QCOMPARE(delta.value().torrents.size(), 4);
    QVERIFY(!m_daemon->calls.at(0).params.contains(QStringLiteral("ids")));
}

void TestTransmissionEngine::anotherServerStartsOver()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    QVERIFY(wait(m_engine->sync(0)).isValid());
    FakeServer other;
    other.setHandler(m_daemon->handler());
    m_engine->setBaseUrl(other.url());
    QVERIFY(m_engine->rpc()->sessionId().isEmpty()); // that was the other server's
    m_daemon->calls.clear();
    // The models' cursor (1) means nothing here: everything, as a full delta.
    const Result<SyncDelta> delta = wait(m_engine->sync(1));
    QVERIFY(delta.value().full);
    QVERIFY(!m_daemon->calls.at(0).params.contains(QStringLiteral("ids")));
}

// --- reads ----------------------------------------------------------------------------------

void TestTransmissionEngine::stoppedAmongAndRunningGoByStatus()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    // The single one is stopped; the errored one is stopped too (a local error stops it).
    const Result<QStringList> stopped = wait(m_engine->stoppedAmong({}));
    QCOMPARE(stopped.value(), QStringList({kSingle, kError}));
    QVERIFY(!m_daemon->last().params.contains(QStringLiteral("ids"))); // every stopped torrent
    QCOMPARE(m_daemon->last().params.value(QStringLiteral("fields")).toArray(),
             QJsonArray({QStringLiteral("hash_string"), QStringLiteral("status")}));
    QVERIFY(wait(m_engine->stoppedAmong({kPack, kSingle})).isValid());
    QCOMPARE(m_daemon->last().params.value(QStringLiteral("ids")).toArray(),
             QJsonArray({kPack, kSingle}));

    const Result<QList<RunningTorrent>> running = wait(m_engine->running());
    QCOMPARE(running.value(),
             QList<RunningTorrent>({{kPack, false}, {kMagnet, false}})); // never "forced"
}

void TestTransmissionEngine::detailsAskForTheirFields()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    const Result<QList<ContentFile>> files = wait(m_engine->files(kPack));
    QCOMPARE(files.value().size(), 3);
    QCOMPARE(m_daemon->last().params.value(QStringLiteral("ids")).toArray(), QJsonArray({kPack}));
    QCOMPARE(m_daemon->last().params.value(QStringLiteral("fields")).toArray(),
             QJsonArray({QStringLiteral("files"), QStringLiteral("file_stats")}));
    QCOMPARE(wait(m_engine->trackers(kPack)).value().size(), 2);
    QCOMPARE(wait(m_engine->webSeeds(kPack)).value(), QStringList({"http://127.0.0.1:9/seed/"}));
    QCOMPARE(wait(m_engine->pieceStates(kPack)).value().size(), 4);
    QVERIFY(wait(m_engine->peers(kPack, 0)).value().full);
    QCOMPARE(wait(m_engine->properties(kPack)).value().name, QStringLiteral("Fixture Pack"));

    // No such torrent: NotFound.
    answers.torrents.clear();
    QCOMPARE(wait(m_engine->files(kPack)).error().kind(), ApiError::Kind::NotFound);
    // Categories: none, said as an empty list.
    QVERIFY(wait(m_engine->categories()).value().isEmpty());
}

void TestTransmissionEngine::tagsAreEveryLabelOnATorrent()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    QCOMPARE(wait(m_engine->tags()).value(), QStringList({"fixtures", "pack"}));
}

// --- actions --------------------------------------------------------------------------------

void TestTransmissionEngine::actionsSpellTransmissionsWords()
{
    const QStringList two{kPack, kSingle};
    const QJsonArray ids{kPack, kSingle};
    struct Case
    {
        std::function<Reply<Done> *()> act;
        QString method;
        QJsonObject params;
    };
    const QList<Case> cases{
        {[&]() { return m_engine->start(two); }, "torrent_start", {{"ids", ids}}},
        {[&]() { return m_engine->stop(two); }, "torrent_stop", {{"ids", ids}}},
        {[&]() { return m_engine->startAll(); }, "torrent_start", {}},
        {[&]() { return m_engine->stopAll(); }, "torrent_stop", {}},
        {[&]() { return m_engine->setForceStart(two, true); }, "torrent_start_now", {{"ids", ids}}},
        {[&]() { return m_engine->setForceStart(two, false); }, "torrent_start", {{"ids", ids}}},
        {[&]() { return m_engine->recheck(two); }, "torrent_verify", {{"ids", ids}}},
        {[&]() { return m_engine->reannounce(two); }, "torrent_reannounce", {{"ids", ids}}},
        {[&]() { return m_engine->queueUp(two); }, "queue_move_up", {{"ids", ids}}},
        {[&]() { return m_engine->queueDown(two); }, "queue_move_down", {{"ids", ids}}},
        {[&]() { return m_engine->remove(two, true); },
         "torrent_remove",
         {{"ids", ids}, {"delete_local_data", true}}},
        {[&]() { return m_engine->remove(two, false); },
         "torrent_remove",
         {{"ids", ids}, {"delete_local_data", false}}},
        {[&]() { return m_engine->setLocation(two, QStringLiteral("/srv/new")); },
         "torrent_set_location",
         {{"ids", ids}, {"location", "/srv/new"}, {"move", true}}},
        {[&]() {
             return m_engine->renameFile(kPack,
                                         QStringLiteral("Fixture Pack/notes.txt"),
                                         QStringLiteral("Fixture Pack/readme.txt"));
         },
         "torrent_rename_path",
         {{"ids", QJsonArray{kPack}}, {"path", "Fixture Pack/notes.txt"}, {"name", "readme.txt"}}},
        {[&]() {
             return m_engine->renameFolder(kPack,
                                           QStringLiteral("Fixture Pack/disc 1"),
                                           QStringLiteral("Fixture Pack/Disc One"));
         },
         "torrent_rename_path",
         {{"ids", QJsonArray{kPack}}, {"path", "Fixture Pack/disc 1"}, {"name", "Disc One"}}},
    };
    for (const Case &c : cases) {
        m_daemon->calls.clear();
        const Result<Done> result = wait(c.act());
        QVERIFY2(result.isValid(),
                 qPrintable(c.method + QLatin1Char(' ') + result.error().toString()));
        QCOMPARE(m_daemon->methods(), QStringList({c.method}));
        QCOMPARE(m_daemon->last().params, c.params);
        QVERIFY(!m_daemon->last().inBatch);
    }
}

void TestTransmissionEngine::noTorrentsNeverReachTheWire()
{
    // An empty `ids` is every torrent to Transmission: nothing may go out for none.
    QVERIFY(wait(m_engine->start({})).isValid());
    QVERIFY(wait(m_engine->stop({})).isValid());
    QVERIFY(wait(m_engine->remove({}, true)).isValid());
    QVERIFY(wait(m_engine->recheck({})).isValid());
    QVERIFY(wait(m_engine->setLocation({}, QStringLiteral("/x"))).isValid());
    QVERIFY(wait(m_engine->setDownloadLimit({}, 1000)).isValid());
    QVERIFY(wait(m_engine->setFilePriority(kPack, {}, FilePriority::Skip)).isValid());
    QVERIFY(wait(m_engine->addTags({}, {QStringLiteral("x")})).isValid());
    QVERIFY(wait(m_engine->toggleSequentialDownload({})).isValid());
    QCOMPARE(m_server->requests.size(), 0);
}

void TestTransmissionEngine::filePrioritiesBecomeWantedAndPriorityLists()
{
    const QJsonArray files{0, 2};
    const QList<std::pair<FilePriority, QJsonObject>> cases{
        {FilePriority::Skip, {{"files_unwanted", files}}},
        {FilePriority::Low, {{"files_wanted", files}, {"priority_low", files}}},
        {FilePriority::Normal, {{"files_wanted", files}, {"priority_normal", files}}},
        {FilePriority::High, {{"files_wanted", files}, {"priority_high", files}}},
        {FilePriority::Maximal, {{"files_wanted", files}, {"priority_high", files}}},
    };
    for (const auto &[priority, expected] : cases) {
        m_daemon->calls.clear();
        QVERIFY(wait(m_engine->setFilePriority(kPack, {0, 2}, priority)).isValid());
        QJsonObject params = expected;
        params.insert(QStringLiteral("ids"), QJsonArray{kPack});
        QCOMPARE(m_daemon->last().method, QStringLiteral("torrent_set"));
        QCOMPARE(m_daemon->last().params, params);
    }
}

void TestTransmissionEngine::limitsLeaveInKilobytesAndModes()
{
    QVERIFY(wait(m_engine->setDownloadLimit({kPack}, 250000)).isValid());
    QCOMPARE(
        m_daemon->last().params,
        QJsonObject(
            {{"ids", QJsonArray{kPack}}, {"download_limited", true}, {"download_limit", 250}}));
    QVERIFY(wait(m_engine->setUploadLimit({kPack}, 0)).isValid());
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"ids", QJsonArray{kPack}}, {"upload_limited", false}}));

    QVERIFY(wait(m_engine->setShareLimits({kPack},
                                          RatioLimit::of(1.5),
                                          MinutesLimit::global(),
                                          MinutesLimit::of(45),
                                          QStringLiteral("Default")))
                .isValid());
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"ids", QJsonArray{kPack}},
                          {"seed_ratio_mode", 1},
                          {"seed_ratio_limit", 1.5},
                          {"seed_idle_mode", 1},
                          {"seed_idle_limit", 45}}));
    QVERIFY(wait(m_engine->setShareLimits({kPack},
                                          RatioLimit::unlimited(),
                                          MinutesLimit::unlimited(),
                                          MinutesLimit::global(),
                                          std::nullopt))
                .isValid());
    QCOMPARE(
        m_daemon->last().params,
        QJsonObject({{"ids", QJsonArray{kPack}}, {"seed_ratio_mode", 2}, {"seed_idle_mode", 0}}));

    // What it doesn't keep is refused before anything is sent (D, K18).
    const qsizetype sent = m_server->requests.size();
    QCOMPARE(wait(m_engine->setShareLimits({kPack},
                                           RatioLimit::global(),
                                           MinutesLimit::of(60),
                                           MinutesLimit::global(),
                                           std::nullopt))
                 .error()
                 .kind(),
             ApiError::Kind::NotFound);
    QCOMPARE(wait(m_engine->setShareLimits({kPack},
                                           RatioLimit::of(2),
                                           MinutesLimit::global(),
                                           MinutesLimit::global(),
                                           QStringLiteral("Remove")))
                 .error()
                 .kind(),
             ApiError::Kind::NotFound);
    QCOMPARE(m_server->requests.size(), sent);
}

void TestTransmissionEngine::turtleAndGlobalLimits()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    const Result<GlobalLimits> limits = wait(m_engine->globalLimits());
    QCOMPARE(limits.value().download, std::optional<qint64>(0));
    QCOMPARE(limits.value().altDownload, std::optional<qint64>(50000));

    QVERIFY(wait(m_engine->setGlobalLimits({1000000, 0, 20000, std::nullopt})).isValid());
    QCOMPARE(m_daemon->last().method, QStringLiteral("session_set"));
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"speed_limit_down_enabled", true},
                          {"speed_limit_down", 1000},
                          {"speed_limit_up_enabled", false},
                          {"alt_speed_down", 20}}));

    // C25 K20: the session as the speed preferences, and a write of them (read first, for
    // the units and a time's other half).
    const Result<QVariantMap> prefs = wait(m_engine->sessionPreferences());
    QCOMPARE(prefs.value().value(QStringLiteral("alt_dl_limit")).toLongLong(), 50000);
    QCOMPARE(prefs.value().value(QStringLiteral("scheduler_day_mask")).toInt(), 127);
    m_daemon->calls.clear();
    QVERIFY(
        wait(m_engine->setSessionPreferences({{QStringLiteral("schedule_to_hour"), 7}})).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"session_get", "session_set"}));
    QCOMPARE(m_daemon->last().params, QJsonObject({{"alt_speed_time_end", 7 * 60}}));
    m_daemon->calls.clear();
    const Result<Done> refused = wait(m_engine->setSessionPreferences({{"web_ui_port", 1}}));
    QCOMPARE(refused.error().kind(), ApiError::Kind::Conflict);
    QCOMPARE(m_daemon->methods(), QStringList({"session_get"})); // nothing set
    // TRANSMISSION › Basics (C25 K31): the port and DHT go as the session's own fields.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->setSessionPreferences(
                     {{QStringLiteral("listen_port"), 51413}, {QStringLiteral("dht"), true}}))
                .isValid());
    QCOMPARE(m_daemon->last().params, QJsonObject({{"peer_port", 51413}, {"dht_enabled", true}}));
    // Settings › Transmission (E5): the pane reads every field it shows; the rest go by
    // Transmission's own names.
    m_daemon->calls.clear();
    QVERIFY(
        wait(m_engine->sessionPreferences()).value().contains(QStringLiteral("seed_queue_size")));
    QCOMPARE(m_daemon->last().params.value(QStringLiteral("fields")).toArray().size(),
             TT::settingsFields().size());
    QVERIFY(wait(m_engine->setSessionPreferences({{QStringLiteral("seed_queue_enabled"), true},
                                                  {QStringLiteral("seed_queue_size"), 4},
                                                  {QStringLiteral("lpd_enabled"), true}}))
                .isValid());
    QCOMPARE(
        m_daemon->last().params,
        QJsonObject({{"seed_queue_enabled", true}, {"seed_queue_size", 4}, {"lpd_enabled", true}}));

    QVERIFY(wait(m_engine->setAltSpeed(true)).isValid());
    QCOMPARE(m_daemon->last().params, QJsonObject({{"alt_speed_enabled", true}}));
    // Toggle: read it (off in the recording), then say what it should be.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->toggleAltSpeed()).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"session_get", "session_set"}));
    QCOMPARE(m_daemon->last().params, QJsonObject({{"alt_speed_enabled", true}}));
}

void TestTransmissionEngine::labelsAreReadChangedAndWrittenWhole()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        if (m == QLatin1String("torrent_get")) {
            // Only the asked ones, as Transmission does.
            PollAnswers some = answers;
            const QJsonArray ids = p.value(QStringLiteral("ids")).toArray();
            if (!ids.isEmpty())
                some.torrents.removeIf([&ids](const QJsonObject &t) {
                    return !ids.contains(t.value(QStringLiteral("hash_string")));
                });
            return some.answer(m, p);
        }
        return answers.answer(m, p);
    };

    // The pack has fixtures + pack, the single fixtures: adding "keep" makes two sets.
    QVERIFY(wait(m_engine->addTags({kPack, kSingle}, {QStringLiteral("keep")})).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get", "torrent_set", "torrent_set"}));
    QCOMPARE(m_daemon->calls.at(0).params.value(QStringLiteral("fields")).toArray(),
             QJsonArray({QStringLiteral("hash_string"), QStringLiteral("labels")}));
    QCOMPARE(m_daemon->calls.at(1).params,
             QJsonObject(
                 {{"ids", QJsonArray{kPack}}, {"labels", strings({"fixtures", "pack", "keep"})}}));
    QCOMPARE(
        m_daemon->calls.at(2).params,
        QJsonObject({{"ids", QJsonArray{kSingle}}, {"labels", strings({"fixtures", "keep"})}}));
    QVERIFY(m_daemon->calls.at(1).inBatch);

    // Removing "fixtures" from both leaves the single with none.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->removeTags({kPack, kSingle}, {QStringLiteral("fixtures")})).isValid());
    QCOMPARE(m_daemon->calls.at(1).params,
             QJsonObject({{"ids", QJsonArray{kPack}}, {"labels", strings({"pack"})}}));
    QCOMPARE(m_daemon->calls.at(2).params,
             QJsonObject({{"ids", QJsonArray{kSingle}}, {"labels", QJsonArray()}}));

    // Nothing to change: only the read.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->removeTags({kError}, {QStringLiteral("fixtures")})).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get"}));

    // Deleting a label takes it off every torrent that has it, both alike: one call.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->deleteTags({QStringLiteral("pack")})).isValid());
    QVERIFY(!m_daemon->calls.at(0).params.contains(QStringLiteral("ids")));
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get", "torrent_set"}));
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"ids", QJsonArray{kPack}}, {"labels", strings({"fixtures"})}}));

    // A comma, or an empty label, is refused before anything goes (K14).
    m_daemon->calls.clear();
    const Result<Done> comma = wait(m_engine->addTags({kPack}, {QStringLiteral("a,b")}));
    QCOMPARE(comma.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(comma.error().message().contains(QStringLiteral("commas")));
    QCOMPARE(wait(m_engine->addTags({kPack}, {QStringLiteral(" ")})).error().kind(),
             ApiError::Kind::Conflict);
    QVERIFY(m_daemon->calls.isEmpty());
    // What Transmission itself says to one, all the same (the recorded answer).
    m_daemon->answer = [](const QString &, const QJsonObject &) {
        return errorOf("error_comma_label.json");
    };
    QVERIFY(wait(m_engine->setAltSpeed(false))
                .error()
                .message()
                .contains(QStringLiteral("labels cannot contain comma")));
}

void TestTransmissionEngine::renamingReadsTheNameAndStaysInPlace()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        if (m == QLatin1String("torrent_rename_path"))
            return fixture("torrent_rename_path.json").value(QStringLiteral("result")).toObject();
        return answers.answer(m, p);
    };
    // Renaming the torrent renames its folder on disk (K28): its name is read first.
    QVERIFY(wait(m_engine->rename(kPack, QStringLiteral("Fixture Pack (2026)"))).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get", "torrent_rename_path"}));
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"ids", QJsonArray{kPack}},
                          {"path", "Fixture Pack"},
                          {"name", "Fixture Pack (2026)"}}));

    // A new folder isn't a new name: refused, nothing sent.
    m_daemon->calls.clear();
    const Result<Done> moved =
        wait(m_engine->renameFile(kPack,
                                  QStringLiteral("Fixture Pack/notes.txt"),
                                  QStringLiteral("Fixture Pack/disc 1/notes.txt")));
    QCOMPARE(moved.error().kind(), ApiError::Kind::Conflict);
    QVERIFY(m_daemon->calls.isEmpty());
}

void TestTransmissionEngine::trackersAreEditedWhole()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    QVERIFY(wait(m_engine->addTrackers(kPack, {QStringLiteral("http://new/announce")})).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get", "torrent_set"}));
    QCOMPARE(m_daemon->calls.at(0).params.value(QStringLiteral("fields")).toArray(),
             QJsonArray({QStringLiteral("tracker_list")}));
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"ids", QJsonArray{kPack}},
                          {"tracker_list",
                           "http://127.0.0.1:9/announce\nudp://127.0.0.1:9/announce\n\n"
                           "http://new/announce\n"}}));
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->removeTrackers(kPack, {QStringLiteral("udp://127.0.0.1:9/announce")}))
                .isValid());
    QCOMPARE(m_daemon->last().params,
             QJsonObject(
                 {{"ids", QJsonArray{kPack}}, {"tracker_list", "http://127.0.0.1:9/announce\n"}}));
    // Already there: read, nothing written.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->addTrackers(kPack, {QStringLiteral("http://127.0.0.1:9/announce")}))
                .isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get"}));
}

void TestTransmissionEngine::sequentialIsReadThenSet()
{
    PollAnswers answers;
    m_daemon->answer = [&answers](const QString &m, const QJsonObject &p) {
        return answers.answer(m, p);
    };
    // The single one is sequential, the pack isn't: each gets the opposite.
    QVERIFY(wait(m_engine->toggleSequentialDownload({kPack, kSingle})).isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_get", "torrent_set", "torrent_set"}));
    QCOMPARE(
        m_daemon->calls.at(1).params,
        QJsonObject({{"ids", QJsonArray{kPack, kError, kMagnet}}, {"sequential_download", true}}));
    QCOMPARE(m_daemon->calls.at(2).params,
             QJsonObject({{"ids", QJsonArray{kSingle}}, {"sequential_download", false}}));
}

void TestTransmissionEngine::addSendsTorrentAddThenLimits()
{
    m_daemon->answer = [](const QString &m, const QJsonObject &p) -> QJsonObject {
        if (m != QLatin1String("torrent_add"))
            return {};
        if (p.contains(QStringLiteral("metainfo")))
            return fixture("torrent_add_duplicate.json").value(QStringLiteral("result")).toObject();
        return fixture("torrent_add.json").value(QStringLiteral("result")).toObject();
    };
    AddOptions options;
    options.savePath = QStringLiteral("/srv/films");
    options.tags = {QStringLiteral("films")};
    options.stopped = true;
    options.sequentialDownload = true;
    options.filePriorities = {FilePriority::Normal, FilePriority::Skip, FilePriority::High};
    options.category = QStringLiteral("ignored"); // no place for it (K27)
    options.dlLimit = 500000;
    const QString magnet = QStringLiteral("magnet:?xt=urn:btih:") + kPack;
    const Result<AddResult> added = wait(m_engine->addUrls({magnet}, options));
    QVERIFY2(added.isValid(), qPrintable(added.error().toString()));
    QCOMPARE(added.value().addedIds, QStringList({kPack}));
    QVERIFY(!added.value().refused);
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_add", "torrent_set"}));
    QCOMPARE(m_daemon->calls.at(0).params,
             QJsonObject({{"filename", magnet},
                          {"download_dir", "/srv/films"},
                          {"labels", strings({"films"})},
                          {"paused", true},
                          {"sequential_download", true},
                          {"files_unwanted", QJsonArray{1}},
                          {"priority_high", QJsonArray{2}}}));
    // Limits aren't taken at add: set right after on what was added.
    QCOMPARE(
        m_daemon->calls.at(1).params,
        QJsonObject(
            {{"ids", QJsonArray{kPack}}, {"download_limited", true}, {"download_limit", 500}}));

    // A .torrent already there: taken, nothing added.
    m_daemon->calls.clear();
    const Result<AddResult> again = wait(
        m_engine->addFile(QStringLiteral("pack.torrent"), QByteArray("d4:infoe"), AddOptions()));
    QVERIFY(again.value().refused);
    QVERIFY(again.value().addedIds.isEmpty());
    QCOMPARE(m_daemon->last().params,
             QJsonObject({{"metainfo", QString::fromLatin1(QByteArray("d4:infoe").toBase64())}}));

    // Several at once: one batch.
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->addMany({{QStringLiteral("a.torrent"), QByteArray("a")}}, {magnet}, {}))
                .isValid());
    QCOMPARE(m_daemon->methods(), QStringList({"torrent_add", "torrent_add"}));
    QVERIFY(m_daemon->calls.at(0).inBatch);

    // A comma in a label: refused before anything goes.
    m_daemon->calls.clear();
    AddOptions comma;
    comma.tags = {QStringLiteral("a,b")};
    QCOMPARE(wait(m_engine->addUrls({magnet}, comma)).error().kind(), ApiError::Kind::Conflict);
    QVERIFY(m_daemon->calls.isEmpty());
}

// TRANSMISSION › Basics (C25 K31): "test port" asks the daemon, which asks from outside.
void TestTransmissionEngine::thePortIsTestedByTransmission()
{
    m_daemon->answer = [](const QString &method, const QJsonObject &) {
        return method == QLatin1String("port_test")
                   ? QJsonObject{{QStringLiteral("port_is_open"), true},
                                 {QStringLiteral("ip_protocol"), QStringLiteral("ipv4")}}
                   : QJsonObject{};
    };
    const Result<bool> open = wait(m_engine->testPort());
    QVERIFY(open.isValid());
    QVERIFY(open.value());
    QCOMPARE(m_daemon->last().method, QStringLiteral("port_test"));
}

void TestTransmissionEngine::whatTransmissionLacksIsNotSupported()
{
    const QList<std::function<ReplyBase *()>> lacking{
        [&]() { return m_engine->toggleFirstLastPiece({kPack}); },
        [&]() { return m_engine->setSuperSeeding({kPack}, true); },
        [&]() { return m_engine->setAutoManagement({kPack}, true); },
        [&]() { return m_engine->addWebSeeds(kPack, {QStringLiteral("http://w/")}); },
        [&]() { return m_engine->removeWebSeeds(kPack, {QStringLiteral("http://w/")}); },
        [&]() { return m_engine->addPeers({kPack}, {QStringLiteral("1.2.3.4:5")}); },
        [&]() { return m_engine->banPeers({QStringLiteral("1.2.3.4:5")}); },
        [&]() { return m_engine->setCategory({kPack}, QStringLiteral("c")); },
        [&]() { return m_engine->createCategory(QStringLiteral("c")); },
        [&]() { return m_engine->removeCategories({QStringLiteral("c")}); },
        [&]() { return m_engine->createTags({QStringLiteral("t")}); },
        [&]() { return m_engine->log(-1, false); },
        [&]() { return m_engine->fetchMetadata(QStringLiteral("magnet:?xt=urn:btih:00")); },
    };
    for (const auto &call : lacking) {
        ReplyBase *reply = call();
        QSignalSpy finished(reply, &ReplyBase::finished);
        ApiError error;
        connect(reply, &ReplyBase::finished, this, [&error, reply]() { error = reply->error(); });
        QVERIFY(finished.wait(2000));
        QCOMPARE(error.kind(), ApiError::Kind::NotFound);
        QVERIFY(error.message().contains(QStringLiteral("not supported")));
    }
    QCOMPARE(m_server->requests.size(), 0);
    // folderContent: Transmission can't list a folder (K23), said by its state.
    QCOMPARE(wait(m_engine->folderContent(QStringLiteral("/srv"))).value().state,
             ServerFolder::State::Unsupported);
}

// K22 (board 47 §3, checked on 4.1.3): Transmission answers set_location, and everything asked
// meanwhile, only once the copy ends. Until then the engine names the torrent it is moving; the
// call waits past the client's own timeout; the poll after it lists every torrent, so the
// moved one is read whatever recently_active says.
void TestTransmissionEngine::aMoveIsBusyUntilItsCopyEnds()
{
    PollAnswers answers;
    m_daemon->answer = [this, &answers](const QString &m, const QJsonObject &p) {
        if (m == QLatin1String("torrent_set_location"))
            m_server->hold = true; // the copy starts: nothing answers until it ends
        return answers.answer(m, p);
    };
    QVERIFY(wait(m_engine->sync(0)).isValid());
    QVERIFY(wait(m_engine->sync(1)).isValid());
    QVERIFY(m_engine->busyMoving().isEmpty());

    m_engine->setTimeout(std::chrono::milliseconds(300));
    QSignalSpy changed(m_engine.get(), &EngineClient::busyMovingChanged);
    Result<Done> moved{ApiError(ApiError::Kind::Unknown, QStringLiteral("not yet"))};
    bool answered = false;
    m_engine->setLocation({kPack}, QStringLiteral("/srv/new"))
        ->then(this, [&](const Result<Done> &result) {
            moved = result;
            answered = true;
        });
    QCOMPARE(m_engine->busyMoving(), QStringList({kPack}));
    QCOMPARE(changed.count(), 1);

    // A poll asked meanwhile waits behind the copy and times out at the client's timeout;
    // the move's own call doesn't.
    const Result<SyncDelta> poll = wait(m_engine->sync(2));
    QVERIFY(poll.isError());
    QCOMPARE(poll.error().kind(), ApiError::Kind::Timeout);
    QTest::qWait(400);
    QVERIFY(!answered);
    QCOMPARE(m_engine->busyMoving(), QStringList({kPack}));

    // The copy ends: the move answers, nothing is busy, the next poll asks for everything.
    m_server->release();
    QTRY_VERIFY_WITH_TIMEOUT(answered, 3000);
    QVERIFY(moved.isValid());
    QVERIFY(m_engine->busyMoving().isEmpty());
    QCOMPARE(changed.count(), 2);
    m_engine->setTimeout(std::chrono::seconds(5));
    m_daemon->calls.clear();
    QVERIFY(wait(m_engine->sync(2)).isValid());
    QVERIFY(!m_daemon->calls.at(0).params.contains(QStringLiteral("ids")));
}

// M14: a move that looks stuck is given up as busy; its answer, when it comes, still settles.
void TestTransmissionEngine::aMoveGivenUpIsNoLongerBusy()
{
    m_daemon->answer = [this](const QString &m, const QJsonObject &) {
        if (m == QLatin1String("torrent_set_location"))
            m_server->hold = true;
        return QJsonObject();
    };
    bool answered = false;
    m_engine->setLocation({kPack}, QStringLiteral("/srv/new"))
        ->then(this, [&](const Result<Done> &) { answered = true; });
    QTRY_COMPARE_WITH_TIMEOUT(m_server->heldCount(), 1, 3000);
    QCOMPARE(m_engine->busyMoving(), QStringList({kPack}));
    QSignalSpy changed(m_engine.get(), &EngineClient::busyMovingChanged);
    m_engine->releaseBusyMoving();
    QVERIFY(m_engine->busyMoving().isEmpty());
    QCOMPARE(changed.count(), 1);
    m_server->release();
    QTRY_VERIFY_WITH_TIMEOUT(answered, 3000);
    QCOMPARE(changed.count(), 1); // nothing more to say
}

// K23: free_space on any path; a folder the move will make is read on its nearest parent.
void TestTransmissionEngine::freeSpaceIsReadOnTheNearestFolderThere()
{
    const QJsonObject missing = fixture("free_space_missing.json").value("error").toObject();
    m_daemon->answer = [&missing](const QString &m, const QJsonObject &p) {
        if (m != QLatin1String("free_space"))
            return QJsonObject();
        const QString path = p.value(QStringLiteral("path")).toString();
        if (path != QLatin1String("/srv"))
            return QJsonObject{{QStringLiteral("__error"), missing}};
        return QJsonObject{{QStringLiteral("path"), path},
                           {QStringLiteral("size_bytes"), 30.1e9},
                           {QStringLiteral("total_size"), 100e9}};
    };
    const Result<FreeSpaceAt> read = wait(m_engine->freeSpace(QStringLiteral("/srv/fast/games/")));
    QVERIFY2(read.isValid(), qPrintable(read.error().toString()));
    QCOMPARE(read.value().path, QStringLiteral("/srv"));
    QCOMPARE(read.value().bytesFree, qint64(30.1e9));
    QCOMPARE(read.value().bytesTotal, qint64(100e9));
    QStringList asked;
    for (const Call &call : std::as_const(m_daemon->calls))
        asked << call.params.value(QStringLiteral("path")).toString();
    QCOMPARE(asked,
             QStringList({QStringLiteral("/srv/fast/games/"),
                          QStringLiteral("/srv/fast"),
                          QStringLiteral("/srv")}));

    // A daemon that doesn't answer is an error, not a walk up.
    m_engine->setTimeout(std::chrono::milliseconds(200));
    m_engine->setMaxRetries(0);
    m_server->hold = true;
    const Result<FreeSpaceAt> silent = wait(m_engine->freeSpace(QStringLiteral("/srv")));
    QVERIFY(silent.isError());
    QCOMPARE(silent.error().kind(), ApiError::Kind::Timeout);
    // A relative path is refused before it is sent.
    m_server->release();
    QVERIFY(wait(m_engine->freeSpace(QStringLiteral("srv"))).isError());
}

QTEST_GUILESS_MAIN(TestTransmissionEngine)
#include "tst_transmissionengine.moc"
