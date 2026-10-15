// #44 — MetadataLookup: a magnet's file list asked of qBittorrent before the add (docs/29).
//
// The clock is the test's own and the server is a function that hands out replies the test
// answers by hand, so the board's timings (§3, D2, D3) are checked to the millisecond without
// waiting a minute. The bodies are the research's (qBittorrent 5.2.3, the swarm's Public Domain
// Sampler and tiny-notes.txt).

#include "core/MetadataLookup.hpp"

#include <QJsonDocument>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

using omaseed::core::ApiError;
using omaseed::core::MetadataLookup;
using omaseed::core::QbtClient;
using omaseed::core::QbtReply;
using omaseed::core::QbtResult;
using omaseed::core::TorrentFileEntry;
using Phase = MetadataLookup::Phase;
using Fetched = QbtClient::FetchedMetadata;

Q_DECLARE_METATYPE(MetadataLookup::Phase)

namespace {

const char *kSampler = R"({"comment":"","created_by":"","creation_date":-1,
 "hash":"1f85b9e8a0d259478ee98344b2b0816f899e59b8",
 "infohash_v1":"f43d3b0a927b9662dbb640d24793a1726aaa4c61",
 "infohash_v2":"1f85b9e8a0d259478ee98344b2b0816f899e59b89e5123b3df7f8ef88633246e",
 "info":{"files":[{"length":812,"path":"Public Domain Sampler/README.txt"},
                  {"length":25165824,"path":"Public Domain Sampler/sampler.mkv"},
                  {"length":43008,"path":"Public Domain Sampler/sampler.srt"}],
         "length":25209644,"name":"Public Domain Sampler","piece_length":65536,"pieces_num":386,"private":false},
 "trackers":[{"tier":0,"url":"http://127.0.0.1:26996/announce"}],"webseeds":[]})";

const char *kTinyNotes = R"({"hash":"ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12",
 "infohash_v1":"ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12","infohash_v2":"",
 "info":{"files":[{"length":4096,"path":"tiny-notes.txt"}],"length":4096,
         "name":"tiny-notes.txt","piece_length":16384,"pieces_num":1,"private":true},
 "trackers":[],"webseeds":[]})";

QJsonObject json(const char *text)
{
    return QJsonDocument::fromJson(QByteArray(text)).object();
}

/// The server side of the test: every poll gets a reply the test answers.
struct FakeServer
{
    QList<QPointer<QbtReply<Fetched>>> replies;
    QStringList sources;

    MetadataLookup::Fetch fetch(QObject *owner)
    {
        return [this, owner](const QString &source) {
            sources.append(source);
            auto *reply = new QbtReply<Fetched>(owner);
            replies.append(reply);
            return reply;
        };
    }
    int polls() const { return int(sources.size()); }
    /// Answers the latest poll.
    void answer(const QbtResult<Fetched> &result)
    {
        QVERIFY(!replies.isEmpty() && !replies.last().isNull());
        replies.last()->complete(result);
    }
    void looking() { answer(Fetched{false, json(R"({"hash":"1f85"})")}); }
    void done(const char *body = kSampler) { answer(Fetched{true, json(body)}); }
    void fail(ApiError::Kind kind, int status)
    {
        answer(QbtResult<Fetched>(ApiError(kind, QStringLiteral("test"), status)));
    }
};

} // namespace

class TestMetadataLookup : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void pollSchedule_data();
    void pollSchedule();
    void phaseTimes_data();
    void phaseTimes();

    void parsesAMultiFileTorrentWithoutItsRoot();
    void parsesASingleFile();
    void refusesABodyWithoutInfo();

    void waitsForTheServer();
    void anOldServerIsUnsupported();
    void aQuickAnswerShowsNothingButTheTree();
    void followsTheBoardsTimeline();
    void neverStopsWhileOpen();
    void pollsOneAtATime();
    void a404IsUnsupported();
    void aRefusedSourceFails();
    void aLostPollIsAskedAgain();
    void theSameKeyStartsNothingNew();
    void anotherKeyDropsTheOldAnswer();
    void stopLetsGo();
    void privateAsksNothing();
    void aFasterClockForTheWalk();

private:
    qint64 m_now = 0;
    FakeServer *m_server = nullptr;
    MetadataLookup *m_lookup = nullptr;
    /// Moves the clock to `ms` and lets the lookup see it.
    void at(qint64 ms)
    {
        m_now = ms;
        m_lookup->wake();
    }
    void startReady(const QString &key = QStringLiteral("k"))
    {
        m_lookup->setServer(true, QStringLiteral("2.15.1"));
        m_lookup->start(
            key, QStringLiteral("magnet:?xt=urn:btih:") + key, QStringLiteral("magnet"));
    }
};

void TestMetadataLookup::init()
{
    m_now = 1000000; // any start: only differences count
    m_server = new FakeServer;
    m_lookup = new MetadataLookup(this);
    m_lookup->setManualClock([this]() { return m_now; });
    m_lookup->setFetch(m_server->fetch(this));
}

void TestMetadataLookup::cleanup()
{
    delete m_lookup;
    m_lookup = nullptr;
    delete m_server;
    m_server = nullptr;
}

void TestMetadataLookup::pollSchedule_data()
{
    QTest::addColumn<qint64>("elapsed");
    QTest::addColumn<qint64>("next");
    QTest::newRow("start") << qint64(0) << qint64(250);
    QTest::newRow("after 0.25") << qint64(250) << qint64(500);
    QTest::newRow("after 0.5") << qint64(500) << qint64(1000);
    QTest::newRow("late answer") << qint64(730) << qint64(1000);
    QTest::newRow("1 s") << qint64(1000) << qint64(2000);
    QTest::newRow("every second") << qint64(2400) << qint64(3000);
    QTest::newRow("59.9 s") << qint64(59900) << qint64(60000);
    QTest::newRow("a minute") << qint64(60000) << qint64(65000);
    QTest::newRow("every 5 s") << qint64(61200) << qint64(65000);
    QTest::newRow("much later") << qint64(600001) << qint64(605000);
}

// docs/29 §3: 0.25 s, 0.5 s and 1 s, then every second, and every 5 s after a minute.
void TestMetadataLookup::pollSchedule()
{
    QFETCH(qint64, elapsed);
    QFETCH(qint64, next);
    QCOMPARE(MetadataLookup::nextPollAt(elapsed), next);
}

void TestMetadataLookup::phaseTimes_data()
{
    QTest::addColumn<qint64>("elapsed");
    QTest::addColumn<Phase>("phase");
    QTest::newRow("0") << qint64(0) << Phase::Quiet;
    QTest::newRow("0.499") << qint64(499) << Phase::Quiet;
    QTest::newRow("0.5") << qint64(500) << Phase::Fetching;
    QTest::newRow("9.999") << qint64(9999) << Phase::Fetching;
    QTest::newRow("10") << qint64(10000) << Phase::Slow;
    QTest::newRow("59.999") << qint64(59999) << Phase::Slow;
    QTest::newRow("60") << qint64(60000) << Phase::Never;
    QTest::newRow("an hour") << qint64(3600000) << Phase::Never;
}

// D2: "fetching" only after 0.5 s; D3: "still looking" at 10 s, "may never come" at 60 s.
void TestMetadataLookup::phaseTimes()
{
    QFETCH(qint64, elapsed);
    QFETCH(Phase, phase);
    QCOMPARE(MetadataLookup::phaseAt(elapsed), phase);
}

// info.files paths carry `<info.name>/`: TorrentFileEntry keeps them below the root, in
// qBittorrent's file-id order.
void TestMetadataLookup::parsesAMultiFileTorrentWithoutItsRoot()
{
    const auto metadata = MetadataLookup::parse(json(kSampler));
    QVERIFY(metadata.has_value());
    QCOMPARE(metadata->name, QStringLiteral("Public Domain Sampler"));
    QCOMPARE(metadata->torrentId, QStringLiteral("1f85b9e8a0d259478ee98344b2b0816f899e59b8"));
    QCOMPARE(metadata->infoHashV1, QStringLiteral("f43d3b0a927b9662dbb640d24793a1726aaa4c61"));
    QCOMPARE(metadata->infoHashV2.size(), 64);
    QVERIFY(metadata->multiFile);
    QVERIFY(!metadata->isPrivate);
    QCOMPARE(metadata->pieceLength, 65536);
    const QList<TorrentFileEntry> files{{QStringLiteral("README.txt"), 812},
                                        {QStringLiteral("sampler.mkv"), 25165824},
                                        {QStringLiteral("sampler.srt"), 43008}};
    QCOMPARE(metadata->files, files);
    // The files' own sum: info.length leaves the BEP-47 pad files out too.
    QCOMPARE(metadata->totalSize, 25209644);
    QCOMPARE(metadata->trackers, QStringList{QStringLiteral("http://127.0.0.1:26996/announce")});
}

// A single-file torrent's path is its name (tiny-notes.txt in the research).
void TestMetadataLookup::parsesASingleFile()
{
    const auto metadata = MetadataLookup::parse(json(kTinyNotes));
    QVERIFY(metadata.has_value());
    QVERIFY(!metadata->multiFile);
    QVERIFY(metadata->isPrivate);
    QCOMPARE(metadata->files, (QList<TorrentFileEntry>{{QStringLiteral("tiny-notes.txt"), 4096}}));

    // Without a files list, info.length stands for the one file.
    const auto bare =
        MetadataLookup::parse(json(R"({"infohash_v1":"AB12AB12AB12AB12AB12AB12AB12AB12AB12AB12",)"
                                   R"("info":{"name":"one.bin","length":7}})"));
    QVERIFY(bare.has_value());
    QCOMPARE(bare->files, (QList<TorrentFileEntry>{{QStringLiteral("one.bin"), 7}}));
    QCOMPARE(bare->torrentId, QStringLiteral("ab12ab12ab12ab12ab12ab12ab12ab12ab12ab12"));
}

void TestMetadataLookup::refusesABodyWithoutInfo()
{
    QVERIFY(!MetadataLookup::parse(json(R"({"hash":"1f85"})")).has_value());
    QVERIFY(!MetadataLookup::parse(json("{}")).has_value());
}

// F7: handed over with the app closed, the lookup waits for the server's login and version;
// its clock starts then.
void TestMetadataLookup::waitsForTheServer()
{
    m_lookup->start(QStringLiteral("k"), QStringLiteral("magnet:?xt=k"), QStringLiteral("magnet"));
    QCOMPARE(m_lookup->phase(), Phase::Waiting);
    at(m_now + 30000);
    QCOMPARE(m_lookup->phase(), Phase::Waiting);
    QCOMPARE(m_server->polls(), 0);

    m_lookup->setServer(false, QString());
    QCOMPARE(m_lookup->phase(), Phase::Waiting);
    m_lookup->setServer(true, QStringLiteral("2.15.1"));
    QCOMPARE(m_lookup->phase(), Phase::Quiet);
    QCOMPARE(m_server->polls(), 1);
    QCOMPARE(m_server->sources.first(), QStringLiteral("magnet:?xt=k"));
    QCOMPARE(m_lookup->seconds(), 0);
}

// D11: below Web API 2.11.9 the sheet is today's, and nothing is asked.
void TestMetadataLookup::anOldServerIsUnsupported()
{
    m_lookup->setServer(true, QStringLiteral("2.11.4"));
    m_lookup->start(QStringLiteral("k"), QStringLiteral("magnet:?xt=k"), QStringLiteral("magnet"));
    QCOMPARE(m_lookup->phase(), Phase::Unsupported);
    QCOMPARE(m_lookup->phaseName(), QStringLiteral("unsupported"));
    QCOMPARE(m_server->polls(), 0);
}

// D2 a: an answer inside the first half second goes straight to the tree.
void TestMetadataLookup::aQuickAnswerShowsNothingButTheTree()
{
    QSignalSpy fetched(m_lookup, &MetadataLookup::fetched);
    QList<QString> phases;
    connect(
        m_lookup, &MetadataLookup::changed, this, [&]() { phases.append(m_lookup->phaseName()); });
    startReady();
    m_server->looking();
    at(m_now + 250);
    QCOMPARE(m_server->polls(), 2);
    m_server->done();
    QCOMPARE(fetched.count(), 1);
    QCOMPARE(m_lookup->phase(), Phase::Done);
    QCOMPARE(m_lookup->metadata().files.size(), 3);
    QVERIFY(!phases.contains(QStringLiteral("fetching")));
    // Done: no more polls.
    at(m_now + 5000);
    QCOMPARE(m_server->polls(), 2);
}

// §3: every poll the schedule names, each phase at its time, the seconds as they pass.
void TestMetadataLookup::followsTheBoardsTimeline()
{
    const qint64 t0 = m_now;
    startReady();
    QCOMPARE(m_server->polls(), 1); // 0 s
    m_server->looking();
    at(t0 + 249);
    QCOMPARE(m_server->polls(), 1);
    at(t0 + 250);
    QCOMPARE(m_server->polls(), 2);
    m_server->looking();
    at(t0 + 499);
    QCOMPARE(m_lookup->phase(), Phase::Quiet);
    at(t0 + 500);
    QCOMPARE(m_lookup->phase(), Phase::Fetching);
    QCOMPARE(m_server->polls(), 3);
    m_server->looking();
    at(t0 + 1000);
    QCOMPARE(m_server->polls(), 4);
    m_server->looking();
    at(t0 + 3000); // 2 s is long gone: one poll now, the next at 4 s
    QCOMPARE(m_server->polls(), 5);
    QCOMPARE(m_lookup->seconds(), 3);
    m_server->looking();
    at(t0 + 3500);
    QCOMPARE(m_server->polls(), 5);
    at(t0 + 4000);
    QCOMPARE(m_server->polls(), 6);
    m_server->looking();

    at(t0 + 10000);
    QCOMPARE(m_lookup->phase(), Phase::Slow);
    QCOMPARE(m_lookup->phaseName(), QStringLiteral("slow"));
    QCOMPARE(m_lookup->seconds(), 10);
    m_server->looking();
    at(t0 + 59000);
    m_server->looking();
    at(t0 + 60000);
    QCOMPARE(m_lookup->phase(), Phase::Never);
    QCOMPARE(m_lookup->seconds(), 60);
    const int atAMinute = m_server->polls();
    m_server->looking();
    // D3 a: every 5 s from now on.
    at(t0 + 61000);
    at(t0 + 64999);
    QCOMPARE(m_server->polls(), atAMinute);
    QCOMPARE(m_lookup->seconds(), 64);
    at(t0 + 65000);
    QCOMPARE(m_server->polls(), atAMinute + 1);
}

// D3 a: the sheet never gives up by itself while it is open.
void TestMetadataLookup::neverStopsWhileOpen()
{
    const qint64 t0 = m_now;
    startReady();
    for (qint64 t = 250; t <= 3600000; t = MetadataLookup::nextPollAt(t)) {
        m_server->looking();
        at(t0 + t);
    }
    QCOMPARE(m_lookup->phase(), Phase::Never);
    QVERIFY(m_server->polls() > 700);
    m_server->done();
    QCOMPARE(m_lookup->phase(), Phase::Done);
}

// A poll still out when the next one is due: nothing more is sent until it answers.
void TestMetadataLookup::pollsOneAtATime()
{
    const qint64 t0 = m_now;
    startReady();
    at(t0 + 250);
    at(t0 + 2000);
    QCOMPARE(m_server->polls(), 1);
    m_server->looking(); // answered at 2 s: the next poll is the 3 s one
    QCOMPARE(m_server->polls(), 1);
    at(t0 + 3000);
    QCOMPARE(m_server->polls(), 2);
}

// D11: a 404 is a server without the endpoint.
void TestMetadataLookup::a404IsUnsupported()
{
    startReady();
    m_server->fail(ApiError::Kind::NotFound, 404);
    QCOMPARE(m_lookup->phase(), Phase::Unsupported);
    at(m_now + 5000);
    QCOMPARE(m_server->polls(), 1);
}

// qBittorrent refusing the source itself ("Unable to parse"): asking again changes nothing.
void TestMetadataLookup::aRefusedSourceFails()
{
    startReady();
    m_server->fail(ApiError::Kind::Unknown, 400);
    QCOMPARE(m_lookup->phase(), Phase::Failed);
    at(m_now + 5000);
    QCOMPARE(m_server->polls(), 1);
    QVERIFY(!m_lookup->isActive());
}

// A network error, a timeout or a 5xx is the next poll's job, as in qBittorrent's Web UI.
void TestMetadataLookup::aLostPollIsAskedAgain()
{
    const qint64 t0 = m_now;
    startReady();
    m_server->fail(ApiError::Kind::Network, 0);
    at(t0 + 250);
    QCOMPARE(m_server->polls(), 2);
    m_server->fail(ApiError::Kind::Server, 502);
    at(t0 + 500);
    QCOMPARE(m_server->polls(), 3);
    m_server->fail(ApiError::Kind::Timeout, 0);
    QCOMPARE(m_lookup->phase(), Phase::Fetching);
    at(t0 + 1000);
    m_server->done();
    QCOMPARE(m_lookup->phase(), Phase::Done);
}

// §6: a typed magnet is looked up once its hash is whole; typing `&dn=…` after it, or the
// sheet asking again, starts nothing new.
void TestMetadataLookup::theSameKeyStartsNothingNew()
{
    startReady(QStringLiteral("aaaa"));
    m_server->looking();
    m_lookup->start(QStringLiteral("aaaa"),
                    QStringLiteral("magnet:?xt=urn:btih:aaaa&dn=x"),
                    QStringLiteral("magnet"));
    QCOMPARE(m_server->polls(), 1);
    QCOMPARE(m_lookup->source(), QStringLiteral("magnet:?xt=urn:btih:aaaa"));
    at(m_now + 250);
    m_server->done();
    m_lookup->start(QStringLiteral("aaaa"),
                    QStringLiteral("magnet:?xt=urn:btih:aaaa"),
                    QStringLiteral("magnet"));
    QCOMPARE(m_lookup->phase(), Phase::Done);
    QCOMPARE(m_server->polls(), 2);
}

// Another magnet pasted over the first: the first one's answer, arriving late, is dropped.
void TestMetadataLookup::anotherKeyDropsTheOldAnswer()
{
    startReady(QStringLiteral("aaaa"));
    QPointer<QbtReply<Fetched>> first = m_server->replies.last();
    m_lookup->start(QStringLiteral("bbbb"),
                    QStringLiteral("magnet:?xt=urn:btih:bbbb"),
                    QStringLiteral("magnet"));
    QCOMPARE(m_server->polls(), 2);
    QCOMPARE(m_server->sources.last(), QStringLiteral("magnet:?xt=urn:btih:bbbb"));
    first->complete(Fetched{true, json(kSampler)});
    QCOMPARE(m_lookup->phase(), Phase::Quiet);
    QCOMPARE(m_lookup->key(), QStringLiteral("bbbb"));
}

// D6 a: closing only stops the polling; qBittorrent goes on by itself.
void TestMetadataLookup::stopLetsGo()
{
    QSignalSpy fetched(m_lookup, &MetadataLookup::fetched);
    startReady();
    m_lookup->stop();
    QCOMPARE(m_lookup->phase(), Phase::Idle);
    QVERIFY(m_lookup->source().isEmpty());
    m_server->done(); // the answer to a poll sent before the stop
    QCOMPARE(fetched.count(), 0);
    at(m_now + 10000);
    QCOMPARE(m_server->polls(), 1);
    // And the same magnet again later is a new lookup (it answers at once from the session).
    startReady();
    QCOMPARE(m_server->polls(), 2);
}

// F5: a private tracker's magnet asks nothing of the server.
void TestMetadataLookup::privateAsksNothing()
{
    m_lookup->setServer(true, QStringLiteral("2.15.1"));
    m_lookup->startPrivate(
        QStringLiteral("k"), QStringLiteral("magnet:?xt=k"), QStringLiteral("tracker.example"));
    QCOMPARE(m_lookup->phase(), Phase::Private);
    QCOMPARE(m_lookup->privateHost(), QStringLiteral("tracker.example"));
    at(m_now + 60000);
    QCOMPARE(m_server->polls(), 0);
    QVERIFY(!m_lookup->isActive());
}

// The walk's test-only clock: the board's minute in three real seconds.
void TestMetadataLookup::aFasterClockForTheWalk()
{
    m_lookup->setClockScale(20);
    const qint64 t0 = m_now;
    startReady();
    m_server->looking();
    at(t0 + 500);
    QCOMPARE(m_lookup->phase(), Phase::Slow);
    m_server->looking();
    at(t0 + 3000);
    QCOMPARE(m_lookup->phase(), Phase::Never);
    QCOMPARE(m_lookup->seconds(), 60);
}

QTEST_GUILESS_MAIN(TestMetadataLookup)
#include "tst_metadatalookup.moc"
